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
