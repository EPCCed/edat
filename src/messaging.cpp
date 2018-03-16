#include "messaging.h"
#include <vector>
#include <thread>

bool Messaging::fireASingleLocalEvent() {
  if (outstandingEvents.size() > 0) {
    scheduler.registerEvent(outstandingEvents.front());
    outstandingEvents.erase(outstandingEvents.begin());
    return true;
  } else {
    return false;
  }
}

void Messaging::pollForEvents() {
  pollingThread=new std::thread(&Messaging::startPollForEvents, this);
}

void Messaging::startPollForEvents() {
  runPollForEvents();
  // Wake up condition variable now
  std::lock_guard<std::mutex> lock(cdtAccessMtx);
  std::lock_guard<std::mutex> lk(*mainThreadConditionVarMutex);
  *mainThreadConditionVarPred=true;
  mainThreadConditionVariable->notify_one();
}

void Messaging::finalise() {
  pollingThread->join();
}

bool Messaging::checkForTermination() {
  std::lock_guard<std::mutex> lock(cdtAccessMtx);
  if (mainThreadConditionVariable != NULL) {
    if (isFinished() && scheduler.isFinished() && threadPool.isThreadPoolFinished()) {
      return true;
    }
  }
  return false;
}

void Messaging::attachMainThread(std::condition_variable * cdt, std::mutex * cdt_mutex, bool * cdt_pred) {
  std::lock_guard<std::mutex> lock(cdtAccessMtx);
  this->mainThreadConditionVariable = cdt;
  this->mainThreadConditionVarMutex = cdt_mutex;
  this->mainThreadConditionVarPred = cdt_pred;
}
