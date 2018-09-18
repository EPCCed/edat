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
  std::mutex outstandingSendRequests_mutex, mpi_mutex, dataArrival_mutex;
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
  void handleRemoteMessageArrival(MPI_Status, MPI_Comm);
protected:
  bool performSinglePoll(int*);
public:
  MPI_P2P_Messaging(Scheduler&, ThreadPool&, ContextManager&, Configuration&);
  MPI_P2P_Messaging(Scheduler&, ThreadPool&, ContextManager&, Configuration&, int);
  virtual void lockMutexForFinalisationTest();
  virtual void unlockMutexForFinalisationTest();
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
