#include "threadpool.h"
#include <stdlib.h>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <cstdlib>
#include <string.h>

#define DEFAULT_NUMBER_THREADS 10

/**
* Initialises the thread pool and sets the number of threads to be a value found by configuration or an environment variable.
*/
ThreadPool::ThreadPool() {
  number_of_threads=DEFAULT_NUMBER_THREADS;
  if(const char* env_num_threads = std::getenv("EDAT_NUM_THREADS")) {
    if (strlen(env_num_threads) > 0) number_of_threads=atoi(env_num_threads);
  }

  actionThreads = new std::thread[number_of_threads];
  active_thread_conditions=new std::condition_variable[number_of_threads];
  active_thread_mutex = new std::mutex[number_of_threads];

  threadCommands = new ThreadPoolCommand[number_of_threads];
  threadBusy = new bool[number_of_threads];
  threadStart = new bool[number_of_threads];
  next_suggested_idle_thread = 0;
  int i;
  for (i = 0; i < number_of_threads; i++) {
    threadBusy[i] = false;
    threadStart[i] = false;
  }
  for (i = 0; i < number_of_threads; i++) {
    actionThreads[i]=std::thread(&ThreadPool::threadEntryProcedure, this, i);
  }
}

/**
* Determines whether the thread pool is finished (idle) or not
*/
bool ThreadPool::isThreadPoolFinished() {
  int i;
  for (i = 0; i < number_of_threads; i++) {
    if (threadBusy[i]) return false;
  }
  return true;
}

/**
* Will attemp to start a thread by mapping the calling function and arguments to a free thread. If this is not possible (they are all busy) then it will
* return false (otherwise true if successful) so that the caller can handle this (probably by queueing up.)
*/
bool ThreadPool::startThread(void (*callFunction)(void *), void *args) {
  std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);
  int idleThreadId = get_index_of_idle_thread();
  if (idleThreadId != -1) {
    threadBusy[idleThreadId] = true;
    thread_start_lock.unlock();
    std::unique_lock<std::mutex> lck(active_thread_mutex[idleThreadId]);
    threadCommands[idleThreadId].setCallFunction(callFunction);
    threadCommands[idleThreadId].setData(args);
    threadStart[idleThreadId] = true;

    active_thread_conditions[idleThreadId].notify_one();
    return true;
  } else {
    return false;
  }
}

/**
* Returns the index of the next idle thread, going in a roundrobin fashion starting from the previous thread that was
* allocated. It returns -1 if there is no idle thread available.
*/
int ThreadPool::get_index_of_idle_thread() {
  for (int i = next_suggested_idle_thread; i < number_of_threads; i++) {
    if (!threadBusy[i]) {
      next_suggested_idle_thread = i + 1;
      if (next_suggested_idle_thread >= number_of_threads) next_suggested_idle_thread = 0;
      return i;
    }
  }
  next_suggested_idle_thread = 0;
  return -1;
}

/**
* Internal thread entry procedure, the threads will sit in here asleep via the condition variable and then be woken up when they
* need to be activated and run the function call with arguments that has been set for them already.
*/
void ThreadPool::threadEntryProcedure(int myThreadId) {
  while (1) {
    std::unique_lock<std::mutex> lck(active_thread_mutex[myThreadId]);
    while (!threadStart[myThreadId]) {
      active_thread_conditions[myThreadId].wait(lck);
    }
    lck.unlock();
    threadStart[myThreadId] = false;
    threadCommands[myThreadId].issueFunctionCall();
    // Return this thread back to the pool
    threadBusy[myThreadId] = false;
  }
}
