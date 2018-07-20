/*
* This provides a very simple example of scheduling tasks and firing events between them. Initially a task is scheduled on rank 0 with a single dependency from any process with the
* event identifier "my_task". On rank 1 another task is scheduled, similarly but this time with EID "my_task2". Rank 1 fires an event with a single integer payload data to rank 0
* which will cause the execution of the scheduled task on rank 0. This task will display a message and fires an event to rank 1 which will make eligable for execution the
* task scheduled on rank 1. Remember the scheduling of tasks and firing of events is non-blocking.
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);
static void my_task2(EDAT_Event*, int);

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

static void my_task(EDAT_Event * events, int num_events) {
  if (events[0].metadata.number_elements > 0 && events[0].metadata.data_type == EDAT_INT) {
    printf("Hello world from %d with data %d!\n", edatGetRank(), *((int *) events[0].data));
  } else {
    printf("Hello world!\n");
  }
  edatFireEvent(NULL, EDAT_NOTYPE, 0, 1, "my_task2");
}

static void my_task2(EDAT_Event * events, int num_events) {
  printf("Task two running on %d\n", edatGetRank());
}
