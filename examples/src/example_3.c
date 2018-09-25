/*
* This example illustrates greedy persistent tasks which will consume any number of events up to a threshold. This is useful for
* making very fine grained tasks more granular. The example also demonstrates the construction of code level configuration and feeding
* this into EDAT. Note that exported environment variables are given higher precidence than code level configuration options.
*/

#include <stdio.h>
#include <unistd.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  char *keys[3], *values[3];

  keys[0]="EDAT_BATCH_EVENTS";
  values[0]="true";
  keys[1]="EDAT_MAX_BATCHED_EVENTS";
  values[1]="5";
  keys[2]="EDAT_BATCHING_EVENTS_TIMEOUT";
  values[2]="0.01";

  edatInitWithConfiguration(3, keys, values);
  if (edatGetRank() == 0) {
    usleep(1000); // Put this in to test with scheduling the tasks when the events are already there
    edatSubmitPersistentGreedyTask(my_task, 1, EDAT_ANY, "my_task");
  } else if (edatGetRank() == 1) {
    for (int i=0;i<10;i++) {
      edatFireEvent(&i, EDAT_INT, 1, 0, "my_task");
    }
  }
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
  printf("Invoked my_task with %d events\n", num_events);
  for (int i=0;i<num_events;i++) {
    printf("Event %d with key %s and data %d\n", i, events[i].metadata.event_id, *((int*) events[i].data));
  }
}
