#ifndef SRC_MESSAGING_H_
#define SRC_MESSAGING_H_

#include "scheduler.h"
#include "threadpool.h"
#include <vector>
#include <thread>

class Messaging {
  std::vector<SpecificEvent*> outstandingEvents;
  std::thread * pollingThread=NULL;
  std::condition_variable * mainThreadConditionVariable= NULL;
  std::mutex * mainThreadConditionVarMutex, cdtAccessMtx, singleProgressMtx;
  bool * mainThreadConditionVarPred;
  virtual void entryThreadPollForEvents();
  virtual void reactivateMainThread();
protected:
  Scheduler & scheduler;
  ThreadPool & threadPool;
  bool continue_polling;
  bool progress_thread;
  int it_count;
  virtual bool fireASingleLocalEvent();
  virtual bool checkForLocalTermination();
  virtual void startProgressThread();
  Messaging(Scheduler & a_scheduler, ThreadPool & a_threadPool);
  virtual bool performSinglePoll(int*) = 0;
public:
  virtual void runPollForEvents() = 0;
  virtual bool pollForEvents();
  virtual void finalise();
  virtual void ceasePollingForEvents() { continue_polling = false; }
  virtual void fireEvent(void *, int, int, int, const char *) = 0;
  virtual void fireEvent(void *, int, int, int, const char *, void (*)(EDAT_Event*, int)) = 0;
  virtual int getRank()=0;
  virtual int getNumRanks()=0;
  virtual bool isFinished()=0;
  virtual void attachMainThread(std::condition_variable*, std::mutex*, bool*);
  virtual bool doesProgressThreadExist() { return progress_thread; }
};

#endif
