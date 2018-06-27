#ifndef SRC_RESILIENCE_H
#define SRC_RESILIENCE_H

#include "configuration.h"
#include "messaging.h"
#include "scheduler.h"
#include <thread>
#include <mutex>
#include <map>
#include <queue>

class EDAT_Ledger {
private:
  Configuration & configuration;
  Messaging * messaging;
  const std::thread::id main_thread_id;
  std::mutex at_mutex, id_mutex;
  std::queue<taskID_t> completed_tasks;
  std::queue<taskID_t> failed_tasks;
  std::map<taskID_t,ActiveTaskDescriptor*> active_tasks;
  std::map<std::thread::id,std::queue<taskID_t>> threadID_to_taskID;
  taskID_t getCurrentlyActiveTask(const std::thread::id);
  void releaseFiredEvents(const taskID_t);
public:
  EDAT_Ledger(Configuration&, Messaging*, const std::thread::id);
  const std::thread::id getMainThreadID(void) const { return main_thread_id; };
  void holdFiredEvent(const std::thread::id, void*, int, int, int, bool, const char *);
  void taskActiveOnThread(const std::thread::id, PendingTaskDescriptor&);
  void taskComplete(const std::thread::id, const taskID_t);
  void finalise(void);
};

namespace resilience {
  extern EDAT_Ledger * process_ledger;
}

#endif
