#include "edat.h"
#include "threadpool.h"
#include "scheduler.h"
#include "messaging.h"
#include "mpi_p2p_messaging.h"
#include <stddef.h>
#include <string>

static ThreadPool * threadPool;
static Scheduler * scheduler;
static Messaging * messaging;

int edatInit(int* argc, char*** argv) {
  threadPool=new ThreadPool();
  threadPool->initThreadPool();
  scheduler=new Scheduler(*threadPool);
  messaging=new MPI_P2P_Messaging(*scheduler);
  messaging->pollForEvents();
  return 0;
}

int edatFinalise(void) {
  while (!threadPool->isThreadPoolFinished());
  while (!scheduler->isFinished());
  messaging->finalise();
  return 0;
}

int edatGetRank() {
  return messaging->getRank();
}

int edatGetNumRanks() {
  return messaging->getNumRanks();
}

int edatScheduleTask(void (*task_fn)(void *, EDAT_Metadata), char* uniqueID) {
  scheduler->registerTask(task_fn, std::string(uniqueID));
  return 0;
}

int edatFireEvent(void* data, int data_count, int data_type, int target, const char * uniqueID) {
  messaging->fireEvent(data, data_count, data_type, target, uniqueID);
}
