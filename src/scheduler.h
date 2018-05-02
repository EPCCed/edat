#ifndef SRC_SCHEDULER_H_
#define SRC_SCHEDULER_H_

#include "edat.h"
#include "threadpool.h"
#include "configuration.h"
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
};

struct PendingTaskDescriptor {
  std::map<DependencyKey, int*> outstandingDependencies, originalDependencies;
  std::map<DependencyKey, std::queue<SpecificEvent*>> arrivedEvents;
  std::vector<DependencyKey> taskDependencyOrder;
  bool freeData, persistent;
  int numArrivedEvents;
  std::string task_name;
  void (*task_fn)(EDAT_Event*, int);
};

class Scheduler {
    int outstandingEventsToHandle; // This tracks the non-persistent events for termination checking
    std::vector<PendingTaskDescriptor*> registeredTasks;
    std::map<DependencyKey, std::queue<SpecificEvent*>> outstandingEvents;
    Configuration & configuration;
    ThreadPool & threadPool;
    std::mutex taskAndEvent_mutex;
    static void threadBootstrapperFunction(void*);
    std::pair<PendingTaskDescriptor*, int> findTaskMatchingEventAndUpdate(SpecificEvent*);
    void consumeEventsByPersistentTasks();
    bool checkProgressPersistentTasks();
    std::vector<PendingTaskDescriptor*>::iterator locatePendingTaskFromName(std::string);
public:
    Scheduler(ThreadPool & tp, Configuration & aconfig) : threadPool(tp), configuration(aconfig) { outstandingEventsToHandle = 0; }
    void registerTask(void (*)(EDAT_Event*, int), std::string, std::vector<std::pair<int, std::string>>, bool);
    void registerEvent(SpecificEvent*);
    bool isFinished();
    void readyToRunTask(PendingTaskDescriptor*);
    bool isTaskScheduled(std::string);
    bool descheduleTask(std::string);
};

#endif
