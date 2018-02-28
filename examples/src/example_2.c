#include "edat.h"
#include <stddef.h>
#include <stdio.h>

void my_task(void*, EDAT_Metadata);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv);
  edatScheduleTask(my_task, "my_task");
  int d;
  if (edatGetRank() == 0) {
    d=33;
    edatFireEvent(&d, EDAT_INT, 1, EDAT_ALL, "my_task");
  }
  edatFinalise();
  return 0;
}

void my_task(void * data, EDAT_Metadata metadata) {
  if (metadata.number_elements > 0 && metadata.data_type == EDAT_INT) {
    printf("[%d] Hello world %d from %d!\n", edatGetRank(), *((int *) data), metadata.source);
  } else {
    printf("Incorrect message\n");
  }
}
