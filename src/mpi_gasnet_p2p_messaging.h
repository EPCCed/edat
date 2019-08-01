/*
* Copyright (c) 2018, EPCC, The University of Edinburgh
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
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
#include <gasnet.h>

#include "messaging.h"
#include "configuration.h"

class MPI_GASNet_P2P_Messaging : public Messaging 
{
    // GASNet handler functions
    friend void req_fire_event(gasnet_token_t token, void *buf, size_t size, int event_id_length, int data_type, int source, int persistent);
    friend void rep_fire_event(gasnet_token_t token);
        
    // private member data
    bool m_protectMPI, m_mpiInitHere, m_terminated, m_eligable_for_termination, m_batchEvents, m_enableBridge, m_gasnet_verbose{ false };
    int m_my_rank, m_total_ranks, m_reply_from_master, m_empty_itertions, m_max_batched_events, m_pending_msgs_out{0};
    double last_event_arrival, batch_timeout;
    int terminated_id, mode=0;
    size_t m_gasnet_segment_size = 0;
    int * termination_codes, *pingback_termination_codes;
    MPI_Request termination_pingback_request=MPI_REQUEST_NULL, termination_messages, termination_completed_request=MPI_REQUEST_NULL,
        terminate_send_req=MPI_REQUEST_NULL, terminate_send_pingback=MPI_REQUEST_NULL;
    MPI_Comm communicator;
    std::map<MPI_Request, char*> outstandingSendRequests;
    std::mutex outstandingSendRequests_mutex, mpi_mutex, dataArrival_mutex;
    std::vector<SpecificEvent*> eventShortTermStore;
    std::vector<gasnet_seginfo_t> m_gasnet_seginfo_table;
    
    // private member functions
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
    MPI_GASNet_P2P_Messaging(Scheduler&, ThreadPool&, ContextManager&, Configuration&);
    MPI_GASNet_P2P_Messaging(Scheduler&, ThreadPool&, ContextManager&, Configuration&, int);
    virtual void lockMutexForFinalisationTest();
    virtual void unlockMutexForFinalisationTest();
    virtual void resetPolling();
    virtual void runPollForEvents();
    virtual void setEligableForTermination() { m_eligable_for_termination=true; };
    virtual void finalise();
    virtual void fireEvent(void *, int, int, int, bool, const char *);
    virtual int getRank();
    virtual int getNumRanks();
    virtual bool isFinished();
    virtual void lockComms();
    virtual void unlockComms();
};
#endif
