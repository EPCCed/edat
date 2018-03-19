#include "threadpool.h"
#include "messaging.h"
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
  progressPollIdleThread=false;
  pollingProgressThread=-1;
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
  std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);
  int i;
  for (i = 0; i < number_of_threads; i++) {
    if (threadBusy[i]) return false;
  }
  return threadQueue.empty();
}

void ThreadPool::setMessaging(Messaging * messaging) {
  this->messaging = messaging;
  progressPollIdleThread=!messaging->doesProgressThreadExist();
  std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);
  int idleThreadId = get_index_of_idle_thread();
  if (idleThreadId != -1) {
    threadBusy[idleThreadId] = true;
    thread_start_lock.unlock();
    std::unique_lock<std::mutex> lck(active_thread_mutex[idleThreadId]);
    threadCommands[idleThreadId].setCallFunction(NULL);
    threadStart[idleThreadId] = true;

    active_thread_conditions[idleThreadId].notify_one();
  }
}

/**
* Will attemp to start a thread by mapping the calling function and arguments to a free thread. If this is not possible (they are all busy) then it will
* queue up the thread and arguments to then be executed by the next available thread when it becomes idle.
*/
void ThreadPool::startThread(void (*callFunction)(void *), void *args) {
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
  } else {
    PendingThreadContainer tc;
    tc.callFunction=callFunction;
    tc.args=args;
    threadQueue.push(tc);
  }
}

/**
* Returns the index of the next idle thread, going in a roundrobin fashion starting from the previous thread that was
* allocated. It returns -1 if there is no idle thread available. Note that if we are polling for progress without a helper thread
* then effectively that is a free thread doing the polling, for optimisation that thread is the last one to be chosen in this case
* as this avoids swapping in and out the progress polling so it is only used if all others are busy.
*/
int ThreadPool::get_index_of_idle_thread() {
  int progressThread;
  bool pendingProgressThread=false;
  if (progressPollIdleThread) {
    std::lock_guard<std::mutex> guard(pollingProgressThreadMutex);
    progressThread=pollingProgressThread;
  }
  for (int i = next_suggested_idle_thread; i < number_of_threads; i++) {
    if (!threadBusy[i]) {
      if (progressPollIdleThread && progressThread==i) {
        // This seems a bit strange but we do it this way to initially ignore the thread that is polling for updates and only use it if no others are free
        pendingProgressThread=true;
      } else {
        next_suggested_idle_thread = i + 1;
        if (next_suggested_idle_thread >= number_of_threads) next_suggested_idle_thread = 0;
        return i;
      }
    }
  }
  if (pendingProgressThread) {
    next_suggested_idle_thread = progressThread + 1;
    if (next_suggested_idle_thread >= number_of_threads) next_suggested_idle_thread = 0;
    return progressThread;
  }
  next_suggested_idle_thread = 0;
  return -1;
}

/**
* Internal thread entry procedure, the threads will sit in here asleep via the condition variable and then be woken up when they
* need to be activated and run the function call with arguments that has been set for them already. After thread execution it will then
* check any further outstanding threads in the queue and execute these one at a time if appropriate. Once all applicable threads are
* run then if we have no progress thread this will poll for progress (as it is now an idle thread) if there are no other threads doing
* the polling. It might be interupted from this polling at any point, which is fine.
*/
void ThreadPool::threadEntryProcedure(int myThreadId) {
  while (1) {
    std::unique_lock<std::mutex> lck(active_thread_mutex[myThreadId]);
    while (!threadStart[myThreadId]) {
      active_thread_conditions[myThreadId].wait(lck);
    }
    lck.unlock();
    threadStart[myThreadId] = false;
    if (threadCommands[myThreadId].getCallFunction() != NULL) {
      threadCommands[myThreadId].issueFunctionCall();
    }
    bool pollQueue=true;
    while (pollQueue) {
      std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);
      if (!threadQueue.empty()) {
        PendingThreadContainer pc=threadQueue.front();
        threadQueue.pop();
        thread_start_lock.unlock();
        pc.callFunction(pc.args);
      } else {
        pollQueue=false;
        // Return this thread back to the pool, do this in here to avoid a queued entry falling between cracks
        threadBusy[myThreadId] = false;
      }
    }

    if (progressPollIdleThread && messaging != NULL) {
      if (progressMutex.try_lock()) {
        {
          std::lock_guard<std::mutex> guard(pollingProgressThreadMutex);
          pollingProgressThread=myThreadId;
        }
        bool continue_poll=true;
        bool firstIt=true; // Always do a poll on the first iteration
        while (firstIt || (!threadStart[myThreadId] && continue_poll)) {
          continue_poll=messaging->pollForEvents();
          firstIt=false;
        }
        {
          std::lock_guard<std::mutex> guard(pollingProgressThreadMutex);
          pollingProgressThread=-1;
        }
        progressMutex.unlock();
      }
    }
  }
}
