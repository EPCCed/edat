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

  if (!messaging->getRank()) {
    std::cout << "EDAT resilience initialised." << std::endl;
    std::cout << "Unsupported: EDAT_MAIN_THREAD_WORKER, edatFirePersistentEvent, edatFireEventWithReflux, edatWait" << std::endl;
  }
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
    eb_mutex.lock();
    while (!iter->second.empty()) {
      event = iter->second.front();
      messaging->fireEvent(event.data, event.data_count, event.data_type,
        event.target, event.persistent, event.event_id);
      if (event.data != NULL) free(event.data);
      iter->second.pop();
    }
    event_battery.erase(task_id);
    eb_mutex.unlock();
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

  long long int task_id = active_tasks.at(thread_id).back();

  int data_size = data_count * messaging->getTypeSize(data_type);
  if (data != NULL) {
    event.data = malloc(data_size);
    memcpy(event.data, data, data_size);
  }

  std::map<long long int,std::queue<LoadedEvent>>::iterator iter = event_battery.find(task_id);
  if (iter == event_battery.end()) {
    std::queue<LoadedEvent> event_cannon;
    event_cannon.push(event);
    std::lock_guard<std::mutex> lock(eb_mutex);
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
  std::queue<SpecificEvent*> event_queue;
  SpecificEvent * spec_evt;
  unsigned int queue_size, i;

  for (iter=arrived_events.begin(); iter!=arrived_events.end(); iter++) {
    queue_size = iter->second.size();
    if (queue_size > 1) {
      // queues aren't really meant to be iterated through, so this is a bit
      // messy...
      SpecificEvent* temp_queue[queue_size];
      i = 0;
      while (!iter->second.empty()) {
        // create copies of each specific event and push them to the new queue
        // also take note of the original
        spec_evt = new SpecificEvent(*(iter->second.front()));
        event_queue.push(spec_evt);
        temp_queue[i] = iter->second.front();
        iter->second.pop();
        i++;
      }
      for (i=0; i<queue_size; i++) {
        // now restore the original queue
        iter->second.push(temp_queue[i]);
      }
      arrived_events_copy.emplace(iter->first,event_queue);
    } else {
      spec_evt = new SpecificEvent(*(iter->second.front()));
      event_queue.push(spec_evt);
      arrived_events_copy.emplace(iter->first,event_queue);
    }
    while (!event_queue.empty()) event_queue.pop();
  }

  aes_mutex.lock();
  arrived_events_store.emplace(task_id, arrived_events_copy);
  aes_mutex.unlock();
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
  std::map<std::thread::id,std::queue<long long int>>::iterator iter = active_tasks.find(thread_id);

  if (iter == active_tasks.end()) {
    std::queue<long long int> task_id_queue;
    task_id_queue.push(task_id);
    std::lock_guard<std::mutex> lock(at_mutex);
    active_tasks.emplace(thread_id, task_id_queue);
  } else {
    iter->second.push(task_id);
  }
  return;
}

/**
* Once a task has completed we can pass the events it fired on to messaging,
* delete the events on which it was dependent, and update the ledger.
*/
void EDAT_Ledger::taskComplete(std::thread::id thread_id, long long int task_id) {
  std::map<DependencyKey,std::queue<SpecificEvent*>>::iterator iter;

  active_tasks.at(thread_id).pop();
  for (iter=arrived_events_store.at(task_id).begin(); iter!=arrived_events_store.at(task_id).end(); iter++) {
    while (!iter->second.empty()) {
      delete iter->second.front();
      iter->second.pop();
    }
  }
  aes_mutex.lock();
  arrived_events_store.erase(task_id);
  aes_mutex.unlock();
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
