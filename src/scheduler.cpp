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
    pthread_raw_data_struct * pthreadData = new pthread_raw_data_struct();
    pthreadData->event_metadata=event;
    pthreadData->task_fn=scheduledTasks.find(event->getUniqueId())->second;
    threadPool.startThread(threadBootstrapperFunction, pthreadData);
    threadPool.startThread((void (*)(void *)) task_fn, (void*) event);
  } else {
    outstandReq_lock.unlock();
    std::lock_guard<std::mutex> sched_tasks_lock(scheduledTasks_mutex);
    scheduledTasks.insert(std::pair<std::string, void (*)(void *, EDAT_Metadata)>(uniqueId, task_fn));
  }
}

void Scheduler::registerEvent(SpecificEvent * event) {
  std::unique_lock<std::mutex> sched_tasks_lock(scheduledTasks_mutex);
  if (scheduledTasks.count(event->getUniqueId())) {
    pthread_raw_data_struct * pthreadData = new pthread_raw_data_struct();
    pthreadData->event_metadata=event;
    pthreadData->task_fn=scheduledTasks.find(event->getUniqueId())->second;
    threadPool.startThread(threadBootstrapperFunction, pthreadData);
    scheduledTasks.erase(event->getUniqueId());
  } else {
    sched_tasks_lock.unlock();
    std::lock_guard<std::mutex> lock(outstandingRequests_mutex);
    outstandingRequests.insert(std::pair<std::string, SpecificEvent*>(event->getUniqueId(), event));
  }
}

void Scheduler::threadBootstrapperFunction(void * pthreadRawData) {
  struct pthread_raw_data_struct * pthreadData=(struct pthread_raw_data_struct *) pthreadRawData;
  struct edat_struct_metadata metaToPass;
  metaToPass.data_type=pthreadData->event_metadata->getMessageType();
  if (metaToPass.data_type == EDAT_NOTYPE) {
    metaToPass.number_elements=0;
  } else {
    metaToPass.number_elements=pthreadData->event_metadata->getMessageLength() / getTypeSize(metaToPass.data_type);
  }
  metaToPass.source=pthreadData->event_metadata->getSourcePid();
  int uuid_len=pthreadData->event_metadata->getUniqueId().size();
  char * uuid=(char*) malloc(uuid_len + 1);
  memcpy(uuid, pthreadData->event_metadata->getUniqueId().c_str(), uuid_len);
  metaToPass.unique_id=uuid;

  pthreadData->task_fn(pthreadData->event_metadata->getData(), metaToPass);
  free(uuid);
  free(pthreadRawData);
}

bool Scheduler::isFinished() {
  std::lock_guard<std::mutex> sched_tasks_lock(scheduledTasks_mutex);
  std::lock_guard<std::mutex> lock(outstandingRequests_mutex);
  return scheduledTasks.empty() && outstandingRequests.empty();
}
