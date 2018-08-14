#include <stddef.h>
#include <stdarg.h>
#include <string>
#include <string.h>
#include "edat.h"
#include "edat_debug.h"
#include "threadpool.h"
#include "scheduler.h"
#include "messaging.h"
#include "mpi_p2p_messaging.h"
#include "contextmanager.h"
#include "concurrency_ctrl.h"
#include "metrics.h"

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

static void scheduleProvidedTask(void (*)(EDAT_Event*, int), std::string, bool, int, bool, va_list);
static std::vector<std::pair<int, std::string>> generateDependencyVector(int, va_list);
static void doInitialisation(Configuration*, bool, int);

void edatInit() {
  configuration=new Configuration();
  doInitialisation(configuration, false, 0);
}

void edatInitWithConfiguration(int numberEntries, char ** keys, char ** values) {
  configuration=new Configuration(numberEntries, keys, values);
  doInitialisation(configuration, false, 0);
}

static void doInitialisation(Configuration * configuration, bool comm_present, int communicator) {
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
  edatActive=true;
  #if DO_METRICS
    metrics::METRICS->edatTimerStart();
  #endif
}

void edatInitialiseWithCommunicator(int communicator) {
  configuration=new Configuration();
  doInitialisation(configuration, true, communicator);
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

void edatSchedulePersistentTask(void (*task_fn)(EDAT_Event*, int), int num_dependencies, ...) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("SchedulePersistentTask");
  #endif
  va_list valist;
  va_start(valist, num_dependencies);
  scheduleProvidedTask(task_fn, "", true, num_dependencies, false, valist);
  va_end(valist);
  #if DO_METRICS
    metrics::METRICS->timerStop("SchedulePersistentTask", timer_key);
  #endif
}

void edatSchedulePersistentGreedyTask(void (*task_fn)(EDAT_Event*, int), int num_dependencies, ...) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("SchedulePersistentTask");
  #endif
  va_list valist;
  va_start(valist, num_dependencies);
  scheduleProvidedTask(task_fn, "", true, num_dependencies, true, valist);
  va_end(valist);
  #if DO_METRICS
    metrics::METRICS->timerStop("SchedulePersistentTask", timer_key);
  #endif
}

void edatSchedulePersistentNamedTask(void (*task_fn)(EDAT_Event*, int), const char * task_name, int num_dependencies, ...) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("SchedulePersistentTask");
  #endif
  va_list valist;
  va_start(valist, num_dependencies);
  scheduleProvidedTask(task_fn, std::string(task_name), true, num_dependencies, false, valist);
  va_end(valist);
  #if DO_METRICS
    metrics::METRICS->timerStop("SchedulePersistentTask", timer_key);
  #endif
}

void edatSchedulePersistentNamedGreedyTask(void (*task_fn)(EDAT_Event*, int), const char * task_name, int num_dependencies, ...) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("SchedulePersistentTask");
  #endif
  va_list valist;
  va_start(valist, num_dependencies);
  scheduleProvidedTask(task_fn, std::string(task_name), true, num_dependencies, true, valist);
  va_end(valist);
  #if DO_METRICS
    metrics::METRICS->timerStop("SchedulePersistentTask", timer_key);
  #endif
}

void edatScheduleTask(void (*task_fn)(EDAT_Event*, int), int num_dependencies, ...) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("ScheduleTask");
  #endif
  va_list valist;
  va_start(valist, num_dependencies);
  scheduleProvidedTask(task_fn, "", false, num_dependencies, false, valist);
  va_end(valist);
  #if DO_METRICS
    metrics::METRICS->timerStop("ScheduleTask", timer_key);
  #endif
}

void edatScheduleNamedTask(void (*task_fn)(EDAT_Event*, int), const char * task_name, int num_dependencies, ...) {
  va_list valist;
  va_start(valist, num_dependencies);
  scheduleProvidedTask(task_fn, std::string(task_name), false, num_dependencies, false, valist);
  va_end(valist);
}

void edatScheduleTask_f(void (*task_fn)(EDAT_Event*, int), const char * task_name, int num_dependencies, int ** ranks, char ** event_ids,
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

int edatDescheduleTask(const char * task_name) {
  return scheduler->descheduleTask(std::string(task_name)) ? 1 : 0;
}

int edatIsTaskScheduled(const char * task_name) {
  return scheduler->isTaskScheduled(std::string(task_name)) ? 1 : 0;
}

void edatFireEvent(void* data, int data_type, int data_count, int target, const char * event_id) {
  #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("FireEvent");
  #endif
  if (target == EDAT_SELF) target=messaging->getRank();
  messaging->fireEvent(data, data_count, data_type, target, false, event_id);
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
* Will schedule a specific task, this is common functionality for all the different call permutations in the API. It will extract out the dependencies
* and package these up before calling into the scheduler
*/
static void scheduleProvidedTask(void (*task_fn)(EDAT_Event*, int), std::string task_name, bool persistent, int num_dependencies, bool greedyConsumer, va_list valist) {
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
