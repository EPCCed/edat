#ifndef SRC_MESSAGING_H_
#define SRC_MESSAGING_H_

#include "scheduler.h"
#include <vector>
#include <thread>

class Messaging {
  std::vector<SpecificEvent*> outstandingEvents;
  std::thread * pollingThread;
protected:
  Scheduler & scheduler;
  bool continue_polling;
  virtual bool fireASingleLocalEvent();
  Messaging(Scheduler & a_scheduler) : scheduler(a_scheduler), continue_polling(true) { }
public:
  virtual void runPollForEvents() = 0;
  virtual void pollForEvents();
  virtual void finalise();
  virtual void ceasePollingForEvents() { continue_polling = false; }
  virtual void fireEvent(void *, int, int, int, const char *) = 0;
  virtual int getRank()=0;
  virtual int getNumRanks()=0;
};

#endif
