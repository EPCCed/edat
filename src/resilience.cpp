#include "resilience.h"
#include "messaging.h"
#include "scheduler.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <queue>

namespace resilience {
  // put the in-process resilience ledger in to a seperate namespace
  // to avoid conflicts
  EDAT_Ledger * process_ledger;
}

EDAT_Ledger::EDAT_Ledger(Scheduler & ascheduler, Messaging * amessaging, const std::thread::id thread_id) : scheduler(ascheduler), messaging(amessaging), main_thread_id(thread_id)  {
  if (!messaging->getRank()) {
    std::cout << "EDAT resilience initialised." << std::endl;
    std::cout << "Unsupported: EDAT_MAIN_THREAD_WORKER, edatFirePersistentEvent, edatFireEventWithReflux, edatWait" << std::endl;
  }
}

/**
* Simple look-up function for what task is running on a thread
*/
taskID_t EDAT_Ledger::getCurrentlyActiveTask(const std::thread::id thread_id) {
  std::lock_guard<std::mutex> lock(id_mutex);
  return threadID_to_taskID.at(thread_id).back();
}

/**
* Called on task completion, hands off events which were fired from the task
* to the messaging system
*/
void EDAT_Ledger::releaseHeldEvents(const taskID_t task_id) {
  std::lock_guard<std::mutex> lock(at_mutex);
  ActiveTaskDescriptor * atd = active_tasks.at(task_id);
  HeldEvent held_event;

  while (!atd->firedEvents.empty()) {
    held_event = atd->firedEvents.front();
    messaging->fireEvent(held_event.spec_evt->getData(), held_event.spec_evt->getMessageLength(), held_event.spec_evt->getMessageType(), held_event.target, false, held_event.event_id);
    free(held_event.spec_evt->getData());
    delete held_event.spec_evt;
    atd->firedEvents.pop();
  }

  return;
}

/**
* Clears all events held for a task, presumably because that task has failed
*/
void EDAT_Ledger::purgeHeldEvents(const taskID_t task_id) {
  std::lock_guard<std::mutex> lock(at_mutex);
  ActiveTaskDescriptor * atd = active_tasks.at(task_id);
  HeldEvent held_event;

  while (!atd->firedEvents.empty()) {
    held_event = atd->firedEvents.front();
    free(held_event.spec_evt->getData());
    delete held_event.spec_evt;
    atd->firedEvents.pop();
  }

  return;
}

/**
* Stores events which are fired from a task. Tasks are diverted in edatFireEvent
* and std::thread::id is used to link the event to a task_id. Events will not be
* fired until the task completes.
*/
void EDAT_Ledger::holdFiredEvent(const std::thread::id thread_id, void * data,
                            int data_count, int data_type, int target,
                            bool persistent, const char * event_id) {
  HeldEvent held_event;
  const taskID_t task_id = getCurrentlyActiveTask(thread_id);
  const int data_size = data_count * messaging->getTypeSize(data_type);
  SpecificEvent * spec_evt = new SpecificEvent(messaging->getRank(), data_count, data_size, data_type, persistent, false, event_id, NULL);

  if (data != NULL) {
    // do this so application developer can safely free after 'firing' an event
    char * data_copy = (char *) malloc(data_size);
    memcpy(data_copy, data, data_size);
    spec_evt->setData(data_copy);
  }

  held_event.target = target;
  held_event.event_id = event_id;
  held_event.spec_evt = spec_evt;

  at_mutex.lock();
  active_tasks.at(task_id)->firedEvents.push(held_event);
  at_mutex.unlock();

  return;
}

/**
* Subversion of edatFireEvent is achieved by checking the thread ID, this
* function links a thread ID to the ID of the task which is running on that
* thread. This means we can use the task ID for the rest of the resilience
* functionality, and we don't need to worry about the thread moving on to other
* things.
*/
void EDAT_Ledger::taskActiveOnThread(const std::thread::id thread_id, PendingTaskDescriptor& ptd) {
  ActiveTaskDescriptor * atd = new ActiveTaskDescriptor(ptd);
  std::map<std::thread::id,std::queue<taskID_t>>::iterator ttt_iter = threadID_to_taskID.find(thread_id);

  at_mutex.lock();
  active_tasks.emplace(ptd.task_id, atd);
  at_mutex.unlock();

  if (ttt_iter == threadID_to_taskID.end()) {
    std::queue<taskID_t> task_id_queue;
    task_id_queue.push(ptd.task_id);
    id_mutex.lock();
    threadID_to_taskID.emplace(thread_id, task_id_queue);
    id_mutex.unlock();
  } else {
    id_mutex.lock();
    ttt_iter->second.push(ptd.task_id);
    id_mutex.unlock();
  }

  // [PROCESS-FAIL] move pending task to active task in DB *here*

  return;
}

/**
* Once a task has completed we can pass the events it fired on to messaging,
* delete the events on which it was dependent, and update the ledger.
*/
void EDAT_Ledger::taskComplete(const std::thread::id thread_id, const taskID_t task_id) {
  std::lock_guard<std::mutex> lock(failure_mutex);
  if (failed_tasks.find(task_id) == failed_tasks.end()) {
    completed_tasks.insert(task_id);

    id_mutex.lock();
    threadID_to_taskID.at(thread_id).pop();
    id_mutex.unlock();

    releaseHeldEvents(task_id);

    std::lock_guard<std::mutex> lock(at_mutex);
    delete active_tasks.at(task_id);
    active_tasks.erase(task_id);
  } else {
    std::cout << "Task " << task_id << " attempted to complete, but has already been reported as failed, and resubmitted to the task scheduler." << std::endl;
  }

  // [PROCESS-FAIL] move task from active to completed here

  return;
}

/**
* Handles a failed thread by marking the task as failed and preventing events
* from being fired. Then reschedules the task by submitting a fresh
* PendingTaskContainer to Scheduler::readyToRunTask. New task ID is reported.
*/
void EDAT_Ledger::threadFailure(const std::thread::id thread_id) {
  std::lock_guard<std::mutex> lock(failure_mutex);
  const taskID_t task_id = getCurrentlyActiveTask(thread_id);

  if (completed_tasks.find(task_id) == completed_tasks.end()) {
    failed_tasks.insert(task_id);
    std::cout << "Task " << task_id  << " has been reported as failed. Any held events will be purged." << std::endl;

    purgeHeldEvents(task_id);
    at_mutex.lock();
    PendingTaskDescriptor * ptd = active_tasks.at(task_id)->generatePendingTask();
    delete active_tasks.at(task_id);
    active_tasks.erase(task_id);
    at_mutex.unlock();

    scheduler.readyToRunTask(ptd);

    std::cout << "Task " << task_id << " rescheduled with new task ID: "
    << ptd->task_id << std::endl;

  } else {
    std::cout << "Task " << task_id << " reported as failed, but has already successfully completed." << std::endl;
  }

  return;
}

/**
* Called by edatFinalise, just deletes the ledger.
*/
void EDAT_Ledger::finalise(void) {
  delete this;
  return;
}
