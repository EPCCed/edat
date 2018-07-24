#ifndef SRC_THREADPACKAGE_H_
#define SRC_THREADPACKAGE_H_

#include <thread>
#include <condition_variable>
#include <mutex>

class ThreadPackage {
  std::thread * thread;
  std::thread::id threadId;
  std::mutex * m;
  std::condition_variable * cv;
  std::unique_lock<std::mutex> my_lock;
  bool completed, abort_thread;

public:
  ThreadPackage(std::thread * tp) : thread(tp), m(new std::mutex()), cv(new std::condition_variable()), completed(false), abort_thread(false) { }
  ThreadPackage(std::thread::id aId) : thread(NULL), threadId(aId), m(new std::mutex()), cv(new std::condition_variable()), completed(false), abort_thread(false) { }
  ThreadPackage() : thread(NULL), m(new std::mutex()), cv(new std::condition_variable()), completed(false), abort_thread(false) { }

  void attachThread(std::thread*, int);
  bool doesMatch(std::thread::id);
  std::thread::id getThreadID(void) {return thread->get_id();};
  void pause();
  void resume();
  bool shouldAbort() { return abort_thread; }
  void abort();
};

#endif /* SRC_THREADPACKAGE_H_ */
