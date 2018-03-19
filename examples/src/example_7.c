#include "edat.h"
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

void accumulation_task(EDAT_Event*, int);
void report_task(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv);
  double * data = (double*) malloc(sizeof(double) * 200);
  for (int i=0;i<200;i++) data[i]=0;
  edatSchedulePersistentTask(accumulation_task, 2, EDAT_SELF, "local_data", EDAT_SELF, "num_fired");
  edatFireEvent(&data, EDAT_ADDRESS, 1, EDAT_SELF, "local_data");
  int num_fired=0;
  edatFireEvent(&num_fired, EDAT_INT, 1, EDAT_SELF, "num_fired");
  edatFinalise();
  return 0;
}

void accumulation_task(EDAT_Event * events, int num_events) {
  int data_index=edatFindEvent(events, num_events, EDAT_SELF, "local_data");
  int num_fired_index=edatFindEvent(events, num_events, EDAT_SELF, "num_fired");

  int num_fired=*((int*) events[num_fired_index].data);
  if (num_fired < 50) {
    num_fired++;
    edatFireEvent(&num_fired, EDAT_INT, 1, EDAT_SELF, "num_fired");

    double * data = *((double **) events[data_index].data);
    for (int i=0;i<200;i++) data[i]+=1;
    edatFireEvent(events[data_index].data, EDAT_ADDRESS, 1, EDAT_SELF, "local_data");
  } else {
    edatScheduleTask(report_task, 2, EDAT_SELF, "local_data2", EDAT_SELF, "report_data");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "report_data");
    edatFireEvent(events[data_index].data, EDAT_ADDRESS, 1, EDAT_SELF, "local_data2");
  }
}

void report_task(EDAT_Event * events, int num_events) {
  int data_index=edatFindEvent(events, num_events, EDAT_SELF, "local_data2");
  double * data = *((double **) events[data_index].data);
  for (int i=0;i<200;i++) {
    printf("[%d] Value is %f\n", i, data[i]);
  }
}
