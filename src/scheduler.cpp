/*
* Copyright (c) 2018, EPCC, The University of Edinburgh
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its
*    contributors may be used to endorse or promote products derived from
*    this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "scheduler.h"
#include "edat.h"
#include "threadpool.h"
#include "misc.h"
#include "metrics.h"
#include <map>
#include <string>
#include <mutex>
#include <stdlib.h>
#include <string.h>
#include <queue>
#include <utility>
#include <set>

#ifndef DO_METRICS
#define DO_METRICS false
#endif

/**
* Registers a task with EDAT, this will determine (and consume) outstanding events & then if applicable will mark ready for execution. Otherwise
* it will store the task in a scheduled state. Persistent tasks are duplicated if they are executed and the duplicate run to separate it from
* the stored version which will be updated by other events arriving.
*/
void Scheduler::registerTask(void (*task_fn)(EDAT_Event*, int), std::string task_name, std::vector<std::pair<int, std::string>> dependencies,
                             bool persistent, bool greedyConsumerOfEvents) {
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  PendingTaskDescriptor * pendingTask=new PendingTaskDescriptor();
  pendingTask->task_fn=task_fn;
  pendingTask->numArrivedEvents=0;
  pendingTask->freeData=true;
  pendingTask->persistent=persistent;
  pendingTask->task_name=task_name;
  pendingTask->greedyConsumerOfEvents=greedyConsumerOfEvents;

  for (std::pair<int, std::string> dependency : dependencies) {
    DependencyKey depKey = DependencyKey(dependency.second, dependency.first);
    pendingTask->taskDependencyOrder.push_back(depKey);
    std::map<DependencyKey, int*>::iterator oDit=pendingTask->originalDependencies.find(depKey);
    if (oDit != pendingTask->originalDependencies.end()) {
      (*(oDit->second))++;
    } else {
      pendingTask->originalDependencies.insert(std::pair<DependencyKey, int*>(depKey, new int(1)));
    }
    bool continueEvtSearch=true, prev_added=false;
    while (continueEvtSearch) {
      std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator it=outstandingEvents.find(depKey);
      if (it != outstandingEvents.end() && !it->second.empty()) {
        prev_added=true;
        pendingTask->numArrivedEvents++;
        SpecificEvent * specificEVTToAdd;
        if (it->second.front()->isPersistent()) {
          // If its persistent event then copy the event
          specificEVTToAdd=new SpecificEvent(*(it->second.front()));
        } else {
          specificEVTToAdd=it->second.front();
          // If not persistent then remove from outstanding events
          outstandingEventsToHandle--;
          it->second.pop();
          if (it->second.empty()) outstandingEvents.erase(it);
        }

        std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator arrivedEventsIT = pendingTask->arrivedEvents.find(depKey);
        if (arrivedEventsIT == pendingTask->arrivedEvents.end()) {
          std::queue<SpecificEvent*> eventQueue;
          eventQueue.push(specificEVTToAdd);
          pendingTask->arrivedEvents.insert(std::pair<DependencyKey, std::queue<SpecificEvent*>>(depKey, eventQueue));
        } else {
          arrivedEventsIT->second.push(specificEVTToAdd);
        }
        continueEvtSearch=greedyConsumerOfEvents;
      } else {
        continueEvtSearch=false;
        if (!prev_added) {
          oDit=pendingTask->outstandingDependencies.find(depKey);
          if (oDit != pendingTask->outstandingDependencies.end()) {
            (*(oDit->second))++;
          } else {
            pendingTask->outstandingDependencies.insert(std::pair<DependencyKey, int*>(depKey, new int(1)));
          }
        }
      }
    }
  }

  if (pendingTask->outstandingDependencies.empty()) {
    PendingTaskDescriptor* exec_Task;
    if (persistent) {
      exec_Task=new PendingTaskDescriptor(*pendingTask);
      for (std::pair<DependencyKey, int*> dependency : pendingTask->originalDependencies) {
        pendingTask->outstandingDependencies.insert(std::pair<DependencyKey, int*>(dependency.first, new int(*(dependency.second))));
      }
      pendingTask->arrivedEvents.clear();
      pendingTask->numArrivedEvents=0;
      registeredTasks.push_back(pendingTask);
    } else {
      exec_Task=pendingTask;
    }
    outstandTaskEvt_lock.unlock();
    readyToRunTask(exec_Task);
    consumeEventsByPersistentTasks();
  } else {
    registeredTasks.push_back(pendingTask);
  }
}

/**
* Pauses a specific task to be reactivated when the dependencies arrive. Will check to find whether any (all?) event dependencies have already arrived and if so then
* is a simple call back with these. Otherwise will call into the thread pool to pause the thread.
*/
EDAT_Event* Scheduler::pauseTask(std::vector<std::pair<int, std::string>> dependencies) {
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  PausedTaskDescriptor * pausedTask=new PausedTaskDescriptor();
  pausedTask->numArrivedEvents=0;
  for (std::pair<int, std::string> dependency : dependencies) {
    DependencyKey depKey = DependencyKey(dependency.second, dependency.first);
    pausedTask->taskDependencyOrder.push_back(depKey);

    std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator it=outstandingEvents.find(depKey);
    if (it != outstandingEvents.end() && !it->second.empty()) {
      pausedTask->numArrivedEvents++;
      SpecificEvent * specificEVTToAdd;
      if (it->second.front()->isPersistent()) {
        // If its persistent event then copy the event
        specificEVTToAdd=new SpecificEvent(*(it->second.front()));
      } else {
        specificEVTToAdd=it->second.front();
        // If not persistent then remove from outstanding events
        outstandingEventsToHandle--;
        it->second.pop();
        if (it->second.empty()) outstandingEvents.erase(it);
      }

      std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator arrivedEventsIT = pausedTask->arrivedEvents.find(depKey);
      if (arrivedEventsIT == pausedTask->arrivedEvents.end()) {
        std::queue<SpecificEvent*> eventQueue;
        eventQueue.push(specificEVTToAdd);
        pausedTask->arrivedEvents.insert(std::pair<DependencyKey, std::queue<SpecificEvent*>>(depKey, eventQueue));
      } else {
        arrivedEventsIT->second.push(specificEVTToAdd);
      }
    } else {
      std::map<DependencyKey, int*>::iterator oDit=pausedTask->outstandingDependencies.find(depKey);
      if (oDit != pausedTask->outstandingDependencies.end()) {
        (*(oDit->second))++;
      } else {
        pausedTask->outstandingDependencies.insert(std::pair<DependencyKey, int*>(depKey, new int(1)));
      }
    }
  }

  if (pausedTask->outstandingDependencies.empty()) {
    return generateEventsPayload(pausedTask, NULL);
  } else {
    pausedTasks.push_back(pausedTask);
    // Now release any locks and keep track of the name of these
    std::vector<std::string> releasedLocks=concurrencyControl.releaseCurrentWorkerLocks();
    threadPool.pauseThread(pausedTask, &outstandTaskEvt_lock);
    concurrencyControl.aquireLocks(releasedLocks);  // Reacquire these locks before control goes back into user code
    return generateEventsPayload(pausedTask, NULL);
  }
}

/**
* Retrieves any events that match the provided dependencies, this allows picking off specific dependencies by a task without it having
* to endure the overhead of task restarting
*/
std::pair<int, EDAT_Event*> Scheduler::retrieveAnyMatchingEvents(std::vector<std::pair<int, std::string>> dependencies) {
  std::queue<SpecificEvent*> foundEvents;
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  for (std::pair<int, std::string> dependency : dependencies) {
    DependencyKey depKey = DependencyKey(dependency.second, dependency.first);
    std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator it=outstandingEvents.find(depKey);
    if (it != outstandingEvents.end() && !it->second.empty()) {
      if (it->second.front()->isPersistent()) {
        // If its persistent event then copy the event
        foundEvents.push(new SpecificEvent(*(it->second.front())));
      } else {
        foundEvents.push(it->second.front());
        // If not persistent then remove from outstanding events
        outstandingEventsToHandle--;
        it->second.pop();
        if (it->second.empty()) outstandingEvents.erase(it);
      }
    }
  }
  if (!foundEvents.empty()) {
    int num_found_events=foundEvents.size();
    EDAT_Event * events_payload = new EDAT_Event[num_found_events];
    for (int i=0;i<num_found_events;i++) {
      SpecificEvent * specEvent=foundEvents.front();
      foundEvents.pop();
      // Using a queue and iterating from the start guarantees event ordering
      generateEventPayload(specEvent, &events_payload[i]);
      delete specEvent;
    }
    return std::pair<int, EDAT_Event*>(num_found_events, events_payload);
  } else {
    return std::pair<int, EDAT_Event*>(0, NULL);
  }
}

/**
* Consumes events by persistent tasks, this is needed as lots of events can be stored and then when we register a persistent task we then want
* to consume all of these. But as we don't want to duplicate tasks internally (especially with lots of dependencies) then handle as tasks are queued for execution
* only. Hence we need to call this when a task is registered (might consume multiple outstanding events) or an event arrives (might fire a task which then
* unlocks consumption of other events.)
*/
void Scheduler::consumeEventsByPersistentTasks() {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("consumeEventsByPersistentTasks");
  #endif
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  bool consumingEvents=checkProgressPersistentTasks();
  while (consumingEvents) consumingEvents=checkProgressPersistentTasks();
  #if DO_METRICS
    metrics::METRICS->timerStop("consumeEventsByPersistentTasks", timer_key);
  #endif
}

/**
* Removes a task (removes it from the task list) based upon its name
*/
bool Scheduler::removeTask(std::string taskName) {
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  std::vector<PendingTaskDescriptor*>::iterator task_iterator=locatePendingTaskFromName(taskName);
  if (task_iterator != registeredTasks.end()) {
    registeredTasks.erase(task_iterator);
    return true;
  } else {
    return false;
  }
}

/**
* Determines whether a task is submitted or not (based upon its name)
*/
bool Scheduler::edatIsTaskSubmitted(std::string taskName) {
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  std::vector<PendingTaskDescriptor*>::iterator task_iterator=locatePendingTaskFromName(taskName);
  return task_iterator != registeredTasks.end();
}

/**
* Returns an iterator to a specific task based on its name or the end of the vector if none is found
*/
std::vector<PendingTaskDescriptor*>::iterator Scheduler::locatePendingTaskFromName(std::string taskName) {
  std::vector<PendingTaskDescriptor*>::iterator it;
  for (it = registeredTasks.begin(); it < registeredTasks.end(); it++) {
    if (!(*it)->task_name.empty() && taskName == (*it)->task_name) return it;
  }
  return it;
}

/**
* Checks all persistent tasks for whether they can consume events, if so will do consumption and event better if we can execute some then this will do. Note that
* for each persistent task will only execute once (i.e. this might directly unlock the next iteration of that task which can comsume more and hence run itself.)
* If any tasks run then returns true, this means it is worth calling again to potentially execute further tasks.
*/
bool Scheduler::checkProgressPersistentTasks() {
  bool progress=false;
  for (PendingTaskDescriptor * pendingTask : registeredTasks) {
    if (pendingTask->persistent) {
      std::vector<DependencyKey> dependenciesToRemove;
      for (std::pair<DependencyKey, int*> dependency : pendingTask->outstandingDependencies) {
        std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator it=outstandingEvents.find(dependency.first);
        if (it != outstandingEvents.end() && !it->second.empty()) {
          pendingTask->numArrivedEvents++;
          SpecificEvent * specificEVTToAdd;
          if (it->second.front()->isPersistent()) {
            // If its persistent event then copy the event
            specificEVTToAdd=new SpecificEvent(*(it->second.front()));
          } else {
            specificEVTToAdd=it->second.front();
            // If not persistent then remove from outstanding events
            outstandingEventsToHandle--;
            it->second.pop();
            if (it->second.empty()) outstandingEvents.erase(it);
          }

          std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator arrivedEventsIT = pendingTask->arrivedEvents.find(dependency.first);
          if (arrivedEventsIT == pendingTask->arrivedEvents.end()) {
            std::queue<SpecificEvent*> eventQueue;
            eventQueue.push(specificEVTToAdd);
            pendingTask->arrivedEvents.insert(std::pair<DependencyKey, std::queue<SpecificEvent*>>(dependency.first, eventQueue));
          } else {
            arrivedEventsIT->second.push(specificEVTToAdd);
          }
          (*(dependency.second))--;
          if (*(dependency.second) <= 0) {
            dependenciesToRemove.push_back(dependency.first);
          }
        }
      }
      if (!dependenciesToRemove.empty()) {
        for (DependencyKey k : dependenciesToRemove) pendingTask->outstandingDependencies.erase(k);
      }
      if (pendingTask->outstandingDependencies.empty()) {
        PendingTaskDescriptor* exec_Task=new PendingTaskDescriptor(*pendingTask);
        for (std::pair<DependencyKey, int*> dependency : pendingTask->originalDependencies) {
          pendingTask->outstandingDependencies.insert(std::pair<DependencyKey, int*>(dependency.first, new int(*(dependency.second))));
        }
        pendingTask->arrivedEvents.clear();
        pendingTask->numArrivedEvents=0;
        readyToRunTask(exec_Task);
        progress=true;
      }
    }
  }
  return progress;
}

/**
* This method supports the registering of multiple events and will attempt to match these up to one or more tasks which greedily consume events
* and then fire off any applicable tasks when the dependencies have been met.
*/
void Scheduler::registerEvents(std::vector<SpecificEvent*> events) {
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  std::vector<DependencyKey> dependencies_to_remove;
  std::map<DependencyKey, int*>::iterator it;
  std::vector<PendingTaskDescriptor *> tasksToRun;
  std::vector<int> events_to_remove, pendingTasksToRemove;
  int i=0, j=0;
  for (PendingTaskDescriptor * pendingTask : registeredTasks) {
    if (pendingTask->greedyConsumerOfEvents) {
      for (std::pair<DependencyKey, int*> dependency : pendingTask->outstandingDependencies) {
        j=0;
        for (SpecificEvent* event : events) {
          DependencyKey dK=DependencyKey(event->getEventId(), event->getSourcePid());
          if (dK == dependency.first) {
            events_to_remove.push_back(j);
            (*(dependency.second))--;
            pendingTask->numArrivedEvents++;
            std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator arrivedEventsIT = pendingTask->arrivedEvents.find(dK);
            if (arrivedEventsIT == pendingTask->arrivedEvents.end()) {
              std::queue<SpecificEvent*> eventQueue;
              eventQueue.push(event);
              pendingTask->arrivedEvents.insert(std::pair<DependencyKey, std::queue<SpecificEvent*>>(dK, eventQueue));
            } else {
              arrivedEventsIT->second.push(event);
            }
          }
          j++;
        }
        if (!events_to_remove.empty()) {
          // Go backwards through list to avoid out of bounds iterator
          for (std::vector<int>::iterator it = events_to_remove.end()-1;it >= events_to_remove.begin() ; it--) {
            events.erase(events.begin() + *it);
          }
          events_to_remove.clear();
        }
        if ((*(dependency.second)) <= 0) dependencies_to_remove.push_back(dependency.first);
      }
      if (!dependencies_to_remove.empty()) {
        for (DependencyKey dk : dependencies_to_remove) pendingTask->outstandingDependencies.erase(dk);
        dependencies_to_remove.clear();
      }
      if (pendingTask->outstandingDependencies.empty()) {
        PendingTaskDescriptor* exec_Task;
        if (!pendingTask->persistent) {
          pendingTasksToRemove.push_back(i);
          exec_Task=pendingTask;
        } else {
          exec_Task=new PendingTaskDescriptor(*pendingTask);
          for (std::pair<DependencyKey, int*> dependency : pendingTask->originalDependencies) {
            pendingTask->outstandingDependencies.insert(std::pair<DependencyKey, int*>(dependency.first, new int(*(dependency.second))));
          }
          pendingTask->arrivedEvents.clear();
          pendingTask->numArrivedEvents=0;
        }
        tasksToRun.push_back(exec_Task);
      }
    }
    i++;
  }
  for (int taskToRemove : pendingTasksToRemove) {
    registeredTasks.erase(registeredTasks.begin() + taskToRemove);
  }
  if (!tasksToRun.empty() || !events.empty()) {
    outstandTaskEvt_lock.unlock();
    for (PendingTaskDescriptor * pt : tasksToRun) {
      readyToRunTask(pt);
    }
    for (SpecificEvent* event : events) {
      registerEvent(event);
    }
  }
}

/**
* Registers an event and will search through the registered and paused tasks to figure out if this can be consumed directly (which might then cause the
* task to execute/resume) or whether it needs to be stored as there is no registered task that can consume it currently.
*/
void Scheduler::registerEvent(SpecificEvent * event) {
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  std::pair<TaskDescriptor*, int> pendingEntry=findTaskMatchingEventAndUpdate(event);
  bool firstIt=true;

  while (pendingEntry.first != NULL && (event->isPersistent() || firstIt)) {
    if (pendingEntry.first->getDescriptorType() == PENDING) {
      PendingTaskDescriptor * pendingTask = (PendingTaskDescriptor*) pendingEntry.first;
      if (pendingTask->outstandingDependencies.empty()) {
        PendingTaskDescriptor* exec_Task;
        if (!pendingTask->persistent) {
          registeredTasks.erase(registeredTasks.begin() + pendingEntry.second);
          exec_Task=pendingTask;
        } else {
          exec_Task=new PendingTaskDescriptor(*pendingTask);
          for (std::pair<DependencyKey, int*> dependency : pendingTask->originalDependencies) {
            pendingTask->outstandingDependencies.insert(std::pair<DependencyKey, int*>(dependency.first, new int(*(dependency.second))));
          }
          pendingTask->arrivedEvents.clear();
          pendingTask->numArrivedEvents=0;
        }
        outstandTaskEvt_lock.unlock();
        readyToRunTask(exec_Task);
        consumeEventsByPersistentTasks();
      }
    } else if (pendingEntry.first->getDescriptorType() == PAUSED) {
      PausedTaskDescriptor * pausedTask = (PausedTaskDescriptor*) pendingEntry.first;
      if (pausedTask->outstandingDependencies.empty()) {
        pausedTasks.erase(pausedTasks.begin() + pendingEntry.second);
        outstandTaskEvt_lock.unlock();
        threadPool.markThreadResume(pausedTask);
      }
    } else {
      raiseError("Task descriptor was not a pending or paused task");
    }
    if (event->isPersistent()) {
      if (!outstandTaskEvt_lock.owns_lock()) outstandTaskEvt_lock.lock();
      // If this is a persistent event keep trying to consume tasks to match against as many as possible
      pendingEntry=findTaskMatchingEventAndUpdate(event);
    } else {
      // If not a persistent task then the event has been consumed and don't do another iteration
      firstIt=false;
    }
  }

  if (pendingEntry.first == NULL) {
    // Will always hit here if the event is persistent as it consumes in the above loop until there are no more pending, matching tasks
    DependencyKey dK=DependencyKey(event->getEventId(), event->getSourcePid());
    std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator it = outstandingEvents.find(dK);
    if (it == outstandingEvents.end()) {
      std::queue<SpecificEvent*> eventQueue;
      eventQueue.push(event);
      outstandingEvents.insert(std::pair<DependencyKey, std::queue<SpecificEvent*>>(dK, eventQueue));
    } else {
      it->second.push(event);
    }

    if (!event->isPersistent()) outstandingEventsToHandle++;
  }
}

/**
* Finds a task that depends on a specific event and updates the outstanding dependencies of that task to no longer be waiting for this
* and place this event in the arrived dependencies of that task. IT will return either the task itself (and index, as the task might be
* runnable hence we need to remove it) or NULL and -1 if no task was found. There is a priority given to registered tasks and then after this
* tasks that are paused and waiting for dependencies to resume.
*/
std::pair<TaskDescriptor*, int> Scheduler::findTaskMatchingEventAndUpdate(SpecificEvent * event) {
  DependencyKey eventDep = DependencyKey(event->getEventId(), event->getSourcePid());
  int i=0;
  std::map<DependencyKey, int*>::iterator it;
  for (PendingTaskDescriptor * pendingTask : registeredTasks) {
    it = pendingTask->outstandingDependencies.find(eventDep);
    if (it != pendingTask->outstandingDependencies.end()) {
      updateMatchingEventInTaskDescriptor(pendingTask, eventDep, it, event);
      return std::pair<TaskDescriptor*, int>(pendingTask, i);
    }
    i++;
  }

  i=0;
  for (PausedTaskDescriptor * pausedTask : pausedTasks) {
    it = pausedTask->outstandingDependencies.find(eventDep);
    if (it != pausedTask->outstandingDependencies.end()) {
      updateMatchingEventInTaskDescriptor(pausedTask, eventDep, it, event);
      return std::pair<TaskDescriptor*, int>(pausedTask, i);
    }
    i++;
  }
  return std::pair<TaskDescriptor*, int>(NULL, -1);
}

/**
* Updates the (found) matching event in the descriptor of the task to go from outstanding to arrived. If the event is persistent then this is a copy of the
* event, otherwise the event directly.
*/
void Scheduler::updateMatchingEventInTaskDescriptor(TaskDescriptor * taskDescriptor, DependencyKey eventDep,
                                                    std::map<DependencyKey, int*>::iterator it, SpecificEvent * event) {
  taskDescriptor->numArrivedEvents++;
  (*(it->second))--;
  if (*(it->second) <= 0) {
    taskDescriptor->outstandingDependencies.erase(it);
  }

  SpecificEvent * specificEVTToAdd;
  if (event->isPersistent()) {
    // If its persistent event then copy the event
    specificEVTToAdd=new SpecificEvent(*event);
  } else {
    specificEVTToAdd=event;
  }
  std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator arrivedEventsIT = taskDescriptor->arrivedEvents.find(eventDep);
  if (arrivedEventsIT == taskDescriptor->arrivedEvents.end()) {
    std::queue<SpecificEvent*> eventQueue;
    eventQueue.push(specificEVTToAdd);
    taskDescriptor->arrivedEvents.insert(std::pair<DependencyKey, std::queue<SpecificEvent*>>(eventDep, eventQueue));
  } else {
    arrivedEventsIT->second.push(specificEVTToAdd);
  }
}

/**
* Marks that a specific task is ready to run. It will pass this onto the thread pool which will try and map this to a free thread if it can, otherwise if there are no idle threads
* then the thread pool will queue it up for execution when a thread becomes available.
*/
void Scheduler::readyToRunTask(PendingTaskDescriptor * taskDescriptor) {
  threadPool.startThread(threadBootstrapperFunction, new TaskExecutionContext(taskDescriptor, &concurrencyControl));
}

EDAT_Event * Scheduler::generateEventsPayload(TaskDescriptor * taskContainer, std::set<int> * eventsThatAreContexts) {
  EDAT_Event * events_payload = new EDAT_Event[taskContainer->numArrivedEvents];
  int i=0;
  if (taskContainer->greedyConsumerOfEvents) {
    // If this is a greedy consumer then just consume events in any order
    for (std::pair<DependencyKey, std::queue<SpecificEvent*>> events : taskContainer->arrivedEvents) {
      while (!events.second.empty()) {
        SpecificEvent * event = events.second.front();
        events.second.pop();
        generateEventPayload(event, &events_payload[i]);
        i++;
      }
    }
  } else {
    for (DependencyKey dependencyKey : taskContainer->taskDependencyOrder) {
      // Pick them off this way to ensure ordering of dependencies wrt task definition for non-greedy consumers
      std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator arrivedEventsIT = taskContainer->arrivedEvents.find(dependencyKey);
      if (arrivedEventsIT == taskContainer->arrivedEvents.end()) {
        raiseError("Can not find the corresponding event dependency key when mapping the task onto a thread\n");
      }
      if (arrivedEventsIT->second.size() <=0) {
        raiseError("Too few events with a corresponding EID for when mapping the task onto a thread\n");
      }
      SpecificEvent * specEvent=arrivedEventsIT->second.front();
      arrivedEventsIT->second.pop();
      generateEventPayload(specEvent, &events_payload[i]);
      if (specEvent->isAContext() && eventsThatAreContexts != NULL) eventsThatAreContexts->emplace(i);
      i++;
    }
  }
  return events_payload;
}

/**
* Generates the EDAT_Event payload (that is provided to the user function) from the specific event object passed in
*/
void Scheduler::generateEventPayload(SpecificEvent * specEvent, EDAT_Event * event) {
  if (specEvent->isAContext()) {
    // If its a context then de-reference the pointer to point to the memory directly and don't free the pointer (as would free the context!)
    event->data=*((char**) specEvent->getData());
  } else {
    event->data=specEvent->getData();
  }
  event->metadata.data_type=specEvent->getMessageType();
  if (event->metadata.data_type == EDAT_NOTYPE) {
    event->metadata.number_elements=0;
  } else {
    event->metadata.number_elements=specEvent->getMessageLength();
  }
  event->metadata.source=specEvent->getSourcePid();
  int event_id_len=specEvent->getEventId().size();
  char * event_id=(char*) malloc(event_id_len + 1);
  memcpy(event_id, specEvent->getEventId().c_str(), event_id_len+1);
  event->metadata.event_id=event_id;
}

/**
* This is the entry point for the thread to execute a task which is provided. In addition to the marshalling required to then call into the task
* with the correct arguments, it also frees the data at the end and will check the task queue. If there are outstanding tasks in the queue then these
* in tern will also be executed by this thread
*/
void Scheduler::threadBootstrapperFunction(void * pthreadRawData) {
  TaskExecutionContext * taskContext = (TaskExecutionContext *) pthreadRawData;
  PendingTaskDescriptor * pendingTaskDescription=taskContext->taskDescriptor;

  std::set<int> eventsThatAreContexts;

  EDAT_Event * events_payload = generateEventsPayload(pendingTaskDescription, &eventsThatAreContexts);
  pendingTaskDescription->task_fn(events_payload, pendingTaskDescription->numArrivedEvents);
  taskContext->concurrencyControl->releaseCurrentWorkerLocks(); // Release any locks held by the task
  for (int j=0;j<pendingTaskDescription->numArrivedEvents;j++) {
    free(events_payload[j].metadata.event_id);
    if (pendingTaskDescription->freeData && events_payload[j].data != NULL && eventsThatAreContexts.count(j) == 0) free(events_payload[j].data);
  }
  delete[] events_payload;
  delete pendingTaskDescription;
  delete taskContext;
}

/**
* Locks the mutex for testing for finalisation (whether the scheduler is completed, no tasks or events outstanding)
*/
void Scheduler::lockMutexForFinalisationTest() {
  taskAndEvent_mutex.lock();
}

/**
* Unlocks the mutex for testing for finalisation
*/
void Scheduler::unlockMutexForFinalisationTest() {
  taskAndEvent_mutex.unlock();
}

/**
* Determines whether the scheduler is finished or not
*/
bool Scheduler::isFinished() {
  for (PendingTaskDescriptor * pendingTask : registeredTasks) {
    if (!pendingTask->persistent) return false;
  }
  return outstandingEventsToHandle==0;
}
