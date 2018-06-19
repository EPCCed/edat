#ifndef SRC_RESILIENCE_H
#define SRC_RESILIENCE_H

#include "configuration.h"
#include "messaging.h"
#include "scheduler.h"
#include <thread>
#include <mutex>
#include <map>
#include <queue>

void resilienceInit(Configuration&, Messaging*, std::thread::id);

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
  std::queue<long long int> completed_tasks;
  std::queue<long long int> failed_tasks;
  std::map<std::thread::id,std::queue<long long int>> active_tasks;
  std::map<long long int,std::queue<LoadedEvent>> loaded_events_store;
  std::map<long long int,std::map<DependencyKey,std::queue<SpecificEvent*>>> arrived_events_store;
  void unloadEvents(long long int);
public:
  EDAT_Ledger(Configuration&, Messaging*, std::thread::id);
  std::thread::id getMainThreadID(void) { return main_thread_id; };
  void loadEvent(std::thread::id, void*, int, int, int, bool, const char *);
  void storeArrivedEvents(long long int,std::map<DependencyKey,std::queue<SpecificEvent*>>);
  void taskActiveOnThread(std::thread::id, long long int);
  void taskComplete(std::thread::id, long long int);
  void finalise(void);
};

namespace resilience {
  extern EDAT_Ledger * process_ledger;
}

#endif
