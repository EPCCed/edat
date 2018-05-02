#include "messaging.h"
#include <vector>
#include <thread>
#include <string.h>
#include "misc.h"

/**
* Constructor which will initialise this aspect of the messaging
*/
Messaging::Messaging(Scheduler & a_scheduler, ThreadPool & a_threadPool, ContextManager& a_contextManager, Configuration & aconfig) : scheduler(a_scheduler),
                      threadPool(a_threadPool), contextManager(a_contextManager), configuration(aconfig) {
  continue_polling=true;
  progress_thread=configuration.get("EDAT_PROGRESS_THREAD", true);
  it_count=0;
}

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
* Starts the progress thread if there is to be one running
*/
void Messaging::startProgressThread() {
  if (progress_thread) pollingThread=new std::thread(&Messaging::entryThreadPollForEvents, this);
}

/**
* Performs a single poll for events and general progress (called when not running with a progress thread.) Note that only one thread at a time
* should be active here hence the protection.
*/
bool Messaging::pollForEvents() {
  if (progress_thread) return true;
  if (singleProgressMtx.try_lock()) {
    bool stillInProgress=performSinglePoll(&it_count);
    if (!stillInProgress) {
      reactivateMainThread();
    }
    singleProgressMtx.unlock();
    return stillInProgress;
  }
  return true;
}

/**
* Reactivates the main thread which we need to do once termination has occurred
*/
void Messaging::reactivateMainThread() {
  std::lock_guard<std::mutex> lock(cdtAccessMtx);
  std::lock_guard<std::mutex> lk(*mainThreadConditionVarMutex);
  *mainThreadConditionVarPred=true;
  mainThreadConditionVariable->notify_one();
}

/**
* Entry method for polling for events in a thread, after this has completed it will wake up the main thread (waiting in finalisation) as everything is
* ready to be shutdown
*/
void Messaging::entryThreadPollForEvents() {
  runPollForEvents();
  // Wake up condition variable now
  reactivateMainThread();
}

/**
* Finalises the messaging system by joining on the polling thread
*/
void Messaging::finalise() {
  if (progress_thread && pollingThread != NULL) pollingThread->join();
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

/**
* Retrieves the size of an event payload type in bytes
*/
int Messaging::getTypeSize(int type) {
  if (type >= BASE_CONTEXT_ID) {
    return contextManager.getContextEventPayloadSize(type);
  } else {
    return getBaseTypeSize(type);
  }
}
