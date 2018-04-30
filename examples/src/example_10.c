#include "edat.h"
#include <stddef.h>
#include <stdio.h>

void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv);
  if (edatGetRank() == 0) {
    edatSchedulePersistentTask(my_task, 2, EDAT_ANY, "my_task", EDAT_SELF, "persistent-evt");
    int j=98;
    edatFirePersistentEvent(&j, EDAT_INT, 1, 0, "persistent-evt");
  } else if (edatGetRank() == 1) {
    int d=33;
    edatFireEvent(&d, EDAT_INT, 1, 0, "my_task");
    edatFireEvent(&d, EDAT_INT, 1, 0, "my_task");
  }
  edatFinalise();
  return 0;
}

void my_task(EDAT_Event * events, int num_events) {
  if (num_events != 2) {
    printf("[%d] Should have 2 events but has %d", edatGetRank(), num_events);
  }
  printf("[%d] Key: 'my_task' Value: %d\n", edatGetRank(), *((int*) events[edatFindEvent(events, num_events, 1, "my_task")].data));
  printf("[%d] Key: 'persistent-evt' Value: %d\n", edatGetRank(), *((int*) events[edatFindEvent(events, num_events, EDAT_SELF, "persistent-evt")].data));
}
