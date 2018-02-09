#include "messaging.h"
#include <vector>
#include <thread>

bool Messaging::fireASingleLocalEvent() {
  if (outstandingEvents.size() > 0) {
    scheduler.registerEvent(outstandingEvents.front());
    outstandingEvents.erase(outstandingEvents.begin());
    return true;
  } else {
    return false;
  }
}

void Messaging::pollForEvents() {
  pollingThread=new std::thread(&Messaging::runPollForEvents, this);
}

void Messaging::finalise() {
  pollingThread->join();
}
