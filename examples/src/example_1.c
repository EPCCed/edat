#include "edat.h"
#include <stddef.h>
#include <stdio.h>

void my_task(void*, EDAT_Metadata);

int main(int argc, char * argv[]) {
  edatInit(&argc, &argv);
  int d;
  if (edatGetRank() == 0) {
    edatScheduleTask(my_task, "my_task");
  } else if (edatGetRank() == 1) {
    d=33;
    edatFireEvent(&d, EDAT_INT, 1, 0, "my_task");
  }
  edatFinalise();
  return 0;
}

void my_task(void * data, EDAT_Metadata metadata) {
  if (metadata.number_elements > 0 && metadata.data_type == EDAT_INT) {
    printf("Hello world %d!\n", *((int *) data));
  } else {
    printf("Hello world!\n");
  }
}
