#ifndef SRC_CONCURRENCY_CTRL_H_
#define SRC_CONCURRENCY_CTRL_H_

#include <map>
#include <set>
#include <string>
#include <mutex>
#include <vector>
#include "threadpool.h"

struct LockContext {
  std::mutex mutex;
  int acquiredWorker;
  bool acquired;
};

class ConcurrencyControl {
  std::map<std::string, LockContext*> locks;
  std::map<int, std::set<std::string>> workerAcquiredLocks;
  std::mutex locks_mtx;
  ThreadPool * threadPool;
  int activeLocks;
  void issueLock(std::string, std::unique_lock<std::mutex> *, int);
public:
  ConcurrencyControl(ThreadPool * threadPool) : threadPool(threadPool), activeLocks(0) { }
  void lock(std::string);
  void unlock(std::string);
  bool test_lock(std::string);
  std::vector<std::string> releaseCurrentWorkerLocks();
  void aquireLocks(std::vector<std::string>);
};
#endif
