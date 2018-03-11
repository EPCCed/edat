#include "edat.h"
#include <stddef.h>
#include <stdio.h>

void my_task(EDAT_Event*, int);
void barrier_task(EDAT_Event* , int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv);
  int myval=(edatGetRank() + 100)* 10;
  if (edatGetRank() == 0) {
    edatScheduleTask(my_task, 1, EDAT_ALL, "a");
    edatFireEvent(&myval, EDAT_INT, 1, 0, "a");
  } else {
    edatFireEvent(&myval, EDAT_INT, 1, 0, "a");
  }
  edatScheduleTask(barrier_task, 1, EDAT_ALL, "barrier");
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_ALL, "barrier");
  edatFinalise();
  return 0;
}

void my_task(EDAT_Event * events, int num_events) {
  printf("[%d] Number of events %d\n", edatGetRank(), num_events);
  int i=0;
  for (i=0;i<num_events;i++) {
    printf("[%d] Item %d from %d with UUID %s: %d\n", edatGetRank(), i, events[i].metadata.source, events[i].metadata.unique_id, *((int*) events[i].data));
  }
}

void barrier_task(EDAT_Event * events, int num_events) {
  printf("[%d] Barrier with %d events\n", edatGetRank(), num_events);
}
