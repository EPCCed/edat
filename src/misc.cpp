/*
 * misc.cpp
 *
 *  Created on: 4 Apr 2016
 *      Author: nick
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "misc.h"
#include "edat.h"

/**
* Displays an error message to stderror and aborts
*/
void raiseError(const char* errorMessage) {
  fprintf(stderr, "%s\n", errorMessage);
  abort();
}

/**
* Retrieves the size of a specific in built type in bytes
*/
int getBaseTypeSize(int type) {
  if (type == EDAT_INT) return sizeof(int);
  if (type == EDAT_FLOAT) return sizeof(float);
  if (type == EDAT_DOUBLE) return sizeof(double);
  if (type == EDAT_BYTE) return sizeof(char);
  if (type == EDAT_ADDRESS) return sizeof(char*);
  if (type == EDAT_NOTYPE) return 0;
  raiseError("Error in type matching\n");
  return -1;
}

/**
* Retrieves a boolean environment variable or returns a default value if this is not found
*/
bool getEnvironmentVariable(const char* name, bool default_value) {
  if(const char* env_value = std::getenv(name)) {
    if (strlen(env_value) > 0) {
      return (strcmp(env_value, "true") == 0);
    }
  }
  return default_value;
}

/**
* Retrieves an unsigned int environment variable or returns a default value if this is not found
*/
unsigned int getEnvironmentVariable(const char* name, unsigned int default_value) {
  if(const char* env_value = std::getenv(name)) {
    if (strlen(env_value) > 0) {
      return atoi(env_value);
    }
  }
  return default_value;
}

/**
* Retrieves an integer environment variable or returns a default value if this is not found
*/
int getEnvironmentVariable(const char* name, int default_value) {
  if(const char* env_value = std::getenv(name)) {
    if (strlen(env_value) > 0) {
      return atoi(env_value);
    }
  }
  return default_value;
}
