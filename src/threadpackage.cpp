#include <thread>
#include <condition_variable>
#include <mutex>
#include "threadpackage.h"
#include "misc.h"

/**
* Constructor for the ThreadPackage, this is one of a number of constructors (the others are trivial and handled in the header file.) Here if the
* core id is not -1 then map the thread to the specific core. This is useful as we might create multiple threads (for instance when others are paused
* and want the core mapping to be unchanged.)
*/
ThreadPackage::ThreadPackage(std::thread * tp, int core_id) : thread(tp), m(new std::mutex()), cv(new std::condition_variable()),
    completed(false), abort_thread(false) {
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
