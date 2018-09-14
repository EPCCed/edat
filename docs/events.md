# Firing events
Events are _fired_ from a source to target process. Firing of events can occur at any point in the users code (e.g. in the main function and/or executing tasks) and is non-blocking, so the user's code will continue to execute as soon as the call has been made. Events serve two purposes, first as a dependency of tasks (contributing to the execution of the task) and secondly providing the ability to send some data to this task if required. If the programmer chooses to associate some payload data with an event, this data is copied by EDAT when the call to fire an event is made. Hence events are truely _fire and forget_, the immediate return of the call to fire an event provides no suggestion of when the event is actually communicated or the consuming task executed, but the programmer can immediately reuse any payload data buffer provided to the event as a separate copy has been made. Once an event is fired it is not possible to delete that event.

# Firing an event
The API call to fire an event is `void edatFireEvent(void* data, int data_type, int number_elements, int target_rank, const char * event_identifier)`, as mentioned above this call is non-blocking (returns immediately) and explicitly copies any provided data into a separate buffer space. In addition to an explicit integer target rank (e.g. 0 to process zero, 1 to process one etc) the user can also provide __EDAT_SELF__ to send the event to itself and __EDAT_ALL__ to send the event to all process (effectively you can think of this as a broadcast, broadcasting the event to all processes including itself.)

A number of data types are predefined by EDAT

EDAT type | Description
--------- | -----------
EDAT_NOTYPE | No type, often used when no payload data is sent
EDAT_INT | Sending of integer value(s)
EDAT_LONG  | Sending of long integer value(s)
EDAT_FLOAT | Sending of single precision floating point value(s)
EDAT_DOUBLE | Sending of double precision floating point value(s)
EDAT_BYTE | Sending of byte(s) of data, this is finest grained type of data
EDAT_ADDRESS | Sending of pointer address(es) to some data, this is of most use when the event is sent to the same process

If the programmer is not going to send any payload data then it is suggested that _NULL_ is used for the data value, _EDAT_NOTYPE_ for the type and _0_ for the number of elements, e.g. `edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "event1")`

An event is consumed as a dependency to a corresponding task and all events must have been consumed for the code to terminate.

```c
#include "edat.h"
#include <stdio.h>

void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit();
  if (edatGetRank() == 0) {
    edatSubmitTask(my_task, 1, EDAT_ALL, "my_event");
  }
  int d=10;
  edatFireEvent(&d, EDAT_INT, 1, 0, "my_event");
  edatFinalise();
  return 0;
}

void my_task(EDAT_Event * events, int num_events) {
  int i=0, value=0;
  for (i=0;i<num_events;i++) {
    value+=*((int*) events[i].data);
  }
  printf("Total summed value is %d\n", value);
}
```

This example implements a reduction on process 0, where values from each process are summed up and displayed. Process 0 submits the task (function name _my_task_) with an event dependency which is to recieve events from all processes with the identifier _my_event_. Each process then fires an event to process 0 with the integer payload data of _10_. Note that as soon as the _edatFireEvent_ call returns then the programmer can reuse the _d_ variable no problem. The _my_task_ task will then be mapped to an idle worker thread for execution once a corresponding event from all processes has been received and it will add up the integer values from each and display the sum.

# Persistent events

Like tasks, there is also a distinction between transitory and persistent events but this is more subtle. The tasks we have discussed up until this point are transitory, i.e. they are consumed as a dependency to a task. It is also possible for events to be persistent, where they are not consumed but instead will effectively fire time and time again. Note that the firing is done locally, i.e. even if a persistent event is sent from a remote process then the fact it is persistent it handled by the target.

The API call for persistent events is `void edatFirePersistentEvent(void* data, int data_type, int number_elements, int target_rank, const char * event_identifier)`. Note that there is no need for persistent events to have been consumed for termination to occur.
