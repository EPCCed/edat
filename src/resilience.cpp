#include "resilience.h"
#include "messaging.h"
#include "scheduler.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
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
void resilienceAddEvent(SpecificEvent& event) {
  DependencyKey depkey = DependencyKey(event.getEventId(), event.getSourcePid());
  external_ledger->addEvent(depkey, event);
  return;
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
  char eoo[4] = {'E', 'O', 'O', '\0'};

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
EDAT_Process_Ledger::EDAT_Process_Ledger(Scheduler& ascheduler, const int my_rank)
  : scheduler(ascheduler), RANK(my_rank) {
    std::stringstream filename;
    filename << "edat_ledger_" << my_rank;
    this->fname = filename.str();
    serialize();
}

EDAT_Process_Ledger::EDAT_Process_Ledger(Scheduler& ascheduler, const int my_rank, const int dead_rank) : scheduler(ascheduler), RANK(my_rank) {
  const char eom[4] = {'E', 'O', 'M', '\0'};
  const char eoo[4] = {'E', 'O', 'O', '\0'};
  char marker_buf[4];
  char * memblock;
  bool found_eom;
  std::stringstream filename, loadname;
  std::string dead_ledger, old_fname;
  std::fstream file;
  std::streampos bookmark;
  taskID_t task_id;
  std::map<DependencyKey,std::queue<SpecificEvent*>>::iterator oe_iter;

  // set fname using recovery rank ID
  filename << "edat_ledger_" << my_rank;
  this->fname = filename.str();

  // rename ledger file of dead rank
  loadname << "edat_ledger_" << dead_rank;
  old_fname = loadname.str();

  loadname << "_DEAD";
  dead_ledger = loadname.str();
  rename(old_fname.c_str(), dead_ledger.c_str());

  // open old ledger file for reading
  file.open(dead_ledger, std::ios::in | std::ios::binary | std::ios::ate);
  file.seekg(std::ios::beg);

  // check for task_log
  found_eom = false;
  while(!found_eom) {
    bookmark = file.tellg();
    file.read(marker_buf, sizeof(marker_buf));
    if(strcmp(marker_buf, eom)) {
      file.seekg(bookmark);
      memblock = new char[sizeof(taskID_t)];
      file.read(memblock, sizeof(taskID_t));
      task_id = *(reinterpret_cast<taskID_t*>(memblock));
      delete[] memblock;

      LoggedTask * lgt = new LoggedTask(file, file.tellg());

      task_log.emplace(task_id, lgt);
    } else {
      found_eom = true;
    }
  }

  // check for outstanding_events
  found_eom = false;
  while(!found_eom) {
    bookmark = file.tellg();
    file.read(marker_buf, sizeof(marker_buf));
    if (strcmp(marker_buf, eom)) {
      DependencyKey depkey = DependencyKey(file, bookmark);
      SpecificEvent * spec_evt = new SpecificEvent(file, file.tellg());

      oe_iter = outstanding_events.find(depkey);
      if (oe_iter == outstanding_events.end()) {
        std::queue<SpecificEvent*> event_queue;
        event_queue.push(spec_evt);
        outstanding_events.emplace(depkey, event_queue);
      } else {
        oe_iter->second.push(spec_evt);
      }

    } else {
      found_eom = true;
    }
  }

  file.read(marker_buf, sizeof(marker_buf));
  if (strcmp(marker_buf, eoo)) raiseError("EDAT_Process_Ledger deserialization error, EOO not found");
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

  std::remove(fname.c_str());
}

void EDAT_Process_Ledger::commit() {
  serialize();
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

void EDAT_Process_Ledger::commit(const DependencyKey& depkey, const SpecificEvent& spec_evt) {
  std::fstream file;
  const char eom[4] = {'E', 'O', 'M', '\0'};
  const char eoo[4] = {'E', 'O', 'O', '\0'};
  char marker_buf[4];
  std::streampos bookmark;

  std::lock_guard<std::mutex> lock(file_mutex);
  file.open(fname, std::ios::binary | std::ios::out | std::ios::in);
  file.seekp(-(2*sizeof(marker_buf)), std::ios::end);
  bookmark = file.tellp();

  // just double check everything at the end of the file is as it should be...
  file.read(marker_buf, sizeof(marker_buf));
  if (strcmp(marker_buf, eom)) raiseError("Error in addEvent commit, EOM not found");
  file.read(marker_buf, sizeof(marker_buf));
  if (strcmp(marker_buf, eoo)) raiseError("Error in addEvent commit, EOO not found");

  // write dependency key, then event
  depkey.serialize(file, bookmark);
  spec_evt.serialize(file, file.tellp());

  // write markers
  file.write(eom, sizeof(eom));
  file.write(eoo, sizeof(eoo));

  file.close();

  return;
}

void EDAT_Process_Ledger::serialize() {
  // serialization schema:
  // map<taskID_t, LoggedTask> : EOM
  // map<DependencyKey,SpecificEvent> : EOM
  // EOO
  std::lock_guard<std::mutex> lock(file_mutex);
  const char eom[4] = {'E', 'O', 'M', '\0'};
  const char eoo[4] = {'E', 'O', 'O', '\0'};
  std::fstream file;
  std::map<DependencyKey,std::queue<SpecificEvent*>>::iterator oe_iter;
  std::map<taskID_t,LoggedTask*>::iterator tl_iter;
  std::queue<SpecificEvent*> temp_queue;
  SpecificEvent * spec_event;

  file.open(fname, std::ios::out | std::ios::binary | std::ios::trunc);

  for (tl_iter = task_log.begin(); tl_iter != task_log.end(); ++tl_iter) {
    file.write(reinterpret_cast<const char *>(&(tl_iter->first)), sizeof(taskID_t));
    tl_iter->second->serialize(file, file.tellp());
  }
  file.write(eom, sizeof(eom));

  for (oe_iter = outstanding_events.begin(); oe_iter != outstanding_events.end(); ++oe_iter) {
    while(!oe_iter->second.empty()) {
      oe_iter->first.serialize(file, file.tellp());
      spec_event = oe_iter->second.front();
      oe_iter->second.pop();
      spec_event->serialize(file, file.tellp());
      temp_queue.push(spec_event);
    }

    while(!temp_queue.empty()) {
      spec_event = temp_queue.front();
      temp_queue.pop();
      oe_iter->second.push(spec_event);
    }
  }
  file.write(eom, sizeof(eom));

  file.write(eoo, sizeof(eoo));

  file.close();
}

/**
* Emplaces the supplied PendingTaskDescriptor in the task_log. Keyed by task_id.
*/
void EDAT_Process_Ledger::addTask(const taskID_t task_id, PendingTaskDescriptor& ptd) {
  std::lock_guard<std::mutex> lock(log_mutex);
  task_log.emplace(task_id, new LoggedTask(ptd));
  commit();
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

  commit(depkey, event);
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
  if (*(od_iter->second) <= 0) ptd->outstandingDependencies.erase(od_iter);

  ptd->numArrivedEvents++;
  if (ae_iter == ptd->arrivedEvents.end()) {
    std::queue<SpecificEvent*> eventQueue;
    eventQueue.push(event);
    ptd->arrivedEvents.insert(std::pair<DependencyKey, std::queue<SpecificEvent*>>(depkey, eventQueue));
  } else {
    ae_iter->second.push(event);
  }

  commit();
  return;
}

void EDAT_Process_Ledger::markTaskRunning(const taskID_t task_id) {
  std::lock_guard<std::mutex> lock(log_mutex);
  LoggedTask * lgt = task_log.at(task_id);
  lgt->state = RUNNING;
  commit(lgt->state, lgt->file_pos);
  return;
}

void EDAT_Process_Ledger::markTaskComplete(const taskID_t task_id) {
  std::lock_guard<std::mutex> lock(log_mutex);
  LoggedTask * lgt = task_log.at(task_id);
  lgt->state = COMPLETE;
  commit(lgt->state, lgt->file_pos);
  return;
}

void EDAT_Process_Ledger::markTaskFailed(const taskID_t task_id) {
  std::lock_guard<std::mutex> lock(log_mutex);
  LoggedTask * lgt = task_log.at(task_id);
  lgt->state = FAILED;
  commit(lgt->state, lgt->file_pos);
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
