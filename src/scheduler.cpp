#include "scheduler.h"
#include "edat.h"
#include "threadpool.h"
#include <map>
#include <string>

std::map<std::string, void (*)(void *, EDAT_Metadata)> Scheduler::scheduledTasks;
std::map<std::string, SpecificEvent*> Scheduler::outstandingRequests;

void Scheduler::registerTask(void (*task_fn)(void *, EDAT_Metadata), std::string uniqueId) {
  if (outstandingRequests.count(uniqueId)) {
    SpecificEvent * event=outstandingRequests.find(uniqueId)->second;
    outstandingRequests.erase(uniqueId);
    threadPool.startThread((void (*)(void *)) task_fn, (void*) event);
  } else {
    scheduledTasks.insert(std::pair<std::string, void (*)(void *, EDAT_Metadata)>(uniqueId, task_fn));
  }
}

void Scheduler::registerEvent(SpecificEvent * event) {
  if (scheduledTasks.count(event->getUniqueId())) {
    threadPool.startThread((void (*)(void *)) scheduledTasks.find(event->getUniqueId())->second, (void*) event);
    scheduledTasks.erase(event->getUniqueId());
  } else {
    outstandingRequests.insert(std::pair<std::string, SpecificEvent*>(event->getUniqueId(), event));
  }
}
