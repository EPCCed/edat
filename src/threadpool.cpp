#include "threadpool.h"
#include <stdlib.h>
#include <thread>
#include <condition_variable>
#include <mutex>

#define NUMBER_THREADS 10

ThreadPool::ThreadPool() {
  actionThreads = new std::thread[NUMBER_THREADS];
  active_thread_conditions=new std::condition_variable[NUMBER_THREADS];
  active_thread_mutex = new std::mutex[NUMBER_THREADS];

  threadCommands = new ThreadPoolCommand[NUMBER_THREADS];
  threadBusy = new bool[NUMBER_THREADS];
  threadStart = new bool[NUMBER_THREADS];
  next_suggested_idle_thread = 0;
  int i;
  for (i = 0; i < NUMBER_THREADS; i++) {
    threadBusy[i] = false;
    threadStart[i] = false;
  }
  for (i = 0; i < NUMBER_THREADS; i++) {
    actionThreads[i]=std::thread(&ThreadPool::threadEntryProcedure, this, i);
  }
}

bool ThreadPool::isThreadPoolFinished() {
  int i;
  for (i = 0; i < NUMBER_THREADS; i++) {
    if (threadBusy[i]) return false;
  }
  return true;
}

void ThreadPool::startThread(void (*callFunction)(void *), void *args) {
  std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);
  int idleThreadId = find_idle_thread();
  if (idleThreadId != -1) {
    threadBusy[idleThreadId] = true;
    thread_start_lock.unlock();
    std::unique_lock<std::mutex> lck(active_thread_mutex[idleThreadId]);
    threadCommands[idleThreadId].setCallFunction(callFunction);
    threadCommands[idleThreadId].setData(args);
    threadStart[idleThreadId] = true;

    active_thread_conditions[idleThreadId].notify_one();
  }
}

int ThreadPool::find_idle_thread() {
  int idle_thread = get_index_of_idle_thread();
  while (idle_thread == -1) {
    idle_thread = get_index_of_idle_thread();
  }
  return idle_thread;
}

int ThreadPool::get_index_of_idle_thread() {
  for (int i = next_suggested_idle_thread; i < NUMBER_THREADS; i++) {
    if (!threadBusy[i]) {
      next_suggested_idle_thread = i + 1;
      if (next_suggested_idle_thread >= NUMBER_THREADS) next_suggested_idle_thread = 0;
      return i;
    }
  }
  next_suggested_idle_thread = 0;
  return -1;
}

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
