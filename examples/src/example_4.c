/*
* This example illustrates multiple event dependencies, where the task on rank 0 depends on two events for running - the first is an event from rank 0 (i.e. itself)
* with event ID "a" and the second is an event from rank 1 with event id "b". Rank 0 then fires an event to itself and in parallel rank 1 fires an event to rank 0 too.
* The task will display the number of events and iterates through them, displaying some meta data and the associated payload data.
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  const task_ptr_t task_array[1] = {my_task};
  edatInit(&argc, &argv, NULL, task_array, 1);
  int myval=(edatGetRank() + 100)* 10;
  if (edatGetRank() == 0) {
    edatScheduleTask(my_task, 2, 0, "a", 1, "b");
    edatFireEvent(&myval, EDAT_INT, 1, 0, "a");
  } else if (edatGetRank() == 1) {
    edatFireEvent(&myval, EDAT_INT, 1, 0, "b");
  }
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
  printf("Number of events %d\n", num_events);
  int i=0;
  for (i=0;i<num_events;i++) {
    printf("Item %d from %d with UUID %s: %d\n", i, events[i].metadata.source, events[i].metadata.event_id, *((int*) events[i].data));
  }
}
