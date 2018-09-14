# Important Concepts

The idea of Event Driven Asynchronous Tasks (EDAT) is to provide the programmer with a realistic distributed memory view of their machine, but still allow them to write task based codes. This is illustrated in the diagram, where programmers understand that memory spaces are distributed and processes together by a network. Each process contains a number of cores and by default EDAT will run one worker per core, workers physically execute tasks. In reality we suggest that you run one process per NUMA region (which often corresponds to a process per processor.)

![](https://github.com/EPCCed/edat/raw/master/docs/edat_processes.png)

There are a number of important concepts relating to this which you should understand to be able to write code using EDAT effectively.

## Tasks
Tasks are some collection of instructions that will operate together to form some logical part of the code. In EDAT tasks are split up via functions (i.e. a function is a task) and submitted to EDAT for execition. When scheduling a task the programmer provides a list of dependencies that must be met before that task can then be executed by a worker thread. In EDAT dependencies are events, which originate from some process and are labelled with an Event IDentifier (EID.)

Splitting parallel code up into independent tasks that only interact via their dependencies (events) often results in a far more loosely coupled and asynchronous code that can greatly help with performance and scalability. 

There are two types of task:
* __Transient__ tasks are eligable for execution on a worker thread when all dependencies are met. Once the task has been scheduled for execution it is removed from the pending queue (i.e. there is a one to one mapping of submitted transient tasks to their execution.) All transient tasks must have executed before a code can terminate.
* __Persistent__ tasks are similar to transient tasks, but once a task is eligible for execution it is not deleted from the pending queue. Instead these persist and can execute multiple times, whenever the dependencies are met. Hence there is no longer a one to one mapping as a single submitted task might execute multiple times and multiple instances of a single persistent task can be running concurrently on separate worker threads. There is no requirement for persistent tasks to have been executed for the code to terminate. 

## Events
Events are send from one process to another (it can be the same process) and are the major way in which tasks and processes will interact. Events are labelled with an identifier (EID) and may contain some optional payload data that the tasks can then process. Whilst this might look a bit like a message, such as that of MPI, crucially the way in which events are sent and delivered to tasks is entirely abstracted from the programmer. Therefore one can concentrate on what interacts rather than the low level details of how.

## Example

This section contains an example code where process 0 submits a task to EDAT with no dependencies (_task1_) which will be mapped to a worker for immediate execution. Process 1 submits two tasks (_task2_ and _task3_), _task2_ has one dependency - namely an event with identifier _event1_ from process 0, before it is eligable for execution by a worker. The other task submitted by process 1 (_task3_) has two dependencies - an event with identifier _event2_ from process 0 and a second event with identifier _event3_ from process 1. As the tasks execute, _task1_ on process 0 will fire the event _event1_ and _event2_ (the later with some payload data) to process 1 and _task2_ on process 1 fires the event _event3_ to itself. This last event, _event3_ in combination with _event2_ from process 0 meet the dependencies of _task3_ which will make it eligable for execution.

```c
#include "edat.h"
#include <stdio.h>

int main() {
  edatInit();
  if (edatGetRank() == 0) {
    edatSubmitTask(task1, 0);
  } else if (edatGetRank() == 1) {
    edatSubmitTask(task2, 1, 0, "event1");
    edatSubmitTask(task3, 2, 0, "event2", 1, "event3");
  }
  edatFinalise();
  return 0;
}

void task1(EDAT_Event * events, int num_events) {
  edatFireEvent(NULL, 0, EDAT_NONE, 1, "event1");
  int data=33;
  edatFireEvent(&data, 1, EDAT_INT, 1, "event2");
}

void task2(EDAT_Event * events, int num_events) {
  int data=100;
  edatFireEvent(&data, 1, EDAT_INT, EDAT_SELF, "event3");
}

void task3(EDAT_Event * events, int num_events) {
  printf("%d\n", *((int*) events[0].data) + *((int*) events[1].data));
}
```

To compile this you will typically execute something like `gcc -o example example.c -Iedatdir/include -Ledatdir -ledat` and then to run `mpiexec -np 2 ./example` (the MPI process launcher is used to run multiple processes.) You must tell gcc the location of the EDAT directory via the _-L_ and _-I_ arguments, to run the code you will also likely need to add the EDAT directory to your _LD_LIBRARY_PATH_ i.e. `export LD_LIBRARY_PATH:edatdir:$LD_LIBRARY_PATH`

## When will my code terminate?

Four conditions must be met for your code to terminate (i.e. for the main thread to progress beyond the _edatFinalise_ call:
* The main thread must have called _edatFinalise_
* All workers on all processes must be idle
* No tasks are outstanding (transitory tasks, persistent tasks do not count - see <a href="https://github.com/EPCCed/edat/blob/master/docs/tasks.md">task submission</a>)
* There are no outstanding events (transitory events, persistent events do not count - see <a href="https://github.com/EPCCed/edat/blob/master/docs/events.md">event firing</a>)

## Misc concepts

### Workers
Each process contains a number of workers which tasks are mapped to for execution. Each worker can only execute one task at a time, and once a task has completed that worker thread will execute the next task or wait until one is available. It is common to map worker threads to cores, for instance on a machine with 12 cores per NUMA region you might have 12 worker threads, but this is not mandated and other mappings are possible with EDAT.

### Main thread
When your program starts up, in the _main_ function of user's code, this is refered to as the main thread. When the _finalize_ function is called this main thread will either go to sleep (i.e. consumes no CPU cycles) until execution has fully completed, or is used as a worker thread. Which behaviour is chosen depends on how you have configured your run (see <a href="https://github.com/EPCCed/edat/blob/master/docs/environment_variables.md">here</a>.)

### Progress polling thread
The current version of EDAT uses polling to check for new events, outstanding tasks and push the delivery of events. This is either an additional polling thread that continually operates or a worker thread that, when it becomes idle, will poll for updates until a task for execution becomes available. Which behaviour is most effective depends on your code andcan be configured by the user (see <a href="https://github.com/EPCCed/edat/blob/master/docs/environment_variables.md">here</a>.)
