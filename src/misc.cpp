/*
 * misc.cpp
 *
 *  Created on: 4 Apr 2016
 *      Author: nick
 */

#include <stdio.h>
#include <stdlib.h>
#include "misc.h"
#include "edat.h"

void raiseError(const char* errorMessage) {
  fprintf(stderr, "%s\n", errorMessage);
  abort();
}

int getTypeSize(int type) {
  if (type == EDAT_INT) return 4;
  if (type == EDAT_FLOAT) return 4;
  if (type == EDAT_DOUBLE) return 8;
  if (type == EDAT_BYTE) return 1;
  if (type == EDAT_NOTYPE) return 0;
  fprintf(stderr, "Error in type matching\n");
  return -1;
}
