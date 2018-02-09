/*
 * misc.h
 *
 *  Created on: 4 Apr 2016
 *      Author: nick
 */

#ifndef SRC_MISC_H_
#define SRC_MISC_H_

struct UUIDStateComparitor {
  bool operator()(std::string a, std::string b) {
    size_t wildCardLocA = a.find('*');
    size_t wildCardLocB = b.find('*');
    if (wildCardLocA != std::string::npos || wildCardLocB != std::string::npos) {
      if (wildCardLocA == std::string::npos) {
        return a.substr(0, wildCardLocB) < b.substr(0, wildCardLocB);
      } else if (wildCardLocB == std::string::npos) {
        return a.substr(0, wildCardLocA) < b.substr(0, wildCardLocA);
      } else {
        return a.substr(0, wildCardLocA) < b.substr(0, wildCardLocB);
      }
    } else {
      return a < b;
    }
  }
};

void raiseError(const char*);

#endif /* SRC_MISC_H_ */
