#include "edat.h"
#include <stddef.h>
#include <stdio.h>

void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv, NULL);
  if (edatGetRank() == 0) {
    edatScheduleTask(my_task, 0);
    edatWait(2, 1, "hello", EDAT_SELF, "fireback");
    printf("Passed wait\n");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "taskwaiter");
  } else if (edatGetRank() == 1) {
    edatFireEvent(NULL, EDAT_NOTYPE, 0, 0, "hello");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, 0, "taskwaiter");
  }
  edatFinalise();
  return 0;
}

void my_task(EDAT_Event * events, int num_events) {
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "fireback");
  printf("Pause task\n");
  edatWait(1, EDAT_ALL, "taskwaiter");
  printf("Passed wait in task\n");
}
