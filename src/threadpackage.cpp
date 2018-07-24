#include <thread>
#include <condition_variable>
#include <mutex>
#include "threadpackage.h"
#include "misc.h"

/**
* Attaches a thread if the core id is not -1 then map the thread to the specific core.
* This is useful as we might create multiple threads (for instance when others are paused
* and want the core mapping to be unchanged.)
*/
void ThreadPackage::attachThread(std::thread* tp, int core_id) {
  this->thread=tp;
  if (core_id != -1) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    int rc = pthread_setaffinity_np(thread->native_handle(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) raiseError("Error setting pthread affinity in mapping threads to cores");
  }
}

/**
* Determines whether a specific thread ID matches the thread represented by this package or not
*/
bool ThreadPackage::doesMatch(std::thread::id checkingThreadId) {
  if (thread != NULL) {
    return thread->get_id() == checkingThreadId;
  } else {
    return threadId == checkingThreadId;
  }
}

void ThreadPackage::abort() {
  abort_thread=true;
  resume();
}

/**
* Pauses the thread
*/
void ThreadPackage::pause() {
  std::unique_lock<std::mutex> lck=std::unique_lock<std::mutex>(*m);
  if (!completed) cv->wait(lck, [this]{return (this->completed);});
  completed=false;
}

/**
* Resumes the thread
*/
void ThreadPackage::resume() {
  std::unique_lock<std::mutex> lck=std::unique_lock<std::mutex>(*m);
  completed=true;
  cv->notify_one();
}
