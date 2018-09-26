/*
* This example provides an illustration of using the concurrency control mechanisms of lock, unlock and testlock. Two tasks will run (but the task function), we fire
* them with different integers just for tracking in the printing out of information. Then each task will do a series of locks, unlocks and testlock. Note that on task
* completion all locks that are current acquired are automatically released, hence in this case mylock2, even though it is never explicitly unlocked
* will be implicitly unlocked when the task completes.
*/

#include <stdio.h>
#include "edat.h"

static void task(EDAT_Event*, int);

int main() {
  const task_ptr_t const task_array[1] = {task};

  edatInit(task_array, 1);
  edatSubmitTask(task, 1, EDAT_SELF, "task_num");
  edatSubmitTask(task, 1, EDAT_SELF, "task_num");
  int t=0;
  edatFireEvent(&t, 1, EDAT_INT, EDAT_SELF, "task_num");
  t=1;
  edatFireEvent(&t, 1, EDAT_INT, EDAT_SELF, "task_num");
  edatFinalise();
	return 0;
}

static void task(EDAT_Event * events, int numevents) {
  int task_id=*((int*) events[0].data);
  edatLock("mylock");
  printf("[%d] Got lock num one\n", task_id);
  printf("[%d] I have got the first lock before unlock: %s\n", task_id, edatTestLock("mylock") ? "true" : "false");
  edatUnlock("mylock");
  printf("[%d] I have got the first lock after unlock: %s\n", task_id, edatTestLock("mylock") ? "true" : "false");
  edatLock("mylock2");
  printf("[%d] Leaving task with mylock2\n", task_id);
}
