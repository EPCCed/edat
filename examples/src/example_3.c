/*
* This example demonstrates firing events with a reflux task. A reflux task is a task that is executed locally (i.e. on the rank that fired the event) when that event
* has been delivered to the target rank. The corresponding reflux task is then eligable for execution once the event has arrived at the target rank.
*/

#include <stdio.h>
#include <stdlib.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);
static void reflux_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  const task_ptr_t task_array[2] = {my_task, reflux_task};
  edatInit(&argc, &argv, NULL, task_array);
  if (edatGetRank() == 0) {
    int * d = (int*) malloc(sizeof(int) * 10);
    printf("[%d] main, ptr = %p\n", edatGetRank(), d);
    int i;
    for (i=0;i<10;i++) {
      d[i]=i;
    }
    edatFireEventWithReflux(d, EDAT_INT, 10, 1, "my_task", reflux_task);
  } else if (edatGetRank() == 1) {
    edatScheduleTask(my_task, 1, EDAT_ANY, "my_task");
  }
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
  if (events[0].metadata.number_elements > 0 && events[0].metadata.data_type == EDAT_INT) {
    int i;
    for (i=0;i<events[0].metadata.number_elements;i++) {
      printf("[%d] %d=%d\n", edatGetRank(), i, ((int *) events[0].data)[i]);
    }
  }
}

static void reflux_task(EDAT_Event * events, int num_events) {
  printf("Done\n");
  free(events[0].data);
}
