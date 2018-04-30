/*
 * misc.h
 *
 *  Created on: 4 Apr 2016
 *      Author: nick
 */

#ifndef SRC_MISC_H_
#define SRC_MISC_H_

#include <map>
#include <string.h>

void raiseError(const char*);
int getBaseTypeSize(int);
bool getEnvironmentVariable(const char*, bool);
unsigned int getEnvironmentVariable(const char*, unsigned int);
int getEnvironmentVariable(const char*, int);

template<typename T> T getEnvironmentMapVariable(const char * name, std::map<const char*, T> lookupMap, T defaultValue) {
  if(const char* env_value = std::getenv(name)) {
    if (strlen(env_value) > 0) {
      typename std::map<const char*, T>::iterator it=lookupMap.find(env_value);
      if (it != lookupMap.end()) return it->second;
    }
  }
  return defaultValue;
}

#endif /* SRC_MISC_H_ */
