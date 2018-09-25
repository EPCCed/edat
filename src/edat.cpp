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

#include <stddef.h>
#include <stdarg.h>
#include <string>
#include <string.h>
#include <thread>
#include <iostream>
#include "edat.h"
#include "edat_debug.h"
#include "threadpool.h"
#include "scheduler.h"
#include "messaging.h"
#include "mpi_p2p_messaging.h"
#include "contextmanager.h"
#include "concurrency_ctrl.h"
#include "metrics.h"
#include "resilience.h"

#ifndef DO_METRICS
#define DO_METRICS false
#endif

static ThreadPool * threadPool;
static Scheduler * scheduler;
static Messaging * messaging;
static ContextManager * contextManager;
static Configuration * configuration;
static ConcurrencyControl * concurrencyControl;

static bool edatActive;

static void submitProvidedTask(void (*)(EDAT_Event*, int), std::string, bool, int, bool, va_list);
static std::vector<std::pair<int, std::string>> generateDependencyVector(int, va_list);
static void doInitialisation(Configuration*, bool, int, const task_ptr_t * const, const int);

void edatInit(const task_ptr_t * const task_array, const int number_of_tasks) {
  configuration=new Configuration();
  doInitialisation(configuration, false, 0, task_array, number_of_tasks);
}

void edatInitWithConfiguration(int numberEntries, char ** keys, char ** values, const task_ptr_t * const task_array, const int number_of_tasks) {
  configuration=new Configuration(numberEntries, keys, values);
  doInitialisation(configuration, false, 0, task_array, number_of_tasks);
}

static void doInitialisation(Configuration * configuration, bool comm_present, int communicator, const task_ptr_t * const task_array, const int number_of_tasks) {
  threadPool=new ThreadPool(*configuration);
  concurrencyControl=new ConcurrencyControl(threadPool);
  contextManager=new ContextManager(*configuration);
  scheduler=new Scheduler(*threadPool, *configuration, *concurrencyControl);
  #if DO_METRICS
    metrics::METRICS = new EDAT_Metrics(*configuration);
  #endif
  if (comm_present) {
    messaging=new MPI_P2P_Messaging(*scheduler, *threadPool, *contextManager, *configuration, communicator);
  } else {
    messaging=new MPI_P2P_Messaging(*scheduler, *threadPool, *contextManager, *configuration);
  }
  threadPool->setMessaging(messaging);
  int resilienceLevel = configuration->get("EDAT_RESILIENCE", 0);
  if (resilienceLevel) {
    const unsigned int COMM_TIMEOUT = configuration->get("EDAT_COMM_TIMEOUT", DEFAULT_COMM_TIMEOUT);
    resilienceInit(resilienceLevel, *scheduler, *threadPool, *messaging, std::this_thread::get_id(), task_array, number_of_tasks, COMM_TIMEOUT);
  }
  messaging->resetPolling();
  edatActive=true;
  #if DO_METRICS
    metrics::METRICS->edatTimerStart();
  #endif
}

void edatInitialiseWithCommunicator(int communicator, const task_ptr_t * const task_array, const int number_of_tasks) {
  configuration=new Configuration();
  doInitialisation(configuration, true, communicator, task_array, number_of_tasks);
}

void edatFinalise(void) {
  if (edatActive) {
    // Puts the thread to sleep and will wake it up when there are no more events and tasks.
    std::mutex * m = new std::mutex();
    std::condition_variable * cv = new std::condition_variable();
    bool * completed = new bool();

    messaging->attachMainThread(cv, m, completed);
    threadPool->notifyMainThreadIsSleeping();
    messaging->setEligableForTermination();
    std::unique_lock<std::mutex> lk(*m);
    cv->wait(lk, [completed]{return *completed;});
  }
  int resilienceLevel = configuration->get("EDAT_RESILIENCE", 0);
  resilienceFinalise(resilienceLevel);
  messaging->finalise();
  #if DO_METRICS
    metrics::METRICS->finalise();
  #endif
  edatActive=false;
}

void edatPauseMainThread(void) {
#if DO_METRICS
  unsigned long int timer_key = metrics::METRICS->timerStart("PauseMainThread");
#endif

  std::mutex * m = new std::mutex();
  std::condition_variable * cv = new std::condition_variable();
  bool * completed = new bool();

  messaging->attachMainThread(cv, m, completed);
  threadPool->notifyMainThreadIsSleeping();
  messaging->setEligableForTermination();
  std::unique_lock<std::mutex> lk(*m);
#if DO_METRICS
  metrics::METRICS->timerStop("PauseMainThread", timer_key);
#endif
  cv->wait(lk, [completed]{return *completed;});

  edatActive=false;
}

void edatRestart() {
  messaging->resetPolling();
  threadPool->resetPolling();
  edatActive=true;
}

int edatGetRank(void) {
  return messaging->getRank();
}

int edatGetNumRanks(void) {
  return messaging->getNumRanks();
}

int edatGetNumWorkers(void) {
  return threadPool->getNumberOfWorkers();
}

int edatGetWorker(void) {
  return threadPool->getCurrentWorkerId();
}

int edatGetNumActiveWorkers(void) {
  return threadPool->getNumberActiveWorkers();
}

void edatSubmitPersistentTask(void (*task_fn)(EDAT_Event*, int), int num_dependencies, ...) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("SubmitPersistentTask");
  #endif
  va_list valist;
  va_start(valist, num_dependencies);
  submitProvidedTask(task_fn, "", true, num_dependencies, false, valist);
  va_end(valist);
  #if DO_METRICS
    metrics::METRICS->timerStop("SubmitPersistentTask", timer_key);
  #endif
}

void edatSubmitPersistentGreedyTask(void (*task_fn)(EDAT_Event*, int), int num_dependencies, ...) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("SubmitPersistentTask");
  #endif
  va_list valist;
  va_start(valist, num_dependencies);
  submitProvidedTask(task_fn, "", true, num_dependencies, true, valist);
  va_end(valist);
  #if DO_METRICS
    metrics::METRICS->timerStop("SubmitPersistentTask", timer_key);
  #endif
}

void edatSubmitPersistentNamedTask(void (*task_fn)(EDAT_Event*, int), const char * task_name, int num_dependencies, ...) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("SubmitPersistentTask");
  #endif
  va_list valist;
  va_start(valist, num_dependencies);
  submitProvidedTask(task_fn, std::string(task_name), true, num_dependencies, false, valist);
  va_end(valist);
  #if DO_METRICS
    metrics::METRICS->timerStop("SubmitPersistentTask", timer_key);
  #endif
}

void edatSubmitPersistentNamedGreedyTask(void (*task_fn)(EDAT_Event*, int), const char * task_name, int num_dependencies, ...) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("SubmitPersistentTask");
  #endif
  va_list valist;
  va_start(valist, num_dependencies);
  submitProvidedTask(task_fn, std::string(task_name), true, num_dependencies, true, valist);
  va_end(valist);
  #if DO_METRICS
    metrics::METRICS->timerStop("SubmitPersistentTask", timer_key);
  #endif
}

void edatSubmitTask(void (*task_fn)(EDAT_Event*, int), int num_dependencies, ...) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("SubmitTask");
  #endif
  va_list valist;
  va_start(valist, num_dependencies);
  submitProvidedTask(task_fn, "", false, num_dependencies, false, valist);
  va_end(valist);
  #if DO_METRICS
    metrics::METRICS->timerStop("SubmitTask", timer_key);
  #endif
}

void edatSubmitNamedTask(void (*task_fn)(EDAT_Event*, int), const char * task_name, int num_dependencies, ...) {
  va_list valist;
  va_start(valist, num_dependencies);
  submitProvidedTask(task_fn, std::string(task_name), false, num_dependencies, false, valist);
  va_end(valist);
}

void edatSubmitTask_f(void (*task_fn)(EDAT_Event*, int), const char * task_name, int num_dependencies, int ** ranks, char ** event_ids,
                        bool persistent, bool greedyConsumer) {
  std::vector<std::pair<int, std::string>> dependencies;
  int my_rank=messaging->getRank();

  for (int i=0; i<num_dependencies; i++) {
    int src=(*ranks)[i];
    if (src == EDAT_SELF) src=my_rank;
    char * event_id=event_ids[i];
    if (src == EDAT_ALL) {
      for (int j=0;j<messaging->getNumRanks();j++) {
        dependencies.push_back(std::pair<int, std::string>(j, std::string(event_id)));
      }
    } else {
      dependencies.push_back(std::pair<int, std::string>(src, std::string(event_id)));
    }
  }
  scheduler->registerTask(task_fn, task_name == NULL ? "" : task_name, dependencies, persistent, greedyConsumer);
}

int edatRemoveTask(const char * task_name) {
  return scheduler->removeTask(std::string(task_name)) ? 1 : 0;
}

int edatIsTaskSubmitted(const char * task_name) {
  return scheduler->edatIsTaskSubmitted(std::string(task_name)) ? 1 : 0;
}

void edatFireEvent(void* data, int data_type, int data_count, int target, const char * event_id) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("FireEvent");
  #endif
  if (target == EDAT_SELF) target=messaging->getRank();
  if (configuration->get("EDAT_RESILIENCE", 0)) {
    resilienceEventFired(data, data_count, data_type, target, false, event_id);
  } else {
    messaging->fireEvent(data, data_count, data_type, target, false, event_id);
  }
  #if DO_METRICS
    metrics::METRICS->timerStop("FireEvent", timer_key);
  #endif
}

void edatFirePersistentEvent(void* data, int data_type, int data_count, int target, const char * event_id) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("FirePersistentEvent");
  #endif
  if (target == EDAT_SELF) target=messaging->getRank();
  messaging->fireEvent(data, data_count, data_type, target, true, event_id);
  #if DO_METRICS
    metrics::METRICS->timerStop("FirePersistentEvent", timer_key);
  #endif
}

/**
* Given an array of events, the number of events, the source rank and a specifc event identifier will return the appropriate index in the event array where that
* can be found or -1 if none is present
*/
int edatFindEvent(EDAT_Event * events, int number_events, int source, const char * event_id) {
  if (source == EDAT_SELF) source=messaging->getRank();
  for (int i=0;i<number_events;i++) {
    if (strcmp(events[i].metadata.event_id, event_id) == 0 &&
        (source == EDAT_ANY || events[i].metadata.source == source)) return i;
  }
  return -1;
}

int edatDefineContext(size_t contextSize) {
  ContextDefinition * definition = new ContextDefinition(contextSize);
  return contextManager->addDefinition(definition);
}

void* edatCreateContext(int contextType) {
  return contextManager->createContext(contextType);
}

/**
* Pauses this task until a number of dependencies (events have arrived) are met. These events are then returned to the caller.
*/
EDAT_Event* edatWait(int num_dependencies, ...) {
  va_list valist;
  va_start(valist, num_dependencies);
  std::vector<std::pair<int, std::string>> dependencies = generateDependencyVector(num_dependencies, valist);
  va_end(valist);
  return scheduler->pauseTask(dependencies);
}

EDAT_Event* edatRetrieveAny(int* retrievedNumber, int num_dependencies, ...) {
  va_list valist;
  va_start(valist, num_dependencies);
  std::vector<std::pair<int, std::string>> dependencies = generateDependencyVector(num_dependencies, valist);
  va_end(valist);
  std::pair<int, EDAT_Event*> foundEvents = scheduler->retrieveAnyMatchingEvents(dependencies);
  *retrievedNumber=foundEvents.first;
  return foundEvents.second;
}

void edatLock(char* lockName) {
  concurrencyControl->lock(std::string(lockName));
}

void edatUnlock(char* lockName) {
  concurrencyControl->unlock(std::string(lockName));
}

int edatTestLock(char* lockName) {
  if (concurrencyControl->test_lock(std::string(lockName))) return 1;
  return 0;
}

void edatLockComms(void) {
  messaging->lockComms();
}

void edatUnlockComms(void) {
   messaging->unlockComms();
}

/**
* Testing function, initiates simulated failure of a task
*/
void edatSyntheticFailure(const int level) {
  int resilienceLevel = configuration->get("EDAT_RESILIENCE", 0);
  if (resilienceLevel) {
    const std::thread::id thread_id = std::this_thread::get_id();
    if (level == 0) {
      std::cout << "Oh no! This task has failed! How terrible, and completely unexpected." << std::endl;
      threadPool->syntheticFailureOfThread(thread_id);
      resilienceThreadFailed(thread_id, resilienceLevel);
    } else if (level == 1) {
      const int COMM_TIMEOUT = configuration->get("EDAT_COMM_TIMEOUT", DEFAULT_COMM_TIMEOUT);
      std::cout << "I've got a bad feeling about rank " << messaging->getRank() << "..." << std::endl;
      messaging->syntheticFinalise();
      scheduler->reset();
      WorkerThread * wt = threadPool->reset();
      const ContinuityData con_data = resilienceSyntheticFinalise(thread_id);
      // knock-down complete, go to sleep
      std::this_thread::sleep_for(std::chrono::seconds(2*COMM_TIMEOUT));
      // reinitialise rank
      threadPool->reinit(wt);
      threadPool->setMessaging(messaging);
      resilienceInit(resilienceLevel, *scheduler, *threadPool, *messaging, con_data.main_thread_id, con_data.task_array, con_data.num_tasks, COMM_TIMEOUT);
      resilienceRestoreTaskToActive(thread_id, con_data.ptd);
      messaging->resetPolling();
      messaging->setEligableForTermination();
    } else {
      std::cout << "Did not recognise failure level. Call edatSyntheticFailure(0) for a simulated thread failure. Call edatSyntheticFailure(1) for a simulated process failure." << std::endl;
    }
  } else {
    std::cout << "edatSyntheticFailure is unavailable when EDAT_RESILIENCE is not true." << std::endl;
  }

  std::cout << "edatSyntheticFailure returning" << std::endl;
  return;
}

/**
* Will schedule a specific task, this is common functionality for all the different call permutations in the API. It will extract out the dependencies
* and package these up before calling into the scheduler
*/
static void submitProvidedTask(void (*task_fn)(EDAT_Event*, int), std::string task_name, bool persistent, int num_dependencies, bool greedyConsumer, va_list valist) {
  scheduler->registerTask(task_fn, task_name, generateDependencyVector(num_dependencies, valist), persistent, greedyConsumer);
}

/**
* A helper function to generate the vector of dependencies from the variable arguments list. This is used when scheduling tasks and pausing a task
* to wait for the arrival of events
*/
static std::vector<std::pair<int, std::string>> generateDependencyVector(int num_dependencies, va_list valist) {
  std::vector<std::pair<int, std::string>> dependencies;
  int my_rank=messaging->getRank();

  for (int i=0; i<num_dependencies; i++) {
    int src=va_arg(valist, int);
    if (src == EDAT_SELF) src=my_rank;
    char * event_id=va_arg(valist, char*);
    if (src == EDAT_ALL) {
      for (int j=0;j<messaging->getNumRanks();j++) {
        dependencies.push_back(std::pair<int, std::string>(j, std::string(event_id)));
      }
    } else {
      dependencies.push_back(std::pair<int, std::string>(src, std::string(event_id)));
    }
  }
  return dependencies;
}
