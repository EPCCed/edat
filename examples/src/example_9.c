#include "edat.h"
#include <stddef.h>
#include <stdio.h>

void my_task(EDAT_Event*, int);
void task2(EDAT_Event*, int);

int id;

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv, NULL);
  if (edatGetRank() == 0) {
	  id =0;
    edatSchedulePersistentTask(my_task, 8, EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt");
	  edatSchedulePersistentTask(task2, 1, EDAT_SELF, "fire");

	  for (int i=0;i<8;i++) {
		  edatFireEvent(&i, EDAT_INT, 1,  EDAT_SELF, "fire");
	  }
  }
  edatFinalise();
  return 0;
}

void task2(EDAT_Event * events, int num_events) {
 	edatFireEvent(&id, EDAT_INT, 1,  EDAT_SELF, "evt");
}

void my_task(EDAT_Event * events, int num_events) {
	for (int i=0;i<num_events;i++) {
		int val = *((int *)events[i].data);
		if (val != id) printf("Miss match on %d %d\n", id, val);
	}
	id++;
  if (id < 10000) {
	  for (int i=0;i<8;i++) {
		  edatFireEvent(NULL, EDAT_NOTYPE, 0,  EDAT_SELF, "fire");
	  }
	}
}

