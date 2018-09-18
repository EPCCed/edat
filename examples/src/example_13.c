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
* This example provides an illustration of using the concurrency control mechanisms of lock, unlock and testlock. Two tasks will run (but the task function), we fire
* them with different integers just for tracking in the printing out of information. Then each task will do a series of locks, unlocks and testlock. Note that on task
* completion all locks that are current acquired are automatically released, hence in this case mylock2, even though it is never explicitly unlocked
* will be implicitly unlocked when the task completes.
*/

#include <stdio.h>
#include "edat.h"

static void task(EDAT_Event*, int);

int main() {
  edatInit();
  edatSubmitTask(task, 1, EDAT_SELF, "task_num");
  edatSubmitTask(task, 1, EDAT_SELF, "task_num");
  int t=0;
  edatFireEvent(&t, 1, EDAT_INT, EDAT_SELF, "task_num");
  t=1;
  edatFireEvent(&t, 1, EDAT_INT, EDAT_SELF, "task_num");
  edatFinalise();
	return 0;
}

static void task(EDAT_Event * events, int numevents) {
  int task_id=*((int*) events[0].data);
  edatLock("mylock");
  printf("[%d] Got lock num one\n", task_id);
  printf("[%d] I have got the first lock before unlock: %s\n", task_id, edatTestLock("mylock") ? "true" : "false");
  edatUnlock("mylock");
  printf("[%d] I have got the first lock after unlock: %s\n", task_id, edatTestLock("mylock") ? "true" : "false");
  edatLock("mylock2");
  printf("[%d] Leaving task with mylock2\n", task_id);
}
