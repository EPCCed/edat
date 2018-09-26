/*
* This example illustrates the edatPauseMainThread call, this is in the debug part of EDAT and specifically designed for debugging or benchmarking work and not for
* end user interaction. Effectively this call puts to sleep (i.e. the underlying worker can be reused) the main thread and it is only reactivated when all EDAT activity
* ceases (i.e. no tasks, no outstanding events etc) and execution can continue.
*/

#include <stdio.h>
#include "edat.h"
#include "edat_debug.h"

static void my_task(EDAT_Event*, int);

int main() {
  const task_ptr_t task_array[1] = {my_task};
  edatInit(task_array, 1);
  if (edatGetRank() == 0) {
    edatSubmitTask(my_task, 1, EDAT_ANY, "my_task");
  } else if (edatGetRank() == 1) {
    edatFireEvent(NULL, EDAT_NOTYPE, 0, 0, "my_task");
  }

  edatPauseMainThread();
  printf("Unpause\n");

  if (edatGetRank() == 0) {
    edatSubmitTask(my_task, 1, EDAT_ANY, "my_task");
  } else if (edatGetRank() == 1) {
    edatFireEvent(NULL, EDAT_NOTYPE, 0, 0, "my_task");
  }

  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
  printf("Hello world!\n");
}
