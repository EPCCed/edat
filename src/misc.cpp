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
  if (type == EDAT_INT) return sizeof(int);
  if (type == EDAT_FLOAT) return sizeof(float);
  if (type == EDAT_DOUBLE) return sizeof(double);
  if (type == EDAT_BYTE) return sizeof(char);
  if (type == EDAT_ADDRESS) return sizeof(char*);
  if (type == EDAT_NOTYPE) return 0;
  fprintf(stderr, "Error in type matching\n");
  return -1;
}
