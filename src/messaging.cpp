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
  continue_polling=false;
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

void Messaging::resetPolling() {
  continue_polling=true;
  startProgressThread();
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
  if (!syntheticFailure) reactivateMainThread();
}

/**
* Finalises the messaging system by joining on the polling thread
*/
void Messaging::finalise() {
  if (progress_thread && pollingThread != NULL) pollingThread->join();
}

/**
* Checks for overall local termination based upon whether the main thread is sleeping and whether the messaging system, schedule and thread pool is finished. This is
* slightly complicated by the fact that we need to ensure all these three components (messaging layer, scheduler and thread pool) are consistent with each other,
* there is a danger that the state of one of these might change during the finalisation test. This is why we lock all three components for finalisation testing
* before carrying it out.
*/
bool Messaging::checkForLocalTermination() {
  std::lock_guard<std::mutex> lock(cdtAccessMtx);
  if (mainThreadConditionVariable != NULL) {
    lockMutexForFinalisationTest();
    scheduler.lockMutexForFinalisationTest();
    threadPool.lockMutexForFinalisationTest();
    bool finished=isFinished() && scheduler.isFinished() && threadPool.isThreadPoolFinished();
    threadPool.unlockMutexForFinalisationTest();
    scheduler.unlockMutexForFinalisationTest();
    unlockMutexForFinalisationTest();
    if (finished) return true;
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
