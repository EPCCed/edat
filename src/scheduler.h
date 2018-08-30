#ifndef SRC_SCHEDULER_H_
#define SRC_SCHEDULER_H_

#include "edat.h"
#include "threadpool.h"
#include "configuration.h"
#include "concurrency_ctrl.h"
#include <map>
#include <string>
#include <mutex>
#include <queue>
#include <utility>
#include <set>
#include <stdlib.h>
#include <string.h>

class SpecificEvent {
  int source_pid, message_length, raw_data_length, message_type;
  char* data;
  std::string event_id;
  bool persistent, aContext;

 public:
  SpecificEvent(int sourcePid, int message_length, int raw_data_length, int message_type, bool persistent, bool aContext, std::string event_id, char* data) {
    this->source_pid = sourcePid;
    this->message_type = message_type;
    this->raw_data_length = raw_data_length;
    this->event_id = event_id;
    this->message_length = message_length;
    this->data = data;
    this->persistent=persistent;
    this->aContext=aContext;
  }

  SpecificEvent(const SpecificEvent& source) {
    // Copy constructor needed as we free the data from event to event, hence take a copy of this
    this->source_pid = source.source_pid;
    this->message_type = source.message_type;
    this->event_id =  source.event_id;
    this->message_length = source.message_length;
    this->raw_data_length=source.raw_data_length;
    this->aContext=source.aContext;
    if (source.data != NULL) {
      this->data = (char*) malloc(this->raw_data_length);
      memcpy(this->data, source.data, this->raw_data_length);
    } else {
      this->data = source.data;
    }
    this->persistent= source.persistent;
  }

  char* getData() const { return data; }
  void setData(char* data) { this->data = data; }
  int getSourcePid() const { return source_pid; }
  void setSourcePid(int sourcePid) { source_pid = sourcePid; }
  std::string getEventId() { return this->event_id; }
  int getMessageLength() { return this->message_length; }
  int getMessageType() { return this->message_type; }
  int getRawDataLength() { return this->raw_data_length; }
  bool isPersistent() { return this->persistent; }
  bool isAContext() { return this->aContext; }
};

class DependencyKey {
  std::string s;
  int i;
public:
  DependencyKey(std::string s, int i) {
    this->s = s;
    this->i = i;
  }

  bool operator<(const DependencyKey& k) const {
    int s_cmp = this->s.compare(k.s);
    if(s_cmp == 0) {
      if (this->i == EDAT_ANY || k.i == EDAT_ANY) return false;
      return this->i < k.i;
    }
    return s_cmp < 0;
  }

  bool operator==(const DependencyKey& k) const {
    if (this->s.compare(k.s) == 0) {
      if (this->i == EDAT_ANY || k.i == EDAT_ANY) return true;
      return this->i == k.i;
    }
    return false;
  }

  void display() {
    printf("Key: %s from %d\n", s.c_str(), i);
  }
};

enum TaskDescriptorType { PENDING, PAUSED };

struct TaskDescriptor {
  std::map<DependencyKey, int*> outstandingDependencies;
  std::map<DependencyKey, std::queue<SpecificEvent*>> arrivedEvents;
  std::vector<DependencyKey> taskDependencyOrder;
  int numArrivedEvents;
  bool greedyConsumerOfEvents=false;
  virtual TaskDescriptorType getDescriptorType() = 0;
};

struct PendingTaskDescriptor : TaskDescriptor {
  std::map<DependencyKey, int*> originalDependencies;
  bool freeData, persistent;
  std::string task_name;
  void (*task_fn)(EDAT_Event*, int);
  virtual TaskDescriptorType getDescriptorType() {return PENDING;}
};

struct PausedTaskDescriptor : TaskDescriptor {
  virtual TaskDescriptorType getDescriptorType() {return PAUSED;}
};

// This TaskExecutionContext is provided to the bootstrapper method, that is static (called from the thread)
// and hence we can pop in here more context to use before and after task execution.
struct TaskExecutionContext {
  PendingTaskDescriptor * taskDescriptor;
  ConcurrencyControl * concurrencyControl;
public:
  TaskExecutionContext(PendingTaskDescriptor * td, ConcurrencyControl * cc) : taskDescriptor(td), concurrencyControl(cc) { }
};

class Scheduler {
    int outstandingEventsToHandle; // This tracks the non-persistent events for termination checking
    std::vector<PendingTaskDescriptor*> registeredTasks;
    std::vector<PausedTaskDescriptor*> pausedTasks;
    std::map<DependencyKey, std::queue<SpecificEvent*>> outstandingEvents;
    Configuration & configuration;
    ThreadPool & threadPool;
    ConcurrencyControl & concurrencyControl;
    std::mutex taskAndEvent_mutex;
    static void threadBootstrapperFunction(void*);
    std::pair<TaskDescriptor*, int> findTaskMatchingEventAndUpdate(SpecificEvent*);
    void consumeEventsByPersistentTasks();
    bool checkProgressPersistentTasks();
    std::vector<PendingTaskDescriptor*>::iterator locatePendingTaskFromName(std::string);
    static EDAT_Event * generateEventsPayload(TaskDescriptor*, std::set<int>*);
    static void generateEventPayload(SpecificEvent*, EDAT_Event*);
    void updateMatchingEventInTaskDescriptor(TaskDescriptor*, DependencyKey, std::map<DependencyKey, int*>::iterator, SpecificEvent*);
public:
    Scheduler(ThreadPool & tp, Configuration & aconfig, ConcurrencyControl & cc) : threadPool(tp), configuration(aconfig),
      concurrencyControl(cc) { outstandingEventsToHandle = 0; }
    void registerTask(void (*)(EDAT_Event*, int), std::string, std::vector<std::pair<int, std::string>>, bool, bool);
    EDAT_Event* pauseTask(std::vector<std::pair<int, std::string>>);
    void registerEvent(SpecificEvent*);
    void registerEvents(std::vector<SpecificEvent*>);
    bool isFinished();
    void lockMutexForFinalisationTest();
    void unlockMutexForFinalisationTest();
    void readyToRunTask(PendingTaskDescriptor*);
    bool edatIsTaskSubmitted(std::string);
    bool removeTask(std::string);
    std::pair<int, EDAT_Event*> retrieveAnyMatchingEvents(std::vector<std::pair<int, std::string>>);
};

#endif
