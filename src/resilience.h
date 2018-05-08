#ifndef SRC_RESILIENCE_H
#define SRC_RESILIENCE_H

#include "messaging.h"
#include <queue>

void resilienceInit(void);

struct LoadedEvent {
  void * data;
  int data_count;
  int data_type;
  int target;
  bool persistent;
  const char * event_id;
};

class EDAT_Ledger {
private:
  Messaging * messaging;
  std::queue<LoadedEvent> event_cannon;
public:
  void setMessaging(Messaging*);
  void loadEvent(void*, int, int, int, bool, const char *);
  void fireCannon(void);
  void finalise(void);
};

namespace resilience {
  extern EDAT_Ledger * process_ledger;
}

#endif
