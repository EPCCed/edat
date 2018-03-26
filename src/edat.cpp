#include <stddef.h>
#include <stdarg.h>
#include <string>
#include <string.h>
#include "edat.h"
#include "threadpool.h"
#include "scheduler.h"
#include "messaging.h"
#include "mpi_p2p_messaging.h"

static ThreadPool * threadPool;
static Scheduler * scheduler;
static Messaging * messaging;

static void scheduleProvidedTask(void (*)(EDAT_Event*, int), std::string, bool, int, va_list);

int edatInit(int* argc, char*** argv) {
  threadPool=new ThreadPool();
  scheduler=new Scheduler(*threadPool);
  messaging=new MPI_P2P_Messaging(*scheduler, *threadPool);
  threadPool->setMessaging(messaging);
  return 0;
}

int edatFinalise(void) {
  // Puts the thread to sleep and will wake it up when there are no more events and tasks.
  std::mutex * m = new std::mutex();
  std::condition_variable * cv = new std::condition_variable();
  bool * completed = new bool();

  messaging->attachMainThread(cv, m, completed);
  threadPool->notifyMainThreadIsSleeping();
  std::unique_lock<std::mutex> lk(*m);
  cv->wait(lk, [completed]{return *completed;});
  messaging->finalise();
  return 0;
}

int edatGetRank() {
  return messaging->getRank();
}

int edatGetNumRanks() {
  return messaging->getNumRanks();
}

int edatSchedulePersistentTask(void (*task_fn)(EDAT_Event*, int), int num_dependencies, ...) {
  va_list valist;
  va_start(valist, num_dependencies);
  scheduleProvidedTask(task_fn, "", true, num_dependencies, valist);
  va_end(valist);
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
  va_list valist;
  va_start(valist, num_dependencies);
  scheduleProvidedTask(task_fn, "", false, num_dependencies, valist);
  va_end(valist);
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
  if (target == EDAT_SELF) target=messaging->getRank();
  messaging->fireEvent(data, data_count, data_type, target, event_id);
  return 0;
}

int edatFireEventWithReflux(void* data, int data_type, int data_count, int target, const char * event_id,
                            void (*reflux_task_fn)(EDAT_Event*, int)) {
  if (target == EDAT_SELF) target=messaging->getRank();
  messaging->fireEvent(data, data_count, data_type, target, event_id, reflux_task_fn);
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

/**
* Will schedule a specific task, this is common functionality for all the different call permutations in the API. It will extract out the dependencies
* and package these up before calling into the scheduler
*/
static void scheduleProvidedTask(void (*task_fn)(EDAT_Event*, int), std::string task_name, bool persistent, int num_dependencies, va_list valist) {
  int my_rank=messaging->getRank();
  std::vector<std::pair<int, std::string>> dependencies;
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
  scheduler->registerTask(task_fn, task_name, dependencies, persistent);
}
