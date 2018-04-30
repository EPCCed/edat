#include <map>
#include <vector>

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
  ContextManager() { definitionId = 2000; }
  int addDefinition(ContextDefinition*);
  void* createContext(int);
};
