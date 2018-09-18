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

#include "configuration.h"
#include <map>
#include <string>
#include <algorithm>
#include <string.h>

// These are configuration keys that might be found set in the environment and if so we want to read and store their values
std::string Configuration::envKeys[] = { "EDAT_NUM_WORKERS", "EDAT_MAIN_THREAD_WORKER", "EDAT_REPORT_WORKER_MAPPING", "EDAT_PROGRESS_THREAD" ,
                                        "EDAT_BATCH_EVENTS", "EDAT_MAX_BATCHED_EVENTS", "EDAT_BATCHING_EVENTS_TIMEOUT", "EDAT_ENABLE_BRIDGE"};

/**
* The constructor which will initialise the configuration settings from the environment variables (if set) and then from the provided
* configuration (if provided.)
*/
Configuration::Configuration(int number_entries, char ** keys, char ** values) {
  extractEnvironmentConfigSettings();
  if (keys != NULL && values != NULL) {
    for (int i=0;i<number_entries;i++) {
      std::string keyStr = std::string(keys[i]);
      std::transform(keyStr.begin(), keyStr.end(),keyStr.begin(), ::toupper);
      configSettings.insert(std::pair<std::string, std::string>(keyStr, std::string(values[i])));
    }
  }
}

/**
* Extracts out all the applicable configuration settings from the environment based on the declared array of possible configuration keys
*/
void Configuration::extractEnvironmentConfigSettings() {
  for (std::string envKey : envKeys) {
    if(const char* env_value = std::getenv(envKey.c_str())) {
      if (strlen(env_value) > 0) {
        configSettings.insert(std::pair<std::string, std::string>(envKey, std::string(env_value)));
      }
    }
  }
}

/**
* Retrieves a boolean configuration value with the specified (case in-sensitive) key. If no such value is found then the default
* is returned
*/
bool Configuration::get(const char* name, bool default_value) {
  std::string keyStr = std::string(name);
  std::transform(keyStr.begin(), keyStr.end(),keyStr.begin(), ::toupper);
  std::map<std::string, std::string>::iterator it=configSettings.find(keyStr);
  if (it != configSettings.end()) {
    std::string valueStr = it->second;
    std::transform(valueStr.begin(), valueStr.end(),valueStr.begin(), ::tolower);
    return valueStr == "true";
  }
  return default_value;
}

/**
* Retrieves an unsigned integer configuration value with the specified (case in-sensitive) key. If no such value is found then the default
* is returned
*/
unsigned int Configuration::get(const char* name, unsigned int default_value) {
  std::string keyStr = std::string(name);
  std::transform(keyStr.begin(), keyStr.end(),keyStr.begin(), ::toupper);
  std::map<std::string, std::string>::iterator it=configSettings.find(keyStr);
  if (it != configSettings.end()) {
    return atoi(it->second.c_str());
  }
  return default_value;
}

/**
* Retrieves an integer configuration value with the specified (case in-sensitive) key. If no such value is found then the default
* is returned
*/
int Configuration::get(const char* name, int default_value) {
  std::string keyStr = std::string(name);
  std::transform(keyStr.begin(), keyStr.end(),keyStr.begin(), ::toupper);
  std::map<std::string, std::string>::iterator it=configSettings.find(keyStr);
  if (it != configSettings.end()) {
    return atoi(it->second.c_str());
  }
  return default_value;
}

double Configuration::get(const char* name, double default_value) {
  std::string keyStr = std::string(name);
  std::transform(keyStr.begin(), keyStr.end(),keyStr.begin(), ::toupper);
  std::map<std::string, std::string>::iterator it=configSettings.find(keyStr);
  if (it != configSettings.end()) {
    return atof(it->second.c_str());
  }
  return default_value;
}
