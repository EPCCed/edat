#include "resilience.h"
#include "messaging.h"
#include "scheduler.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <thread>
#include <mutex>
#include <chrono>
#include <map>
#include <queue>

static EDAT_Thread_Ledger * internal_ledger;
static EDAT_Process_Ledger * external_ledger;
static const char call[4] = {'u', 'o', 'k', '\0'};
static const char response[4] = {'a', 'o', 'k', '\0'};
static const char obit[4] = {'r', 'i', 'p', '\0'};

/**
* Allocates the two ledgers and notifies the user that resilience is active.
* internal_ledger is in-memory only, and includes storage for events which
* have been fired from active tasks and held.
* external_ledger exists off-memory, and can be used for recovering from a
* failed process. It includes storage for tasks which are scheduled, but have
* not run.
*/
void resilienceInit(Scheduler& ascheduler, ThreadPool& athreadpool, Messaging& amessaging, const std::thread::id thread_id, const task_ptr_t * const task_array, const int num_tasks, const int beat_period) {
  int my_rank = amessaging.getRank();
  int num_ranks = amessaging.getNumRanks();
  char ledger_name_buffer[20];
  sprintf(ledger_name_buffer, "edat_ledger_%d", my_rank);
  std::string ledger_name = std::string(ledger_name_buffer);
  FILE * ftest;
  bool recovery;

  internal_ledger = new EDAT_Thread_Ledger(ascheduler, athreadpool, amessaging, thread_id);
  ftest = fopen(ledger_name_buffer, "r");
  if (ftest == NULL) {
    recovery = false;
  } else {
    fclose(ftest);
    recovery = true;
  }
  external_ledger = new EDAT_Process_Ledger(ascheduler, amessaging, my_rank, num_ranks, task_array, num_tasks, beat_period, ledger_name, recovery);

  if (!my_rank) {
    std::cout << "EDAT resilience initialised." << std::endl;
    std::cout << "Unsupported: EDAT_MAIN_THREAD_WORKER, edatFirePersistentEvent, edatFireEventWithReflux, edatWait" << std::endl;
    std::cout << "Please do not use the following character strings as event IDs: ";
    std::cout << std::string(call) << " " << std::string(response) << " " << std::string(obit) << std::endl;
  }
  external_ledger->beginMonitoring();

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
* Adds an event which has arrived on-process to the external_ledger
*/
bool resilienceAddEvent(SpecificEvent& event) {
  const int source_id = event.getSourcePid();
  std::string event_id = event.getEventId();
  const char * c_event_id = event_id.c_str();

  if (!strcmp(c_event_id, call)) {
    external_ledger->respondToMonitor();
    return false;
  } else if (!strcmp(c_event_id, response)) {
    if (source_id != RESILIENCE_MASTER) external_ledger->registerMonitorResponse(source_id);
    return false;
  } else if (!strcmp(c_event_id, obit)) {
    const int dead_rank = *((int *) event.getData());
    external_ledger->registerObit(dead_rank);
    return false;
  } else {
    DependencyKey depkey = DependencyKey(event_id, source_id);
    external_ledger->addEvent(depkey, event);
    return true;
  }
}

/**
* Moves an event which has already arrived and been stored in the external_ledger
* to its associated task in the task_log
*/
void resilienceMoveEventToTask(const DependencyKey depkey, const taskID_t task_id) {
  external_ledger->moveEventToTask(depkey, task_id);
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
  external_ledger->endMonitoring();
  external_ledger->deleteLedgerFile();
  delete internal_ledger;
  delete external_ledger;
}

void resilienceSyntheticFinalise(void) {
  external_ledger->endMonitoring();
  delete internal_ledger;
  delete external_ledger;
  return;
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
* Deserialization routine, instatiates a LoggedTask from an open istream and
* a streampos marking the start of the object
*/
LoggedTask::LoggedTask(std::istream& file, const std::streampos object_begin) {
  char eoo[4] = {'E', 'O', 'O', '\0'};
  char marker_buf[4];
  char * memblock;

  this->file_pos = object_begin;
  file.seekg(object_begin);

  memblock = new char[sizeof(TaskState)];
  file.read(memblock, sizeof(TaskState));
  this->state = *(reinterpret_cast<TaskState*>(memblock));
  delete[] memblock;

  this->ptd = new PendingTaskDescriptor(file, file.tellg());

  file.read(marker_buf, sizeof(marker_buf));
  if(strcmp(marker_buf, eoo)) raiseError("LoggedTask deserialization error, EOO not found");
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
* Serialization routine, leaves put pointer at end of object
*/
void LoggedTask::serialize(std::ostream& file, const std::streampos object_begin) {
  // serialization schema:
  // TaskState state, PendingTaskDescriptor ptd, EOO
  const char eoo[4] = {'E', 'O', 'O', '\0'};

  file.seekp(object_begin);
  file_pos = object_begin;

  file.write(reinterpret_cast<const char *>(&state), sizeof(TaskState));
  ptd->serialize(file, file.tellp());

  file.write(eoo, sizeof(eoo));

  return;
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
* Constructor, generates file name for serialization.
*/
EDAT_Process_Ledger::EDAT_Process_Ledger(Scheduler& ascheduler, Messaging& amessaging, const int my_rank, const int num_ranks, const task_ptr_t * const thetaskarray, const int num_tasks, const int a_beat_period, std::string ledger_name)
  : scheduler(ascheduler), messaging(amessaging), RANK(my_rank), NUM_RANKS(num_ranks), task_array(thetaskarray), number_of_tasks(num_tasks), beat_period(a_beat_period), fname(ledger_name) {
    serialize();
}

EDAT_Process_Ledger::EDAT_Process_Ledger(Scheduler& ascheduler, Messaging& amessaging, const int my_rank, const int num_ranks, const task_ptr_t * const thetaskarray, const int num_tasks, const int a_beat_period, std::string ledger_name, bool recovery) : scheduler(ascheduler), messaging(amessaging), RANK(my_rank), NUM_RANKS(num_ranks), task_array(thetaskarray), number_of_tasks(num_tasks), beat_period(a_beat_period), fname(ledger_name) {
  if (recovery) {
    const char tsk[4] = {'T', 'S', 'K', '\0'};
    const char evt[4] = {'E', 'V', 'T', '\0'};
    const char eoo[4] = {'E', 'O', 'O', '\0'};
    const char eol[4] = {'E', 'O', 'L', '\0'};
    char marker_buf[4], ledger_name_buf[24];
    const size_t marker_size = sizeof(marker_buf);
    bool found_eol;
    char * memblock;
    taskID_t task_id;
    LoggedTask * lgt;
    SpecificEvent * spec_evt;
    std::map<DependencyKey,std::queue<SpecificEvent*>>::iterator oe_iter;
    std::map<DependencyKey,std::queue<SpecificEvent*>>::iterator ae_iter;
    std::map<taskID_t,LoggedTask*>::iterator tl_iter;
    std::map<taskID_t,std::queue<SpecificEvent*>> orphaned_events;
    std::map<taskID_t,std::queue<SpecificEvent*>>::iterator orph_iter;
    std::string dead_ledger;
    std::fstream file;
    std::streampos bookmark;

    // rename ledger file of dead rank
    sprintf(ledger_name_buf, "edat_ledger_%d_%s", RANK, "_DEAD");
    dead_ledger = std::string(ledger_name_buf);
    rename(fname.c_str(), ledger_name_buf);

    // open old ledger file for reading
    file.open(dead_ledger, std::ios::in | std::ios::binary | std::ios::ate);
    file.seekg(std::ios::beg);

    found_eol = false;
    while (!found_eol) {
      bookmark = file.tellg();
      file.read(marker_buf, marker_size);
      if (!strcmp(marker_buf, tsk)) {
        // found a task
        memblock = new char[sizeof(taskID_t)];
        file.read(memblock, sizeof(taskID_t));
        task_id = *(reinterpret_cast<taskID_t*>(memblock));
        delete[] memblock;

        lgt = new LoggedTask(file, file.tellg());

        task_log.emplace(task_id, lgt);

        file.read(marker_buf, marker_size);
        if (strcmp(marker_buf, eoo)) raiseError("EDAT_Process_Ledger task deserialization error, EOO not found");
      } else if (!strcmp(marker_buf, evt)) {
        // found an event
        memblock = new char[sizeof(taskID_t)];
        file.read(memblock, sizeof(taskID_t));
        task_id = *(reinterpret_cast<taskID_t*>(memblock));
        delete[] memblock;

        spec_evt = new SpecificEvent(file, file.tellg());
        spec_evt->setFilePos(bookmark);

        file.read(marker_buf, marker_size);
        if (strcmp(marker_buf, eoo)) raiseError("EDAT_Process_Ledger event deserialization error, EOO not found");

        DependencyKey depkey = DependencyKey(spec_evt->getEventId(), spec_evt->getSourcePid());
        if (!task_id) {
          // event is outstanding
          oe_iter = outstanding_events.find(depkey);
          if (oe_iter == outstanding_events.end()) {
            std::queue<SpecificEvent*> event_queue;
            event_queue.push(spec_evt);
            outstanding_events.emplace(depkey, event_queue);
          } else {
            oe_iter->second.push(spec_evt);
          }
        } else {
          // event belongs to a task, which might not have been loaded yet...
          tl_iter = task_log.find(task_id);
          if (tl_iter == task_log.end()) {
            // task hasn't been loaded yet, so hold on to the event for now
            orph_iter = orphaned_events.find(task_id);
            if(orph_iter == orphaned_events.end()) {
              std::queue<SpecificEvent*> event_queue;
              event_queue.push(spec_evt);
              orphaned_events.emplace(task_id, event_queue);
            } else {
              orph_iter->second.push(spec_evt);
            }
          } else {
            // give task the event
            ae_iter = tl_iter->second->ptd->arrivedEvents.find(depkey);
            if (ae_iter == tl_iter->second->ptd->arrivedEvents.end()) {
              std::queue<SpecificEvent*> event_queue;
              event_queue.push(spec_evt);
              tl_iter->second->ptd->arrivedEvents.emplace(depkey, event_queue);
            } else {
              ae_iter->second.push(spec_evt);
            }
          }

        }
      } else if (!strcmp(marker_buf, eol)) {
        // found the end of the ledger
        found_eol = true;
      } else {
        // something bad has happened
        raiseError("EDAT_Process_Ledger deserialization error, unable to parse marker");
      }
    }
    file.close();

    // deal with any orphaned events
    for (orph_iter = orphaned_events.begin(); orph_iter != orphaned_events.end(); ++orph_iter) {
      while (!orph_iter->second.empty()) {
        spec_evt = orph_iter->second.front();
        orph_iter->second.pop();
        DependencyKey depkey = DependencyKey(spec_evt->getEventId(), spec_evt->getSourcePid());
        tl_iter = task_log.find(orph_iter->first);
        if (tl_iter == task_log.end()) {
          // something bad has happened
          raiseError("EDAT_Process_Ledger deserialization error, unable to match orphaned event with task");
        } else {
          ae_iter = tl_iter->second->ptd->arrivedEvents.find(depkey);
          if (ae_iter == tl_iter->second->ptd->arrivedEvents.end()) {
            std::queue<SpecificEvent*> event_queue;
            event_queue.push(spec_evt);
            tl_iter->second->ptd->arrivedEvents.emplace(depkey, event_queue);
          } else {
            ae_iter->second.push(spec_evt);
          }
        }
      }
    }
  }
  // write new file
  serialize();
}

/**
* Includes deep delete of the task_log.
*/
EDAT_Process_Ledger::~EDAT_Process_Ledger() {
  std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator oe_iter;
  std::map<taskID_t,LoggedTask*>::iterator tl_iter;

  while(!outstanding_events.empty()) {
    oe_iter = outstanding_events.begin();
    while(!oe_iter->second.empty()) {
      delete oe_iter->second.front();
      oe_iter->second.pop();
    }
    outstanding_events.erase(oe_iter);
  }

  while (!task_log.empty()) {
    tl_iter = task_log.begin();
    delete tl_iter->second;
    task_log.erase(tl_iter);
  }
}

void EDAT_Process_Ledger::commit(const taskID_t task_id, LoggedTask& lgt) {
  std::fstream file;
  const char tsk[4] = {'T', 'S', 'K', '\0'};
  const char eoo[4] = {'E', 'O', 'O', '\0'};
  const char eol[4] = {'E', 'O', 'L', '\0'};
  char marker_buf[4];
  const size_t marker_size = sizeof(marker_buf);
  std::streampos bookmark;

  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::binary | std::ios::out | std::ios::in);
  file.seekp(-marker_size, std::ios::end);
  bookmark = file.tellp();

  file.read(marker_buf, marker_size);
  if (strcmp(marker_buf, eol)) raiseError("Error in addTask commit, EOL not found");

  file.seekp(bookmark);
  file.write(tsk, marker_size);
  file.write(reinterpret_cast<const char *>(&task_id), sizeof(taskID_t));
  lgt.serialize(file, file.tellp());
  file.write(eoo, marker_size);
  file.write(eol, marker_size);

  file.close();

  return;
}

void EDAT_Process_Ledger::commit(SpecificEvent& spec_evt) {
  std::fstream file;
  const char evt[4] = {'E', 'V', 'T', '\0'};
  const char eoo[4] = {'E', 'O', 'O', '\0'};
  const char eol[4] = {'E', 'O', 'L', '\0'};
  char marker_buf[4];
  const size_t marker_size = sizeof(marker_buf);
  const taskID_t no_task_id = 0;
  std::streampos bookmark;

  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::binary | std::ios::out | std::ios::in);
  file.seekp(-marker_size, std::ios::end);
  bookmark = file.tellp();

  // just double check everything at the end of the file is as it should be...
  file.read(marker_buf, marker_size);
  if (strcmp(marker_buf, eol)) raiseError("Error in addEvent commit, EOL not found");

  file.seekp(bookmark);
  spec_evt.setFilePos(bookmark);
  file.write(evt, marker_size);
  file.write(reinterpret_cast<const char *>(&no_task_id), sizeof(taskID_t));
  spec_evt.serialize(file, file.tellp());
  file.write(eoo, marker_size);
  file.write(eol, marker_size);

  file.close();

  return;
}

void EDAT_Process_Ledger::commit(const taskID_t task_id, const SpecificEvent& spec_evt) {
  std::fstream file;
  const char evt[4] = {'E', 'V', 'T', '\0'};
  const char eoo[4] = {'E', 'O', 'O', '\0'};
  char marker_buf[4];
  const size_t marker_size = sizeof(marker_buf);

  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::binary | std::ios::out | std::ios::in);

  file.seekp(spec_evt.getFilePos());
  file.read(marker_buf, marker_size);
  if (strcmp(marker_buf, evt)) {
    std::cout << marker_buf << std::endl;
    raiseError("Error in moveEventToTask commit, EVT not found");
  }

  file.write(reinterpret_cast<const char *>(&task_id), sizeof(taskID_t));
  spec_evt.serialize(file, file.tellp());

  file.read(marker_buf, marker_size);
  if (strcmp(marker_buf, eoo)) raiseError("Error in moveEventToTask commit, EOO not found");

  file.close();

  return;
}

void EDAT_Process_Ledger::commit(const TaskState& state, const std::streampos file_pos) {
  std::fstream file;
  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::binary | std::ios::out | std::ios::in);
  file.seekp(file_pos);
  file.write(reinterpret_cast<const char *>(&state), sizeof(TaskState));
  file.close();
  return;
}

int EDAT_Process_Ledger::getFuncID(const task_ptr_t task_fn) {
  for (int func_id = 0; func_id<number_of_tasks; ++func_id) {
    if (task_array[func_id] == task_fn) return func_id;
  }
  return -1;
}

void EDAT_Process_Ledger::serialize() {
  // serialization schema:
  // TSK : taskID_t, LoggedTask : EOO
  // EVT : taskID_t, SpecificEvent : EOO
  // EOL
  const char tsk[4] = {'T', 'S', 'K', '\0'};
  const char evt[4] = {'E', 'V', 'T', '\0'};
  const char eoo[4] = {'E', 'O', 'O', '\0'};
  const char eol[4] = {'E', 'O', 'L', '\0'};
  char marker_buf[4];
  const size_t marker_size = sizeof(marker_buf);
  const taskID_t no_task_id = 0;
  std::fstream file;
  std::map<DependencyKey,std::queue<SpecificEvent*>>::iterator oe_iter;
  std::map<taskID_t,LoggedTask*>::iterator tl_iter;
  std::queue<SpecificEvent*> temp_queue;
  SpecificEvent * spec_event;

  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::out | std::ios::binary | std::ios::trunc);

  for (tl_iter = task_log.begin(); tl_iter != task_log.end(); ++tl_iter) {
    file.write(tsk, marker_size);
    file.write(reinterpret_cast<const char *>(&(tl_iter->first)), sizeof(taskID_t));
    tl_iter->second->serialize(file, file.tellp());
    file.write(eoo, marker_size);
  }

  for (oe_iter = outstanding_events.begin(); oe_iter != outstanding_events.end(); ++oe_iter) {
    while(!oe_iter->second.empty()) {
      spec_event = oe_iter->second.front();
      spec_event->setFilePos(file.tellp());
      file.write(evt, marker_size);
      file.write(reinterpret_cast<const char *>(&no_task_id), sizeof(taskID_t));
      oe_iter->second.pop();
      temp_queue.push(spec_event);
      spec_event->serialize(file, file.tellp());
      file.write(eoo, marker_size);
    }

    while(!temp_queue.empty()) {
      spec_event = temp_queue.front();
      temp_queue.pop();
      oe_iter->second.push(spec_event);
    }
  }

  file.write(eol, marker_size);

  file.close();

  return;
}

void EDAT_Process_Ledger::monitorProcesses(std::mutex& mp_monitor_mutex, bool& mp_monitor, const int mp_NUM_RANKS, bool * mp_live_ranks, Messaging& mp_messaging, const int mp_beat_period, std::mutex& mp_dead_ranks_mutex, std::set<int>& mp_dead_ranks) {
  int rank;
  mp_monitor_mutex.lock();
  mp_monitor = true;
  while (mp_monitor) {
    for (rank = 0; rank < mp_NUM_RANKS; rank++) mp_live_ranks[rank] = false;
    mp_live_ranks[RESILIENCE_MASTER] = true;
    mp_monitor_mutex.unlock();
    mp_messaging.fireEvent(NULL, 0, EDAT_NONE, EDAT_ALL, false, call);

    std::this_thread::sleep_for(std::chrono::seconds(mp_beat_period));

    mp_monitor_mutex.lock();
    for (rank = 0; rank < mp_NUM_RANKS; rank++) {
      if (!mp_live_ranks[rank]) {
        // rank is dead, attempt recovery
        printf("RIP rank %d\n", rank);
        announceRankDead(mp_dead_ranks_mutex, mp_dead_ranks, rank, mp_messaging);
      }
    }
  }

  return;
}

void EDAT_Process_Ledger::announceRankDead(std::mutex& ard_dead_ranks_mutex, std::set<int>& ard_dead_ranks, int rank, Messaging& ard_messaging) {
  std::pair<std::set<int>::iterator,bool> set_ret;
  // try to emplace the dead rank in the dead_ranks_set
  ard_dead_ranks_mutex.lock();
  set_ret = ard_dead_ranks.emplace(rank);
  ard_dead_ranks_mutex.unlock();
  // if true, rank was not already in the set, so give all ranks the sad news
  if (set_ret.second) ard_messaging.fireEvent(&rank, 1, EDAT_INT, EDAT_ALL, false, obit);

  return;
}

/**
* Emplaces the supplied PendingTaskDescriptor in the task_log. Keyed by task_id.
*/
void EDAT_Process_Ledger::addTask(const taskID_t task_id, PendingTaskDescriptor& ptd) {
  std::lock_guard<std::mutex> lock(log_mutex);

  if (ptd.func_id == -1) {
    ptd.func_id = getFuncID(ptd.task_fn);
  }

  LoggedTask * lgt = new LoggedTask(ptd);
  task_log.emplace(task_id, lgt);

  commit(task_id, *lgt);

  return;
}

/**
* Adds an event which has arrived on-process but not yet been matched with a
* task to outstanding_events
*/
void EDAT_Process_Ledger::addEvent(const DependencyKey depkey, const SpecificEvent& event) {
  std::lock_guard<std::mutex> lock(log_mutex);

  std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator oe_iter = outstanding_events.find(depkey);
  SpecificEvent * event_copy = new SpecificEvent(event);

  if (oe_iter == outstanding_events.end()) {
    std::queue<SpecificEvent*> eventQueue;
    eventQueue.push(event_copy);
    outstanding_events.insert(std::pair<DependencyKey, std::queue<SpecificEvent*>>(depkey, eventQueue));
  } else {
    oe_iter->second.push(event_copy);
  }

  commit(*event_copy);
  return;
}

/**
* Updates a task embedded in the log with an arrived event.
*/
void EDAT_Process_Ledger::moveEventToTask(const DependencyKey depkey, const taskID_t task_id) {
  std::lock_guard<std::mutex> lock(log_mutex);

  PendingTaskDescriptor * ptd = task_log.at(task_id)->ptd;
  std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator oe_iter = outstanding_events.find(depkey);
  std::map<DependencyKey, int*>::iterator od_iter = ptd->outstandingDependencies.find(depkey);
  std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator ae_iter = ptd->arrivedEvents.find(depkey);

  SpecificEvent* event = oe_iter->second.front();
  oe_iter->second.pop();
  if (oe_iter->second.empty()) outstanding_events.erase(oe_iter);

  (*(od_iter->second))--;

  ptd->numArrivedEvents++;
  if (ae_iter == ptd->arrivedEvents.end()) {
    std::queue<SpecificEvent*> eventQueue;
    eventQueue.push(event);
    ptd->arrivedEvents.insert(std::pair<DependencyKey, std::queue<SpecificEvent*>>(depkey, eventQueue));
  } else {
    ae_iter->second.push(event);
  }

  commit(task_id, *event);
  return;
}

void EDAT_Process_Ledger::markTaskRunning(const taskID_t task_id) {
  log_mutex.lock();
  LoggedTask * const lgt = task_log.at(task_id);
  const std::streampos bookmark = lgt->file_pos;
  lgt->state = RUNNING;
  log_mutex.unlock();
  commit(RUNNING, bookmark);
  return;
}

void EDAT_Process_Ledger::markTaskComplete(const taskID_t task_id) {
  log_mutex.lock();
  LoggedTask * const lgt = task_log.at(task_id);
  const std::streampos bookmark = lgt->file_pos;
  lgt->state = COMPLETE;
  log_mutex.unlock();
  commit(COMPLETE, bookmark);
  return;
}

void EDAT_Process_Ledger::markTaskFailed(const taskID_t task_id) {
  log_mutex.lock();
  LoggedTask * const lgt = task_log.at(task_id);
  const std::streampos bookmark = lgt->file_pos;
  lgt->state = FAILED;
  log_mutex.unlock();
  commit(FAILED, bookmark);
  return;
}

void EDAT_Process_Ledger::beginMonitoring() {
  if (RANK == RESILIENCE_MASTER) {
    live_ranks = new bool[NUM_RANKS];
    monitor_thread = std::thread(monitorProcesses, std::ref(monitor_mutex), std::ref(monitor), NUM_RANKS, live_ranks, std::ref(messaging), beat_period, std::ref(dead_ranks_mutex), std::ref(dead_ranks));
  }

  return;
}

void EDAT_Process_Ledger::respondToMonitor() {
  if (RANK != RESILIENCE_MASTER) {
    messaging.fireEvent(NULL, 0, EDAT_NOTYPE, RESILIENCE_MASTER, false, response);
  }
  return;
}

void EDAT_Process_Ledger::registerMonitorResponse(int rank) {
  std::lock_guard<std::mutex> lock(monitor_mutex);
  live_ranks[rank] = true;
  return;
}

void EDAT_Process_Ledger::registerObit(const int rank) {
  if (RANK != RESILIENCE_MASTER) {
    std::lock_guard<std::mutex> lock(dead_ranks_mutex);
    dead_ranks.emplace(rank);
  }
  return;
}

void EDAT_Process_Ledger::endMonitoring() {
  if (RANK == RESILIENCE_MASTER) {
    monitor_mutex.lock();
    monitor = false;
    for (int rank = 0; rank < NUM_RANKS; rank++) live_ranks[rank] = true;
    monitor_mutex.unlock();
    monitor_thread.join();
    delete[] live_ranks;
  }

  return;
}

void EDAT_Process_Ledger::deleteLedgerFile() {
  std::remove(fname.c_str());
  return;
}

/**
* debug display function
*/
void EDAT_Process_Ledger::display() const {
  std::map<DependencyKey,std::queue<SpecificEvent*>>::const_iterator oe_iter;
  std::map<taskID_t,LoggedTask*>::const_iterator tl_iter;

  std::cout << fname << ":" << std::endl;
  std::cout << "\tRANK = " << RANK << std::endl;
  std::cout << "\toutstanding_events:" << std::endl;
  std::cout << "\t\tsize = " << outstanding_events.size() << std::endl;
  for (oe_iter = outstanding_events.begin(); oe_iter != outstanding_events.end(); ++oe_iter) {
    std::cout << "\t\tDependency ";
    oe_iter->first.display();
    std::cout << "\t\tQueue size = " << oe_iter->second.size() << std::endl;
    if (!oe_iter->second.empty()) {
      std::cout << "\t\tFirst event in queue event_id = " <<
      oe_iter->second.front()->getEventId() << std::endl;
      std::cout << "\t\tFirst event in queue file_pos = " << oe_iter->second.front()->getFilePos() << std::endl;
    }
  }
  std::cout << "\ttask_log:" << std::endl;
  std::cout << "\t\tsize = " << task_log.size() << std::endl;
  for (tl_iter = task_log.begin(); tl_iter != task_log.end(); ++tl_iter) {
    std::cout << "\t\ttask_id: " << tl_iter->first << std::endl;
    std::cout << "\t\tfile_pos = " << tl_iter->second->file_pos << std::endl;
    std::cout << "\t\tstate = " << tl_iter->second->state << std::endl;
    std::cout << "\t\ttask_name = " << tl_iter->second->ptd->task_name << std::endl;
    std::cout << "\t\tnumArrivedEvents = " << tl_iter->second->ptd->numArrivedEvents << std::endl;
  }

  return;
}
