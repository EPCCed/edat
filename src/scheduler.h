#ifndef SRC_SCHEDULER_H_
#define SRC_SCHEDULER_H_

#include "edat.h"
#include "threadpool.h"
#include <map>
#include <string>
#include <mutex>

class SpecificEvent {
  int source_pid, message_length, message_type;
  char* data;
  std::string unique_id;

 public:
  SpecificEvent(int sourcePid, int message_length, int message_type, std::string unique_id, char* data) {
    this->source_pid = sourcePid;
    this->message_type = message_type;
    this->unique_id = unique_id;
    this->message_length = message_length;
    this->data = data;
  }

  char* getData() const { return data; }
  void setData(char* data) { this->data = data; }
  int getSourcePid() const { return source_pid; }
  void setSourcePid(int sourcePid) { source_pid = sourcePid; }
  std::string getUniqueId() { return this->unique_id; }
  int getMessageLength() { return this->message_length; }
  int getMessageType() { return this->message_type; }
};

struct pthread_raw_data_struct {
  SpecificEvent * event_metadata;
  void (*task_fn)(void *, EDAT_Metadata);
};

class Scheduler {
    static std::map<std::string, void (*)(void *, EDAT_Metadata)> scheduledTasks;
    static std::map<std::string, SpecificEvent*> outstandingRequests;
    ThreadPool & threadPool;
    std::mutex scheduledTasks_mutex, outstandingRequests_mutex;
    static void threadBootstrapperFunction(void*);
public:
    Scheduler(ThreadPool & tp) : threadPool(tp) { }
    void registerTask(void (*)(void *, EDAT_Metadata), std::string);
    void registerEvent(SpecificEvent*);
    bool isFinished();
};

#endif
