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
* This example illustrates persistent events, where task "my_task" on rank 0 depends on two events before it can be executed by a worker. Then "persistent-evt" event
* is fired from rank 0 to itself, but crucially this is a persistent event and will fire time and time again (so that dependency for "my_task" is always met.)
* Rank 1 will fire two events, both of identifier "my_task" to rank 0 and this effectively causes "my_task" to run twice (as "persistent-evt" is a persistent event.)
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit();
  if (edatGetRank() == 0) {
    edatSubmitPersistentTask(my_task, 2, EDAT_ANY, "my_task", EDAT_SELF, "persistent-evt");
    int j=98;
    edatFirePersistentEvent(&j, EDAT_INT, 1, 0, "persistent-evt");
  } else if (edatGetRank() == 1) {
    int d=33;
    edatFireEvent(&d, EDAT_INT, 1, 0, "my_task");
    edatFireEvent(&d, EDAT_INT, 1, 0, "my_task");
  }
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
  if (num_events != 2) {
    printf("[%d] Should have 2 events but has %d", edatGetRank(), num_events);
  }
  printf("[%d] Key: 'my_task' Value: %d\n", edatGetRank(), *((int*) events[edatFindEvent(events, num_events, 1, "my_task")].data));
  printf("[%d] Key: 'persistent-evt' Value: %d\n", edatGetRank(), *((int*) events[edatFindEvent(events, num_events, EDAT_SELF, "persistent-evt")].data));
}
