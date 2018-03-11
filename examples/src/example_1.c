#include "edat.h"
#include <stddef.h>
#include <stdio.h>

void my_task(EDAT_Event*, int);
void my_task2(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv);
  if (edatGetRank() == 0) {
    edatScheduleTask(my_task, "my_task");
  } else if (edatGetRank() == 1) {
    edatScheduleTask(my_task2, "my_task2");
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
