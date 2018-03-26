#include "threadpool.h"
#include "messaging.h"
#include <stdlib.h>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <cstdlib>
#include <string.h>
#include <iostream>
#include <sched.h>
#include "misc.h"

#define THREAD_MAPPING_AUTO 0
#define THREAD_MAPPING_LINEAR 1
#define THREAD_MAPPING_LINEARFROMCORE 1

static std::map<const char*, int> thread_mapping_lookup={{"auto", THREAD_MAPPING_AUTO},
  {"linear", THREAD_MAPPING_LINEAR}, {"linearfromcore", THREAD_MAPPING_LINEARFROMCORE}} ;

/**
* Initialises the thread pool and sets the number of threads to be a value found by configuration or an environment variable.
*/
ThreadPool::ThreadPool() {
  progressPollIdleThread=false;
  pollingProgressThread=-1;
  number_of_threads=getEnvironmentVariable("EDAT_NUM_THREADS", std::thread::hardware_concurrency());
  main_thread_is_worker=getEnvironmentVariable("EDAT_MAIN_THREAD_WORKER", false);

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
  mapThreadsToCores(main_thread_is_worker);
  if (main_thread_is_worker) threadBusy[0] = true;
}

/**
* Maps the threads to cores by setting the affinity if required. There are a number of options here, linear will just go from core 0 all the way to core n and cycle
* round. Linear from core will go from the process core. Both approaches will start off from plus one if we don't include the main thread as a worker and the current core
* (or zero) if the main thread is a worker.
*/
void ThreadPool::mapThreadsToCores(bool main_thread_is_worker) {
  int thread_to_core_mapping=getEnvironmentMapVariable("EDAT_THREAD_MAPPING", thread_mapping_lookup, THREAD_MAPPING_AUTO);

  if (thread_to_core_mapping != THREAD_MAPPING_AUTO) {
    int total_num_cores=std::thread::hardware_concurrency();
    int my_core=sched_getcpu();
    for (int i=0;i<number_of_threads; i++) {
      if (thread_to_core_mapping==THREAD_MAPPING_LINEAR || thread_to_core_mapping==THREAD_MAPPING_LINEARFROMCORE) {
        int core_id;
        if (thread_to_core_mapping==THREAD_MAPPING_LINEAR) {
          core_id=(i+(main_thread_is_worker ? 0 : 1)) % total_num_cores;
        } else if (thread_to_core_mapping==THREAD_MAPPING_LINEARFROMCORE) {
          core_id=(i+my_core+(main_thread_is_worker ? 0 : 1)) % total_num_cores;
        }
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        int rc = pthread_setaffinity_np(actionThreads[i].native_handle(), sizeof(cpu_set_t), &cpuset);
        if (rc != 0) raiseError("Error setting pthread affinity in mapping threads to cores");
      }
    }
  }

  if (getEnvironmentVariable("EDAT_REPORT_THREAD_MAPPING", false)) {
    for (int i=0;i<number_of_threads; i++) {
          std::unique_lock<std::mutex> lck(active_thread_mutex[i]);
          active_thread_conditions[i].notify_one();
    }
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

/**
* Sets the messaging and based on this will then launch a thread to poll for progress if possible, it is important that one is launched for this
* or else they will all sit there idle not doing anything! In-fact the only situation that this will not happen is when there is only 1 thread and
* the main thread is being used as a worker.
*/
void ThreadPool::setMessaging(Messaging * messaging) {
  this->messaging = messaging;
  progressPollIdleThread=!messaging->doesProgressThreadExist();
  if (progressPollIdleThread) {
    launchThreadToPollForProgressIfPossible();
  }
}

/**
* We are notified that the main thread is sleeping, if I am then reusing the main thread as a worker it will mark the first threads as eligable to run.
* Note that we are not strictly reusing the thread, but instead having one sleeping and one active (and then swapping over when the main thread becomes
* idle.) In the case of one worker only, then we need to inform this straight away to poll for progress.
*/
void ThreadPool::notifyMainThreadIsSleeping() {
  if (main_thread_is_worker) threadBusy[0] = false;
  if (number_of_threads == 1 && progressPollIdleThread) launchThreadToPollForProgressIfPossible();
}

/**
* Launches a thread to poll for progress, this is ideally called when the code starts up but if there is not a free thread
* (i.e. one worker only and the master is going to be repurposed as a worker) then is called when the main thread goes idle.
*/
void ThreadPool::launchThreadToPollForProgressIfPossible() {
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
  bool reported_mapping=false;
  bool should_report_mapping=getEnvironmentVariable("EDAT_REPORT_THREAD_MAPPING", false);

  while (1) {
    std::unique_lock<std::mutex> lck(active_thread_mutex[myThreadId]);
    while (!threadStart[myThreadId]) {
      active_thread_conditions[myThreadId].wait(lck);
      if (should_report_mapping && !reported_mapping) {
        reported_mapping=true;
        std::cout << "Thread #" << myThreadId << ": on core " << sched_getcpu() << "\n";
      }
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
