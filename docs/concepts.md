# Important Concepts

The idea of Event Driven Asynchronous Tasks (EDAT) is to provide the programmer with a realistic distributed memory view of their machine, but still allow them to write task based codes. 

[](https://github.com/EPCCed/edat/edit/master/docs/edat_processes.png)

There are a number of important concepts relating to this which you should understand to be able to write code using EDAT effectively.

## Tasks
Tasks are some collection of instructions that will operate together to form some logical part of the code. In EDAT tasks are split up via functions (i.e. a function is a task) and scheduled to run. When scheduling a task the programmer provides a list of dependencies that must be met before that task can then be executed by a worker thread. In EDAT dependencies are events, which originate from some process and are labelled with an Event IDentifier (EID.)

Splitting parallel code up into independent tasks that only interact via their dependencies (events) often results in a far more loosely coupled and asynchronous code that can greatly help with performance and scalability. 

There are two types of task:
* __Transient__ tasks are scheduled and will be eligable for execution on a worker thread when all dependencies are met. Once the task has been scheduled for execution it is removed from the pending queue (i.e. there is a one to one mapping of scheduled transient tasks to their execution.) All transient tasks must have executed before a code can terminate.
* __Persistent__ tasks are similar to transient tasks, but once a task is eligible for execution it is not deleted from the pending queue. Instead these persist and can execute multiple times, whenever the dependencies are met. Hence there is no longer a one to one mapping as a single scheduled task might execute multiple times and multiple instances of a single persistent task can be running concurrently on separate worker threads. There is no requirement for persistent tasks to have been executed for the code to terminate. 

## Events
Events are send from one process to another (it can be the same process) and are the major way in which tasks and processes will interact. Events are labelled with an identifier (EID) and may contain some optional payload data that the tasks can then process. Whilst this might look a bit like a message, such as that of MPI, crucially the way in which events are sent and delivered to tasks is entirely abstracted from the programmer. Therefore one can concentrate on what interacts rather than the low level details of how.

## Threads
Each process contains a number of worker threads which tasks are mapped to for execution. Each worker thread can only execute one task at a time, and once a task has completed that worker thread will execute the next task or wait until one is available. It is common to map worker threads to cores, for instance on a machine with 12 cores per NUMA region you might have 12 worker threads, but this is not mandated and other mappings are possible with EDAT.

### Main thread
When your program starts up, in the _main_ function of user's code, this is refered to as the main thread. When the _finalize_ function is called this main thread will either go to sleep (i.e. consumes no CPU cycles) until execution has fully completed, or is used as a worker thread. Which behaviour is chosen depends on how you have configured your run (see <a href="https://github.com/EPCCed/edat/blob/master/docs/environment_variables.md">here</a>.)

### Progress polling thread
The current version of EDAT uses polling to check for new events, schedule tasks and push the delivery of events. This is either an additional polling thread that continually operates or a worker thread that, when it becomes idle, will poll for updates until a task for execution becomes available. Which behaviour is most effective depends on your code andcan be configured by the user (see <a href="https://github.com/EPCCed/edat/blob/master/docs/environment_variables.md">here</a>.)
