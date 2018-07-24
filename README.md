# Event Driven Asynchronous Tasks (EDAT)

This library provides support for Event Driven Asychronous Tasks, task based programming over large scale distributed memory machines. The programmer is explicitly aware of the distributed nature of their code, so they can optimise for important aspects such as locality, but still abstracted from the underlying mechanisms of parallelism. Tasks are scheduled by the programmer and depend on a number of events arriving before they can execute on workers. Events are sent, either locally or remotely with some optional payload data.

Please note that this is code is of a research nature and primarily of use to explore the notion of task based programming over distributed memory architectures.

The <a href="https://github.com/EPCCed/edat/tree/master/docs">docs folder</a> contains documentation and the <a href="https://github.com/EPCCed/edat/tree/master/examples">examples folder</a> a number of examples. We suggest starting with the <a href="https://github.com/EPCCed/edat/blob/master/docs/getting_started.md">getting started</a> document which links to all other documentation.

## Installation

EDAT requires a C++11 compiler (such as GCC) and MPI installation (such as MPICH or OpenMPI.) Once you have downloaded the code, ensure you are in the top level directory and issue *make*. This will build the code and results in both statically and dynamically linkable libraries in this same directory (*libedat.so* and *libedat.a*.) Due to the research nature of the code we don't currently have an install option, but you can copy these to your */usr/lib* directory and *include/edat.h* to */usr/include* (or whereever is appropriate on your system.)

Note that the makefile assumes your compilation command is *mpicxx*, you can change this in the makefile - for instance *CC* is common on Crays. We have tested the library and examples on both the GCC and Cray compilers.

## Building the examples

Go into the *examples* directory and issue *make*, this will build the examples in *examples/src* and by default will dynamically link to the edat library (on Crays it will automatically statically link.) 

If you are dynamically linking then you will need to ensure your LD_LIBRARY_PATH points to the location of the edat library (e.g. *export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/username/edat*). To execute an example it is easiest to use *mpiexec*, e.g. *mpiexec -np 2 ./example_1* but you can use whatever job launcher is most appropriate on your system such as *aprun* on Crays.

## Writing your own EDAT code

You will need to add the *edat/include* directory to your search path and include *edat.h*. When building your code you need to link against the edat library (e.g. *-L/home/username/edat -ledat*). The examples provide illustrations of doing this.

An an example you can look at the following simple example (this is the same as <a href="https://github.com/EPCCed/edat/blob/master/examples/src/example_1.c">example_1</a>.)
```
#include "edat.h"
#include <stdio.h>

void my_task(EDAT_Event*, int);
void my_task2(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv, NULL);
  if (edatGetRank() == 0) {
    edatScheduleTask(my_task, 1, EDAT_ANY, "my_task");
  } else if (edatGetRank() == 1) {
    edatScheduleTask(my_task2, 1, EDAT_ANY, "my_task2");
    int d=33;
    edatFireEvent(&d, EDAT_INT, 1, 0, "my_task");
  }
  edatFinalise();
  return 0;
}

void my_task(EDAT_Event * events, int num_events) {
  if (events[0].metadata.number_elements > 0 && events[0].metadata.data_type == EDAT_INT) {
    printf("Hello world from %d with data %d!\n", edatGetRank(), *((int *) events[0].data));
  } else {
    printf("Hello world!\n");
  }
  edatFireEvent(NULL, EDAT_NOTYPE, 0, 1, "my_task2");
}

void my_task2(EDAT_Event * events, int num_events) {
  printf("Task two running on %d\n", edatGetRank());
}
```

In the main function we first initialise EDAT (even though EDAT uses MPI behind the scenes there is no need to explicitly initialise that as the library does that for you.) The last think we do in the main function is to finalise EDAT which will effectively sleep until all tasks have run and events consumed.

Process 0 will schedule a task that relies on an event with the Event IDenfitier (EID) *my_task* sent from any other process to activate. Process 1 also schedules a task that relies in an event with EID *my_task2* from any process to run. Process 1 then sends an event to process 0 with a single integer as payload data.

Once this event has been sent from process 1 to 0, process 0 will run its scheduled task on a worker which effectively means the execution of the *my_task* function. As an input to this function are passed the number of events and each specific event (with both data and metadata) so that the task can then use these if needed. 

In this example process 0 will then fire an event with EID *my_task2* but no payload data to process 1 which causes the execution of that task on a worker of process 1 and the code in the *my_task2* function.

**But how many workers does each process have?** By default this is set as the number of logical cores of your machine, but this is configurable by both environment variables and from within code. There are other options that can be used to control other aspects such as worker to core placement. For more information on this see <a href="https://github.com/EPCCed/edat/blob/master/docs/environment_variables.md">this documentation</a>.

## Finding out more

We have documentation in the docs directory. As I say this code is of a research nature so there are some aspects of the API which will be less frequently used but these are included for specific experiments or with certain applications in mind. 
