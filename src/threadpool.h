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

#ifndef SRC_THREADPOOL_H_
#define SRC_THREADPOOL_H_

#include <thread>
#include <condition_variable>
#include <mutex>
#include <queue>
#include "configuration.h"
#include "threadpackage.h"

typedef unsigned long long int taskID_t;

class Messaging;
struct PausedTaskDescriptor;

class ThreadPoolCommand {
  void (*callFunction)(void *);
  void *data;
 public:
  void setCallFunction(void (*callFunction)(void *)) { this->callFunction = callFunction; }
  void issueFunctionCall() {
    void (*myCall)(void *)=this->callFunction;
    this->callFunction=NULL;
    myCall(data);
  }
  void (*getCallFunction())(void *) { return callFunction; }
  void setData(void *data) { this->data = data; }
  void *getData() { return this->data; }
};

struct PendingThreadContainer {
  void (*callFunction)(void *);
  void *args;
  taskID_t task_id;
};

struct WorkerThread {
  ThreadPackage * activeThread;
  std::map<PausedTaskDescriptor*, ThreadPackage*> pausedThreads;
  std::queue<ThreadPackage*> waitingThreads, idleThreads;
  std::mutex pausedAndWaitingMutex;
  int core_id=-1;
  taskID_t active_task_id=0;
  bool synthFail = false;
  ThreadPoolCommand threadCommand;
  WorkerThread() = default;
  WorkerThread(WorkerThread&);
};

class ThreadPool {
  Configuration & configuration;
  int number_of_workers, pollingProgressThread;
  bool main_thread_is_worker, restartAnotherPoller;
  ThreadPackage * mainThreadPackage;
  PausedTaskDescriptor* pausedMainThreadDescriptor=NULL;
  WorkerThread * workers;
  std::mutex thread_start_mutex, progressMutex, pollingProgressThreadMutex, pausedTasksToWorkersMutex;
  std::queue<PendingThreadContainer> threadQueue;
  std::map<PausedTaskDescriptor*, int> pausedTasksToWorkers;

  bool *threadBusy, progressPollIdleThread;
  int next_suggested_idle_thread;
  Messaging * messaging=NULL;

  void threadEntryProcedure(int);
  int get_index_of_idle_thread();
  void mapThreadsToCores(bool);
  void launchThreadToPollForProgressIfPossible();
  int findIndexFromThreadId(std::thread::id);
  static void threadReportCoreIdFunction(void *);
  void killWorker(const int);
 public:
  ThreadPool(Configuration&);
  void lockMutexForFinalisationTest();
  void unlockMutexForFinalisationTest();
  void startThread(void (*)(void *), void *);
  bool isThreadPoolFinished();
  void setMessaging(Messaging*);
  void notifyMainThreadIsSleeping();
  void pauseThread(PausedTaskDescriptor*, std::unique_lock<std::mutex>*);
  void markThreadResume(PausedTaskDescriptor*);
  void resetPolling();
  int getNumberOfWorkers() { return number_of_workers; }
  int getCurrentWorkerId();
  int getNumberActiveWorkers();
  void killWorker(const std::thread::id);
  void syntheticFailureOfThread(const std::thread::id);
  WorkerThread * reset();
  void reinit(WorkerThread *);
};

#endif /* SRC_THREADPOOL_H_ */
