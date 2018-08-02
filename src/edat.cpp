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

static bool edatActive;

static void scheduleProvidedTask(void (*)(EDAT_Event*, int), std::string, bool, int, va_list);
static std::vector<std::pair<int, std::string>> generateDependencyVector(int, va_list);

int edatInit(int* argc, char*** argv, edat_struct_configuration* edat_config, const task_ptr_t * const task_array, const int number_of_tasks) {
  configuration=new Configuration(edat_config);
  threadPool=new ThreadPool(*configuration);
  contextManager=new ContextManager(*configuration);
  scheduler=new Scheduler(*threadPool, *configuration);
  #if DO_METRICS
    metrics::METRICS = new EDAT_Metrics(*configuration);
  #endif
  messaging=new MPI_P2P_Messaging(*scheduler, *threadPool, *contextManager, *configuration);
  threadPool->setMessaging(messaging);
  if (configuration->get("EDAT_RESILIENCE", false)) {
    resilienceInit(*scheduler, *threadPool, *messaging, std::this_thread::get_id(), task_array, number_of_tasks);
  }
  messaging->resetPolling();
  edatActive=true;
  #if DO_METRICS
    metrics::METRICS->edatTimerStart();
  #endif
  return 0;
}

int edatFinalise(void) {
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
  messaging->finalise();
  if (configuration->get("EDAT_RESILIENCE", false)) {
    resilienceFinalise();
  }
  #if DO_METRICS
    metrics::METRICS->finalise();
  #endif
  edatActive=false;
  return 0;
}

int edatPauseMainThread(void) {
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
  return 0;
}

int edatRestart() {
  messaging->resetPolling();
  threadPool->resetPolling();
  edatActive=true;
  return 0;
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

int edatSchedulePersistentTask(void (*task_fn)(EDAT_Event*, int), int num_dependencies, ...) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("SchedulePersistentTask");
  #endif
  va_list valist;
  va_start(valist, num_dependencies);
  scheduleProvidedTask(task_fn, "", true, num_dependencies, valist);
  va_end(valist);
  #if DO_METRICS
    metrics::METRICS->timerStop("SchedulePersistentTask", timer_key);
  #endif
  return 0;
}

int edatSchedulePersistentNamedTask(void (*task_fn)(EDAT_Event*, int), const char * task_name, int num_dependencies, ...) {
  va_list valist;
  va_start(valist, num_dependencies);
  scheduleProvidedTask(task_fn, std::string(task_name), true, num_dependencies, valist);
  va_end(valist);
  return 0;
}

int edatScheduleTask(void (*task_fn)(EDAT_Event*, int), int num_dependencies, ...) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("ScheduleTask");
  #endif
  va_list valist;
  va_start(valist, num_dependencies);
  scheduleProvidedTask(task_fn, "", false, num_dependencies, valist);
  va_end(valist);
  #if DO_METRICS
    metrics::METRICS->timerStop("ScheduleTask", timer_key);
  #endif
  return 0;
}

int edatScheduleNamedTask(void (*task_fn)(EDAT_Event*, int), const char * task_name, int num_dependencies, ...) {
  va_list valist;
  va_start(valist, num_dependencies);
  scheduleProvidedTask(task_fn, std::string(task_name), false, num_dependencies, valist);
  va_end(valist);
  return 0;
}

int edatDescheduleTask(const char * task_name) {
  return scheduler->descheduleTask(std::string(task_name)) ? 1 : 0;
}

int edatIsTaskScheduled(const char * task_name) {
  return scheduler->isTaskScheduled(std::string(task_name)) ? 1 : 0;
}

int edatFireEvent(void* data, int data_type, int data_count, int target, const char * event_id) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("FireEvent");
  #endif
  if (target == EDAT_SELF) target=messaging->getRank();
  if (configuration->get("EDAT_RESILIENCE", false)) {
    resilienceEventFired(data, data_count, data_type, target, false, event_id);
  } else {
    messaging->fireEvent(data, data_count, data_type, target, false, event_id);
  }
  #if DO_METRICS
    metrics::METRICS->timerStop("FireEvent", timer_key);
  #endif
  return 0;
}

int edatFirePersistentEvent(void* data, int data_type, int data_count, int target, const char * event_id) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("FirePersistentEvent");
  #endif
  if (target == EDAT_SELF) target=messaging->getRank();
  messaging->fireEvent(data, data_count, data_type, target, true, event_id);
  #if DO_METRICS
    metrics::METRICS->timerStop("FirePersistentEvent", timer_key);
  #endif
  return 0;
}

int edatFireEventWithReflux(void* data, int data_type, int data_count, int target, const char * event_id,
                            void (*reflux_task_fn)(EDAT_Event*, int)) {
  if (target == EDAT_SELF) target=messaging->getRank();
  messaging->fireEvent(data, data_count, data_type, target, false, event_id, reflux_task_fn);
  return 0;
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

/**
* Testing function, initiates simulated failure of a task
*/
void edatSyntheticFailure(int level) {
  if (configuration->get("EDAT_RESILIENCE", false)) {
    if (level == 0) {
      const std::thread::id thread_id = std::this_thread::get_id();
      std::cout << "Oh no! This task has failed! How terrible, and completely unexpected." << std::endl;
      threadPool->syntheticFailureOfThread(thread_id);
      resilienceThreadFailed(thread_id);
    } else if (level == 1) {
      std::cout << "I've got a bad feeling about rank " << messaging->getRank() << "..." << std::endl;
      messaging->syntheticFinalise();
      scheduler->reset();
      const int beat_period = resilienceSyntheticFinalise();
      std::this_thread::sleep_for(std::chrono::seconds(2*beat_period));
    } else {
      std::cout << "Did not recognise failure level. Call edatSyntheticFailure(0) for a simulated thread failure. Call edatSyntheticFailure(1) for a simulated process failure." << std::endl;
    }
  } else {
    std::cout << "edatSyntheticFailure is unavailable when EDAT_RESILIENCE is not true." << std::endl;
  }

  return;
}

/**
* Will schedule a specific task, this is common functionality for all the different call permutations in the API. It will extract out the dependencies
* and package these up before calling into the scheduler
*/
static void scheduleProvidedTask(void (*task_fn)(EDAT_Event*, int), std::string task_name, bool persistent, int num_dependencies, va_list valist) {
  scheduler->registerTask(task_fn, task_name, generateDependencyVector(num_dependencies, valist), persistent);
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
