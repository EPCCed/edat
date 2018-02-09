/*
 * misc.cpp
 *
 *  Created on: 4 Apr 2016
 *      Author: nick
 */

#include <stdio.h>
#include <stdlib.h>

void raiseError(const char* errorMessage) {
  fprintf(stderr, "%s\n", errorMessage);
  abort();
}
