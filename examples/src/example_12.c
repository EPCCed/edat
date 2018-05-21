#include "edat.h"
#include <stddef.h>
#include <stdio.h>

void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv, NULL);
  if (edatGetRank() == 0) {
    edatScheduleTask(my_task, 1, EDAT_ANY, "my_task");
  } else if (edatGetRank() == 1) {
    edatFireEvent(NULL, EDAT_NOTYPE, 0, 0, "my_task");
  }

  edatPauseMainThread();
  printf("Unpause\n");

  if (edatGetRank() == 0) {
    edatScheduleTask(my_task, 1, EDAT_ANY, "my_task");
  } else if (edatGetRank() == 1) {
    edatFireEvent(NULL, EDAT_NOTYPE, 0, 0, "my_task");
  }

  edatFinalise();
  return 0;
}

void my_task(EDAT_Event * events, int num_events) {
  printf("Hello world!\n");
}

