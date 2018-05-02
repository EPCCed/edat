#ifndef SRC_THREADPOOL_H_
#define SRC_THREADPOOL_H_

#include <thread>
#include <condition_variable>
#include <mutex>
#include <queue>
#include "configuration.h"

class Messaging;

class ThreadPoolCommand {
  void (*callFunction)(void *);
  void *data;
 public:
  void setCallFunction(void (*callFunction)(void *)) { this->callFunction = callFunction; }
  void issueFunctionCall() { this->callFunction(data); }
  void (*getCallFunction())(void *) { return callFunction; }
  void setData(void *data) { this->data = data; }
  void *getData() { return this->data; }
};

struct PendingThreadContainer {
  void (*callFunction)(void *);
  void *args;
};

class ThreadPool {
  Configuration & configuration;
  int number_of_threads, pollingProgressThread;
  bool main_thread_is_worker;
  std::thread * actionThreads;
  std::condition_variable * active_thread_conditions;
  std::mutex * active_thread_mutex, thread_start_mutex, progressMutex, pollingProgressThreadMutex;
  std::queue<PendingThreadContainer> threadQueue;

  ThreadPoolCommand *threadCommands;
  bool *threadBusy, *threadStart, progressPollIdleThread;
  int next_suggested_idle_thread;
  Messaging * messaging=NULL;

  void threadEntryProcedure(int);
  int get_index_of_idle_thread();
  void mapThreadsToCores(bool);
  void launchThreadToPollForProgressIfPossible();
 public:
  ThreadPool(Configuration&);
  void startThread(void (*)(void *), void *);
  bool isThreadPoolFinished();
  void setMessaging(Messaging*);
  void notifyMainThreadIsSleeping();
};

#endif /* SRC_THREADPOOL_H_ */
