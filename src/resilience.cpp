#include "resilience.h"
#include "messaging.h"
#include "scheduler.h"
#include "metrics.h"
#include "mpi.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <thread>
#include <mutex>
#include <chrono>
#include <map>
#include <queue>

// ledgers
static EDAT_Thread_Ledger * internal_ledger;
static EDAT_Process_Ledger * external_ledger;

// monitoring event IDs
static const char obit[4] = {'r', 'i', 'p', '\0'};
static const char phoenix[4] = {'p', 'i', 'r', '\0'};

// serialization object markers
static const char tsk[4] = {'T', 'S', 'K', '\0'};
static const char evt[4] = {'E', 'V', 'T', '\0'};
static const char hvt[4] = {'H', 'V', 'T', '\0'};
static const char eoo[4] = {'E', 'O', 'O', '\0'};
static const char eol[4] = {'E', 'O', 'L', '\0'};
static const size_t marker_size = 4 * sizeof(char);

static bool started = false;

/**
* Allocates the two ledgers and notifies the user that resilience is active.
* internal_ledger is in-memory only, and includes storage for events which
* have been fired from active tasks and held.
* external_ledger exists off-memory, and can be used for recovering from a
* failed process. It includes storage for tasks which are scheduled, but have
* not run.
*/
void resilienceInit(Scheduler& ascheduler, ThreadPool& athreadpool, Messaging& amessaging, const std::thread::id thread_id, const task_ptr_t * const task_array, const int num_tasks, const unsigned int comm_timeout) {
  #if DO_METRICS
  unsigned long int timer_key = metrics::METRICS->timerStart("resilienceInit");
  #endif

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
  external_ledger = new EDAT_Process_Ledger(ascheduler, amessaging, my_rank, num_ranks, task_array, num_tasks, comm_timeout, ledger_name, recovery);

  if (!my_rank) {
    std::cout << "EDAT resilience initialised." << std::endl;
    std::cout << "Unsupported: EDAT_MAIN_THREAD_WORKER, edatFirePersistentEvent, edatFireEventWithReflux, edatWait, contexts" << std::endl;
    std::cout << "Please do not use the following character strings as event IDs: ";
    std::cout << std::string(obit) << " " << std::string(phoenix) << std::endl;
  }

  if (recovery) external_ledger->recover();

  started = true;
  external_ledger->beginMonitoring();

  if (recovery) amessaging.fireEvent(&my_rank, 1, EDAT_INT, EDAT_ALL, false, phoenix);

  #if DO_METRICS
  metrics::METRICS->timerStop("resilienceInit", timer_key);
  #endif

  return;
}

/**
* Creates a record of a task in the external_ledger task_log. It will be updated
* when events arrive and at state changes.
*/
void resilienceTaskScheduled(PendingTaskDescriptor& ptd) {
  #if DO_METRICS
  unsigned long int timer_key = metrics::METRICS->timerStart("resilienceTaskScheduled");
  #endif
  external_ledger->addTask(ptd.task_id, ptd);
  #if DO_METRICS
  metrics::METRICS->timerStop("resilienceTaskScheduled", timer_key);
  #endif
  return;
}

/**
* Adds an event which has arrived on-process to the external_ledger
*/
bool resilienceAddEvent(SpecificEvent& event) {
  const int source_id = event.getSourcePid();
  std::string event_id = event.getEventId();
  const char * c_event_id = event_id.c_str();

  if (!strcmp(c_event_id, obit)) {
    const int dead_rank = *((int *) event.getData());
    external_ledger->registerObit(dead_rank);
    return false;
  } else if (!strcmp(c_event_id, phoenix)) {
    const int revived_rank = *((int *) event.getData());
    external_ledger->registerPhoenix(revived_rank);
    return false;
  } else {
    #if DO_METRICS
    unsigned long int timer_key = metrics::METRICS->timerStart("resilienceAddEvent");
    #endif
    DependencyKey depkey = DependencyKey(event_id, source_id);
    external_ledger->addEvent(depkey, event);

    #if DO_METRICS
    metrics::METRICS->timerStop("resilienceAddEvent", timer_key);
    #endif
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

ContinuityData resilienceSyntheticFinalise(const std::thread::id thread_id) {
  external_ledger->endMonitoring();
  const std::thread::id main_thread = internal_ledger->getMainThread();
  PendingTaskDescriptor * ptd = internal_ledger->getPendingTaskFromCurrentlyActiveTask(thread_id);
  const std::pair<const task_ptr_t * const, const int> task_data = external_ledger->getTaskArray();
  ContinuityData con_data = ContinuityData(main_thread, task_data.first, task_data.second, ptd);
  delete internal_ledger;
  delete external_ledger;
  return con_data;
}

void resilienceRestoreTaskToActive(const std::thread::id thread_id, PendingTaskDescriptor * ptd) {
  internal_ledger->taskActiveOnThread(thread_id, *ptd);
  return;
}

bool resilienceIsFinished(void) {
  if (started) {
    return external_ledger->isFinished();
  } else {
    return true;
  }
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
  char marker_buf[4];

  this->file_pos = object_begin;
  file.seekg(object_begin);

  file.read(reinterpret_cast<char *>(&(this->state)), sizeof(TaskState));

  this->ptd = new PendingTaskDescriptor(file, file.tellg());

  file.read(marker_buf, marker_size);
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
  file.seekp(object_begin);
  file_pos = object_begin;

  file.write(reinterpret_cast<const char *>(&state), sizeof(TaskState));
  ptd->serialize(file);

  file.write(eoo, marker_size);

  return;
}

/**
* Serialization routine, leaves put pointer at end of object
*/
void LoggedTask::serialize(std::ostream& file) {
  // serialization schema:
  // TaskState state, PendingTaskDescriptor ptd, EOO
  file_pos = file.tellp();

  file.write(reinterpret_cast<const char *>(&state), sizeof(TaskState));
  ptd->serialize(file);

  file.write(eoo, marker_size);

  return;
}

/**
* Simple look-up function for what task is running on a thread
*/
taskID_t EDAT_Thread_Ledger::getCurrentlyActiveTask(const std::thread::id thread_id) {
  std::lock_guard<std::mutex> lock(id_mutex);
  return threadID_to_taskID.at(thread_id).back();
}

PendingTaskDescriptor * EDAT_Thread_Ledger::getPendingTaskFromCurrentlyActiveTask(const std::thread::id thread_id) {
  id_mutex.lock();
  taskID_t task_id = threadID_to_taskID.at(thread_id).back();
  id_mutex.unlock();

  at_mutex.lock();
  PendingTaskDescriptor * ptd = active_tasks.at(task_id)->generatePendingTask();
  at_mutex.unlock();

  ptd->task_id = task_id;

  return ptd;
}

/**
* Called on task completion, hands off events which were fired from the task
* to the messaging system
*/
void EDAT_Thread_Ledger::releaseHeldEvents(const taskID_t task_id) {
  std::lock_guard<std::mutex> lock(at_mutex);
  ActiveTaskDescriptor * atd = active_tasks.at(task_id);
  const int my_rank = messaging.getRank();
  while (!atd->firedEvents.empty()) {
    HeldEvent * held_event = atd->firedEvents.front();
    if (held_event->target == my_rank) {
      held_event->fire(messaging);
      free(held_event->spec_evt->getData());
      delete held_event->spec_evt;
      delete held_event;
    } else {
      external_ledger->holdEvent(held_event);
    }
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
  HeldEvent * held_event;

  while (!atd->firedEvents.empty()) {
    held_event = atd->firedEvents.front();
    free(held_event->spec_evt->getData());
    delete held_event->spec_evt;
    delete held_event;
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
      if (target != edatGetRank()) {
        HeldEvent * held_event = new HeldEvent();
        const int data_size = data_count * messaging.getTypeSize(data_type);
        held_event->spec_evt = new SpecificEvent(messaging.getRank(), data_count, data_size, data_type, persistent, false, event_id, NULL);

        if (data != NULL) {
          // do this so application developer can safely free after 'firing' an event
          char * data_copy = (char *) malloc(data_size);
          memcpy(data_copy, data, data_size);
          held_event->spec_evt->setData(data_copy);
        }

        held_event->target = target;

        external_ledger->eventFiredFromMain(target, std::string(event_id), held_event);
      }
      messaging.fireEvent(data, data_count, data_type, target, persistent, event_id);
  } else {
    // event has been fired from a task and should be held
    HeldEvent * held_event = new HeldEvent();
    const taskID_t task_id = getCurrentlyActiveTask(thread_id);
    const int data_size = data_count * messaging.getTypeSize(data_type);
    held_event->spec_evt = new SpecificEvent(messaging.getRank(), data_count, data_size, data_type, persistent, false, event_id, NULL);

    if (data != NULL) {
      // do this so application developer can safely free after 'firing' an event
      char * data_copy = (char *) malloc(data_size);
      memcpy(data_copy, data, data_size);
      held_event->spec_evt->setData(data_copy);
    }

    held_event->target = target;

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
EDAT_Process_Ledger::EDAT_Process_Ledger(Scheduler& ascheduler, Messaging& amessaging, const int my_rank, const int num_ranks, const task_ptr_t * const thetaskarray, const int num_tasks, const unsigned int a_timeout, std::string ledger_name)
  : scheduler(ascheduler), messaging(amessaging), RANK(my_rank), NUM_RANKS(num_ranks), COMM_TIMEOUT(a_timeout), task_array(thetaskarray), number_of_tasks(num_tasks), fname(ledger_name) {

  dead_ranks = new bool[NUM_RANKS];
  for (int rank=0; rank<NUM_RANKS; rank++) {
    std::vector<HeldEvent*> held_v;
    std::multiset<std::string> str_ms;
    held_events.emplace(rank, held_v);
    sent_event_ids.emplace(rank,str_ms);
    dead_ranks[rank] = false;
  }

  protectMPI = messaging.getProtectMPI();
  serialize();
}

EDAT_Process_Ledger::EDAT_Process_Ledger(Scheduler& ascheduler, Messaging& amessaging, const int my_rank, const int num_ranks, const task_ptr_t * const thetaskarray, const int num_tasks, const unsigned int a_timeout, std::string ledger_name, bool recovery) : scheduler(ascheduler), messaging(amessaging), RANK(my_rank), NUM_RANKS(num_ranks), COMM_TIMEOUT(a_timeout), task_array(thetaskarray), number_of_tasks(num_tasks), fname(ledger_name) {

  dead_ranks = new bool[NUM_RANKS];
  for (int rank=0; rank<NUM_RANKS; rank++) {
    std::vector<HeldEvent*> held_v;
    std::multiset<std::string> str_ms;
    held_events.emplace(rank, held_v);
    sent_event_ids.emplace(rank, str_ms);
    dead_ranks[rank] = false;
  }
  protectMPI = messaging.getProtectMPI();

  if (recovery) {
    std::cout << "Reinitialising rank " << RANK << " ledger" << std::endl;
    char marker_buf[4], ledger_name_buf[24];
    bool found_eol;
    std::map<DependencyKey,std::queue<SpecificEvent*>>::iterator oe_iter;
    std::map<DependencyKey,std::queue<SpecificEvent*>>::iterator ae_iter;
    std::map<taskID_t,LoggedTask*>::iterator tl_iter;
    std::map<taskID_t,std::queue<SpecificEvent*>> orphaned_events;
    std::map<taskID_t,std::queue<SpecificEvent*>>::iterator orph_iter;
    std::string dead_ledger;
    std::fstream file;
    std::streampos bookmark;

    // rename ledger file of dead rank
    sprintf(ledger_name_buf, "edat_ledger_%d_DEAD", RANK);
    dead_ledger = std::string(ledger_name_buf);
    rename(fname.c_str(), ledger_name_buf);

    // open old ledger file for reading
    file.open(dead_ledger, std::ios::in | std::ios::binary | std::ios::ate);
    file.seekg(std::ios::beg);

    file.read(reinterpret_cast<char *>(dead_ranks), NUM_RANKS*sizeof(bool));

    found_eol = false;
    while (!found_eol) {
      bookmark = file.tellg();
      file.read(marker_buf, marker_size);
      if (!strcmp(marker_buf, tsk)) {
        // found a task
        taskID_t task_id;
        file.read(reinterpret_cast<char *>(&task_id), sizeof(taskID_t));
        LoggedTask * lgt = new LoggedTask(file, file.tellg());
        task_log.emplace(task_id, lgt);

        file.read(marker_buf, marker_size);
        if (strcmp(marker_buf, eoo)) raiseError("EDAT_Process_Ledger task deserialization error, EOO not found");
      } else if (!strcmp(marker_buf, evt)) {
        // found an event
        taskID_t task_id;
        file.read(reinterpret_cast<char *>(&task_id), sizeof(taskID_t));

        SpecificEvent * spec_evt = new SpecificEvent(file, file.tellg());
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
              tl_iter->second->ptd->numArrivedEvents++;
            } else {
              ae_iter->second.push(spec_evt);
            }
          }
        }
      } else if (!strcmp(marker_buf, hvt)) {
        // found a held event
        HeldEvent * held_event = new HeldEvent(file, file.tellp());
        if (held_event->target == EDAT_ALL) {
          raiseError("Found a held_event with EDAT_ALL as target...");
        } else {
          if(held_event->state == HELD) held_events.at(held_event->target).emplace_back(held_event);
        }
      } else if (!strcmp(marker_buf, eol)) {
        // found the end of the ledger
        found_eol = true;
      } else {
        // something bad has happened
        printf("ERROR: marker_buf=%s\n", marker_buf);
        raiseError("EDAT_Process_Ledger deserialization error, unable to parse marker");
      }
    } // while (!found_eol)
    file.close();

    // deal with any orphaned events
    for (orph_iter = orphaned_events.begin(); orph_iter != orphaned_events.end(); ++orph_iter) {
      while (!orph_iter->second.empty()) {
        SpecificEvent * spec_evt = orph_iter->second.front();
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
            tl_iter->second->ptd->numArrivedEvents++;
          } else {
            ae_iter->second.push(spec_evt);
          }
        }
      }
    }

    // this rank had to be recovered, so it should be in the dead_ranks set (for now)
    dead_ranks_mutex.lock();
    dead_ranks[RANK] = true;
    dead_ranks_mutex.unlock();
  }

  serialize();
}

/**
* Includes deep delete of the task_log.
*/
EDAT_Process_Ledger::~EDAT_Process_Ledger() {
  std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator oe_iter;
  std::map<taskID_t,LoggedTask*>::iterator tl_iter;

  delete[] dead_ranks;

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
  #if DO_METRICS
  unsigned long int timer_key = metrics::METRICS->timerStart("commit_addTask");
  #endif

  std::fstream file;

  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::binary | std::ios::out | std::ios::in);
  file.seekp(-marker_size, std::ios::end);

  file.write(tsk, marker_size);
  file.write(reinterpret_cast<const char *>(&task_id), sizeof(taskID_t));
  lgt.serialize(file, file.tellp());
  file.write(eoo, marker_size);
  file.write(eol, marker_size);

  file.close();

  #if DO_METRICS
  metrics::METRICS->timerStop("commit_addTask", timer_key);
  #endif

  return;
}

void EDAT_Process_Ledger::commit(SpecificEvent& spec_evt) {
  #if DO_METRICS
  unsigned long int timer_key = metrics::METRICS->timerStart("commit_addEvent");
  #endif
  std::fstream file;
  const taskID_t no_task_id = 0;

  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::binary | std::ios::out | std::ios::in);
  file.seekp(-marker_size, std::ios::end);

  file.write(evt, marker_size);
  spec_evt.setFilePos(file.tellp());
  file.write(reinterpret_cast<const char *>(&no_task_id), sizeof(taskID_t));
  spec_evt.serialize(file, file.tellp());
  file.write(eoo, marker_size);
  file.write(eol, marker_size);

  file.close();

  #if DO_METRICS
  metrics::METRICS->timerStop("commit_addEvent", timer_key);
  #endif
  return;
}

void EDAT_Process_Ledger::commit(HeldEvent& held_event) {
  #if DO_METRICS
  unsigned long int timer_key = metrics::METRICS->timerStart("commit_holdEvent");
  #endif
  std::fstream file;

  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::binary | std::ios::out | std::ios::in);
  file.seekp(-marker_size, std::ios::end);

  file.write(hvt, marker_size);
  held_event.serialize(file, file.tellp());
  file.write(eol, marker_size);

  file.close();

  #if DO_METRICS
  metrics::METRICS->timerStop("commit_holdEvent", timer_key);
  #endif
  return;
}

void EDAT_Process_Ledger::commit(const int rank, const bool rank_is_dead) {
  #if DO_METRICS
  unsigned long int timer_key = metrics::METRICS->timerStart("commit_deadRank");
  #endif
  std::fstream file;
  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::binary | std::ios::out | std::ios::in);

  file.seekp(rank*sizeof(bool), std::ios::beg);
  file.write(reinterpret_cast<const char *>(&rank_is_dead), sizeof(bool));

  file.close();

  #if DO_METRICS
  metrics::METRICS->timerStop("commit_deadRank", timer_key);
  #endif
  return;
}

void EDAT_Process_Ledger::commit(const taskID_t task_id, const SpecificEvent& spec_evt) {
  #if DO_METRICS
  unsigned long int timer_key = metrics::METRICS->timerStart("commit_mvEvtToTsk");
  #endif
  std::fstream file;

  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::binary | std::ios::out | std::ios::in);

  file.seekp(spec_evt.getFilePos());

  file.write(reinterpret_cast<const char *>(&task_id), sizeof(taskID_t));

  file.close();

  #if DO_METRICS
  metrics::METRICS->timerStop("commit_mvEvtToTsk", timer_key);
  #endif
  return;
}

void EDAT_Process_Ledger::commit(const TaskState& state, const std::streampos file_pos) {
  #if DO_METRICS
  unsigned long int timer_key = metrics::METRICS->timerStart("commit_taskState");
  #endif
  std::fstream file;
  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::binary | std::ios::out | std::ios::in);
  file.seekp(file_pos);
  file.write(reinterpret_cast<const char *>(&state), sizeof(TaskState));
  file.close();
  #if DO_METRICS
  metrics::METRICS->timerStop("commit_taskState", timer_key);
  #endif
  return;
}

void EDAT_Process_Ledger::commit(const HeldEventState& state, const std::streampos file_pos) {
  #if DO_METRICS
  unsigned long int timer_key = metrics::METRICS->timerStart("commit_hvtState");
  #endif
  std::fstream file;
  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::binary | std::ios::out | std::ios::in);
  file.seekp(file_pos);
  file.write(reinterpret_cast<const char *>(&state), sizeof(HeldEventState));
  file.close();
  #if DO_METRICS
  metrics::METRICS->timerStop("commit_hvtState", timer_key);
  #endif
  return;
}

int EDAT_Process_Ledger::getFuncID(const task_ptr_t task_fn) {
  for (int func_id = 0; func_id<number_of_tasks; ++func_id) {
    if (task_array[func_id] == task_fn) return func_id;
  }
  raiseError("Could not find task function");
  // shouldn't ever reach this return...
  return 0;
}

void EDAT_Process_Ledger::fireHeldEvents(const int target) {
  std::vector<HeldEvent*>::iterator he_iter;

  std::lock_guard<std::mutex> he_lock(held_events_mutex);

  for (he_iter = held_events.at(target).begin(); he_iter != held_events.at(target).end(); ++he_iter) {
    HeldEvent * held_event = *he_iter;
    sent_event_ids.at(target).emplace(held_event->spec_evt->getEventId());
    held_event->fire(messaging);
  }

  return;
}

void EDAT_Process_Ledger::serialize() {
  // serialization schema:
  // dead_ranks as int[NUM_RANKS]
  // TSK : taskID_t, LoggedTask : EOO
  // EVT : taskID_t, SpecificEvent : EOO
  // EOL
  const taskID_t no_task_id = 0;
  std::fstream file;
  std::map<DependencyKey,std::queue<SpecificEvent*>>::iterator oe_iter;
  std::map<taskID_t,LoggedTask*>::iterator tl_iter;
  std::map<int,std::vector<HeldEvent*>>::iterator he_iter;
  std::queue<SpecificEvent*> spec_q;
  SpecificEvent * spec_event;

  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::out | std::ios::binary | std::ios::trunc);

  file.write(reinterpret_cast<const char *>(dead_ranks), NUM_RANKS*sizeof(bool));

  for (tl_iter = task_log.begin(); tl_iter != task_log.end(); ++tl_iter) {
    file.write(tsk, marker_size);
    file.write(reinterpret_cast<const char *>(&(tl_iter->first)), sizeof(taskID_t));
    tl_iter->second->serialize(file);
    file.write(eoo, marker_size);
  }

  for (oe_iter = outstanding_events.begin(); oe_iter != outstanding_events.end(); ++oe_iter) {
    while(!oe_iter->second.empty()) {
      spec_event = oe_iter->second.front();
      spec_event->setFilePos(file.tellp());
      file.write(evt, marker_size);
      file.write(reinterpret_cast<const char *>(&no_task_id), sizeof(taskID_t));
      oe_iter->second.pop();
      spec_q.push(spec_event);
      spec_event->serialize(file);
      file.write(eoo, marker_size);
    }

    while(!spec_q.empty()) {
      spec_event = spec_q.front();
      spec_q.pop();
      oe_iter->second.push(spec_event);
    }
  }

  for (he_iter = held_events.begin(); he_iter != held_events.end(); ++he_iter) {
    for (std::vector<HeldEvent*>::iterator iter = he_iter->second.begin(); iter != he_iter->second.end(); ++iter) {
      HeldEvent * held_event = *iter;
      file.write(hvt, marker_size);
      held_event->serialize(file);
      file.write(eoo, marker_size);
    }
  }

  file.write(eol, marker_size);

  file.close();

  return;
}

void EDAT_Process_Ledger::monitorProcesses() {
  const unsigned int MAX_FAIL = (1000 * COMM_TIMEOUT) / REST_PERIOD;
  int rank, test_result=0;
  unsigned int * fail_count = new unsigned int[NUM_RANKS];
  bool * unconfirmed_events = new bool[NUM_RANKS];
  std::string event_id;
  for (rank=0; rank<NUM_RANKS; rank++) fail_count[rank]=0;

  monitor_mutex.lock();
  monitor = true;
  while (monitor) {
    monitor_mutex.unlock();

    // look for unconfirmed events and start receive if necessary
    held_events_mutex.lock();
    for (rank=0; rank<NUM_RANKS; rank++) {
      if (rank != RANK && !sent_event_ids.at(rank).empty()) {
        unconfirmed_events[rank] = true;
        if (!fail_count[rank]) {
          if (protectMPI) messaging.lockMPI();
          MPI_Start(&recv_requests[rank]);
          if (protectMPI) messaging.unlockMPI();
        }
      } else {
        unconfirmed_events[rank] = false;
      }
    }
    held_events_mutex.unlock();

    for (rank=0; rank<NUM_RANKS; rank++) {
      if (unconfirmed_events[rank]) break;
      if (rank == NUM_RANKS-1) finished = true;
    }

    // have a nap
    std::this_thread::sleep_for(std::chrono::milliseconds(REST_PERIOD));

    // test for receipt of event confirmations
    for (rank=0; rank<NUM_RANKS; rank++) {
      if (unconfirmed_events[rank]) {
        if (protectMPI) messaging.lockMPI();
        MPI_Test(&recv_requests[rank], &test_result, MPI_STATUS_IGNORE);
        if (protectMPI) messaging.unlockMPI();
        if (test_result) {
          // event confirmation has been received
          fail_count[rank]=0;
          event_id = std::string(&recv_conf_buffer[rank*max_event_id_size]);
          confirmEventReceivedAtTarget(rank, event_id);
        } else {
          fail_count[rank]++;
          if (fail_count[rank] > MAX_FAIL) {
            // event confirmation has not been received
            dead_ranks_mutex.lock();
            if (!dead_ranks[rank]) {
              dead_ranks_mutex.unlock();
              // this is news...
              std::cout << "[" << RANK << "] RIP rank " << rank << std::endl;
              registerObit(rank);
              for (int target=0; target<NUM_RANKS; target++) {
                if (target != RANK && target != rank) {
                  messaging.fireEvent(&rank, 1, EDAT_INT, target, false, obit);
                }
              }
            } else {
              // news came in while I was busy
              dead_ranks_mutex.unlock();
            }
            // cancel the active receive and purge the sent_event_ids
            if (protectMPI) messaging.lockMPI();
            MPI_Cancel(&recv_requests[rank]);
            if (protectMPI) messaging.unlockMPI();
            held_events_mutex.lock();
            sent_event_ids.at(rank).clear();
            held_events_mutex.unlock();
          }
        }
      }
    }
  } // while (monitor)

  if (protectMPI) messaging.lockMPI();
  MPI_Waitall(NUM_RANKS, recv_requests, MPI_STATUSES_IGNORE);
  if (protectMPI) messaging.unlockMPI();

  delete[] unconfirmed_events;
  delete[] fail_count;

  return;
}

void EDAT_Process_Ledger::confirmEventReceivedAtTarget(const int rank, const std::string recv_evt_id) {
  std::multiset<std::string>::iterator seid_iter;
  std::vector<HeldEvent*>::reverse_iterator he_iter;
  bool found_match = false;

  std::lock_guard<std::mutex> lock(held_events_mutex);

  seid_iter = sent_event_ids.at(rank).find(recv_evt_id);
  if (seid_iter == sent_event_ids.at(rank).end()) {
    printf("[%d] Unmatched event_id = %s from %d\n", RANK, recv_evt_id.c_str(), rank);
    raiseError("Received event id does not match any event sent from this rank\n");
  }  else {
    sent_event_ids.at(rank).erase(seid_iter);
  }

  for (he_iter = held_events.at(rank).rbegin(); he_iter != held_events.at(rank).rend(); ++he_iter) {
    HeldEvent * held_event = *he_iter;
    if (held_event->matchEventId(recv_evt_id)) {
      found_match = true;
      held_event->state = CONFIRMED;
      commit(CONFIRMED, held_event->file_pos);
      free(held_event->spec_evt->getData());
      delete held_event->spec_evt;
      delete held_event;
      break;
    }
  }

  if (found_match) {
    held_events.at(rank).erase(--(he_iter.base()));
  } else {
    printf("[%d] Unmatched event_id = %s from %d\n", RANK, recv_evt_id.c_str(), rank);
    raiseError("Received event id does not match any held event in ths rank");
  }

  return;
}

/**
* Restores the state of the EDAT rank from the ledger
*/
void EDAT_Process_Ledger::recover() {
  std::map<taskID_t,LoggedTask*>::iterator tl_iter;
  std::map<DependencyKey,std::queue<SpecificEvent*>>::iterator oe_iter;
  taskID_t highest_previous_task_id = task_log.size();
  LoggedTask * logged_task;
  PendingTaskDescriptor * exec_task, * sched_task;
  std::queue<SpecificEvent*> temp_q;
  SpecificEvent * spec_event;
  std::queue<std::pair<LoggedTask*,PendingTaskDescriptor*>> exec_queue;

  std::lock_guard<std::mutex> lock(log_mutex);

  // make sure that any new task IDs are unique
  for (tl_iter = task_log.begin(); tl_iter != task_log.end(); ++tl_iter) {
    if (tl_iter->first > highest_previous_task_id) highest_previous_task_id = tl_iter->first;
  }
  TaskDescriptor::resetTaskID(highest_previous_task_id);

  for (tl_iter = task_log.begin(); tl_iter != task_log.end(); ++tl_iter) {
    logged_task = tl_iter->second;
    if (logged_task->state == RUNNING) {
      // task is ready to go! It will get held up by the locked log_mutex until this function returns
      // depending on the number of tasks which were still running vs number of threads it may end up sitting in a queue
      exec_task = new PendingTaskDescriptor();
      exec_task->deepCopy(*(logged_task->ptd));
      exec_task->generateTaskID();
      exec_task->task_fn = getFunc(exec_task->func_id);

      // mark old task as failed
      logged_task->state = FAILED;
      commit(FAILED, logged_task->file_pos);

      // add 'new' task to queue to be run soon
      LoggedTask * lgt = new LoggedTask(*exec_task);
      exec_queue.push(std::pair<LoggedTask*,PendingTaskDescriptor*>(lgt, exec_task));
    } else if (logged_task->state == SCHEDULED) {
      // task still missing events, add it to Scheduler::registeredTasks
      sched_task = new PendingTaskDescriptor();
      sched_task->deepCopy(*(logged_task->ptd));
      sched_task->task_fn = getFunc(sched_task->func_id);
      scheduler.registerTask(sched_task);
    }
  }

  while (!exec_queue.empty()) {
    LoggedTask * lgt = exec_queue.front().first;
    PendingTaskDescriptor * exec_task = exec_queue.front().second;
    task_log.emplace(exec_task->task_id, lgt);
    commit(exec_task->task_id, *lgt);
    scheduler.readyToRunTask(exec_task);
    exec_queue.pop();
  }

  for (oe_iter = outstanding_events.begin(); oe_iter != outstanding_events.end(); ++oe_iter) {
    std::queue<SpecificEvent*> event_q;
    while (!oe_iter->second.empty()) {
      spec_event = oe_iter->second.front();
      oe_iter->second.pop();
      temp_q.push(spec_event);
      event_q.push(new SpecificEvent(*spec_event));
    }

    scheduler.registerEvent(std::pair<DependencyKey,std::queue<SpecificEvent*>>(oe_iter->first,event_q));

    while (!temp_q.empty()) {
      spec_event = temp_q.front();
      temp_q.pop();
      oe_iter->second.push(spec_event);
    }
  }

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

  int source = event.getSourcePid();
  std::string event_id = event.getEventId();

  if (source != RANK) {
    if(protectMPI) messaging.lockMPI();
    MPI_Wait(&send_requests[source], MPI_STATUS_IGNORE);
    memcpy(&send_conf_buffer[source*max_event_id_size], event_id.c_str(), event_id.size()+1);
    MPI_Start(&send_requests[source]);
    if (protectMPI) messaging.unlockMPI();
  }

  log_mutex.lock();
  std::map<DependencyKey, std::queue<SpecificEvent*>>::iterator oe_iter = outstanding_events.find(depkey);
  SpecificEvent * event_copy = new SpecificEvent(event, false);

  if (oe_iter == outstanding_events.end()) {
    std::queue<SpecificEvent*> eventQueue;
    eventQueue.push(event_copy);
    outstanding_events.insert(std::pair<DependencyKey, std::queue<SpecificEvent*>>(depkey, eventQueue));
  } else {
    oe_iter->second.push(event_copy);
  }

  log_mutex.unlock();
  commit(*event_copy);
  return;
}

/**
* Updates a task embedded in the log with an arrived event.
*/
void EDAT_Process_Ledger::moveEventToTask(const DependencyKey depkey, const taskID_t task_id) {
  log_mutex.lock();

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

  log_mutex.unlock();
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
  if (lgt->state != FAILED) {
    const std::streampos bookmark = lgt->file_pos;
    lgt->state = COMPLETE;
    log_mutex.unlock();
    commit(COMPLETE, bookmark);
  } else {
    log_mutex.unlock();
  }
  return;
}

void EDAT_Process_Ledger::markTaskFailed(const taskID_t task_id) {
  log_mutex.lock();
  LoggedTask * const lgt = task_log.at(task_id);
  if (lgt->state != COMPLETE) {
    const std::streampos bookmark = lgt->file_pos;
    lgt->state = FAILED;
    log_mutex.unlock();
    commit(FAILED, bookmark);
  } else {
    log_mutex.unlock();
  }
  return;
}

void EDAT_Process_Ledger::beginMonitoring() {
  finished=false;
  recv_conf_buffer = new char[NUM_RANKS*max_event_id_size];
  send_conf_buffer = new char[NUM_RANKS*max_event_id_size];
  recv_requests = (MPI_Request*) malloc(NUM_RANKS*sizeof(MPI_Request));
  send_requests = (MPI_Request*) malloc(NUM_RANKS*sizeof(MPI_Request));
  if (protectMPI) messaging.lockMPI();
  for (int rank = 0; rank<NUM_RANKS; rank++) {
    MPI_Recv_init(&recv_conf_buffer[rank*max_event_id_size], max_event_id_size, MPI_CHAR, rank, RESILIENCE_MPI_TAG, MPI_COMM_WORLD, &recv_requests[rank]);
    MPI_Send_init(&send_conf_buffer[rank*max_event_id_size], max_event_id_size, MPI_CHAR, rank, RESILIENCE_MPI_TAG, MPI_COMM_WORLD, &send_requests[rank]);
  }
  if (protectMPI) messaging.unlockMPI();

  monitor_thread = std::thread(&EDAT_Process_Ledger::monitorProcesses, this);

  return;
}

void EDAT_Process_Ledger::registerObit(const int rank) {
  dead_ranks_mutex.lock();
  dead_ranks[rank] = true;
  dead_ranks_mutex.unlock();

  commit(rank, 1);
  return;
}

void EDAT_Process_Ledger::registerPhoenix(const int rank) {
  std::lock_guard<std::mutex> lock(dead_ranks_mutex);
  dead_ranks[rank] = false;
  commit(rank, 0);
  fireHeldEvents(rank);
  return;
}

void EDAT_Process_Ledger::endMonitoring() {
  monitor_mutex.lock();
  monitor = false;
  monitor_mutex.unlock();
  monitor_thread.join();
  finished = true;

  if (protectMPI) messaging.lockMPI();
  for (int rank=0; rank<NUM_RANKS; rank++) {
    MPI_Request_free(&recv_requests[rank]);
    MPI_Request_free(&send_requests[rank]);
  }
  if (protectMPI) messaging.unlockMPI();
  free(recv_requests);
  free(send_requests);
  delete[] recv_conf_buffer;
  delete[] send_conf_buffer;

  return;
}

void EDAT_Process_Ledger::deleteLedgerFile() {
  std::remove(fname.c_str());
  return;
}

void EDAT_Process_Ledger::holdEvent(HeldEvent* held_event) {
  std::lock_guard<std::mutex> lock(held_events_mutex);
  if (held_event->target == EDAT_ALL) {
    for (int rank = 0; rank < NUM_RANKS; rank++) {
      if (rank != RANK) {
        HeldEvent * he_copy = new HeldEvent(*held_event, rank);
        held_events[rank].emplace_back(he_copy);
        commit(*he_copy);
        dead_ranks_mutex.lock();
        if (!dead_ranks[rank]) {
          dead_ranks_mutex.unlock();
          sent_event_ids[rank].emplace(he_copy->spec_evt->getEventId());
          he_copy->fire(messaging);
        } else dead_ranks_mutex.unlock();
      } else {
        HeldEvent * he_copy = new HeldEvent(*held_event, RANK);
        he_copy->fire(messaging);
        free(he_copy->spec_evt->getData());
        delete he_copy->spec_evt;
        delete he_copy;
      }
    }
    free(held_event->spec_evt->getData());
    delete held_event->spec_evt;
    delete held_event;
  } else {
    held_events[held_event->target].emplace_back(held_event);
    commit(*held_event);
    dead_ranks_mutex.lock();
    if (!dead_ranks[held_event->target]) {
      dead_ranks_mutex.unlock();
      sent_event_ids[held_event->target].emplace(held_event->spec_evt->getEventId());
      held_event->fire(messaging);
    } else dead_ranks_mutex.unlock();
  }
  return;
}

bool EDAT_Process_Ledger::isFinished() const {
  return finished;
}

void EDAT_Process_Ledger::eventFiredFromMain(const int target, const std::string event_id, HeldEvent* held_event) {
  std::lock_guard<std::mutex> lock(held_events_mutex);

  held_events[target].emplace_back(held_event);
  sent_event_ids[target].emplace(event_id);
  commit(*held_event);

  return;
}
