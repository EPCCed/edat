/*
* Copyright (c) 2018, EPCC, The University of Edinburgh
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
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
  double get(const char*, double);

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
  Configuration() : Configuration(0, NULL, NULL) { }
  Configuration(int, char**, char**);
};

#endif
