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
  bool completed;

public:
  ThreadPackage(std::thread * tp) : thread(tp), m(new std::mutex()), cv(new std::condition_variable()), completed(false) { }
  ThreadPackage(std::thread::id aId) : thread(NULL), threadId(aId), m(new std::mutex()), cv(new std::condition_variable()), completed(false) { }
  ThreadPackage(std::thread*, int);

  bool doesMatch(std::thread::id);
  void pause();
  void resume();
};

#endif /* SRC_THREADPACKAGE_H_ */
