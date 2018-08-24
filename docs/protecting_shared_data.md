# Protecting shared data
As multiple workers execute in the same memory space of a single process it is possible that they might need to interact with some shared state. Whilst EDAT interoperates fine with all other threading libraries, so for instance it is possible to use state protection from pthreads or C++11, EDAT itself provides two general approaches for doing this.

# Using events as mutexes
More of a question of style rather than specific API call, but it is possible to use events are a mutex.

```
#include <stdlib.h>
#include "edat.h" 

int * shared_data;

int main() {
  edatInit();
  if (edatGetRank()==0) {
    edatSchedulePersistentTask(my_task, 2, EDAT_SELF, "data", 1, "values");
    shared_data=(int*) malloc(sizeof(int) * 10);
    edatFireEvent(shared_data, EDAT_ADDRESS, 1, EDAT_SELF, "data");
  } else if (edatGetRank()==1) {
    int d=22;
    edatFireEvent(&d, EDAT_INT, 1, 0, "values");
  }
  edatFinalise();
}

void my_task(EDAT_Event * events, int num_events) {
  .....
  edatFireEvent(shared_data, EDAT_ADDRESS, 1, EDAT_SELF, "data");
}

```

The code snippet illustrates this, where there are two processes, and process zero schedules a persistent task. Process zero then allocates _shared_data_ and fires the address of this (_EDAT_ADDRESS_) to itself. The task is only executed by a worker on process zero when both this _data_ event and a _values_ event are received. But crucially regardless of whether any other _values_ events are received by process zero whilst this task is running, another instance of the task will not run until the task fires the _data_ event to itself. This way of consuming and re-firing events are a way in which the programmer can protect critical sections of code.

For a finer grained approach it would also be possible to use the _edatWait_ call, where a tasks waits for an event and then when it leaves the critical section fires this event to itself.

# Explicit locking API calls
EDAT also provides explicit locking calls where the programmer can interact with a named lock. It is the name that is the unique identifier and there can only be one worker on a process holding a lock with a specific name at any one time. `void edatLock(char* lock_name)` is used to acquire a lock with a specific name, `void edatUnlock(char* lock_name)` will release the lock and `int edatTestLock(char* lock_name)` checks whether the lock has been acquired or not (it does not change the state of the lock.)

Crucially locks are at a worker level rather than task level, what we mean by this is that when a task completes all currently acquired locks by that task are automatically released regardless of whether the programmer calls _edatUnlock_ or not. Additionally if the programmer calls _edatWait_ then all acquired locks by that task are automatically released and will then be reacquired when the task is mapped to a worker for execution. The programmer should be a bit careful with this later behaviour, as it might be that a task is mapped to a worker and ready to continue execution but waits excessively to reaquire its locks due to the other running tasks holding them.

```
int my_task(EDAT_Event * events, int num_events) {
  edatLock("my_lock");
  edatLock("second_lock");
  printf("%d %d\n", edatTestLock("my_lock"), edatTestLock("another"));
  edatUnlock("my_lock");
}
```

In this code snippet the task will acquire two locks, _my_lock_ and _second_lock_. It will then test whether two locks have been acquired and display the integer result (1 and 0 being printed.) Lastly the programmer explicitly unlocks the _my_lock_ lock, but it doesn't matter that they have failed to unlock _second_lock_ as this is automatically unlocked by EDAT when the task completes anyway (hence the unlock here is surplus to requirements.)
