#include "messaging.h"
#include <vector>
#include <thread>

/**
* If an event is local (to myself) then fire it locally rather than hitting the messaging system, does just one at a time to avoid starvation
*/
bool Messaging::fireASingleLocalEvent() {
  if (outstandingEvents.size() > 0) {
    scheduler.registerEvent(outstandingEvents.front());
    outstandingEvents.erase(outstandingEvents.begin());
    return true;
  } else {
    return false;
  }
}

/**
* Starts the polling for events in a thread
*/
void Messaging::pollForEvents() {
  pollingThread=new std::thread(&Messaging::startPollForEvents, this);
}

/**
* Entry method for polling for events in a thread, after this has completed it will wake up the main thread (waiting in finalisation) as everything is
* ready to be shutdown
*/
void Messaging::startPollForEvents() {
  runPollForEvents();
  // Wake up condition variable now
  std::lock_guard<std::mutex> lock(cdtAccessMtx);
  std::lock_guard<std::mutex> lk(*mainThreadConditionVarMutex);
  *mainThreadConditionVarPred=true;
  mainThreadConditionVariable->notify_one();
}

/**
* Finalises the messaging system by joining on the polling thread
*/
void Messaging::finalise() {
  pollingThread->join();
}

/**
* Checks for overall local termination based upon whether the main thread is sleeping and whether the messaging system, schedule and thread pool is finished
*/
bool Messaging::checkForLocalTermination() {
  std::lock_guard<std::mutex> lock(cdtAccessMtx);
  if (mainThreadConditionVariable != NULL) {
    if (isFinished() && scheduler.isFinished() && threadPool.isThreadPoolFinished()) {
      return true;
    }
  }
  return false;
}

/**
* Called from the main edat file upon finalisation. This hooks up the termination condition variable that will be slept on by the main thread
* and we will wake up from inside here when termination has been determined.
*/
void Messaging::attachMainThread(std::condition_variable * cdt, std::mutex * cdt_mutex, bool * cdt_pred) {
  std::lock_guard<std::mutex> lock(cdtAccessMtx);
  this->mainThreadConditionVariable = cdt;
  this->mainThreadConditionVarMutex = cdt_mutex;
  this->mainThreadConditionVarPred = cdt_pred;
}
