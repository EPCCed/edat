# Scheduling tasks

Tasks are scheduled for execution by the programmer at any point in their code (e.g. either by the main thread when the program starts or by tasks executing.) All calls to schedule tasks are non-blocking and the programmer's code will continue immediately after the scheduling call has been made. 

## Scheduling a task
`edatScheduleTask(task function pointer, number of event dependencies, <int event source, char * event identifier>)` This scheduling call will schedule the corresponding task to execute when a number of dependencies have been met. These dependencies are events and for each event a pair must be provided - the source process of the event and the event identifier (a string.) 

The task function itself is of signature `void task(EDAT_Event* events, int number_of_events)`, where the number of events and data associated with each of these is passed in. EDAT guarantees that the order of events passed into the task will exactly match the dependency order provided to the _edatScheduleTask_ call. For instance if you schedule a task depending on two events, the first one from process 0 with id _a_ and the second from process 1 with id _b_, then it is guaranteed that the first event passed to the task function will that from process 0 with id _a_ and the second from process 1 with id _b_. 
