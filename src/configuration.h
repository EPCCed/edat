#ifndef SRC_CONFIGURATION_H_
#define SRC_CONFIGURATION_H_

#include <map>
#include <string>
#include <algorithm>
#include "edat.h"

class Configuration {
  std::map<std::string, std::string> configSettings;
  static std::string envKeys[];
  void extractEnvironmentConfigSettings();
public:
  bool get(const char*, bool);
  unsigned int get(const char*, unsigned int);
  int get(const char*, int);

  /**
  * Retrieves the value in a provided map if the configuration item with the provided key (case-insensitive) is found, if so
  * the corresponding configuration value is then matched as the key in the provided map and the value this maps to is returned.
  * Otherwise (either no configuration item or entry in the provided map) then the default value is returned
  */
  template<typename T> T get(const char * name, std::map<const char*, T> lookupMap, T defaultValue) {
    std::string keyStr = std::string(name);
    std::transform(keyStr.begin(), keyStr.end(),keyStr.begin(), ::toupper);
    std::map<std::string, std::string>::iterator it=configSettings.find(keyStr);
    if (it != configSettings.end()) {
      typename std::map<const char*, T>::iterator providedMapit=lookupMap.find(it->second.c_str());
      if (providedMapit != lookupMap.end()) return providedMapit->second;
    }
    return defaultValue;
  }

  Configuration(edat_struct_configuration*);
};

#endif
