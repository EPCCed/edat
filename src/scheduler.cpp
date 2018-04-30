#include "scheduler.h"
#include "edat.h"
#include "threadpool.h"
#include "misc.h"
#include <map>
#include <string>
#include <mutex>
#include <stdlib.h>
#include <string.h>
#include <queue>
#include <utility>
#include <set>

/**
* Registers a task with EDAT, this will determine (and consume) outstanding events & then if applicable will mark ready for execution. Otherwise
* it will store the task in a scheduled state. Persistent tasks are duplicated if they are executed and the duplicate run to separate it from
* the stored version which will be updated by other events arriving.
*/
void Scheduler::registerTask(void (*task_fn)(EDAT_Event*, int), std::string task_name, std::vector<std::pair<int, std::string>> dependencies, bool persistent) {
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  PendingTaskDescriptor * pendingTask=new PendingTaskDescriptor();
  pendingTask->task_fn=task_fn;
  pendingTask->freeData=true;
  pendingTask->persistent=persistent;
  pendingTask->task_name=task_name;
  for (std::pair<int, std::string> dependency : dependencies) {
    DependencyKey depKey = DependencyKey(dependency.second, dependency.first);
    std::map<DependencyKey, int*>::iterator oDit=pendingTask->originalDependencies.find(depKey);
    if (oDit != pendingTask->originalDependencies.end()) {
      (*(oDit->second))++;
    } else {
      pendingTask->originalDependencies.insert(std::pair<DependencyKey, int*>(depKey, new int(1)));
    }
    std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator it=outstandingEvents.find(depKey);
    if (it != outstandingEvents.end() && !it->second.empty()) {
      if (it->second.front()->isPersistent()) {
        // If its persistent event then copy the event
        pendingTask->arrivedEvents.push_back(new SpecificEvent(*(it->second.front())));
      } else {
        pendingTask->arrivedEvents.push_back(it->second.front());
        // If not persistent then remove from outstanding events
        outstandingEventsToHandle--;
        it->second.pop();
        if (it->second.empty()) outstandingEvents.erase(it);
      }
    } else {
      oDit=pendingTask->outstandingDependencies.find(depKey);
      if (oDit != pendingTask->outstandingDependencies.end()) {
        (*(oDit->second))++;
      } else {
        pendingTask->outstandingDependencies.insert(std::pair<DependencyKey, int*>(depKey, new int(1)));
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
* Consumes events by persistent tasks, this is needed as lots of events can be stored and then when we register a persistent task we then want
* to consume all of these. But as we don't want to duplicate tasks internally (especially with lots of dependencies) then handle as tasks are queued for execution
* only. Hence we need to call this when a task is registered (might consume multiple outstanding events) or an event arrives (might fire a task which then
* unlocks consumption of other events.)
*/
void Scheduler::consumeEventsByPersistentTasks() {
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  bool consumingEvents=checkProgressPersistentTasks();
  while (consumingEvents) consumingEvents=checkProgressPersistentTasks();
}

/**
* Deschedules a task (removes it from the task list) based upon its name
*/
bool Scheduler::descheduleTask(std::string taskName) {
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
* Determines whether a task is scheduled or not (based upon its name)
*/
bool Scheduler::isTaskScheduled(std::string taskName) {
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
      for (std::pair<DependencyKey, int*> dependency : pendingTask->outstandingDependencies) {
        std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator it=outstandingEvents.find(dependency.first);
        if (it != outstandingEvents.end() && !it->second.empty()) {
          if (it->second.front()->isPersistent()) {
            // If its a persistent event then copy the event
            pendingTask->arrivedEvents.push_back(new SpecificEvent(*(it->second.front())));
          } else {
            pendingTask->arrivedEvents.push_back(it->second.front());
            // If not persistent then remove from outstanding events
            outstandingEventsToHandle--;
            it->second.pop();
            if (it->second.empty()) outstandingEvents.erase(it);
          }
          (*(dependency.second))--;
          if (*(dependency.second) <= 0) {
            pendingTask->outstandingDependencies.erase(dependency.first);
          }
        }
      }
      if (pendingTask->outstandingDependencies.empty()) {
        PendingTaskDescriptor* exec_Task=new PendingTaskDescriptor(*pendingTask);
        for (std::pair<DependencyKey, int*> dependency : pendingTask->originalDependencies) {
          pendingTask->outstandingDependencies.insert(std::pair<DependencyKey, int*>(dependency.first, new int(*(dependency.second))));
        }
        pendingTask->arrivedEvents.clear();
        readyToRunTask(exec_Task);
        progress=true;
      }
    }
  }
  return progress;
}

/**
* Registers an event and will search through the registered tasks to figure out if this can be consumed directly (which might then cause the
* task to execute) or whether it needs to be stored as there is no scheduled task that can consume it currently.
*/
void Scheduler::registerEvent(SpecificEvent * event) {
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  std::pair<PendingTaskDescriptor*, int> pendingTask=findTaskMatchingEventAndUpdate(event);
  bool firstIt=true;

  while (pendingTask.first != NULL && (event->isPersistent() || firstIt)) {
    if (pendingTask.first->outstandingDependencies.empty()) {
      PendingTaskDescriptor* exec_Task;
      if (!pendingTask.first->persistent) {
        registeredTasks.erase(registeredTasks.begin() + pendingTask.second);
        exec_Task=pendingTask.first;
      } else {
        exec_Task=new PendingTaskDescriptor(*pendingTask.first);
        for (std::pair<DependencyKey, int*> dependency : pendingTask.first->originalDependencies) {
          pendingTask.first->outstandingDependencies.insert(std::pair<DependencyKey, int*>(dependency.first, new int(*(dependency.second))));
        }
        pendingTask.first->arrivedEvents.clear();
      }
      outstandTaskEvt_lock.unlock();
      readyToRunTask(exec_Task);
      consumeEventsByPersistentTasks();
    }
    if (event->isPersistent()) {
      // If this is a persistent event keep trying to consume tasks to match against as many as possible
      pendingTask=findTaskMatchingEventAndUpdate(event);
    } else {
      // If not a persistent task then the event has been consumed and don't do another iteration
      firstIt=false;
    }
  }

  if (pendingTask.first == NULL) {
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
* runnable hence we need to remove it) or NULL and -1 if no task was found.
*/
std::pair<PendingTaskDescriptor*, int> Scheduler::findTaskMatchingEventAndUpdate(SpecificEvent * event) {
  DependencyKey eventDep = DependencyKey(event->getEventId(), event->getSourcePid());
  int i=0;
  for (PendingTaskDescriptor * pendingTask : registeredTasks) {
    std::map<DependencyKey, int*>::iterator it = pendingTask->outstandingDependencies.find(eventDep);
    if (it != pendingTask->outstandingDependencies.end()) {
      (*(it->second))--;
      if (*(it->second) <= 0) {
        pendingTask->outstandingDependencies.erase(it);
      }
      if (event->isPersistent()) {
        // If its persistent then copy the event
        pendingTask->arrivedEvents.push_back(new SpecificEvent(*event));
      } else {
        pendingTask->arrivedEvents.push_back(event);
      }
      return std::pair<PendingTaskDescriptor*, int>(pendingTask, i);
    }
    i++;
  }
  return std::pair<PendingTaskDescriptor*, int>(NULL, -1);
}

/**
* Marks that a specific task is ready to run. It will pass this onto the thread pool which will try and map this to a free thread if it can, otherwise if there are no idle threads
* then the thread pool will queue it up for execution when a thread becomes available.
*/
void Scheduler::readyToRunTask(PendingTaskDescriptor * taskDescriptor) {
  threadPool.startThread(threadBootstrapperFunction, taskDescriptor);
}

/**
* This is the entry point for the thread to execute a task which is provided. In addition to the marshalling required to then call into the task
* with the correct arguments, it also frees the data at the end and will check the task queue. If there are outstanding tasks in the queue then these
* in tern will also be executed by this thread
*/
void Scheduler::threadBootstrapperFunction(void * pthreadRawData) {
  PendingTaskDescriptor * taskContainer=(PendingTaskDescriptor *) pthreadRawData;
  EDAT_Event * events_payload = new EDAT_Event[taskContainer->arrivedEvents.size()];
  int i=0;
  for (SpecificEvent * specEvent : taskContainer->arrivedEvents) {
    events_payload[i].data=specEvent->getData();
    events_payload[i].metadata.data_type=specEvent->getMessageType();
    if (events_payload[i].metadata.data_type == EDAT_NOTYPE) {
      events_payload[i].metadata.number_elements=0;
    } else {
      events_payload[i].metadata.number_elements=specEvent->getMessageLength() / getTypeSize(events_payload[i].metadata.data_type);
    }
    events_payload[i].metadata.source=specEvent->getSourcePid();
    int event_id_len=specEvent->getEventId().size();
    char * event_id=(char*) malloc(event_id_len + 1);
    memcpy(event_id, specEvent->getEventId().c_str(), event_id_len+1);
    events_payload[i].metadata.event_id=event_id;
    i++;
  }
  taskContainer->task_fn(events_payload, i);
  for (int j=0;j<i;j++) {
    free(events_payload[j].metadata.event_id);
    if (taskContainer->freeData && events_payload[j].data != NULL) free(events_payload[j].data);
  }
  delete events_payload;
  free(pthreadRawData);
}

/**
* Determines whether the scheduler is finished or not
*/
bool Scheduler::isFinished() {
  std::lock_guard<std::mutex> lock(taskAndEvent_mutex);
  for (PendingTaskDescriptor * pendingTask : registeredTasks) {
    if (!pendingTask->persistent) return false;
  }
  return outstandingEventsToHandle==0;
}
