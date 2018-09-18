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

#include <stdlib.h>
#include "contextmanager.h"
#include "misc.h"

/**
* Adds the definition of a context to the manager, this will also update the unique context definition id that is used as the type
*/
int ContextManager::addDefinition(ContextDefinition * definition) {
  definitions.insert(std::pair<int, ContextDefinition*>(definitionId, definition));
  definitionId++;
  return definitionId-1;
}

/**
* Creates a specific context based on the type of the definition (that has already been defined.) This will allocate the required
* memory and then store a pointer to this memory, the idea being that the manager is overall responsible for each of these contexts
*/
void* ContextManager::createContext(int contextType) {
  std::map<int, ContextDefinition*>::iterator it = definitions.find(contextType);
  if (it != definitions.end()) {
    return it->second->create();
  } else {
    raiseError("Can not find context type, make sure you have defined it correctly");
    return NULL;
  }
}

/**
* Determines whether a specific type is a context type or not (in such case it would be a base type or errornous.) This is mainly
* for firing events.
*/
bool ContextManager::isTypeAContext(int type) {
  if (type >= BASE_CONTEXT_ID) {
    return definitions.count(type) > 0;
  }
  return false;
}

/**
* Retrieves the size of the context payload from the type. Currently this is easy as we just pass a pointer to the context, but
* in the future will need to be extended most likely.
*/
int ContextManager::getContextEventPayloadSize(int contextType) {
  std::map<int, ContextDefinition*>::iterator it = definitions.find(contextType);
  if (it != definitions.end()) {
    return sizeof(char*);
  } else {
    raiseError("Can not find context type, make sure you have defined it correctly");
    return -1;
  }
}

/**
* Creates a specific instantiation of a context and stores a pointer to it.
*/
void* ContextDefinition::create() {
  char * data=(char*) malloc(numberBytes);
  dataInstances.push_back((void*) data);
  return (void*) data;
}
