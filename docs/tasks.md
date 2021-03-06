# Submitting tasks

Tasks are submitted to EDAT for execution by the programmer at any point in their code (e.g. either by the main thread when the program starts or by tasks executing.) All calls to submit tasks are non-blocking and the programmer's code will continue immediately after the scheduling call has been made. 

## Submitting a task
`void edatSubmitTask(task function pointer, number of event dependencies, <int event source, char * event identifier>)` This call will schedule the corresponding task to execute when a number of dependencies have been met. These dependencies are events and for each event a pair must be provided - the source process of the event and the event identifier (a string.) 

When submitting a task, for each event the programmer must provide the source rank. In addition to explicit integer numbers (such as 0 for rank zero, 1 for rank one etc), the programmer can also provide a number of inbuilt constants. __EDAT_SELF__ simply denotes the event originates from that same process, __EDAT_ANY__ effectiely wildcards the source as it denotes that an event with the matching idenfitier can arrive from any process to activate the task. __EDAT_ALL__ requires that an event with the matching identifier must arrive from __EVERY__ process (including itself) before the task can be executed by a worker thread. This last constant, the _EDAT_ALL_, can be thought of as syntactic sugar because the programmer could explicitly specify a pair for each process (e.g. instead of _EDAT_ALL, "eventid"_ something like _0, "eventid", 1, "eventid", 2, "eventid"_ etc.) But this second approach is just a bit more messy and also need to be explicitly modified by the programmer whenever a different number of processes are selected.

The task function itself is of signature `void task(EDAT_Event* events, int number_of_events)`, where the number of events and data associated with each of these is passed in. EDAT guarantees that the order of events passed into the task will exactly match the dependency order provided to the _edatSubmitTask_ call. For instance if you submit a task depending on two events, the first one from process 0 with id _a_ and the second from process 1 with id _b_, then it is guaranteed that the first event passed to the task function will that from process 0 with id _a_ and the second from process 1 with id _b_. 

The _EDAT_Event_ structure is made up of the following members:

Member | Type | Description
------ | ---- | -----------
data | void * | The (optional) payload data associated with the event
metadata | EDAT_Metadata | Meta data associated with the event

The _EDAT_Metadata_ structure is made up of the following members:

Member | Type | Description
------ | ---- | -----------
data_type | int | The type of the payload data provided
number_elements | int | The number of elements (of type _data_type_) in the payload data
source | int | The rank of the process that sent this event
event_id | char * | The Event IDentifier (EID)

All transitory tasks that have been submitted must be executed before the code can terminate. Note that you should *not* free the event structure or payload data provided to the task function as this will be done automatically by the EDAT runtime upon task completion.

```c
#include "edat.h"
#include <stdio.h>

void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit();
  edatSubmitTask(my_task, 1, EDAT_ANY, "my_task");
  if (edatGetRank() == 0) {
    int d=33;
    edatFireEvent(&d, EDAT_INT, 1, EDAT_ALL, "my_task");
  }
  edatFinalise();
  return 0;
}

void my_task(EDAT_Event * events, int num_events) {
  if (events[0].metadata.number_elements > 0 && events[0].metadata.data_type == EDAT_INT) {
    printf("[%d] Hello world %d from %d!\n", edatGetRank(), *((int *) events[0].data), events[0].metadata.source);
  } else {
    printf("Event has no payload data or it is not an integer\n");
  }
}
```

The example code here illustrates each process scheduling a task (the _my_task_ function) with 1 event dependency. This even can originate from any process and it must have the identifier _my_task_. Process 0 will then fire an event with payload data of a single integer (value _33_) to every process including itself (due to the _EDAT_ALL_, see <a href="https://github.com/EPCCed/edat/blob/master/docs/events.md">events documentation</a> for more details.) Once this event arrives on each process then the task will be mapped to an idle worker thread for execution and it will do some error checking on the meta data and display the associated message.

# Persistent tasks
Whilst transitory tasks only execute once, i.e. they are submitted and when their depedencies are met they are then moved to a ready queue for execution by a worker thread when one becomes available. Instead persistent tasks are not moved to the ready queue but instead a copy is made. There are a number of guarantees associated with persistent tasks in terms of the order of consumption of events. When an event arrives that matches the dependencies of a persistent task then this event will be _consumed_ by that persistent task. If a persisitent task depends on multiple events (e.g. event _a_ and _b_), then if multiple instances of event _a_ arrive then multiple instances of the submitted persistent task will consume this event. In this example the first event _b_ arriving will be consumed by the first instance of the persistent task that is waiting for this. Hence we guarantee that we are never in a situation where multiple instances of persistent task are in progress and each is partially fulfilled by separate event dependencies (as this could result in deadlock.)  

The API call to submit a persistent task is very similar to a transient task `void edatSubmitPersistentTask(task function pointer, number of event dependencies, <int event source, char * event identifier>)` and the task function itself is identical.

# Named tasks
A name can be associated with (both transient and persistent) tasks for then referencing these later on. The API call is similar, `void edatSubmitNamedTask(task function pointer, char * task_name, number of event dependencies, <int event source, char * event identifier>)` and `void edatSubmitPersistentNamedTask(task function pointer, char * task_name, number of event dependencies, <int event source, char * event identifier>)` respectively.

EDAT provides some task management functionality based on the name, `int edatIsTaskSubmitted(char * task_name)` returns 1 if the task is submitted and 0 if the task is not submitted. `int edatRemoveTask(char * task_name)` will remove a task from running and any events already consumed by the task (but the task is not yet eligible for execution as there are outstanding events) will be lost. This removal tends to be most useful for persistent tasks, where a task has been executed a number of times but then the code moves on and this behaviour is no longer appropriate. Note that it is not possible to remove a task that is running by a worker thread or is in the ready queue waiting for a free worker thread to execute upon. 

# Finding events in a task
As mentioned above, EDAT guarantees that the order of events passed to a task matches the event order provided by the programmer when the task was submitted. EDAT also provides a helper function, _edatFindEvent_, which will search through the tasks events for an event that matches an identifier and source, returning the index or -1 if none is found. The API is `int edatFindEvent(EDAT_Event* task_events, int number_of_events, int source_to_find, const char* event_identifier_to_find)`.  
