#ifndef SRC_MPI_P2P_MESSAGING_H_
#define SRC_MPI_P2P_MESSAGING_H_

#include <map>
#include <vector>
#include <mutex>
#include "mpi.h"
#include "messaging.h"
#include "configuration.h"

class MPI_P2P_Messaging : public Messaging {
  bool protectMPI, mpiInitHere, terminated, eligable_for_termination, batchEvents, enableBridge;
  int my_rank, total_ranks, reply_from_master, empty_itertions, max_batched_events;
  double last_event_arrival, batch_timeout;
  int terminated_id, mode=0;
  int * termination_codes, *pingback_termination_codes;
  MPI_Request termination_pingback_request=MPI_REQUEST_NULL, termination_messages, termination_completed_request=MPI_REQUEST_NULL,
    terminate_send_req=MPI_REQUEST_NULL, terminate_send_pingback=MPI_REQUEST_NULL;
  MPI_Comm communicator;
  std::map<MPI_Request, char*> outstandingSendRequests;
  std::mutex outstandingSendRequests_mutex, mpi_mutex;
  std::vector<SpecificEvent*> eventShortTermStore;
  void initMPI();
  void checkSendRequestsForProgress();
  void sendSingleEvent(void *, int, int, int, bool, const char *);
  void trackTentativeTerminationCodes();
  bool confirmTerminationCodes();
  bool checkForCodeInList(int*, int);
  bool compareTerminationRanks();
  bool handleTerminationProtocolMessagesAsWorker();
  bool handleTerminationProtocol();
  void initialise(MPI_Comm);
protected:
  bool performSinglePoll(int*);
public:
  MPI_P2P_Messaging(Scheduler&, ThreadPool&, ContextManager&, Configuration&);
  MPI_P2P_Messaging(Scheduler&, ThreadPool&, ContextManager&, Configuration&, int);
  virtual void resetPolling();
  virtual void runPollForEvents();
  virtual void setEligableForTermination() { eligable_for_termination=true; };
  virtual void finalise();
  virtual void fireEvent(void *, int, int, int, bool, const char *);
  virtual int getRank();
  virtual int getNumRanks();
  virtual bool isFinished();
  virtual void lockComms();
  virtual void unlockComms();
};
#endif
