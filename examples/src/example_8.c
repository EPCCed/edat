#include "edat.h"
#include <stddef.h>
#include <stdio.h>

void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv);
  if (edatGetRank() == 0) {
    edatScheduleTask(my_task, 3, EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt");
    edatFireEvent(NULL, EDAT_NOTYPE, 0,  EDAT_SELF, "evt");
    edatFireEvent(NULL, EDAT_NOTYPE, 0,  EDAT_SELF, "evt");
    edatFireEvent(NULL, EDAT_NOTYPE, 0,  EDAT_SELF, "evt");
  }
  edatFinalise();
  return 0;
}

void my_task(EDAT_Event * events, int num_events) {
	printf("Task fired!\n");
}
