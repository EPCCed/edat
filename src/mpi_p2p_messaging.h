#ifndef SRC_MPI_P2P_MESSAGING_H_
#define SRC_MPI_P2P_MESSAGING_H_

#include <map>
#include <mutex>
#include "mpi.h"
#include "messaging.h"
#include "configuration.h"

class MPI_P2P_Messaging : public Messaging {
  bool protectMPI, mpiInitHere, terminated, eligable_for_termination;
  int my_rank, total_ranks, reply_from_master;
  int terminated_id, mode=0;
  int * termination_codes, *pingback_termination_codes;
  MPI_Request termination_pingback_request=MPI_REQUEST_NULL, termination_messages, termination_completed_request=MPI_REQUEST_NULL,
    terminate_send_req=MPI_REQUEST_NULL, terminate_send_pingback=MPI_REQUEST_NULL;
  std::map<MPI_Request, char*> outstandingSendRequests;
  std::map<MPI_Request, PendingTaskDescriptor*> outstandingRefluxTasks;
  std::mutex outstandingSendRequests_mutex, outstandingRefluxTasks_mutex, mpi_mutex;
  void initMPI();
  void checkSendRequestsForProgress();
  void sendSingleEvent(void *, int, int, int, bool, const char *, void (*)(EDAT_Event*, int));
  void handleFiringOfEvent(void *, int, int, int, bool, const char *, void (*)(EDAT_Event*, int));
  void trackTentativeTerminationCodes();
  bool confirmTerminationCodes();
  bool checkForCodeInList(int*, int);
  bool compareTerminationRanks();
  bool handleTerminationProtocolMessagesAsWorker();
  bool handleTerminationProtocol();
protected:
  bool performSinglePoll(int*);
public:
  MPI_P2P_Messaging(Scheduler&, ThreadPool&, ContextManager&, Configuration&);
  virtual void resetPolling();
  virtual void runPollForEvents();
  virtual void setEligableForTermination() { eligable_for_termination=true; };
  virtual void finalise();
  virtual void fireEvent(void *, int, int, int, bool, const char *);
  virtual void fireEvent(void *, int, int, int, bool, const char *, void (*)(EDAT_Event*, int));
  virtual int getRank();
  virtual int getNumRanks();
  virtual bool isFinished();
};
#endif
