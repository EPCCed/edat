#include "configuration.h"
#include <map>
#include <string>
#include <algorithm>
#include <string.h>

// These are configuration keys that might be found set in the environment and if so we want to read and store their values
std::string Configuration::envKeys[] = { "EDAT_NUM_WORKERS", "EDAT_MAIN_THREAD_WORKER", "EDAT_REPORT_WORKER_MAPPING", "EDAT_PROGRESS_THREAD" ,
                                        "EDAT_BATCH_EVENTS", "EDAT_MAX_BATCHED_EVENTS", "EDAT_BATCHING_EVENTS_TIMEOUT"};

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
