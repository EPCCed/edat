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
* This provides an example of a persistent task called with a specific piece of shared data (the double memory on the heap) which is passed as an address so that the tasks can
* access the same physical memory. The adder task is called any number of times (based on the num_fired event) and then the report task runs at the end. Note that we provide
* two approaches here, the first will send the data with a different event id to the report task (as the adder persistent task is still scheduled and so will likely consume that
* event which we don't want.) The second approach names this persistent event and then explicitly deschedules it to avoid any further event consumption.
*/

#include <stdlib.h>
#include <stdio.h>
#include "edat.h"

#define APPROACH 2

static void accumulation_task(EDAT_Event*, int);
static void report_task(EDAT_Event*, int);

int main() {
  edatInit();
  double * data = (double*) malloc(sizeof(double) * 200);
  int i;
  for (i=0;i<200;i++) data[i]=0;
#if APPROACH == 1
  edatSubmitPersistentTask(accumulation_task, 2, EDAT_SELF, "local_data", EDAT_SELF, "num_fired");
#elif APPROACH == 2
  edatSubmitPersistentNamedTask(accumulation_task, "adder", 2, EDAT_SELF, "local_data", EDAT_SELF, "num_fired");
#endif
  edatFireEvent(&data, EDAT_ADDRESS, 1, EDAT_SELF, "local_data");
  int num_fired=0;
  edatFireEvent(&num_fired, EDAT_INT, 1, EDAT_SELF, "num_fired");
  edatFinalise();
  return 0;
}

static void accumulation_task(EDAT_Event * events, int num_events) {
  int data_index=edatFindEvent(events, num_events, EDAT_SELF, "local_data");
  int num_fired_index=edatFindEvent(events, num_events, EDAT_SELF, "num_fired");

  int num_fired=*((int*) events[num_fired_index].data);
  int i;
  if (num_fired < 50) {
    num_fired++;
    edatFireEvent(&num_fired, EDAT_INT, 1, EDAT_SELF, "num_fired");

    double * data = *((double **) events[data_index].data);
    for (i=0;i<200;i++) data[i]+=1;
    edatFireEvent(events[data_index].data, EDAT_ADDRESS, 1, EDAT_SELF, "local_data");
  } else {
#if APPROACH == 1
    edatSubmitTask(report_task, 2, EDAT_SELF, "local_data_2", EDAT_SELF, "report_data");
    edatFireEvent(events[data_index].data, EDAT_ADDRESS, 1, EDAT_SELF, "local_data_2");
#elif APPROACH == 2
    edatRemoveTask("adder");
    edatSubmitTask(report_task, 2, EDAT_SELF, "local_data", EDAT_SELF, "report_data");
    edatFireEvent(events[data_index].data, EDAT_ADDRESS, 1, EDAT_SELF, "local_data");
#endif
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "report_data");
  }
}

static void report_task(EDAT_Event * events, int num_events) {
#if APPROACH == 1
  int data_index=edatFindEvent(events, num_events, EDAT_SELF, "local_data_2");
#elif APPROACH == 2
  int data_index=edatFindEvent(events, num_events, EDAT_SELF, "local_data");
#endif
  int i;
  double * data = *((double **) events[data_index].data);
  for (i=0;i<200;i++) {
    printf("[%d] Value is %f\n", i, data[i]);
  }
}
