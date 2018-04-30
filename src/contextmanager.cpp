#include "contextmanager.h"
#include "misc.h"

int ContextManager::addDefinition(ContextDefinition * definition) {
  definitions.insert(std::pair<int, ContextDefinition*>(definitionId, definition));
  definitionId++;
  return definitionId-1;
}

void* ContextManager::createContext(int contextType) {
  std::map<int, ContextDefinition*>::iterator it = definitions.find(contextType);
  if (it != definitions.end()) {
    return it->second->create();
  } else {
    raiseError("Can not find context type, make sure you have defined it correctly");
    return NULL;
  }
}

int ContextManager::getContextEventPayloadSize(int contextType) {
  std::map<int, ContextDefinition*>::iterator it = definitions.find(contextType);
  if (it != definitions.end()) {
    return sizeof(char*);
  } else {
    raiseError("Can not find context type, make sure you have defined it correctly");
    return -1;
  }
}

void* ContextDefinition::create() {
  char * data=(char*) malloc(numberBytes);
  dataInstances.push_back((void*) data);
  return (void*) data;
}
