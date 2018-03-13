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

void Scheduler::registerTask(void (*task_fn)(EDAT_Event*, int), std::vector<std::pair<int, std::string>> dependencies) {
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  PendingTaskDescriptor * pendingTask=new PendingTaskDescriptor();
  pendingTask->task_fn=task_fn;
  pendingTask->freeData=true;
  for (std::pair<int, std::string> dependency : dependencies) {
    std::map<DependencyKey, SpecificEvent*>::iterator it;
    DependencyKey depKey = DependencyKey(dependency.second, dependency.first);
    it=outstandingEvents.find(depKey);
    if (it != outstandingEvents.end()) {
      pendingTask->arrivedEvents.push_back(it->second);
      outstandingEvents.erase(it);
    } else {
      pendingTask->outstandingDependencies.insert(depKey);
    }
  }

  if (pendingTask->outstandingDependencies.empty()) {
    outstandTaskEvt_lock.unlock();
    readyToRunTask(pendingTask);
  } else {
    registeredTasks.push_back(pendingTask);
  }
}

void Scheduler::registerEvent(SpecificEvent * event) {
  std::unique_lock<std::mutex> outstandTaskEvt_lock(taskAndEvent_mutex);
  std::pair<PendingTaskDescriptor*, int> pendingTask=findTaskMatchingEventAndUpdate(event);
  if (pendingTask.first != NULL) {
    if (pendingTask.first->outstandingDependencies.empty()) {
      registeredTasks.erase(registeredTasks.begin() + pendingTask.second);
      outstandTaskEvt_lock.unlock();
      readyToRunTask(pendingTask.first);
    }
  } else {
    outstandingEvents.insert(std::pair<DependencyKey, SpecificEvent*>(DependencyKey(event->getEventId(), event->getSourcePid()), event));
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
  return registeredTasks.empty() && outstandingEvents.empty() && taskQueue.empty();
}
