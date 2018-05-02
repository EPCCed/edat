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
