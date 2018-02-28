#include "edat.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

void my_task(void*, EDAT_Metadata);
void reflux_task(void *, EDAT_Metadata);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv);
  if (edatGetRank() == 0) {
    int * d = (int*) malloc(sizeof(int) * 10);
    int i;
    for (i=0;i<10;i++) {
      d[i]=i;
    }
    edatFireEventWithReflux(d, EDAT_INT, 10, 1, "my_task", reflux_task);
  } else if (edatGetRank() == 1) {
    edatScheduleTask(my_task, "my_task");
  }
  edatFinalise();
  return 0;
}

void my_task(void * data, EDAT_Metadata metadata) {
  if (metadata.number_elements > 0 && metadata.data_type == EDAT_INT) {
    int i;
    for (i=0;i<metadata.number_elements;i++) {
      printf("[%d] %d=%d\n", edatGetRank(), i, ((int *) data)[i]);
    }
  }
}

void reflux_task(void * data, EDAT_Metadata metadata) {
  printf("Done\n");
  free(data);
}
