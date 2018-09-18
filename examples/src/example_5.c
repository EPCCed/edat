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
* This example illustrates the EDAT_ALL rank modifier, rank 0 will submit a task (my_task) that depends on an event with id "a" from all ranks. Once the ranks have fired their
* corresponding events, another task "barrier" is submitted by all ranks and then all ranks fire an associated event to every rank with the identifier "barrier". This second
* pattern is an example of a (non-blocking) barrier as the task is eligable for execution on each rank at the same local point in the main function (i.e. after the barrier
* events have been fired.) Remember the submission of tasks and firing of events is non-blocking.
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);
static void barrier_task(EDAT_Event* , int);

int main() {
  edatInit();
  int myval=(edatGetRank() + 100)* 10;
  if (edatGetRank() == 0) {
    edatSubmitTask(my_task, 1, EDAT_ALL, "a");
    edatFireEvent(&myval, EDAT_INT, 1, 0, "a");
  } else {
    edatFireEvent(&myval, EDAT_INT, 1, 0, "a");
  }
  edatSubmitTask(barrier_task, 1, EDAT_ALL, "barrier");
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_ALL, "barrier");
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
  printf("[%d] Number of events %d\n", edatGetRank(), num_events);
  int i=0;
  for (i=0;i<num_events;i++) {
    printf("[%d] Item %d from %d with UUID %s: %d\n", edatGetRank(), i, events[i].metadata.source, events[i].metadata.event_id, *((int*) events[i].data));
  }
}

static void barrier_task(EDAT_Event * events, int num_events) {
  printf("[%d] Barrier with %d events\n", edatGetRank(), num_events);
}
