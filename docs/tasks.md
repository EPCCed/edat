# Scheduling tasks

Tasks are scheduled for execution by the programmer at any point in their code (e.g. either by the main thread when the program starts or by tasks executing.) All calls to schedule tasks are non-blocking and the programmer's code will continue immediately after the scheduling call has been made. 

## Scheduling a task
`edatScheduleTask(task function pointer, number of event dependencies, <int event source, char * event identifier>)` This scheduling call will schedule the corresponding task to execute when a number of dependencies have been met. These dependencies are events and for each event a pair must be provided - the source process of the event and the event identifier (a string.) 

When scheduling a task, for each event the programmer must provide the source rank. In addition to explicit integer numbers (such as 0 for rank zero, 1 for rank one etc), the programmer can also provide a number of inbuilt constants. __EDAT_SELF__ simply denotes the event originates from that same process, __EDAT_ANY__ effectiely wildcards the source as it denotes that an event with the matching idenfitier can arrive from any process to activate the task. __EDAT_ALL__ requires that an event with the matching identifier must arrive from __EVERY__ process (including itself) before the task can be executed by a worker thread. This last constant, the _EDAT_ALL_, can be thought of as syntactic sugar because the programmer could explicitly specify a pair for each process (e.g. instead of _EDAT_ALL, "eventid"_ something like _0, "eventid", 1, "eventid", 2, "eventid"_ etc.) But this second approach is just a bit more messy and also need to be explicitly modified by the programmer whenever a different number of processes are selected.

The task function itself is of signature `void task(EDAT_Event* events, int number_of_events)`, where the number of events and data associated with each of these is passed in. EDAT guarantees that the order of events passed into the task will exactly match the dependency order provided to the _edatScheduleTask_ call. For instance if you schedule a task depending on two events, the first one from process 0 with id _a_ and the second from process 1 with id _b_, then it is guaranteed that the first event passed to the task function will that from process 0 with id _a_ and the second from process 1 with id _b_. 

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

All transitory tasks that have been scheduled must be executed before the code can terminate.

# Persistent tasks
Whilst transitory tasks only execute once, i.e. they are scheduled and when their depedencies are met they are then moved to a ready queue for execution by a worker thread when one becomes available. Instead persistent tasks are not moved to the ready queue but instead a copy is made. There are a number of guarantees associated with persistent tasks in terms of the order of consumption of events. When an event arrives that matches the dependencies of a persistent task then this event will be _consumed_ by that persistent task. If a persisitent task depends on multiple events (e.g. event _a_ and _b_), then if multiple instances of event _a_ arrive then multiple instances of the scheduled persistent task will consume this event. In this example the first event _b_ arriving will be consumed by the first instance of the scheduled persistent task that is waiting for this. Hence we guarantee that we are never in a situation where multiple instances of persistent task are scheduled and each is partially fulfilled by separate event dependencies (as this could result in deadlock.)  
