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

std::queue<PendingTaskDescriptor*> Scheduler::taskQueue;
std::mutex Scheduler::taskQueue_mutex;

void Scheduler::registerTask(void (*task_fn)(EDAT_Event*, int), std::vector<std::pair<int, std::string>> dependencies, bool persistent) {
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  PendingTaskDescriptor * pendingTask=new PendingTaskDescriptor();
  pendingTask->task_fn=task_fn;
  pendingTask->freeData=true;
  pendingTask->persistent=persistent;
  for (std::pair<int, std::string> dependency : dependencies) {
    std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator it;
    DependencyKey depKey = DependencyKey(dependency.second, dependency.first);
    pendingTask->originalDependencies.insert(depKey);
    it=outstandingEvents.find(depKey);
    if (it != outstandingEvents.end() && !it->second.empty()) {
      pendingTask->arrivedEvents.push_back(it->second.front());
      it->second.pop();
      if (it->second.empty()) outstandingEvents.erase(it);
    } else {
      pendingTask->outstandingDependencies.insert(depKey);
    }
  }

  if (pendingTask->outstandingDependencies.empty()) {
    PendingTaskDescriptor* exec_Task;
    if (persistent) {
      exec_Task=new PendingTaskDescriptor(*pendingTask);
      pendingTask->outstandingDependencies=pendingTask->originalDependencies;
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
* Checks all persistent tasks for whether they can consume events, if so will do consumption and event better if we can execute some then this will do. Note that
* for each persistent task will only execute once (i.e. this might directly unlock the next iteration of that task which can comsume more and hence run itself.)
* If any tasks run then returns true, this means it is worth calling again to potentially execute further tasks.
*/
bool Scheduler::checkProgressPersistentTasks() {
  bool progress=false;
  for (PendingTaskDescriptor * pendingTask : registeredTasks) {
    if (pendingTask->persistent) {
      for (DependencyKey depKey : pendingTask->outstandingDependencies) {
        std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator it=outstandingEvents.find(depKey);
        if (it != outstandingEvents.end() && !it->second.empty()) {
          pendingTask->arrivedEvents.push_back(it->second.front());
          it->second.pop();
          if (it->second.empty()) outstandingEvents.erase(it);
          pendingTask->outstandingDependencies.erase(depKey);
        }
      }
      if (pendingTask->outstandingDependencies.empty()) {
        PendingTaskDescriptor* exec_Task=new PendingTaskDescriptor(*pendingTask);
        pendingTask->outstandingDependencies=pendingTask->originalDependencies;
        pendingTask->arrivedEvents.clear();
        readyToRunTask(exec_Task);
        progress=true;
      }
    }
  }
  return progress;
}

void Scheduler::registerEvent(SpecificEvent * event) {
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  std::pair<PendingTaskDescriptor*, int> pendingTask=findTaskMatchingEventAndUpdate(event);
  if (pendingTask.first != NULL) {
    if (pendingTask.first->outstandingDependencies.empty()) {
      PendingTaskDescriptor* exec_Task;
      if (!pendingTask.first->persistent) {
        registeredTasks.erase(registeredTasks.begin() + pendingTask.second);
        exec_Task=pendingTask.first;
      } else {
        exec_Task=new PendingTaskDescriptor(*pendingTask.first);
        pendingTask.first->outstandingDependencies=pendingTask.first->originalDependencies;
        pendingTask.first->arrivedEvents.clear();
      }
      outstandTaskEvt_lock.unlock();
      readyToRunTask(exec_Task);
      consumeEventsByPersistentTasks();
    }
  } else {
    DependencyKey dK=DependencyKey(event->getEventId(), event->getSourcePid());
    std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator it = outstandingEvents.find(dK);
    if (it == outstandingEvents.end()) {
      std::queue<SpecificEvent*> eventQueue;
      eventQueue.push(event);
      outstandingEvents.insert(std::pair<DependencyKey, std::queue<SpecificEvent*>>(dK, eventQueue));
    } else {
      it->second.push(event);
    }
  }
}

std::pair<PendingTaskDescriptor*, int> Scheduler::findTaskMatchingEventAndUpdate(SpecificEvent * event) {
  DependencyKey eventDep = DependencyKey(event->getEventId(), event->getSourcePid());
  int i=0;
  for (PendingTaskDescriptor * pendingTask : registeredTasks) {
    std::set<DependencyKey>::iterator it = pendingTask->outstandingDependencies.find(eventDep);
    if (it != pendingTask->outstandingDependencies.end()) {
      pendingTask->outstandingDependencies.erase(it);
      pendingTask->arrivedEvents.push_back(event);
      return std::pair<PendingTaskDescriptor*, int>(pendingTask, i);
    }
    i++;
  }
  return std::pair<PendingTaskDescriptor*, int>(NULL, -1);
}

void Scheduler::readyToRunTask(PendingTaskDescriptor * taskDescriptor) {
  std::lock_guard<std::mutex> lock(taskQueue_mutex);
  bool taskExecuting = threadPool.startThread(threadBootstrapperFunction, taskDescriptor);
  if (!taskExecuting) {
    taskQueue.push(taskDescriptor);
  }
}

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
    memcpy(event_id, specEvent->getEventId().c_str(), event_id_len);
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

  std::unique_lock<std::mutex> lock(taskQueue_mutex);
  if (!taskQueue.empty()) {
    PendingTaskDescriptor * taskDescriptor=taskQueue.front();
    taskQueue.pop();
    lock.unlock();
    threadBootstrapperFunction(taskDescriptor);
  }
}

bool Scheduler::isFinished() {
  std::lock_guard<std::mutex> lock(taskAndEvent_mutex);
  std::lock_guard<std::mutex> tq_lock(taskQueue_mutex);
  for (PendingTaskDescriptor * pendingTask : registeredTasks) {
    if (!pendingTask->persistent) return false;
  }
  return outstandingEvents.empty() && taskQueue.empty();
}
