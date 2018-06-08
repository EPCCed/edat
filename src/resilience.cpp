#include "resilience.h"
#include "messaging.h"
#include "threadpool.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <queue>

namespace resilience {
  EDAT_Ledger * process_ledger;
}

void resilienceInit(Configuration& configuration, Messaging* messaging, std::thread::id main_thread_id) {
  resilience::process_ledger = new EDAT_Ledger(configuration, messaging, main_thread_id);

  if (!messaging->getRank()) std::cout << "EDAT resilience initialised." << std::endl;

  return;
}

EDAT_Ledger::EDAT_Ledger(Configuration & aconfig, Messaging * amessaging, std::thread::id thread_id) : configuration(aconfig) {
  messaging = amessaging;
  main_thread_id = thread_id;
}

void EDAT_Ledger::fireCannon(long long int task_id) {
  std::map<long long int, std::queue<LoadedEvent>>::iterator iter = event_battery.find(task_id);

  if (iter != event_battery.end()) {
    while (!event_battery.at(task_id).empty()) {
      LoadedEvent event = event_battery.at(task_id).front();
      messaging->fireEvent(event.data, event.data_count, event.data_type,
        event.target, event.persistent, event.event_id);
      if (event.data != NULL) free(event.data);
      event_battery.at(task_id).pop();
    }
  }

  return;
}

void EDAT_Ledger::loadEvent(std::thread::id thread_id, void * data,
                            int data_count, int data_type, int target,
                            bool persistent, const char * event_id) {
  LoadedEvent event;
  event.data_count = data_count;
  event.data_type = data_type;
  event.target = target;
  event.persistent = persistent;
  event.event_id = event_id;

  long long int task_id = active_tasks.at(thread_id);

  int data_size = data_count * messaging->getTypeSize(data_type);
  if (data != NULL) {
    event.data = malloc(data_size);
    memcpy(event.data, data, data_size);
  }

  std::map<long long int,std::queue<LoadedEvent>>::iterator iter = event_battery.find(task_id);
  if (iter == event_battery.end()) {
    std::queue<LoadedEvent> event_cannon;
    event_cannon.push(event);
    event_battery.emplace(task_id, event_cannon);
  } else {
    event_battery.at(task_id).push(event);
  }

  return;
}

void EDAT_Ledger::taskActiveOnThread(std::thread::id thread_id, long long int task_id) {
  active_tasks[thread_id] = task_id;
  return;
}

void EDAT_Ledger::taskComplete(long long int task_id) {
  fireCannon(task_id);
  return;
}

void EDAT_Ledger::finalise(void) {
  delete this;

  return;
}
