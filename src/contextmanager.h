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

#ifndef SRC_CONTEXTMANAGER_H_
#define SRC_CONTEXTMANAGER_H_

#include <map>
#include <vector>
#include <stddef.h>
#include "configuration.h"

static int BASE_CONTEXT_ID=2000;

class ContextDefinition {
  size_t numberBytes;
  std::vector<void*> dataInstances;
public:
  ContextDefinition(size_t numberBytes) { this->numberBytes = numberBytes; }
  size_t getSize() { return numberBytes; }
  void* create();
};

class ContextManager {
  Configuration & configuration;
  int definitionId;
  std::map<int, ContextDefinition*> definitions;
public:
  ContextManager(Configuration & aconfig) : configuration(aconfig) { definitionId = BASE_CONTEXT_ID; }
  int addDefinition(ContextDefinition*);
  void* createContext(int);
  int getContextEventPayloadSize(int);
  bool isTypeAContext(int);
};

#endif
