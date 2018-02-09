#include "threadpool.h"
#include <stdlib.h>

#define NUMBER_THREADS 100

pthread_t *ThreadPool::actionThreads;
pthread_t ThreadPool::listenerThread;
pthread_cond_t *ThreadPool::active_thread_conditions;
pthread_mutex_t *ThreadPool::active_thread_mutex, ThreadPool::thread_start_mutex;
ThreadPoolCommand *ThreadPool::threadCommands;
volatile bool *ThreadPool::threadBusy, *ThreadPool::threadStart;
volatile int ThreadPool::next_suggested_idle_thread;

void ThreadPool::initThreadPool() {
  actionThreads = (pthread_t *)malloc(sizeof(pthread_t) * NUMBER_THREADS);
  active_thread_conditions = (pthread_cond_t *)malloc(sizeof(pthread_cond_t) * NUMBER_THREADS);
  active_thread_mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t) * NUMBER_THREADS);
  threadCommands = new ThreadPoolCommand[NUMBER_THREADS];
  threadBusy = new bool[NUMBER_THREADS];
  threadStart = new bool[NUMBER_THREADS];
  next_suggested_idle_thread = 0;
  int i;
  for (i = 0; i < NUMBER_THREADS; i++) {
    pthread_cond_init(&active_thread_conditions[i], NULL);
    pthread_mutex_init(&active_thread_mutex[i], NULL);
    threadBusy[i] = false;
    threadStart[i] = false;
  }
  for (i = 0; i < NUMBER_THREADS; i++) {
    int *specificId = (int *)malloc(sizeof(int));
    *specificId = i;
    pthread_create(&actionThreads[i], NULL, threadEntryProcedure, (void *)specificId);
  }
  pthread_mutex_init(&thread_start_mutex, NULL);
}

bool ThreadPool::isThreadPoolFinished() {
  int i;
  for (i = 0; i < NUMBER_THREADS; i++) {
    if (threadBusy[i]) return false;
  }
  return true;
}

bool ThreadPool::isThreadPoolFinishedApartFromListenerThread(int tid) {
  int i, numFinished = 0, idFinished = -1;
  for (i = 0; i < NUMBER_THREADS; i++) {
    if (threadBusy[i] && i != tid) return false;
  }
  return true;
}

void ThreadPool::startThread(void (*callFunction)(void *), void *args) {
  pthread_mutex_lock(&thread_start_mutex);
  int idleThreadId = find_idle_thread();
  if (idleThreadId != -1) {
    threadBusy[idleThreadId] = true;
    pthread_mutex_unlock(&thread_start_mutex);
    pthread_mutex_lock(&active_thread_mutex[idleThreadId]);
    threadCommands[idleThreadId].setCallFunction(callFunction);
    threadCommands[idleThreadId].setData(args);
    threadStart[idleThreadId] = true;
    pthread_cond_signal(&active_thread_conditions[idleThreadId]);
    pthread_mutex_unlock(&active_thread_mutex[idleThreadId]);
  } else {
    pthread_mutex_unlock(&thread_start_mutex);
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
  int i;

  for (i = next_suggested_idle_thread; i < NUMBER_THREADS; i++) {
    if (!threadBusy[i]) {
      next_suggested_idle_thread = i + 1;
      if (next_suggested_idle_thread >= NUMBER_THREADS) next_suggested_idle_thread = 0;
      return i;
    }
  }
  next_suggested_idle_thread = 0;
  return -1;
}

void *ThreadPool::threadEntryProcedure(void *threadData) {
  int myThreadId = *((int *)threadData);
  while (1) {
    pthread_mutex_lock(&active_thread_mutex[myThreadId]);
    while (!threadStart[myThreadId]) {
      pthread_cond_wait(&active_thread_conditions[myThreadId], &active_thread_mutex[myThreadId]);
    }
    pthread_mutex_unlock(&active_thread_mutex[myThreadId]);
    threadStart[myThreadId] = false;
    threadCommands[myThreadId].issueFunctionCall();
    // Return this thread back to the pool
    threadBusy[myThreadId] = false;
  }
  return NULL;
}
