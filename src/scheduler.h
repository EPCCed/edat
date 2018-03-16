#ifndef SRC_SCHEDULER_H_
#define SRC_SCHEDULER_H_

#include "edat.h"
#include "threadpool.h"
#include <map>
#include <string>
#include <mutex>
#include <queue>
#include <utility>
#include <set>

class SpecificEvent {
  int source_pid, message_length, message_type;
  char* data;
  std::string event_id;

 public:
  SpecificEvent(int sourcePid, int message_length, int message_type, std::string event_id, char* data) {
    this->source_pid = sourcePid;
    this->message_type = message_type;
    this->event_id = event_id;
    this->message_length = message_length;
    this->data = data;
  }

  char* getData() const { return data; }
  void setData(char* data) { this->data = data; }
  int getSourcePid() const { return source_pid; }
  void setSourcePid(int sourcePid) { source_pid = sourcePid; }
  std::string getEventId() { return this->event_id; }
  int getMessageLength() { return this->message_length; }
  int getMessageType() { return this->message_type; }
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
  std::set<DependencyKey> outstandingDependencies, originalDependencies;
  std::vector<SpecificEvent*> arrivedEvents;
  bool freeData, persistent;
  void (*task_fn)(EDAT_Event*, int);
};

class Scheduler {
    std::vector<PendingTaskDescriptor*> registeredTasks;
    std::map<DependencyKey, SpecificEvent*> outstandingEvents;
    static std::queue<PendingTaskDescriptor*> taskQueue;

    ThreadPool & threadPool;
    std::mutex taskAndEvent_mutex;
    static std::mutex taskQueue_mutex;
    static void threadBootstrapperFunction(void*);
    std::pair<PendingTaskDescriptor*, int> findTaskMatchingEventAndUpdate(SpecificEvent*);
public:
    Scheduler(ThreadPool & tp) : threadPool(tp) { }
    void registerTask(void (*)(EDAT_Event*, int), std::vector<std::pair<int, std::string>>, bool);
    void registerEvent(SpecificEvent*);
    bool isFinished();
    void readyToRunTask(PendingTaskDescriptor*);
};

#endif
