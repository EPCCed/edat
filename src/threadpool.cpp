#include "threadpool.h"
#include "messaging.h"
#include "metrics.h"
#include <stdlib.h>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <cstdlib>
#include <string.h>
#include <iostream>
#include <sched.h>
#include <chrono>
#include "misc.h"

#ifndef DO_METRICS
#define DO_METRICS false
#endif

#define THREAD_MAPPING_AUTO 0
#define THREAD_MAPPING_LINEAR 1
#define THREAD_MAPPING_LINEARFROMCORE 1

#define NUMBER_POLL_THREAD_ITERATIONS_IGNORE_THREADBUSY 10

static std::map<const char*, int> thread_mapping_lookup={{"auto", THREAD_MAPPING_AUTO},
  {"linear", THREAD_MAPPING_LINEAR}, {"linearfromcore", THREAD_MAPPING_LINEARFROMCORE}} ;

/**
* Initialises the thread pool and sets the number of threads to be a value found by configuration or an environment variable.
*/
ThreadPool::ThreadPool(Configuration & aconfig) : configuration(aconfig) {
  progressPollIdleThread=false;
  restartAnotherPoller=false;
  pollingProgressThread=-1;
  number_of_threads=configuration.get("EDAT_NUM_THREADS", std::thread::hardware_concurrency());
  main_thread_is_worker=configuration.get("EDAT_MAIN_THREAD_WORKER", false);

  threadBusy = new bool[number_of_threads];
  next_suggested_idle_thread = 0;
  int i;
  for (i = 0; i < number_of_threads; i++) {
    threadBusy[i] = (i==0 && main_thread_is_worker);
  }

  workers=new WorkerThread[number_of_threads];
  mapThreadsToCores(main_thread_is_worker);

  for (int i=0;i<number_of_threads;i++) {
    new (&workers[i]) WorkerThread();
    if (i==0 && main_thread_is_worker) {
      // If the main thread is a worker then link the active thread to this
      workers[i].activeThread=new ThreadPackage(std::this_thread::get_id());
    } else {
      workers[i].activeThread=new ThreadPackage(new std::thread(&ThreadPool::threadEntryProcedure, this, i), workers[i].core_id);
    }
  }

  // If the main thread is not a worker then still track it (needed for pausing and resuming the main thread)
  if (!main_thread_is_worker) mainThreadPackage=new ThreadPackage(std::this_thread::get_id());

  if (configuration.get("EDAT_REPORT_THREAD_MAPPING", false)) {
    std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);
    // If we want to report the mapping of threads to cores then instruct all workers (apart from main worker if main maps to worker 0) to report this
    for (int i=0;i<number_of_threads; i++) {
      if (!threadBusy[i]) {
        threadBusy[i]=true;
        workers[i].threadCommand.setCallFunction(threadReportCoreIdFunction);
        workers[i].threadCommand.setData(new int(i));
        workers[i].activeThread->resume();
      }
    }
  }
}

/**
* Maps the threads to cores by setting the affinity if required. There are a number of options here, linear will just go from core 0 all the way to core n and cycle
* round. Linear from core will go from the process core. Both approaches will start off from plus one if we don't include the main thread as a worker and the current core
* (or zero) if the main thread is a worker. Doesn't do the physical mapping (that is done in the thread package), but instead sets the core_id of the worker
*/
void ThreadPool::mapThreadsToCores(bool main_thread_is_worker) {
  int thread_to_core_mapping=configuration.get("EDAT_THREAD_MAPPING", thread_mapping_lookup, THREAD_MAPPING_AUTO);

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
        workers[i].core_id=core_id;
      }
    }
  }
}

/**
* Retrieves the ID of the thread which has called this or -1 if it is the master thread
*/
int ThreadPool::getCurrentThreadId() {
  std::thread::id this_id = std::this_thread::get_id();
  return findIndexFromThreadId(this_id);
}

/**
* Will pause a specific thread running on a worker as represented by the descriptor provided. This will locate the thread, pause it and then create a new thread
* (or reuse an existing one) to then keep the worker busy.
*/
void ThreadPool::pauseThread(PausedTaskDescriptor * pausedTaskDescriptor, std::unique_lock<std::mutex> * lock) {
  std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);
  std::thread::id this_id = std::this_thread::get_id();
  int threadIndex=findIndexFromThreadId(this_id);
  if (threadIndex >= 0) {
    // If the thread is currently running on a worker
    std::unique_lock<std::mutex> pausedAndWaitingLock(workers[threadIndex].pausedAndWaitingMutex);

    ThreadPackage * thisThread=workers[threadIndex].activeThread;

    workers[threadIndex].pausedThreads.insert(std::pair<PausedTaskDescriptor*, ThreadPackage*>(pausedTaskDescriptor, thisThread));

    // This thread is now not active so update the status
    threadBusy[threadIndex]=false;

    if (workers[threadIndex].idleThreads.empty()) {
      // If there are no idle threads then create a new one to be the new active thread
      workers[threadIndex].activeThread=new ThreadPackage(new std::thread(&ThreadPool::threadEntryProcedure, this, threadIndex), workers[threadIndex].core_id);
    } else {
      // If there is an idle thread then we are going to reactivate it
      ThreadPackage * reactivated=workers[threadIndex].idleThreads.front();
      workers[threadIndex].idleThreads.pop();
      workers[threadIndex].activeThread=reactivated;
      reactivated->resume();
    }

    std::unique_lock<std::mutex> pausedTasksLock(pausedTasksToWorkersMutex);
    // Now pops in the mapping from the descriptor to the specific worker that has paused that task (for resumption later on)
    pausedTasksToWorkers.insert(std::pair<PausedTaskDescriptor*, int>(pausedTaskDescriptor, threadIndex));
    pausedTasksLock.unlock();
    pausedAndWaitingLock.unlock();
    thread_start_lock.unlock();
    // We explicitly unlock the provided lock here as otherwise there can be a race conflict with resume
    if (lock != NULL) lock->unlock();
    thisThread->pause();  // Pause this thread here
  } else {
    thread_start_lock.unlock();
    // If the thread is not running on the worker then it might be the main program entry point if this is not set to be a worker
    if (!main_thread_is_worker && mainThreadPackage->doesMatch(this_id)) {
      // If it is the main program entry point & this is not set to be a worker then handle this here
      pausedMainThreadDescriptor = pausedTaskDescriptor;
      // We explicitly unlock the provided lock here as otherwise there can be a race conflict with resume
      if (lock != NULL) lock->unlock();
      mainThreadPackage->pause();
    } else {
      raiseError("Locally running thread not found");
    }
  }
}

/**
* Marks a thread for resumption, if the worker is currently busy then it will add it to a ready (waiting to run) queue. Otherwise will explicitly activate the thread
*/
void ThreadPool::markThreadResume(PausedTaskDescriptor * pausedTaskDescriptor) {
  std::unique_lock<std::mutex> pausedTasksLock(pausedTasksToWorkersMutex);
  std::map<PausedTaskDescriptor*, int>::iterator pausedTasksToWorkersIt=pausedTasksToWorkers.find(pausedTaskDescriptor);
  if (pausedTasksToWorkersIt != pausedTasksToWorkers.end()) {
    // If the paused task descriptor corresponds to a worker that is holding the thread in a paused state
    int threadIndex=pausedTasksToWorkersIt->second;
    pausedTasksToWorkers.erase(pausedTasksToWorkersIt); // Remove this mapping (descriptor -> worker index)

    std::unique_lock<std::mutex> pausedAndWaitingLock(workers[threadIndex].pausedAndWaitingMutex);
    // For the specific worker find the thread package associated with the paused task descriptor
    std::map<PausedTaskDescriptor*, ThreadPackage*>::iterator it = workers[threadIndex].pausedThreads.find(pausedTaskDescriptor);
    if (it != workers[threadIndex].pausedThreads.end()) {
      workers[threadIndex].pausedThreads.erase(it); // Remove this mapping
      workers[threadIndex].waitingThreads.push(it->second); // Place the paused thread package on the waiting (ready to run) queue

      {
        std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);
        if (!threadBusy[threadIndex]) {
          // If the worker is not busy then reactivate it here, this will effectively find the thread placed on the wait queue, pause the worker and resume to the other thread
          threadBusy[threadIndex] = true;
          workers[threadIndex].threadCommand.setCallFunction(NULL);
          workers[threadIndex].activeThread->resume();
        }
      }

      if (progressPollIdleThread) {
        // If we reactivate the polling thread then can starve it. Hence if this is the case inform that thread it should attempt to reactivate another thread to poll
        std::unique_lock<std::mutex> pollingProgressLock(pollingProgressThreadMutex);
        if (pollingProgressThread==threadIndex) restartAnotherPoller=true;
      }

    } else {
      raiseError("Can not resume thread as not found");
    }
  } else {
    // If there is no mapping from the paused task descriptor to a worker, then it might be the main thread running not as a worker - if so then handle
    if (!main_thread_is_worker && pausedMainThreadDescriptor == pausedTaskDescriptor) {
      pausedMainThreadDescriptor=NULL;
      mainThreadPackage->resume();
    } else {
      raiseError("Can not resume thread as the descriptor is not found");
    }
  }
}

/**
* Locates the worker index that is running an active thread with the provided thread Id. Returns -1 if none is found
*/
int ThreadPool::findIndexFromThreadId(std::thread::id threadIDToFind) {
  for (int i = 0; i < number_of_threads; i++) {
    if (workers[i].activeThread->doesMatch(threadIDToFind)) return i;
  }
  return -1;
}

/**
* Determines whether the thread pool is finished (idle) or not
*/
bool ThreadPool::isThreadPoolFinished() {
  std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);
  int i;
  for (i = 0; i < number_of_threads; i++) {
    if (threadBusy[i]) return false;
    std::unique_lock<std::mutex> pausedLock(workers[i].pausedAndWaitingMutex);
    if (!workers[i].pausedThreads.empty() || !workers[i].waitingThreads.empty()) return false;
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
  if (main_thread_is_worker) {
    std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);
    threadBusy[0] = false;
    // Creates a new active thread so that we can now use the worker to run tasks
    workers[0].activeThread=new ThreadPackage(new std::thread(&ThreadPool::threadEntryProcedure, this, 0), workers[0].core_id);
  }
  if (number_of_threads == 1 && progressPollIdleThread) launchThreadToPollForProgressIfPossible();
}

/**
* Resets the polling of progress, this is required when we pause the framework but don't finalise and effectively then want to restart for the next
* set of asynchronous tasks
*/
void ThreadPool::resetPolling() {
  if (main_thread_is_worker) {
    std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);
    workers[0].activeThread->abort();
    workers[0].activeThread=new ThreadPackage(std::this_thread::get_id());
    threadBusy[0] = true;
  }
  if (progressPollIdleThread) {
    launchThreadToPollForProgressIfPossible();
  }
}

/**
* Launches a thread to poll for progress, this is ideally called when the code starts up but if there is not a free thread
* (i.e. one worker only and the master is going to be repurposed as a worker) then is called when the main thread goes idle.
*/
void ThreadPool::launchThreadToPollForProgressIfPossible() {
  std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);
  int idleThreadId = get_index_of_idle_thread();
  if (idleThreadId != -1) {
    // Do this to ensure that have waited until there is no thread polling for progress currently (hence be a bit careful where this is called from)
    progressMutex.lock();
    progressMutex.unlock();
    threadBusy[idleThreadId] = true;
    thread_start_lock.unlock();
    workers[idleThreadId].threadCommand.setCallFunction(NULL);
    workers[idleThreadId].activeThread->resume();
  }
}

/**
* Will attemp to start a thread by mapping the calling function and arguments to a free thread. If this is not possible (they are all busy) then it will
* queue up the thread and arguments to then be executed by the next available thread when it becomes idle.
*/
void ThreadPool::startThread(void (*callFunction)(void *), void *args, taskID_t task_id) {
  std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);
  int idleThreadId = get_index_of_idle_thread();
  if (idleThreadId != -1) {
    threadBusy[idleThreadId] = true;
    thread_start_lock.unlock();
    workers[idleThreadId].threadCommand.setCallFunction(callFunction);
    workers[idleThreadId].threadCommand.setData(args);
    workers[idleThreadId].active_task_id = task_id;
    workers[idleThreadId].activeThread->resume();
  } else {
    PendingThreadContainer tc;
    tc.callFunction=callFunction;
    tc.args=args;
    tc.task_id = task_id;
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
* Function called by threads to report their core mapping if configured by the user
*/
void ThreadPool::threadReportCoreIdFunction(void * pthreadRawData) {
  int * myThreadId=(int*) pthreadRawData;
  std::cout << "Thread #" << *myThreadId << ": on core " << sched_getcpu() << "\n";
  delete myThreadId;
}

/**
* Internal thread entry procedure, the threads will sit in here asleep via the condition variable and then be woken up when they
* need to be activated and run the function call with arguments that has been set for them already. After thread execution it will then
* check any further outstanding threads in the queue and/or any paused threads eligable for execution. If any are found then it will execute
* these one at a time if appropriate. Once all applicable threads are
* run then if we have no progress thread this will poll for progress (as it is now an idle thread) if there are no other threads doing
* the polling. It might be interupted from this polling at any point, which is fine.
*/
void ThreadPool::threadEntryProcedure(int myThreadId) {
  bool should_report_mapping=configuration.get("EDAT_REPORT_THREAD_MAPPING", false);
  #if DO_METRICS
    std::chrono::steady_clock::time_point thread_activated;
  #endif

  ThreadPackage * myThreadPackage=workers[myThreadId].activeThread;

  while (1) {
    myThreadPackage->pause();
    #if DO_METRICS
      thread_activated = std::chrono::steady_clock::now();
    #endif
    if (myThreadPackage->shouldAbort()) {
      delete myThreadPackage;
      return;
    }
    if (workers[myThreadId].threadCommand.getCallFunction() != NULL) {
      #if DO_METRICS
        unsigned long int timer_key = metrics::METRICS->timerStart("Task");
      #endif
      workers[myThreadId].threadCommand.issueFunctionCall();
      #if DO_METRICS
        metrics::METRICS->timerStop("Task", timer_key);
      #endif
    }
    bool pollQueue=true, restartPoll=false;
    while (pollQueue) {
      std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex);

      if (!threadQueue.empty()) {
        PendingThreadContainer pc=threadQueue.front();
        threadQueue.pop();
        thread_start_lock.unlock();
        #if DO_METRICS
          unsigned long int timer_key = metrics::METRICS->timerStart("Task");
        #endif
        workers[myThreadId].active_task_id = pc.task_id;
        pc.callFunction(pc.args);
        #if DO_METRICS
          metrics::METRICS->timerStop("Task", timer_key);
        #endif
      } else {
        // Check no paused tasks that need to be reactivated (currently limited to the same thread)
        std::unique_lock<std::mutex> pausedAndWaitingLock(workers[myThreadId].pausedAndWaitingMutex);
        if (!workers[myThreadId].waitingThreads.empty()) {
          // First grab the thread to reactivate from the head of the queue
          ThreadPackage * reactivateThread=workers[myThreadId].waitingThreads.front();
          workers[myThreadId].waitingThreads.pop();

          workers[myThreadId].idleThreads.push(myThreadPackage); // Add me as an idle thread that can be reused in future

          workers[myThreadId].activeThread=reactivateThread;  // The active thread is now the reactivated one
          pausedAndWaitingLock.unlock();
          thread_start_lock.unlock();
          workers[myThreadId].activeThread->resume(); // Resume the reactivated thread
          myThreadPackage->pause();  // Pause myself (worker given over to the reactivated thread)
        } else {
          pollQueue=false;
          // Return this thread back to the pool, do this in here to avoid a queued entry falling between cracks
          threadBusy[myThreadId] = false;
        }
      }
      workers[myThreadId].active_task_id = 0;
    }
    #if DO_METRICS
      metrics::METRICS->threadReport(myThreadId, std::chrono::steady_clock::now() - thread_activated);
    #endif

    if (progressPollIdleThread && messaging != NULL) {
      if (progressMutex.try_lock()) {
        {
          std::lock_guard<std::mutex> guard(pollingProgressThreadMutex);
          pollingProgressThread=myThreadId;
        }
        int non_lock_iterations=0;
        bool continue_poll=true;
        bool firstIt=true; // Always do a poll on the first iteration
        while (firstIt || continue_poll) {
          continue_poll=messaging->pollForEvents();
          if (continue_poll) {
            // Try to lock the thread_start_mutex and check the thread busy status for this thread. If this is not possible then allow a number of polling
            // iterations to proceed without the lock, but there is a maximum number as you don't want to starve a task waiting for the thread!
            std::unique_lock<std::mutex> thread_start_lock(thread_start_mutex, std::defer_lock);
            bool isLocked=thread_start_lock.try_lock();
            if (!isLocked) {
              non_lock_iterations++;
              if (non_lock_iterations > NUMBER_POLL_THREAD_ITERATIONS_IGNORE_THREADBUSY) {
                thread_start_lock.lock();
                isLocked=true;
              }
            }
            if (isLocked) {
              continue_poll=!threadBusy[myThreadId];
              non_lock_iterations=0;
            }
          }
          firstIt=false;
        }
        {
          std::lock_guard<std::mutex> guard(pollingProgressThreadMutex);
          pollingProgressThread=-1;
          restartPoll=restartAnotherPoller;
          if (restartAnotherPoller) restartAnotherPoller=false;
        }
        progressMutex.unlock();
        if (restartPoll) launchThreadToPollForProgressIfPossible();
      }
    }
  }

}

void ThreadPool::replaceFailedThread(const std::thread::id thread_id) {
  int worker_idx = findIndexFromThreadId(thread_id);
  
  // assume the old core is no longer functional so unset the workers affinity, and give it a brand new threadpackage
  workers[worker_idx].core_id = -1;
  workers[worker_idx].activeThread = new ThreadPackage(new std::thread(&ThreadPool::threadEntryProcedure, this, worker_idx), workers[worker_idx].core_id);

  return;
}

void ThreadPool::syntheticFailureOfThread(const std::thread::id thread_id) {
  int worker_idx = findIndexFromThreadId(thread_id);
  workers[worker_idx].activeThread->abort();

  return;
}
