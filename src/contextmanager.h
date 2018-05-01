#ifndef SRC_CONTEXTMANAGER_H_
#define SRC_CONTEXTMANAGER_H_

#include <map>
#include <vector>
#include <stddef.h>

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
  int definitionId;
  std::map<int, ContextDefinition*> definitions;
public:
  ContextManager() { definitionId = BASE_CONTEXT_ID; }
  int addDefinition(ContextDefinition*);
  void* createContext(int);
  int getContextEventPayloadSize(int);
  bool isTypeAContext(int);
};

#endif
