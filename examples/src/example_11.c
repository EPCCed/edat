#include "edat.h"
#include <stddef.h>
#include <stdio.h>

void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv, NULL);
  if (edatGetRank() == 0) {
    edatScheduleTask(my_task, 0);
    EDAT_Event * events = edatWait(2, 1, "hello", EDAT_SELF, "fireback");
    printf("Passed wait first size is %d and second size is %d element(s)\n", events[0].metadata.number_elements, events[1].metadata.number_elements);
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "taskwaiter");
  } else if (edatGetRank() == 1) {
    int d=44;
    edatFireEvent(&d, EDAT_INT, 1, 0, "hello");
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
