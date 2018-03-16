#ifndef SRC_THREADPOOL_H_
#define SRC_THREADPOOL_H_

#include <thread>
#include <condition_variable>
#include <mutex>

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
  int number_of_threads;
  std::thread * actionThreads;
  std::condition_variable * active_thread_conditions;
  std::mutex * active_thread_mutex, thread_start_mutex;
  ThreadPoolCommand *threadCommands;
  bool *threadBusy, *threadStart;
  int next_suggested_idle_thread;

  void threadEntryProcedure(int);
  int get_index_of_idle_thread();

 public:
  ThreadPool();
  bool startThread(void (*)(void *), void *);
  bool isThreadPoolFinished();
};

#endif /* SRC_THREADPOOL_H_ */
