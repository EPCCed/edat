/*
* A simple example where all ranks schedule a task that will be eligable for execution when they receive an event from any rank with the identifier "my_task". Then rank 0
* fires an event with a single integer payload data to all ranks which will cause the scheduled task on each rank to be eligable for running and mapped to
* a worker for execution.
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv, NULL);
  edatScheduleTask(my_task, 1, EDAT_ANY, "my_task");
  if (edatGetRank() == 0) {
    int d=33;
    edatFireEvent(&d, EDAT_INT, 1, EDAT_ALL, "my_task");
  }
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
  if (events[0].metadata.number_elements > 0 && events[0].metadata.data_type == EDAT_INT) {
    printf("[%d] Hello world %d from %d!\n", edatGetRank(), *((int *) events[0].data), events[0].metadata.source);
  } else {
    printf("Incorrect message\n");
  }
}
