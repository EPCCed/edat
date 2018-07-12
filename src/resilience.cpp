#include "resilience.h"
#include "messaging.h"
#include "scheduler.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <queue>

static EDAT_Thread_Ledger * internal_ledger;
static EDAT_Process_Ledger * external_ledger;

/**
* Allocates the two ledgers and notifies the user that resilience is active.
* internal_ledger is in-memory only, and includes storage for events which
* have been fired from active tasks and held.
* external_ledger exists off-memory, and can be used for recovering from a
* failed process. It includes storage for tasks which are scheduled, but have
* not run.
*/
void resilienceInit(Scheduler& ascheduler, ThreadPool& athreadpool, Messaging& amessaging, const std::thread::id thread_id) {
  int my_rank = amessaging.getRank();

  internal_ledger = new EDAT_Thread_Ledger(ascheduler, athreadpool, amessaging, thread_id);
  external_ledger = new EDAT_Process_Ledger(ascheduler, my_rank);

  if (!my_rank) {
    std::cout << "EDAT resilience initialised." << std::endl;
    std::cout << "Unsupported: EDAT_MAIN_THREAD_WORKER, edatFirePersistentEvent, edatFireEventWithReflux, edatWait" << std::endl;
  }

  return;
}

/**
* Essentially repeats the steps of Scheduler::updateMatchingEventInTaskDescriptor
* but for the external_ledger copy of the task.
*/
void resilienceEventArrivedAtTask(const taskID_t task_id, const DependencyKey depkey, const SpecificEvent& event) {
  external_ledger->addArrivedEventToTask(task_id, depkey, event);
  return;
}

/**
* Creates a record of a task in the external_ledger task_log. It will be updated
* when events arrive and at state changes.
*/
void resilienceTaskScheduled(PendingTaskDescriptor& ptd) {
  external_ledger->addTask(ptd.task_id, ptd);
  return;
}

/**
* Grabs the calling thread ID, and hands off to
* EDAT_Thread_Ledger::holdFiredEvent. That member function now checks the
* thread ID against that of the main thread.
*/
void resilienceEventFired(void * data, int data_count, int data_type,
                          int target, bool persistent, const char * event_id) {
  const std::thread::id this_thread = std::this_thread::get_id();

  internal_ledger->holdFiredEvent(this_thread, data, data_count, data_type, target, false, event_id);

  return;
}

/**
* Marks task as active in both ledgers. In internal_ledger this triggers
* creation of storage for events which are fired.
*/
void resilienceTaskRunning(const std::thread::id thread_id, PendingTaskDescriptor& ptd) {
  internal_ledger->taskActiveOnThread(thread_id, ptd);
  external_ledger->markTaskRunning(ptd.task_id);
  return;
}

/**
* Marks task as completed in both ledgers.
*/
void resilienceTaskCompleted(const std::thread::id thread_id, const taskID_t task_id) {
  internal_ledger->taskComplete(thread_id, task_id);
  external_ledger->markTaskComplete(task_id);
  return;
}

/**
* Marks thread as failed in external_ledger, and triggers recovery process
* from internal_ledger.
*/
void resilienceThreadFailed(const std::thread::id thread_id) {
  const taskID_t task_id = internal_ledger->getCurrentlyActiveTask(thread_id);
  internal_ledger->threadFailure(thread_id, task_id);
  external_ledger->markTaskFailed(task_id);

  return;
}

/**
* Clears ledgers from memory.
*/
void resilienceFinalise(void) {
  delete internal_ledger;
  delete external_ledger;
}

/**
* Allocates memory for a new PendingTaskDescriptor, and deep copies the source
* to it.
*/
LoggedTask::LoggedTask(PendingTaskDescriptor& src) {
  ptd = new PendingTaskDescriptor();
  ptd->deepCopy(src);
}

/**
* Deep deletes the PendingTaskContainer held in the LoggedTask.
*/
LoggedTask::~LoggedTask() {
  std::map<DependencyKey,int*>::iterator oDiter;
  std::map<DependencyKey,std::queue<SpecificEvent*>>::iterator aEiter;

  for (oDiter = ptd->outstandingDependencies.begin(); oDiter != ptd->outstandingDependencies.end(); ++oDiter) {
    delete oDiter->second;
  }
  for (aEiter = ptd->arrivedEvents.begin(); aEiter != ptd->arrivedEvents.end(); ++aEiter) {
    while (!aEiter->second.empty()) {
      delete aEiter->second.front();
      aEiter->second.pop();
    }
  }
  for (oDiter = ptd->originalDependencies.begin(); oDiter != ptd->originalDependencies.end(); ++oDiter) {
    delete oDiter->second;
  }

  delete ptd;
}

/**
* Simple look-up function for what task is running on a thread
*/
taskID_t EDAT_Thread_Ledger::getCurrentlyActiveTask(const std::thread::id thread_id) {
  std::lock_guard<std::mutex> lock(id_mutex);
  return threadID_to_taskID.at(thread_id).back();
}

/**
* Called on task completion, hands off events which were fired from the task
* to the messaging system
*/
void EDAT_Thread_Ledger::releaseHeldEvents(const taskID_t task_id) {
  std::lock_guard<std::mutex> lock(at_mutex);
  ActiveTaskDescriptor * atd = active_tasks.at(task_id);
  HeldEvent held_event;

  while (!atd->firedEvents.empty()) {
    held_event = atd->firedEvents.front();
    messaging.fireEvent(held_event.spec_evt->getData(), held_event.spec_evt->getMessageLength(), held_event.spec_evt->getMessageType(), held_event.target, false, held_event.event_id);
    free(held_event.spec_evt->getData());
    delete held_event.spec_evt;
    atd->firedEvents.pop();
  }

  return;
}

/**
* Clears all events held for a task, presumably because that task has failed
*/
void EDAT_Thread_Ledger::purgeHeldEvents(const taskID_t task_id) {
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
void EDAT_Thread_Ledger::holdFiredEvent(const std::thread::id thread_id, void * data,
                            int data_count, int data_type, int target,
                            bool persistent, const char * event_id) {
  if (thread_id == main_thread_id) {
    // if event has been fired from main() it should pass straight through
    messaging.fireEvent(data, data_count, data_type, target, persistent, event_id);
  } else {
    // event has been fired from a task and should be held
    HeldEvent held_event;
    const taskID_t task_id = getCurrentlyActiveTask(thread_id);
    const int data_size = data_count * messaging.getTypeSize(data_type);
    SpecificEvent * spec_evt = new SpecificEvent(messaging.getRank(), data_count, data_size, data_type, persistent, false, event_id, NULL);

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
  }
  return;
}

/**
* Subversion of edatFireEvent is achieved by checking the thread ID, this
* function links a thread ID to the ID of the task which is running on that
* thread. This means we can use the task ID for the rest of the resilience
* functionality, and we don't need to worry about the thread moving on to other
* things.
*/
void EDAT_Thread_Ledger::taskActiveOnThread(const std::thread::id thread_id, PendingTaskDescriptor& ptd) {
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

  return;
}

/**
* Once a task has completed we can pass the events it fired on to messaging,
* delete the events on which it was dependent, and update the ledger.
*/
void EDAT_Thread_Ledger::taskComplete(const std::thread::id thread_id, const taskID_t task_id) {
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
    std::cout << "[" << messaging.getRank() << "] Task " << task_id << " attempted to complete, but has already been reported as failed, and resubmitted to the task scheduler." << std::endl;
  }

  return;
}

/**
* Handles a failed thread by marking the task as failed and preventing events
* from being fired. Then reschedules the task by submitting a fresh
* PendingTaskContainer to Scheduler::readyToRunTask. New task ID is reported.
*/
void EDAT_Thread_Ledger::threadFailure(const std::thread::id thread_id, const taskID_t task_id) {
  const int my_rank = messaging.getRank();
  std::lock_guard<std::mutex> lock(failure_mutex);

  if (completed_tasks.find(task_id) == completed_tasks.end()) {
    failed_tasks.insert(task_id);
    std::cout << "[" << my_rank << "] Task " << task_id  << " has been reported as failed. Any held events will be purged." << std::endl;

    threadpool.killWorker(thread_id);

    purgeHeldEvents(task_id);
    at_mutex.lock();
    PendingTaskDescriptor * ptd = active_tasks.at(task_id)->generatePendingTask();
    delete active_tasks.at(task_id);
    active_tasks.erase(task_id);
    at_mutex.unlock();

    external_ledger->addTask(ptd->task_id, *ptd);
    scheduler.readyToRunTask(ptd);

    std::cout << "[" << my_rank << "] Task " << task_id << " rescheduled with new task ID: "
    << ptd->task_id << std::endl;
  } else {
    std::cout << "[" << my_rank << "] Task " << task_id << " reported as failed, but has already successfully completed." << std::endl;
  }

  return;
}

/**
* Includes deep delete of the task_log.
*/
EDAT_Process_Ledger::~EDAT_Process_Ledger() {
  std::map<taskID_t,LoggedTask*>::iterator tl_iter;
  while (!task_log.empty()) {
    tl_iter = task_log.begin();
    delete tl_iter->second;
    task_log.erase(tl_iter);
  }
}

/**
* Emplaces the supplied PendingTaskDescriptor in the task_log. Keyed by task_id.
*/
void EDAT_Process_Ledger::addTask(const taskID_t task_id, PendingTaskDescriptor& ptd) {
  std::lock_guard<std::mutex> lock(log_mutex);
  task_log.emplace(task_id, new LoggedTask(ptd));
  return;
}

/**
* Updates a task embedded in the log with an arrived event.
*/
void EDAT_Process_Ledger::addArrivedEventToTask(const taskID_t task_id, const DependencyKey depkey, const SpecificEvent& event) {
  std::lock_guard<std::mutex> lock(log_mutex);

  PendingTaskDescriptor * ptd = task_log.at(task_id)->ptd;
  std::map<DependencyKey, int*>::iterator oDiter = ptd->outstandingDependencies.find(depkey);
  std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator aEiter = ptd->arrivedEvents.find(depkey);
  SpecificEvent * event_copy = new SpecificEvent(event);

  (*(oDiter->second))--;
  if (*(oDiter->second) <= 0) ptd->outstandingDependencies.erase(oDiter);

  ptd->numArrivedEvents++;
  if (aEiter == ptd->arrivedEvents.end()) {
    std::queue<SpecificEvent*> eventQueue;
    eventQueue.push(event_copy);
    ptd->arrivedEvents.insert(std::pair<DependencyKey, std::queue<SpecificEvent*>>(depkey, eventQueue));
  } else {
    aEiter->second.push(event_copy);
  }

  return;
}

void EDAT_Process_Ledger::markTaskRunning(const taskID_t task_id) {
  std::lock_guard<std::mutex> lock(log_mutex);
  task_log.at(task_id)->state = RUNNING;
  return;
}

void EDAT_Process_Ledger::markTaskComplete(const taskID_t task_id) {
  std::lock_guard<std::mutex> lock(log_mutex);
  task_log.at(task_id)->state = COMPLETE;
  return;
}

void EDAT_Process_Ledger::markTaskFailed(const taskID_t task_id) {
  std::lock_guard<std::mutex> lock(log_mutex);
  task_log.at(task_id)->state = FAILED;
  return;
}
