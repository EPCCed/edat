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
  std::thread::id main_thread_id;
  std::mutex at_mutex, id_mutex;
  std::queue<taskID_t> completed_tasks;
  std::queue<taskID_t> failed_tasks;
  std::map<taskID_t,ActiveTaskDescriptor*> active_tasks;
  std::map<std::thread::id,std::queue<taskID_t>> threadID_to_taskID;
  taskID_t getCurrentlyActiveTask(std::thread::id);
  void releaseFiredEvents(taskID_t);
public:
  EDAT_Ledger(Configuration&, Messaging*, std::thread::id);
  std::thread::id getMainThreadID(void) { return main_thread_id; };
  void holdFiredEvent(std::thread::id, void*, int, int, int, bool, const char *);
  void taskActiveOnThread(std::thread::id, PendingTaskDescriptor&);
  void taskComplete(std::thread::id, taskID_t);
  void finalise(void);
};

namespace resilience {
  extern EDAT_Ledger * process_ledger;
}

#endif
