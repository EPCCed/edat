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
* This provides a very simple example of submitting tasks and firing events between them. Initially a task is submitted on rank 0 with a single dependency from any process with the
* event identifier "my_task". On rank 1 another task is submitted, similarly but this time with EID "my_task2". Rank 1 fires an event with a single integer payload data to rank 0
* which will cause the execution of the outstanding task on rank 0. This task will display a message and fires an event to rank 1 which will make eligable for execution the
* task submitted on rank 1. Remember the submission of tasks and firing of events is non-blocking.
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);
static void my_task2(EDAT_Event*, int);

int main() {
  edatInit();
  if (edatGetRank() == 0) {
    edatSubmitTask(my_task, 1, EDAT_ANY, "my_task");
  } else if (edatGetRank() == 1) {
    edatSubmitTask(my_task2, 1, EDAT_ANY, "my_task2");
    int d=33;
    edatFireEvent(&d, EDAT_INT, 1, 0, "my_task");
  }
  edatFinalise();
  return 0;
}

static void my_task(EDAT_Event * events, int num_events) {
  if (events[0].metadata.number_elements > 0 && events[0].metadata.data_type == EDAT_INT) {
    printf("Hello world from %d with data %d!\n", edatGetRank(), *((int *) events[0].data));
  } else {
    printf("Hello world!\n");
  }
  edatFireEvent(NULL, EDAT_NOTYPE, 0, 1, "my_task2");
}

static void my_task2(EDAT_Event * events, int num_events) {
  printf("Task two running on %d\n", edatGetRank());
}
