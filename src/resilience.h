#ifndef SRC_RESILIENCE_H
#define SRC_RESILIENCE_H

#include "messaging.h"
#include "scheduler.h"
#include "threadpool.h"
#include "mpi.h"
#include <thread>
#include <mutex>
#include <map>
#include <set>
#include <queue>
#include <fstream>

#define RESILIENCE_MASTER 0
#define DEFAULT_COMM_TIMEOUT 5
#define RESILIENCE_MPI_TAG 12404

typedef void (*task_ptr_t) (EDAT_Event*, int);

struct ContinuityData {
  const std::thread::id main_thread_id;
  const task_ptr_t * const task_array;
  const int num_tasks;
  PendingTaskDescriptor * ptd;
  ContinuityData(const std::thread::id a_thread, const task_ptr_t * const a_task_array, const int a_num_tasks, PendingTaskDescriptor* task) : main_thread_id(a_thread), task_array(a_task_array), num_tasks(a_num_tasks), ptd(task) {};
};

void resilienceInit(Scheduler&, ThreadPool&, Messaging&, const std::thread::id, const task_ptr_t * const, const int, const unsigned int);
void resilienceTaskScheduled(PendingTaskDescriptor&);
bool resilienceAddEvent(SpecificEvent&);
void resilienceMoveEventToTask(const DependencyKey, const taskID_t);
void resilienceEventFired(void*, int, int, int, bool, const char *);
void resilienceTaskRunning(const std::thread::id, PendingTaskDescriptor&);
void resilienceTaskCompleted(const std::thread::id, const taskID_t);
void resilienceThreadFailed(const std::thread::id);
void resilienceFinalise(void);
ContinuityData resilienceSyntheticFinalise(const std::thread::id);
void resilienceRestoreTaskToActive(const std::thread::id thread_id, PendingTaskDescriptor*);
bool resilienceIsFinished(void);

enum TaskState { SCHEDULED, RUNNING, COMPLETE, FAILED };

struct LoggedTask {
  std::streampos file_pos;
  TaskState state = SCHEDULED;
  PendingTaskDescriptor * ptd;
  LoggedTask() = default;
  LoggedTask(PendingTaskDescriptor&);
  LoggedTask(std::istream&, const std::streampos);
  ~LoggedTask();
  void serialize(std::ostream&, const std::streampos);
};

class EDAT_Thread_Ledger {
private:
  Scheduler& scheduler;
  ThreadPool& threadpool;
  Messaging& messaging;
  const std::thread::id main_thread_id;
  std::mutex at_mutex, id_mutex, failure_mutex;
  std::set<taskID_t> completed_tasks;
  std::set<taskID_t> failed_tasks;
  std::map<taskID_t,ActiveTaskDescriptor*> active_tasks;
  std::map<std::thread::id,std::queue<taskID_t>> threadID_to_taskID;
  void releaseHeldEvents(const taskID_t);
  void purgeHeldEvents(const taskID_t);
public:
  EDAT_Thread_Ledger(Scheduler& ascheduler, ThreadPool& athreadpool, Messaging& amessaging, const std::thread::id thread_id)
    : scheduler(ascheduler), threadpool(athreadpool), messaging(amessaging), main_thread_id(thread_id) {};
  taskID_t getCurrentlyActiveTask(const std::thread::id);
  PendingTaskDescriptor* getPendingTaskFromCurrentlyActiveTask(const std::thread::id);
  void holdFiredEvent(const std::thread::id, void*, int, int, int, bool, const char*);
  void taskActiveOnThread(const std::thread::id, PendingTaskDescriptor&);
  void taskComplete(const std::thread::id, const taskID_t);
  void threadFailure(const std::thread::id, const taskID_t);
  const std::thread::id getMainThread() const { return main_thread_id; };
};

class EDAT_Process_Ledger {
private:
  Scheduler& scheduler;
  Messaging& messaging;
  const int RANK;
  const int NUM_RANKS;
  const unsigned int COMM_TIMEOUT;
  const unsigned int REST_PERIOD = 10;
  const task_ptr_t * const task_array;
  const int number_of_tasks;
  const int max_event_id_size = 400;
  char * recv_conf_buffer;
  char * send_conf_buffer;
  MPI_Request * recv_requests;
  MPI_Request * send_requests;
  bool monitor, finished=true, protectMPI;
  std::thread monitor_thread;
  std::string fname;
  std::mutex log_mutex, file_mutex, monitor_mutex, dead_ranks_mutex, held_events_mutex;
  std::map<DependencyKey,std::queue<SpecificEvent*>> outstanding_events;
  std::map<taskID_t,LoggedTask*> task_log;
  std::set<int> dead_ranks;
  std::map<int,std::vector<HeldEvent*>> held_events;
  std::map<int,std::multiset<std::string>> sent_event_ids;
  void commit(const taskID_t, LoggedTask&);
  void commit(SpecificEvent&);
  void commit(HeldEvent&);
  void commit(const int, const int);
  void commit(const taskID_t, const SpecificEvent&);
  void commit(const TaskState&, const std::streampos);
  void commit(const HeldEventState&, const std::streampos);
  void serialize();
  int getFuncID(const task_ptr_t);
  const task_ptr_t getFunc(const int func_id) { return task_array[func_id]; }
  void monitorProcesses();
  void confirmEventReceivedAtTarget(const int, const std::string);
  void fireHeldEvents(const int);
public:
  EDAT_Process_Ledger(Scheduler&, Messaging&, const int, const int, const task_ptr_t * const, const int, const unsigned int, std::string);
  EDAT_Process_Ledger(Scheduler&, Messaging&, const int, const int, const task_ptr_t * const, const int, const unsigned int, std::string, bool);
  ~EDAT_Process_Ledger();
  void recover();
  void addEvent(const DependencyKey, const SpecificEvent&);
  void addTask(const taskID_t, PendingTaskDescriptor&);
  void moveEventToTask(const DependencyKey, const taskID_t);
  void markTaskRunning(const taskID_t);
  void markTaskComplete(const taskID_t);
  void markTaskFailed(const taskID_t);
  void beginMonitoring();
  void registerObit(const int);
  void registerPhoenix(const int);
  void endMonitoring();
  void deleteLedgerFile();
  const std::pair<const task_ptr_t * const, const int> getTaskArray() const { return std::pair<const task_ptr_t * const, const int>(task_array, number_of_tasks); };
  void holdEvent(HeldEvent*);
  bool isFinished() const;
  void eventFiredFromMain(const int, const std::string, HeldEvent*);
};

#endif
