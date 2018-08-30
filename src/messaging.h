#ifndef SRC_MESSAGING_H_
#define SRC_MESSAGING_H_

#include "scheduler.h"
#include "threadpool.h"
#include "contextmanager.h"
#include "configuration.h"
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
  ContextManager & contextManager;
  Configuration & configuration;
  bool continue_polling;
  bool progress_thread;
  int it_count;
  virtual bool fireASingleLocalEvent();
  virtual bool checkForLocalTermination();
  Messaging(Scheduler&, ThreadPool&, ContextManager&, Configuration&);
  virtual bool performSinglePoll(int*) = 0;
  virtual int getTypeSize(int);
  virtual void startProgressThread();
public:
  virtual void lockMutexForFinalisationTest() = 0;
  virtual void unlockMutexForFinalisationTest() = 0;
  virtual void resetPolling();
  virtual void runPollForEvents() = 0;
  virtual void setEligableForTermination() = 0;
  virtual bool pollForEvents();
  virtual void finalise();
  virtual void fireEvent(void *, int, int, int, bool, const char *) = 0;
  virtual int getRank()=0;
  virtual int getNumRanks()=0;
  virtual bool isFinished()=0;
  virtual void lockComms()=0;
  virtual void unlockComms()=0;
  virtual void attachMainThread(std::condition_variable*, std::mutex*, bool*);
  virtual bool doesProgressThreadExist() { return progress_thread; }
};

#endif
