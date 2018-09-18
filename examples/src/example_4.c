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
* This example illustrates multiple event dependencies, where the task on rank 0 depends on two events for running - the first is an event from rank 0 (i.e. itself)
* with event ID "a" and the second is an event from rank 1 with event id "b". Rank 0 then fires an event to itself and in parallel rank 1 fires an event to rank 0 too.
* The task will display the number of events and iterates through them, displaying some meta data and the associated payload data.
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);

int main() {
  edatInit();
  int myval=(edatGetRank() + 100)* 10;
  if (edatGetRank() == 0) {
    edatSubmitTask(my_task, 2, 0, "a", 1, "b");
    edatFireEvent(&myval, EDAT_INT, 1, 0, "a");
  } else if (edatGetRank() == 1) {
    edatFireEvent(&myval, EDAT_INT, 1, 0, "b");
  }
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
  printf("Number of events %d\n", num_events);
  int i=0;
  for (i=0;i<num_events;i++) {
    printf("Item %d from %d with UUID %s: %d\n", i, events[i].metadata.source, events[i].metadata.event_id, *((int*) events[i].data));
  }
}
