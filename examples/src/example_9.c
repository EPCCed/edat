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
* This example illustrates chaining of tasks, where we have a persistent task with 8 dependencies, a single matching dependency fired from the "task2" task only. Initially rank 0
* will fire eight events of id "fire", which causes "task2" to execute 8 times on a worker and effectively fire an event with id "evt" eight times. Each of these "evt" events has as
* payload data an integer ID value which is then tested in the "my_task" task to ensure there is no missmatch between event order which should be strict in the semantics of EDAT.
*/

#include <stdio.h>
#include "edat.h"

static void my_task(EDAT_Event*, int);
static void task2(EDAT_Event*, int);

int id;

int main() {
  edatInit();
  if (edatGetRank() == 0) {
	  id =0;
    edatSubmitPersistentTask(my_task, 8, EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt", EDAT_SELF, "evt");
	  edatSubmitPersistentTask(task2, 1, EDAT_SELF, "fire");

	  for (int i=0;i<8;i++) {
		  edatFireEvent(&i, EDAT_INT, 1,  EDAT_SELF, "fire");
	  }
  }
  edatFinalise();
  return 0;
}

static void task2(EDAT_Event * events, int num_events) {
 	edatFireEvent(&id, EDAT_INT, 1,  EDAT_SELF, "evt");
}

static void my_task(EDAT_Event * events, int num_events) {
	for (int i=0;i<num_events;i++) {
		int val = *((int *)events[i].data);
		if (val != id) printf("Miss match on %d %d\n", id, val);
	}
	id++;
  if (id < 10000) {
	  for (int i=0;i<8;i++) {
		  edatFireEvent(NULL, EDAT_NOTYPE, 0,  EDAT_SELF, "fire");
	  }
	}
}

