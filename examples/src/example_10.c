/*
* This example illustrates persistent events, where task "my_task" on rank 0 depends on two events before it can be executed by a worker. Then "persistent-evt" event
* is fired from rank 0 to itself, but crucially this is a persistent event and will fire time and time again (so that dependency for "my_task" is always met.)
* Rank 1 will fire two events, both of identifier "my_task" to rank 0 and this effectively causes "my_task" to run twice (as "persistent-evt" is a persistent event.)
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  const task_ptr_t task_array[1] = {my_task};
  edatInit(task_array, 1);
  if (edatGetRank() == 0) {
    edatSubmitPersistentTask(my_task, 2, EDAT_ANY, "my_task", EDAT_SELF, "persistent-evt");
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

static void my_task(EDAT_Event * events, int num_events) {
  if (num_events != 2) {
    printf("[%d] Should have 2 events but has %d", edatGetRank(), num_events);
  }
  printf("[%d] Key: 'my_task' Value: %d\n", edatGetRank(), *((int*) events[edatFindEvent(events, num_events, 1, "my_task")].data));
  printf("[%d] Key: 'persistent-evt' Value: %d\n", edatGetRank(), *((int*) events[edatFindEvent(events, num_events, EDAT_SELF, "persistent-evt")].data));
}
