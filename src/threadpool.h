#ifndef SRC_THREADPOOL_H_
#define SRC_THREADPOOL_H_

#include "pthread.h"

class ThreadPoolCommand {
  void (*callFunction)(void *);
  void *data;

 public:
  void setCallFunction(void (*callFunction)(void *)) { this->callFunction = callFunction; }
  void issueFunctionCall() { this->callFunction(data); }
  void setData(void *data) { this->data = data; }
  void *getData() { return this->data; }
};

class ThreadPool {
  static pthread_t *actionThreads;
  static pthread_t listenerThread;
  static pthread_cond_t *active_thread_conditions;
  static pthread_mutex_t *active_thread_mutex, thread_start_mutex;
  static ThreadPoolCommand *threadCommands;
  static volatile bool *threadBusy, *threadStart;
  static volatile int next_suggested_idle_thread;
  static void *threadEntryProcedure(void *);
  static int get_index_of_idle_thread();
  static int find_idle_thread();

 public:
  static void initThreadPool();
  static void startThread(void (*)(void *), void *);
  static bool isThreadPoolFinished();
  static bool isThreadPoolFinishedApartFromListenerThread(int);
};

#endif /* SRC_THREADPOOL_H_ */
