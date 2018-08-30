/*
* This example illustrates persistent tasks, which (unlike transient tasks) do not de-register once they are eligable for execution but instead stick around and can be
* run multiple times. Multiple events are fired from rank 1 to rank 0, rank 0 will submit a persistent task and the way the dependencies have been expressed
* in this example means that a copy of the task is run for each (of 20) event. Note there is a short delay before rank 0 submits its persistent task, this is to
* queue up multiple events on that rank to ensure it handles this mode of operation OK.
*/

#include <stdio.h>
#include <unistd.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);

int main() {
  edatInit();
  if (edatGetRank() == 0) {
    usleep(1000); // Waiting here to queue up multiple events to ensure it handles that correctly
    edatSubmitPersistentTask(my_task, 1, 1, "a");
  } else if (edatGetRank() == 1) {
    int i;
    for (i=0;i<20;i++) {
      edatFireEvent(&i, EDAT_INT, 1, 0, "a");
    }
  }
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
  printf("[%d] Fired\n", *((int*) events[0].data));
}
