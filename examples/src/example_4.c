#include "edat.h"
#include <stddef.h>
#include <stdio.h>

void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv);
  int myval=(edatGetRank() + 100)* 10;
  if (edatGetRank() == 0) {
    edatScheduleTask(my_task, 2, 0, "a", 1, "b");
    edatFireEvent(&myval, EDAT_INT, 1, 0, "a");
  } else {
    edatFireEvent(&myval, EDAT_INT, 1, 0, "b");
  }
  edatFinalise();
  return 0;
}

void my_task(EDAT_Event * events, int num_events) {
  printf("Number of events %d\n", num_events);
  int i=0;
  for (i=0;i<num_events;i++) {
    printf("Item %d from %d with UUID %s: %d\n", i, events[i].metadata.source, events[i].metadata.event_id, *((int*) events[i].data));
  }
}
