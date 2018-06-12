#include "resilience.h"
#include "configuration.h"
#include "messaging.h"
#include "scheduler.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <queue>

namespace resilience {
  // put the resilience ledger for each process in to a seperate namespace
  // to avoid conflicts
  EDAT_Ledger * process_ledger;
}

/**
* Instantiates the process ledger and reports to stdout that EDAT is running
* resiliently
*/
void resilienceInit(Configuration& configuration, Messaging* messaging, std::thread::id main_thread_id) {
  resilience::process_ledger = new EDAT_Ledger(configuration, messaging, main_thread_id);

  if (!messaging->getRank()) std::cout << "EDAT resilience initialised." << std::endl;
  return;
}

EDAT_Ledger::EDAT_Ledger(Configuration & aconfig, Messaging * amessaging, std::thread::id thread_id) : configuration(aconfig) {
  messaging = amessaging;
  main_thread_id = thread_id;
}

/**
* Called on task completion, hands off events which were fired from the task
* to the messaging system
*/
void EDAT_Ledger::fireCannon(long long int task_id) {
  std::map<long long int, std::queue<LoadedEvent>>::iterator iter = event_battery.find(task_id);
  LoadedEvent event;

  if (iter != event_battery.end()) {
    while (!iter->second.empty()) {
      event = iter->second.front();
      messaging->fireEvent(event.data, event.data_count, event.data_type,
        event.target, event.persistent, event.event_id);
      if (event.data != NULL) free(event.data);
      iter->second.pop();
    }
  }
  return;
}

/**
* Stores events which are fired from a task. Tasks are diverted in edatFireEvent
* and std::thread::id is used to link the event to a task_id. Events will not be
* fired until the task completes.
*/
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
    iter->second.push(event);
  }
  return;
}

/**
* Events which have arrived and triggered the running of a task are copied and
* stored until the task completes, in case it needs to be restarted.
*/
void EDAT_Ledger::storeArrivedEvents(long long int task_id, std::map<DependencyKey,std::queue<SpecificEvent*>> arrived_events) {
  std::map<DependencyKey,std::queue<SpecificEvent*>> arrived_events_copy;
  std::map<DependencyKey,std::queue<SpecificEvent*>>::iterator iter;
  std::queue<SpecificEvent*> evt_q;
  SpecificEvent * spec_evt;

  for (iter=arrived_events.begin(); iter!=arrived_events.end(); iter++) {
    spec_evt = new SpecificEvent(*(iter->second.front()));
    evt_q.push(spec_evt);
    arrived_events_copy.emplace(iter->first,evt_q);
    evt_q.pop();
  }

  arrived_events_store.emplace(task_id, arrived_events_copy);
  return;
}

/**
* Subversion of edatFireEvent is achieved by checking the thread ID, this
* function links a thread ID to the ID of the task which is running on that
* thread. This means we can use the task ID for the rest of the resilience
* functionality, and we don't need to worry about the thread moving on to other
* things.
*/
void EDAT_Ledger::taskActiveOnThread(std::thread::id thread_id, long long int task_id) {
  active_tasks[thread_id] = task_id;
  return;
}

/**
* Once a task has completed we can pass the events it fired on to messaging,
* delete the events on which it was dependent, and update the ledger.
*/
void EDAT_Ledger::taskComplete(long long int task_id) {
  std::map<DependencyKey,std::queue<SpecificEvent*>>::iterator iter;

  for (iter=arrived_events_store.at(task_id).begin(); iter!=arrived_events_store.at(task_id).end(); iter++) {
    delete iter->second.front();
  }
  arrived_events_store.erase(task_id);
  fireCannon(task_id);
  return;
}

/**
* Called by edatFinalise, just deleted the ledger.
*/
void EDAT_Ledger::finalise(void) {
  delete this;
  return;
}
