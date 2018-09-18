/*
* Copyright (c) 2018, EPCC, The University of Edinburgh
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*   list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its
*    contributors may be used to endorse or promote products derived from
*    this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
* This example illustrates the edatWait call which will pause the current task (including the main thread) until the event dependencies have been met. This means
* that a task can start running, do some work and then wait until further dependencies are met whilst maintaing all the context (i.e. internal variables) of that task.
* The wait will efficiently pause the task (i.e. it is put to sleep and the worker can be reused to run other tasks.) In this example the main thread on rank 0 waits and is
* reactivated when rank 1 sends an event and rank 0 sends itself an event (running in a separate, "my_task", task concurrently.) This "my_task" itself waits for the "taskwaiter"
* event from all ranks before it can reactivate.
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit();
  if (edatGetRank() == 0) {
    edatSubmitTask(my_task, 0);
    EDAT_Event * events = edatWait(2, 1, "hello", EDAT_SELF, "fireback");
    printf("Passed wait first size is %d and second size is %d element(s)\n", events[0].metadata.number_elements, events[1].metadata.number_elements);
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "taskwaiter");
  } else if (edatGetRank() == 1) {
    int d=44;
    edatFireEvent(&d, EDAT_INT, 1, 0, "hello");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, 0, "taskwaiter");
  }
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "fireback");
  printf("Pause task\n");
  edatWait(1, EDAT_ALL, "taskwaiter");
  printf("Passed wait in task\n");
}
