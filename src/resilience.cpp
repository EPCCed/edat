#include "resilience.h"
#include "messaging.h"
#include "threadpool.h"
#include <thread>
#include <map>
#include <queue>
#include <iostream>

namespace resilience {
  EDAT_Ledger * process_ledger;
}

void resilienceInit(Configuration& configuration, Messaging* messaging) {
  resilience::process_ledger = new EDAT_Ledger(configuration);
  resilience::process_ledger->setMessaging(messaging);

  int my_rank = messaging->getRank();

  std::cout << "[" << my_rank << "] " << "EDAT Resilience initialised."
  << std::endl;

  return;
}

EDAT_Ledger::EDAT_Ledger(Configuration & aconfig) : configuration(aconfig) {}

void EDAT_Ledger::setMessaging(Messaging* messaging) {
  this->messaging = messaging;

  return;
}

void EDAT_Ledger::loadEvent(std::thread::id thread_id, void * data,
                            int data_count, int data_type, int target,
                            bool persistent, const char * event_id) {
  LoadedEvent event;
  event.data = data;
  event.data_count = data_count;
  event.data_type = data_type;
  event.target = target;
  event.persistent = persistent;
  event.event_id = event_id;

  std::map<std::thread::id,std::queue<LoadedEvent>>::iterator iter = event_battery.find(thread_id);
  if (iter == event_battery.end()) {
    std::queue<LoadedEvent> event_cannon;
    event_cannon.push(event);
    event_battery.emplace(thread_id, event_cannon);
  } else {
    event_battery.at(thread_id).push(event);
  }

  return;
}

void EDAT_Ledger::fireCannon(std::thread::id thread_id) {
  std::map<std::thread::id, std::queue<LoadedEvent>>::iterator iter = event_battery.find(thread_id);

  if (iter != event_battery.end()) {
    while (!event_battery.at(thread_id).empty()) {
      LoadedEvent event = event_battery.at(thread_id).front();
      messaging->fireEvent(event.data, event.data_count, event.data_type,
        event.target, event.persistent, event.event_id);
      event_battery.at(thread_id).pop();
    }
  }

  return;
}

void EDAT_Ledger::finalise(void) {
  delete this;

  return;
}
