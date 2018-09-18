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
* A simple example where rank 0 depends on multiple identical tasks (the same source and event identifier.) These are fired, whilst each has the same signature
* (source and identifier), as the task effectively consumes events then this will work fine and each is considered a separate event (i.e. there are 3 events
* provided to the task when it runs.)
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);

int main() {
  edatInit();
  if (edatGetRank() == 0) {
    edatSubmitTask(my_task, 3, EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt");
    edatFireEvent(NULL, EDAT_NOTYPE, 0,  EDAT_SELF, "evt");
    edatFireEvent(NULL, EDAT_NOTYPE, 0,  EDAT_SELF, "evt");
    edatFireEvent(NULL, EDAT_NOTYPE, 0,  EDAT_SELF, "evt");
  }
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
	printf("Task fired!\n");
}
