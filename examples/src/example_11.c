/*
* This example illustrates the edatWait call which will pause the current task (including the main thread) until the event dependencies have been met. This means
* that a task can start running, do some work and then wait until further dependencies are met whilst maintaing all the context (i.e. internal variables) of that task.
* The wait will efficiently pause the task (i.e. it is put to sleep and the worker can be reused to run other tasks.) In this example the main thread on rank 0 waits and is
* reactivated when rank 1 sends an event and rank 0 sends itself an event (running in a separate, "my_task", task concurrently.) This "my_task" itself waits for the "taskwaiter"
* event from all ranks before it can reactivate.
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(NULL, 0);
  if (edatGetRank() == 0) {
    edatSubmitTask(my_task, 0);
    EDAT_Event * events = edatWait(2, 1, "hello", EDAT_SELF, "fireback");
    printf("Passed wait first size is %d and second size is %d element(s)\n", events[0].metadata.number_elements, events[1].metadata.number_elements);
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "taskwaiter");
  } else if (edatGetRank() == 1) {
    int d=44;
    edatFireEvent(&d, EDAT_INT, 1, 0, "hello");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, 0, "taskwaiter");
  }
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "fireback");
  printf("Pause task\n");
  edatWait(1, EDAT_ALL, "taskwaiter");
  printf("Passed wait in task\n");
}
