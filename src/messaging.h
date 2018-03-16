#ifndef SRC_MESSAGING_H_
#define SRC_MESSAGING_H_

#include "scheduler.h"
#include <vector>
#include <thread>

class Messaging {
  std::vector<SpecificEvent*> outstandingEvents;
  std::thread * pollingThread;
  std::condition_variable * mainThreadConditionVariable= NULL;
  std::mutex * mainThreadConditionVarMutex, cdtAccessMtx;
  bool * mainThreadConditionVarPred;
protected:
  Scheduler & scheduler;
  ThreadPool & threadPool;
  bool continue_polling;
  virtual bool fireASingleLocalEvent();
  virtual bool checkForTermination();
  virtual void startPollForEvents();
  Messaging(Scheduler & a_scheduler, ThreadPool & a_threadPool) : scheduler(a_scheduler), threadPool(a_threadPool), continue_polling(true) { }
public:
  virtual void runPollForEvents() = 0;
  virtual void pollForEvents();
  virtual void finalise();
  virtual void ceasePollingForEvents() { continue_polling = false; }
  virtual void fireEvent(void *, int, int, int, const char *) = 0;
  virtual void fireEvent(void *, int, int, int, const char *, void (*)(EDAT_Event*, int)) = 0;
  virtual int getRank()=0;
  virtual int getNumRanks()=0;
  virtual bool isFinished()=0;
  virtual void attachMainThread(std::condition_variable*, std::mutex*, bool*);
};

#endif
