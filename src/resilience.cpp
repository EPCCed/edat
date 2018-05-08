#include "resilience.h"
#include "messaging.h"

namespace resilience {
  EDAT_Ledger * process_ledger;
}

void resilienceInit(void) {
  resilience::process_ledger = new EDAT_Ledger();

  return;
}

void EDAT_Ledger::setMessaging(Messaging* messaging) {
  this->messaging = messaging;

  return;
}

void EDAT_Ledger::loadEvent(void * data, int data_count, int data_type,
  int target, bool persistent, const char * event_id) {
  LoadedEvent event;
  event.data = data;
  event.data_count = data_count;
  event.data_type = data_type;
  event.target = target;
  event.persistent = persistent;
  event.event_id = event_id;

  resilience::process_ledger->event_cannon.push(event);

  return;
}

void EDAT_Ledger::fireCannon(void) {
  LoadedEvent event;

  while (!resilience::process_ledger->event_cannon.empty()) {
    event = resilience::process_ledger->event_cannon.front();
    messaging->fireEvent(event.data, event.data_count, event.data_type,
      event.target, event.persistent, event.event_id);
    resilience::process_ledger->event_cannon.pop();
  }

  return;
}

void EDAT_Ledger::finalise(void) {
  delete this;

  return;
}
