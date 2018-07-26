/*
* This example illustrates chaining of tasks, where we have a persistent task with 8 dependencies, a single matching dependency fired from the "task2" task only. Initially rank 0
* will fire eight events of id "fire", which causes "task2" to execute 8 times on a worker and effectively fire an event with id "evt" eight times. Each of these "evt" events has as
* payload data an integer ID value which is then tested in the "my_task" task to ensure there is no missmatch between event order which should be strict in the semantics of EDAT.
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);
static void task2(EDAT_Event*, int);

int id;

int main(int argc, char * argv[]) {
  const task_ptr_t task_array[2] = {my_task, task2};
  int print_id = 0;
  edatInit(&argc, &argv, NULL, task_array, 2);
  if (edatGetRank() == 0) {
    print_id = 1;
	  id =0;
    edatSchedulePersistentTask(my_task, 8, EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt");
	  edatSchedulePersistentTask(task2, 1, EDAT_SELF, "fire");

	  for (int i=0;i<8;i++) {
		  edatFireEvent(&i, EDAT_INT, 1,  EDAT_SELF, "fire");
	  }
  }
  edatFinalise();

  if (print_id) printf("id = %d\n", id);

  return 0;
}

static void task2(EDAT_Event * events, int num_events) {
 	edatFireEvent(&id, EDAT_INT, 1,  EDAT_SELF, "evt");
}

static void my_task(EDAT_Event * events, int num_events) {
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
