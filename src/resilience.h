#ifndef SRC_RESILIENCE_H
#define SRC_RESILIENCE_H

#include "messaging.h"
#include "scheduler.h"
#include <thread>
#include <mutex>
#include <map>
#include <set>
#include <queue>

class EDAT_Ledger {
private:
  Scheduler & scheduler;
  Messaging * messaging;
  const std::thread::id main_thread_id;
  std::mutex at_mutex, id_mutex, failure_mutex;
  std::set<taskID_t> completed_tasks;
  std::set<taskID_t> failed_tasks;
  std::map<taskID_t,ActiveTaskDescriptor*> active_tasks;
  std::map<std::thread::id,std::queue<taskID_t>> threadID_to_taskID;
  taskID_t getCurrentlyActiveTask(const std::thread::id);
  void releaseHeldEvents(const taskID_t);
  void purgeHeldEvents(const taskID_t);
public:
  EDAT_Ledger(Scheduler&, Messaging*, const std::thread::id);
  const std::thread::id getMainThreadID(void) const { return main_thread_id; };
  void holdFiredEvent(const std::thread::id, void*, int, int, int, bool, const char *);
  void taskActiveOnThread(const std::thread::id, PendingTaskDescriptor&);
  void taskComplete(const std::thread::id, const taskID_t);
  void threadFailure(const std::thread::id);
  void finalise(void);
};

namespace resilience {
  extern EDAT_Ledger * process_ledger;
}

#endif
