/*
* A simple example where rank 0 depends on multiple identical tasks (the same source and event identifier.) These are fired, whilst each has the same signature
* (source and identifier), as the task effectively consumes events then this will work fine and each is considered a separate event (i.e. there are 3 events
* provided to the task when it runs.)
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);

int main() {
  edatInit();
  if (edatGetRank() == 0) {
    edatScheduleTask(my_task, 3, EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt");
    edatFireEvent(NULL, EDAT_NOTYPE, 0,  EDAT_SELF, "evt");
    edatFireEvent(NULL, EDAT_NOTYPE, 0,  EDAT_SELF, "evt");
    edatFireEvent(NULL, EDAT_NOTYPE, 0,  EDAT_SELF, "evt");
  }
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
	printf("Task fired!\n");
}
