#ifndef SRC_RESILIENCE_H
#define SRC_RESILIENCE_H

#include "configuration.h"
#include "messaging.h"
#include "scheduler.h"
#include <thread>
#include <mutex>
#include <map>
#include <queue>

struct LoadedEvent {
  void * data = NULL;
  int data_count;
  int data_type;
  int target;
  bool persistent;
  const char * event_id = NULL;
};

class EDAT_Ledger {
private:
  Configuration & configuration;
  Messaging * messaging;
  std::thread::id main_thread_id;
  std::mutex at_mutex, les_mutex, aes_mutex, ct_mutex, ft_mutex;
  std::queue<taskID_t> completed_tasks;
  std::queue<taskID_t> failed_tasks;
  std::map<std::thread::id,std::queue<taskID_t>> active_tasks;
  std::map<taskID_t,std::queue<LoadedEvent>> loaded_events_store;
  std::map<taskID_t,std::map<DependencyKey,std::queue<SpecificEvent*>>> arrived_events_store;
  void unloadEvents(taskID_t);
public:
  EDAT_Ledger(Configuration&, Messaging*, std::thread::id);
  std::thread::id getMainThreadID(void) { return main_thread_id; };
  void loadEvent(std::thread::id, void*, int, int, int, bool, const char *);
  void storeArrivedEvents(taskID_t,std::map<DependencyKey,std::queue<SpecificEvent*>>);
  void taskActiveOnThread(std::thread::id, taskID_t);
  void taskComplete(std::thread::id, taskID_t);
  void finalise(void);
};

namespace resilience {
  extern EDAT_Ledger * process_ledger;
}

#endif
