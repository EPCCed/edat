#include "edat.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

void my_task(EDAT_Event*, int);
void reflux_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv, NULL);
  if (edatGetRank() == 0) {
    int * d = (int*) malloc(sizeof(int) * 10);
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

void my_task(EDAT_Event * events, int num_events) {
  if (events[0].metadata.number_elements > 0 && events[0].metadata.data_type == EDAT_INT) {
    int i;
    for (i=0;i<events[0].metadata.number_elements;i++) {
      printf("[%d] %d=%d\n", edatGetRank(), i, ((int *) events[0].data)[i]);
    }
  }
}

void reflux_task(EDAT_Event * events, int num_events) {
  printf("Done\n");
  free(events[0].data);
}
