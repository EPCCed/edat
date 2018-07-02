#ifndef SRC_RESILIENCE_H
#define SRC_RESILIENCE_H

#include "messaging.h"
#include "scheduler.h"
#include <thread>
#include <mutex>
#include <map>
#include <set>
#include <queue>

void resilienceInit(Scheduler& ascheduler, Messaging& amessaging, const std::thread::id thread_id);
void resilienceTaskScheduled();
void resilienceEventArrived();
void resilienceEventFired(void*, int, int, int, bool, const char *);
void resilienceTaskRunning(const std::thread::id, PendingTaskDescriptor&);
void resilienceTaskCompleted(const std::thread::id, const taskID_t);
void resilienceThreadFailed(const std::thread::id);
void resilienceFinalise(void);

class EDAT_Thread_Ledger {
private:
  Scheduler& scheduler;
  Messaging& messaging;
  const std::thread::id main_thread_id;
  std::mutex at_mutex, id_mutex, failure_mutex;
  std::set<taskID_t> completed_tasks;
  std::set<taskID_t> failed_tasks;
  std::map<taskID_t,ActiveTaskDescriptor*> active_tasks;
  std::map<std::thread::id,std::queue<taskID_t>> threadID_to_taskID;
  void releaseHeldEvents(const taskID_t);
  void purgeHeldEvents(const taskID_t);
public:
  EDAT_Thread_Ledger(Scheduler& ascheduler, Messaging& amessaging, const std::thread::id thread_id)
    : scheduler(ascheduler), messaging(amessaging), main_thread_id(thread_id) {};
  taskID_t getCurrentlyActiveTask(const std::thread::id);
  void holdFiredEvent(const std::thread::id, void*, int, int, int, bool, const char*);
  void taskActiveOnThread(const std::thread::id, PendingTaskDescriptor&);
  void taskComplete(const std::thread::id, const taskID_t);
  void threadFailure(const taskID_t);
};

class EDAT_Process_Ledger {
private:
  Scheduler& scheduler;
  const int RANK;
  int commit();
public:
  EDAT_Process_Ledger(Scheduler& ascheduler, const int my_rank)
    : scheduler(ascheduler), RANK(my_rank) {};
  void addTask(taskID_t, PendingTaskDescriptor&);
  void addArrivedEvent(taskID_t, SpecificEvent&);
  void markTaskActive(taskID_t);
  void markTaskComplete(taskID_t);
  void markTaskFailed(taskID_t);
};

#endif
