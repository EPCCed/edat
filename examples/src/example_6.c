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
* This example illustrates persistent tasks, which (unlike transient tasks) do not de-register once they are eligable for execution but instead stick around and can be
* run multiple times. Multiple events are fired from rank 1 to rank 0, rank 0 will submit a persistent task and the way the dependencies have been expressed
* in this example means that a copy of the task is run for each (of 20) event. Note there is a short delay before rank 0 submits its persistent task, this is to
* queue up multiple events on that rank to ensure it handles this mode of operation OK.
*/

#include <stdio.h>
#include <unistd.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);

int main() {
  edatInit();
  if (edatGetRank() == 0) {
    usleep(1000); // Waiting here to queue up multiple events to ensure it handles that correctly
    edatSubmitPersistentTask(my_task, 1, 1, "a");
  } else if (edatGetRank() == 1) {
    int i;
    for (i=0;i<20;i++) {
      edatFireEvent(&i, EDAT_INT, 1, 0, "a");
    }
  }
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
  printf("[%d] Fired\n", *((int*) events[0].data));
}
