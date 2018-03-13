#include "edat.h"
#include "threadpool.h"
#include "scheduler.h"
#include "messaging.h"
#include "mpi_p2p_messaging.h"
#include <stddef.h>
#include <stdarg.h>
#include <string>
#include <utility>

static ThreadPool * threadPool;
static Scheduler * scheduler;
static Messaging * messaging;

int edatInit(int* argc, char*** argv) {
  threadPool=new ThreadPool();
  scheduler=new Scheduler(*threadPool);
  messaging=new MPI_P2P_Messaging(*scheduler);
  messaging->pollForEvents();
  return 0;
}

int edatFinalise(void) {
  while (!messaging->isFinished() || !threadPool->isThreadPoolFinished() || !scheduler->isFinished());
  messaging->finalise();
  return 0;
}

int edatGetRank() {
  return messaging->getRank();
}

int edatGetNumRanks() {
  return messaging->getNumRanks();
}

int edatScheduleTask(void (*task_fn)(EDAT_Event*, int), int num_dependencies, ...) {
  std::vector<std::pair<int, std::string>> dependencies;
  va_list valist;
  va_start(valist, num_dependencies);
  for (int i=0; i<num_dependencies; i++) {
    int src=va_arg(valist, int);
    char * event_id=va_arg(valist, char*);
    if (src == EDAT_ALL) {
      for (int j=0;j<messaging->getNumRanks();j++) {
        dependencies.push_back(std::pair<int, std::string>(j, std::string(event_id)));
      }
    } else {
      dependencies.push_back(std::pair<int, std::string>(src, std::string(event_id)));
    }
  }
  va_end(valist);
  scheduler->registerTask(task_fn, dependencies);
  return 0;
}

int edatFireEvent(void* data, int data_type, int data_count, int target, const char * event_id) {
  messaging->fireEvent(data, data_count, data_type, target, event_id);
}

int edatFireEventWithReflux(void* data, int data_type, int data_count, int target, const char * event_id,
                            void (*reflux_task_fn)(EDAT_Event*, int)) {
  messaging->fireEvent(data, data_count, data_type, target, event_id, reflux_task_fn);
}
