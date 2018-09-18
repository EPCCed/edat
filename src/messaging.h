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

#ifndef SRC_MESSAGING_H_
#define SRC_MESSAGING_H_

#include "scheduler.h"
#include "threadpool.h"
#include "contextmanager.h"
#include "configuration.h"
#include <vector>
#include <thread>

class Messaging {
  std::vector<SpecificEvent*> outstandingEvents;
  std::thread * pollingThread=NULL;
  std::condition_variable * mainThreadConditionVariable= NULL;
  std::mutex * mainThreadConditionVarMutex, cdtAccessMtx, singleProgressMtx;
  bool * mainThreadConditionVarPred;
  virtual void entryThreadPollForEvents();
  virtual void reactivateMainThread();
protected:
  Scheduler & scheduler;
  ThreadPool & threadPool;
  ContextManager & contextManager;
  Configuration & configuration;
  bool continue_polling;
  bool progress_thread;
  int it_count;
  virtual bool fireASingleLocalEvent();
  virtual bool checkForLocalTermination();
  Messaging(Scheduler&, ThreadPool&, ContextManager&, Configuration&);
  virtual bool performSinglePoll(int*) = 0;
  virtual int getTypeSize(int);
  virtual void startProgressThread();
public:
  virtual void lockMutexForFinalisationTest() = 0;
  virtual void unlockMutexForFinalisationTest() = 0;
  virtual void resetPolling();
  virtual void runPollForEvents() = 0;
  virtual void setEligableForTermination() = 0;
  virtual bool pollForEvents();
  virtual void finalise();
  virtual void fireEvent(void *, int, int, int, bool, const char *) = 0;
  virtual int getRank()=0;
  virtual int getNumRanks()=0;
  virtual bool isFinished()=0;
  virtual void lockComms()=0;
  virtual void unlockComms()=0;
  virtual void attachMainThread(std::condition_variable*, std::mutex*, bool*);
  virtual bool doesProgressThreadExist() { return progress_thread; }
};

#endif
