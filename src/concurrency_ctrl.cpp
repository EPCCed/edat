#include <map>
#include <string>
#include <mutex>
#include <vector>
#include "concurrency_ctrl.h"
#include "misc.h"

/**
* Locks a specific lock based on the name provided. If the lock does not exist then this is created and stored internally. It will also track
* what workers have acquired what locks, this is so locks can easily be released en-mass when the task completes or pauses.
*/
void ConcurrencyControl::lock(std::string name) {
  int myWorker=threadPool->getCurrentWorkerId();
  std::unique_lock<std::mutex> lock_structure(locks_mtx);

  issueLock(name, &lock_structure, myWorker);

  std::map<int, std::set<std::string>>::iterator workerIt = workerAcquiredLocks.find(myWorker);
  if (workerIt != workerAcquiredLocks.end()) {
    workerIt->second.insert(name);
  } else {
    std::set<std::string> workerSet;
    workerSet.insert(name);
    workerAcquiredLocks.insert(std::pair<int, std::set<std::string>>(myWorker, workerSet));
  }
}

/**
* Issues a specific lock, it will reuse the lock if this already exists or create a new one if not. Note that it will block on the locking here
* if another task already has the lock, hence we unlock the overall protection lock (which is fine, it doesn't conflict.) Note that it is
* perfectly acceptable for the same task to lock multiple times, but this is not acquired multiple times and still one single unlock will release it.
*/
void ConcurrencyControl::issueLock(std::string name, std::unique_lock<std::mutex> * lock_structure, int myWorker) {
  std::map<std::string, LockContext*>::iterator it = locks.find(name);
  if (it != locks.end()) {
    if (it->second->acquiredWorker == myWorker) return;
    lock_structure->unlock();
    it->second->mutex.lock();
    it->second->acquired=true;
    it->second->acquiredWorker=myWorker;
    lock_structure->lock();
  } else {
    LockContext * newContext = new LockContext();
    newContext->mutex.lock();
    newContext->acquired=true;
    newContext->acquiredWorker=myWorker;
    locks.insert(std::pair<std::string, LockContext*>(name, newContext));
  }
  activeLocks++;
}

/**
* Will acquire all the locks in the provided vector, locking them all. Note how we sort the lock names alphabetically and then lock on these in
* order. This is to avoid deadlock where different tasks are locking multiple locks in different orders. This is used when the task
* reactivates after it has been paused.
*/
void ConcurrencyControl::aquireLocks(std::vector<std::string> locksToAcquire) {
  // First sort the locks by lock name to ensure that there is no deadlock with other workers acquiring locks at the same time
  std::sort(locksToAcquire.begin(), locksToAcquire.end());
  int myWorker=threadPool->getCurrentWorkerId();

  std::unique_lock<std::mutex> lock_structure(locks_mtx);

  std::map<int, std::set<std::string>>::iterator workerIt = workerAcquiredLocks.find(myWorker);
  if (workerIt == workerAcquiredLocks.end()) {
    std::set<std::string> workerSet;
    workerAcquiredLocks.insert(std::pair<int, std::set<std::string>>(myWorker, workerSet));
  }

  for (std::string lockName : locksToAcquire) {
    issueLock(lockName, &lock_structure, myWorker);
    workerIt = workerAcquiredLocks.find(myWorker); // Refind as unlock the lock_structure in the issue, so the iterator might be nudged off
    workerIt->second.insert(lockName);
  }

}

/**
* Releases all the locks for the current worker (the current task.) This is useful when the task completes or when it is explicitly paused,
* this call returns a vector of the lock names that were released, this is useful so that in the pause-resume case they can be easily reacquired.
*/
std::vector<std::string> ConcurrencyControl::releaseCurrentWorkerLocks() {
  std::unique_lock<std::mutex> lock_structure(locks_mtx);

  std::vector<std::string> locksReleased;
  if (activeLocks > 0) {
    int myWorker=threadPool->getCurrentWorkerId();
    std::map<int, std::set<std::string>>::iterator workerIt = workerAcquiredLocks.find(myWorker);
    if (workerIt != workerAcquiredLocks.end()) {
      for (std::string lockName : workerIt->second) {
        locksReleased.push_back(lockName);
        std::map<std::string, LockContext*>::iterator it = locks.find(lockName);
        if (it != locks.end() && it->second->acquired) {
          if (it->second->acquiredWorker == myWorker) {
            it->second->acquired=false;
            it->second->acquiredWorker=-1;
            it->second->mutex.unlock();
            activeLocks--;
          }
        }
      }
      workerIt->second.clear(); // Clear all acquired locks for the worker (as a new task will run here.)
    }
  }
  return locksReleased;
}

/**
* Unlocks a specific lock based on its name. If the lock is not found or is acquired by another task then this will raise an error.
*/
void ConcurrencyControl::unlock(std::string name) {
  int myWorker=threadPool->getCurrentWorkerId();
  std::unique_lock<std::mutex> lock_structure(locks_mtx);

  std::map<std::string, LockContext*>::iterator it = locks.find(name);
  if (it != locks.end() && it->second->acquired) {
    if (it->second->acquiredWorker == myWorker) {
    it->second->acquired=false;
    it->second->acquiredWorker=-1;
    it->second->mutex.unlock();
    activeLocks--;
    } else {
      raiseError("Can not unlock a lock that the task does not hold");
    }
  } else {
    raiseError("Can not unlock a lock that the task does not hold");
  }
  std::map<int, std::set<std::string>>::iterator workerIt = workerAcquiredLocks.find(myWorker);
  if (workerIt != workerAcquiredLocks.end()) {
    std::set<std::string>::iterator lockNamesIterator=workerIt->second.find(name);
    if (lockNamesIterator != workerIt->second.end()) workerIt->second.erase(lockNamesIterator);
  }
}

/**
* Tests whether a given lock has been acquired by the current task (the current worker actually, but as we release locks on worker termination
* it is the same thing.)
*/
bool ConcurrencyControl::test_lock(std::string name) {
  std::unique_lock<std::mutex> lock_structure(locks_mtx);
  std::map<std::string, LockContext*>::iterator it = locks.find(name);
  if (it != locks.end() && it->second->acquired) {
    if (it->second->acquiredWorker == threadPool->getCurrentWorkerId()) return true;
  }
  return false;
}
