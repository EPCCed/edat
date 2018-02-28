#include "scheduler.h"
#include "edat.h"
#include "threadpool.h"
#include "misc.h"
#include <map>
#include <string>
#include <mutex>
#include <stdlib.h>
#include <string.h>

std::map<std::string, void (*)(void *, EDAT_Metadata)> Scheduler::scheduledTasks;
std::map<std::string, SpecificEvent*> Scheduler::outstandingRequests;

void Scheduler::registerTask(void (*task_fn)(void *, EDAT_Metadata), std::string uniqueId) {
  std::unique_lock<std::mutex> outstandReq_lock(outstandingRequests_mutex);
  if (outstandingRequests.count(uniqueId)) {
    SpecificEvent * event=outstandingRequests.find(uniqueId)->second;
    outstandingRequests.erase(uniqueId);
    outstandReq_lock.unlock();
    TaskLaunchContainer * taskContainer = new TaskLaunchContainer();
    taskContainer->event_metadata=event;
    taskContainer->freeData=true;
    taskContainer->task_fn=scheduledTasks.find(event->getUniqueId())->second;
    threadPool.startThread(threadBootstrapperFunction, taskContainer);
  } else {
    outstandReq_lock.unlock();
    std::lock_guard<std::mutex> sched_tasks_lock(scheduledTasks_mutex);
    scheduledTasks.insert(std::pair<std::string, void (*)(void *, EDAT_Metadata)>(uniqueId, task_fn));
  }
}

void Scheduler::registerEvent(SpecificEvent * event) {
  std::unique_lock<std::mutex> sched_tasks_lock(scheduledTasks_mutex);
  if (scheduledTasks.count(event->getUniqueId())) {
    TaskLaunchContainer * taskContainer = new TaskLaunchContainer();
    taskContainer->event_metadata=event;
    taskContainer->freeData=true;
    taskContainer->task_fn=scheduledTasks.find(event->getUniqueId())->second;
    threadPool.startThread(threadBootstrapperFunction, taskContainer);
    scheduledTasks.erase(event->getUniqueId());
  } else {
    sched_tasks_lock.unlock();
    std::lock_guard<std::mutex> lock(outstandingRequests_mutex);
    outstandingRequests.insert(std::pair<std::string, SpecificEvent*>(event->getUniqueId(), event));
  }
}

void Scheduler::readyToRunTask(TaskLaunchContainer * taskContainer) {
  threadPool.startThread(threadBootstrapperFunction, taskContainer);
}

void Scheduler::threadBootstrapperFunction(void * pthreadRawData) {
  TaskLaunchContainer * taskContainer=(TaskLaunchContainer *) pthreadRawData;
  struct edat_struct_metadata metaToPass;
  metaToPass.data_type=taskContainer->event_metadata->getMessageType();
  if (metaToPass.data_type == EDAT_NOTYPE) {
    metaToPass.number_elements=0;
  } else {
    metaToPass.number_elements=taskContainer->event_metadata->getMessageLength() / getTypeSize(metaToPass.data_type);
  }
  metaToPass.source=taskContainer->event_metadata->getSourcePid();
  int uuid_len=taskContainer->event_metadata->getUniqueId().size();
  char * uuid=(char*) malloc(uuid_len + 1);
  memcpy(uuid, taskContainer->event_metadata->getUniqueId().c_str(), uuid_len);
  metaToPass.unique_id=uuid;

  taskContainer->task_fn(taskContainer->event_metadata->getData(), metaToPass);
  free(uuid);
  if (taskContainer->freeData && taskContainer->event_metadata->getData() != NULL) free(taskContainer->event_metadata->getData());
  free(pthreadRawData);
}

bool Scheduler::isFinished() {
  std::lock_guard<std::mutex> sched_tasks_lock(scheduledTasks_mutex);
  std::lock_guard<std::mutex> lock(outstandingRequests_mutex);
  return scheduledTasks.empty() && outstandingRequests.empty();
}
