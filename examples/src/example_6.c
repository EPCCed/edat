#include "edat.h"
#include <stddef.h>
#include <stdio.h>

void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv);
  if (edatGetRank() == 0) {
    edatSchedulePersistentTask(my_task, 1, 1, "a");
  } else if (edatGetRank() == 1) {
    int i;
    for (i=0;i<20;i++) {
      edatFireEvent(&i, EDAT_INT, 1, 0, "a");
    }
  }
  edatFinalise();
  return 0;
}

void my_task(EDAT_Event * events, int num_events) {
  printf("[%d] Fired\n", *((int*) events[0].data));
}
