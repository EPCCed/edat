# Consuming events during task execution

In addition to tasks being submitted with a number of dependencies and then consuming these before being eligable to run, it is also possible for tasks to consume events whilst they are running. This means that a task can begin with a bare set of dependencies and then later on in execution obtain addition ones. There are two reasons for this in contrast to splitting the task into two sub-tasks; firstly it means that the task has a longer runtime and is more coarse grained, hence the overhead of scheduling is more likely to be amortised. Secondly the state of the task (i.e. local variables) are still available after these additional events have been retrieved, in contrast to two separate tasks one run after the other where the local state is lost.

# Waiting for events
`EDAT_Event* edatWait(int number_of_events, <int event source, char * event identifier>)` is the API call for waiting for a number of events and the same as scheduling tasks each event a pair must be provided - the source process of the event and the event identifier (a string.) The task will not continue beyond this point until all event depenendencies have been met. If events have not yet arrived to meet these dependencies then the task will be paused and context switched from the worker, the worker then being free to execute other tasks. Once the task is reactivated with the events it will continue beyond this call and all the local state will be available to it.

```
void my_task(EDAT_Event * events, int num_events) {
  int total_num_events=num_events;
  
  EDAT_Event * new_events=edatWait(1, EDAT_SELF, "hello");
  total_num_events+=1;
}
```  

The code snippet illustrates this call, where the task starts executing with the events it has initially recieved and specifically sets the _total_num_events_ variable to be this number. The task is then paused for a new event from itself with identifier _hello_ and once this is available the call returns with this event as the first member of _new_events_ and the _total_num_events_ variable (which still holds the previous value) is incremented by one.

As with task scheduling, the events are held in the _EDAT_Event_ array in the same order that the dependencies were specified in the call.

# Non-blocking event consumption
In addition to blocking for events to be made available, the _edatRetrieveAny_ call (`EDAT_Event* edatRetrieveAny(int * number_retrieved, int number_of_events, <int event source, char * event identifier>`) will check for the provided events and return any that are available. Crucially this call does not block, so irrespective of the number of matching dependencies available, execution will continue straight away and there is no task pausing. The `int * number_retrieved` argument returns the number of events retrieved, which may be zero.

```
void my_task(EDAT_Event * events, int num_events) {
  int num_retrieved=0;
  EDAT_Event * new_events;
  while (num_retrieved == 0) {
   new_events=edatRetrieveAny(&num_retrieved, 1, EDAT_SELF, "hello");
  }
}
```

The code snippet illustrates this call, where we are spinning until an event has been received. In this simple case it would be better to use an _edatWait_ instead, but hopefully the general idea is obvious to the reader where some additional functionality could be added such that the task is kept busy even if the event has not been received.
