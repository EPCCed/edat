/*
* This example illustrates greedy persistent tasks which will consume any number of events up to a threshold. This is useful for
* making very fine grained tasks more granular. The example also demonstrates the construction of code level configuration and feeding
* this into EDAT. Note that exported environment variables are given higher precidence than code level configuration options.
*/
#include <stdio.h>
#include <stdlib.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);
static struct edat_struct_configuration generateConfig();

int main(int argc, char * argv[]) {
  struct edat_struct_configuration config=generateConfig();
  edatInit(&argc, &argv, &config);
  if (edatGetRank() == 0) {
    edatSchedulePersistentGreedyTask(my_task, 1, EDAT_ANY, "my_task");
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

static struct edat_struct_configuration generateConfig() {
  char **keys, **values;
  keys=(char**) malloc(sizeof(char*) * 3);
  values=(char**) malloc(sizeof(char*) * 3);

  keys[0]="EDAT_BATCH_EVENTS";
  values[0]="true";
  keys[1]="EDAT_MAX_BATCHED_EVENTS";
  values[1]="5";
  keys[2]="EDAT_BATCHING_EVENTS_TIMEOUT";
  values[2]="0.0001";
  struct edat_struct_configuration config ={ keys, values, 3};
  return config;
}
