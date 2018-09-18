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
* This example illustrates the edatPauseMainThread call, this is in the debug part of EDAT and specifically designed for debugging or benchmarking work and not for
* end user interaction. Effectively this call puts to sleep (i.e. the underlying worker can be reused) the main thread and it is only reactivated when all EDAT activity
* ceases (i.e. no tasks, no outstanding events etc) and execution can continue.
*/

#include <stdio.h>
#include "edat.h"
#include "edat_debug.h"

static void my_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv, NULL);
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

