#include "mpi_p2p_messaging.h"
#include "misc.h"
#include "scheduler.h"
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <mutex>
#include <cstdlib>
#include <ctime>

#define MPI_TAG 16384
#define MPI_TERMINATION_TAG 16385
#define MPI_TERMINATION_CONFIRM_TAG 16386
#define SEND_PROGRESS_PERIOD 10
#define MAX_TERMINATION_COUNT 100

void MPI_P2P_Messaging::initMPI() {
  int is_mpi_init, provided;
  MPI_Initialized(&is_mpi_init);
  if (is_mpi_init) {
    mpiInitHere = false;
    MPI_Query_thread(&provided);
    if (provided != MPI_THREAD_MULTIPLE && provided != MPI_THREAD_SERIALIZED) {
      raiseError("You must initialise MPI in thread serialised or multiple, or let EDAT do this for you");
    }
    protectMPI = provided == MPI_THREAD_SERIALIZED;
  } else {
    mpiInitHere = true;
    MPI_Init_thread(NULL, NULL, MPI_THREAD_SERIALIZED, &provided);
    protectMPI = true;
  }
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &total_ranks);
  std::srand(std::time(nullptr));
  char * mpi_buffer = (char*) malloc((sizeof(int) * MAX_TERMINATION_COUNT) + MPI_BSEND_OVERHEAD);
  MPI_Buffer_attach(mpi_buffer, MAX_TERMINATION_COUNT*sizeof(int) + MPI_BSEND_OVERHEAD );
  if (my_rank == 0) {
    termination_codes=new int[total_ranks];
    pingback_termination_codes=new int[total_ranks];
    for (int i=0;i<total_ranks;i++) termination_codes[i]=-1;
  }
}

void MPI_P2P_Messaging::fireEvent(void * data, int data_count, int data_type, int target,
                                  const char * event_id, void (*reflux_task_fn)(EDAT_Event*, int)) {
  handleFiringOfEvent(data, data_count, data_type, target, event_id, reflux_task_fn);
}

void MPI_P2P_Messaging::fireEvent(void * data, int data_count, int data_type, int target, const char * event_id) {
  handleFiringOfEvent(data, data_count, data_type, target, event_id, NULL);
}

void MPI_P2P_Messaging::handleFiringOfEvent(void * data, int data_count, int data_type, int target,
                                            const char * event_id, void (*reflux_task_fn)(EDAT_Event*, int)) {
  if (target == my_rank || target == EDAT_ALL) {
    int data_size=getTypeSize(data_type) * data_count;
    char * buffer_data=(char*) malloc(data_size);
    memcpy(buffer_data, data, data_size);
    SpecificEvent* event=new SpecificEvent(my_rank, data_count * getTypeSize(data_type), data_type, std::string(event_id), (char*) buffer_data);
    scheduler.registerEvent(event);
  }
  if (target != my_rank) {
    if (target != EDAT_ALL) {
      sendSingleEvent(data, data_count, data_type, target, event_id, reflux_task_fn);
    } else {
      for (int i=0;i<total_ranks;i++) {
        if (i != my_rank) {
          sendSingleEvent(data, data_count, data_type, i, event_id, reflux_task_fn);
        }
      }
    }
  }
}

void MPI_P2P_Messaging::sendSingleEvent(void * data, int data_count, int data_type, int target,
                                        const char * event_id, void (*reflux_task_fn)(EDAT_Event*, int)) {
  int event_id_len=strlen(event_id);
  int type_element_size=getTypeSize(data_type);
  int packet_size=(type_element_size * data_count) + (sizeof(int) * 3) + event_id_len + 1;
  char * buffer = (char*) malloc(packet_size);
  memcpy(buffer, &data_type, sizeof(int));
  memcpy(&buffer[4], &my_rank, sizeof(int));
  memcpy(&buffer[8], &event_id_len, sizeof(int));
  memcpy(&buffer[12], event_id, sizeof(char) * (event_id_len + 1));
  if (data != NULL) memcpy(&buffer[(12 + event_id_len + 1)], data, type_element_size * data_count);
  MPI_Request request;
  MPI_Issend(buffer, packet_size, MPI_BYTE, target, MPI_TAG, MPI_COMM_WORLD, &request);
  {
    std::lock_guard<std::mutex> out_sendReq_lock(outstandingSendRequests_mutex);
    outstandingSendRequests.insert(std::pair<MPI_Request, char*>(request, buffer));
  }
  if (reflux_task_fn != NULL) {
    std::lock_guard<std::mutex> out_reflux_lock(outstandingRefluxTasks_mutex);
    SpecificEvent * event=new SpecificEvent(target, data_count, data_type, event_id, (char*) data);
    PendingTaskDescriptor * taskDescriptor=new PendingTaskDescriptor();
    taskDescriptor->task_fn=reflux_task_fn;
    taskDescriptor->freeData=false;
    taskDescriptor->arrivedEvents.push_back(event);
    outstandingRefluxTasks.insert(std::pair<MPI_Request, PendingTaskDescriptor*>(request, taskDescriptor));
  }
}

bool MPI_P2P_Messaging::isFinished() {
  std::lock_guard<std::mutex> out_sendReq_lock(outstandingSendRequests_mutex);
  std::lock_guard<std::mutex> reflex_tasks_lock(outstandingRefluxTasks_mutex);
  return outstandingSendRequests.empty() && outstandingRefluxTasks.empty();
}

void MPI_P2P_Messaging::finalise() {
  continue_polling=false;
  Messaging::finalise();
  if (mpiInitHere) MPI_Finalize();
}

int MPI_P2P_Messaging::getRank() {
  return my_rank;
}

int MPI_P2P_Messaging::getNumRanks() {
  return total_ranks;
}

void MPI_P2P_Messaging::checkSendRequestsForProgress() {
  std::vector<MPI_Request> allreqHandles, storedReqHandles;
  std::lock_guard<std::mutex> out_sendReq_lock(outstandingSendRequests_mutex);
  for(auto imap : outstandingSendRequests) {
    allreqHandles.push_back(imap.first);
    // Needed as the MPI call resets the request back to null, hence we rely on the index and grab the origonal request based on this
    storedReqHandles.push_back(imap.first);
  }

  if (!allreqHandles.empty()) {
    MPI_Request * req_handles=(MPI_Request *) allreqHandles.data();
    int * returnIndicies=new int[allreqHandles.size()];
    int out_count;
    MPI_Testsome(allreqHandles.size(), req_handles, &out_count, returnIndicies, MPI_STATUSES_IGNORE);
    if (out_count > 0) {
      std::lock_guard<std::mutex> reflex_tasks_lock(outstandingRefluxTasks_mutex);
      for (int i=0;i<out_count;i++) {
        auto it = outstandingSendRequests.find(storedReqHandles[returnIndicies[i]]);
        if (it != outstandingSendRequests.end()) {
          free(it->second);
          outstandingSendRequests.erase(it);
        }
        auto it2 = outstandingRefluxTasks.find(storedReqHandles[returnIndicies[i]]);
        if (it2 != outstandingRefluxTasks.end()) {
          scheduler.readyToRunTask(it2->second);
          outstandingRefluxTasks.erase(it2);
        }
      }
    }
    delete returnIndicies;
  }
}

void MPI_P2P_Messaging::runPollForEvents() {
  int pending_message, message_size;
  char* buffer, *data_buffer;
  MPI_Status message_status;
  int iteration_counter=0;
  while (continue_polling) {
    fireASingleLocalEvent();
    if (iteration_counter == SEND_PROGRESS_PERIOD) {
      checkSendRequestsForProgress();
      iteration_counter=0;
    } else {
      iteration_counter++;
    }
    MPI_Iprobe(MPI_ANY_SOURCE, MPI_TAG, MPI_COMM_WORLD, &pending_message, &message_status);
    if (pending_message) {
      terminated=false;
      MPI_Get_count(&message_status, MPI_BYTE, &message_size);
      buffer = (char*)malloc(message_size);
      MPI_Recv(buffer, message_size, MPI_BYTE, message_status.MPI_SOURCE, MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      int data_type = *((int*)buffer);
      int source_pid = *((int*)&buffer[4]);
      int event_id_length = strlen(&buffer[12]);
      int data_size = message_size - (12 + event_id_length + 1);
      if (data_size > 0) {
        data_buffer = (char*)malloc(data_size);
        memcpy(data_buffer, &buffer[12 + event_id_length + 1], data_size);
      } else {
        data_buffer = NULL;
      }
      SpecificEvent* event=new SpecificEvent(source_pid, data_size, data_type, std::string(&buffer[12]), data_buffer);
      scheduler.registerEvent(event);
      free(buffer);
    } else {
      bool current_terminated=checkForTermination();
      if (current_terminated && !terminated) {
        terminated_id=std::rand();
        if (my_rank != 0) {
          MPI_Bsend(&terminated_id, 1, MPI_INT, 0, MPI_TERMINATION_TAG, MPI_COMM_WORLD);
          if (termination_pingback_request == MPI_REQUEST_NULL) {
            MPI_Irecv(NULL, 0, MPI_INT, 0, MPI_TERMINATION_TAG, MPI_COMM_WORLD, &termination_pingback_request);
          }
        }
      }
      terminated=current_terminated;
    }
    if (my_rank == 0) termination_codes[0]=terminated ? terminated_id : -1;
    continue_polling=handleTerminationProtocol();
  }
}

/**
* Handles the termination protocol, we have a process (0) as the master and others as workers. This is needed as we can tell if I am
* idle myself (terminated), but that doesn't mean that I won't reactivate with another event. Hence we need to only terminate when all
* ranks are completed. To check this, when ranks individually terminate they generate a random ID (which changes each time they reactivate
* and re-terminate.) These are then sent to the master, which stores them and when ids have been received from each rank then it sends a command
* to ping-back the latest termination ID (or -1 if it is currently active again.) If all these latest IDs match the previous IDs for each worker
* then we assume the system is in a steady state and hence should terminate. If the ids are different then you go back to the first stage of gathering
* termination IDs and pinging back.
*/
bool MPI_P2P_Messaging::handleTerminationProtocol() {
  if (my_rank == 0) {
    bool rt=true;
    if (mode == 0) trackTentativeTerminationCodes();
    if (mode == 1) rt=confirmTerminationCodes();
    return rt;
  } else {
    return handleTerminationProtocolMessagesAsWorker();
  }
}

/**
* Will handle the termination protocol as a worker (non-rank 0.) This involves two aspects, the first is waiting a ping back from
* the master to send the current termination id (or -1 if I am now active.) This ping back will only be requested when the master
* has been told from each rank that it is terminated, although obviously those ranks might now have progressed and be active again!
* The second is checking for a termination command from the master, this is after the ping back and the master will determine if
* all ranks are indeed completed or if it needs to crack on again
*/
bool MPI_P2P_Messaging::handleTerminationProtocolMessagesAsWorker() {
  if (termination_pingback_request != MPI_REQUEST_NULL) {
    int completed;
    MPI_Test(&termination_pingback_request, &completed, MPI_STATUS_IGNORE);
    if (completed) {
      int not_completed=-1;
      // Send the master either my termination id or that I am active
      MPI_Bsend(terminated ? &terminated_id : &not_completed, 1, MPI_INT, 0, MPI_TERMINATION_CONFIRM_TAG, MPI_COMM_WORLD);
      // Irrespective register the reply recieve which tells the worker whether it should terminate or not
      MPI_Irecv(&reply_from_master, 1, MPI_INT, 0, MPI_TERMINATION_CONFIRM_TAG, MPI_COMM_WORLD, &termination_completed_request);
    }
  }
  if (termination_completed_request != MPI_REQUEST_NULL) {
    int completed;
    MPI_Test(&termination_completed_request, &completed, MPI_STATUS_IGNORE);
    if (completed) {
      if (reply_from_master == 1) {
        // All finished
        return false;
      } else {
        // Not finished, need to restart
        if (termination_pingback_request == MPI_REQUEST_NULL) {
          // Reregister the ping-back recv if required
          MPI_Irecv(NULL, 0, MPI_INT, 0, MPI_TERMINATION_TAG, MPI_COMM_WORLD, &termination_pingback_request);
        }
      }
    }
  }
  return true;
}

/**
* On the master we confirm the termination codes, this is grabbing back the codes from each worker and then comparing them against the previous code
* if they all match then the system is in a steady state & terminate. Otherwise need to restart termination. Will tell each worker whether it should
* terminate or not depending on the values that the master receives.
*/
bool MPI_P2P_Messaging::confirmTerminationCodes() {
  int msg_pending, termination_command=0;
  bool updated=false;
  MPI_Status status;
  MPI_Iprobe(MPI_ANY_SOURCE, MPI_TERMINATION_CONFIRM_TAG, MPI_COMM_WORLD, &msg_pending, &status);
  while (msg_pending) {
    updated=true;
    MPI_Recv(&pingback_termination_codes[status.MPI_SOURCE], 1, MPI_INT, status.MPI_SOURCE, MPI_TERMINATION_CONFIRM_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Iprobe(MPI_ANY_SOURCE, MPI_TERMINATION_CONFIRM_TAG, MPI_COMM_WORLD, &msg_pending, &status);
  }
  if (!checkForCodeInList(pingback_termination_codes, -2)) {
    // All responses are in, now process
    if (!checkForCodeInList(pingback_termination_codes, -1)) {
      // All still termination
      if (compareTerminationRanks()) {
        // Terminated, all finished!
        termination_command=1;
      } else {
        // Not terminated as code is different, therefore go back and re-request ping back (update termination codes too!)
        termination_command=0;
        mode=0;
      }
    } else {
      // There is an active process
      termination_command=0;
      mode=0;
    }
    if (mode == 0) termination_codes[0]=pingback_termination_codes[0];
    for (int i=1;i<total_ranks;i++) {
      MPI_Send(&termination_command, 1, MPI_INT, i, MPI_TERMINATION_CONFIRM_TAG, MPI_COMM_WORLD);
      if (mode == 0) termination_codes[i]=pingback_termination_codes[i];
    }
  }
  return termination_command==0;
}

/**
* Tracks termination codes being sent to the master from other, worker processes. It will loop through and grab all the messages it can greedily. If this has then terminated
* it will check that all have terminated and if so will send the ping back command to all workers (to tell them to send their id or -1 if been reactivated.) We progress
* through greedily to grab these ids (irrespective if this master process is still active) in order to avoid lots of messages backing up if workers go from active to
* terminated continuously.
*/
void MPI_P2P_Messaging::trackTentativeTerminationCodes() {
  int msg_pending;
  bool updated=false;
  MPI_Status status;
  MPI_Iprobe(MPI_ANY_SOURCE, MPI_TERMINATION_TAG, MPI_COMM_WORLD, &msg_pending, &status);
  while (msg_pending) {
    updated=true;
    MPI_Recv(&termination_codes[status.MPI_SOURCE], 1, MPI_INT, status.MPI_SOURCE, MPI_TERMINATION_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Iprobe(MPI_ANY_SOURCE, MPI_TERMINATION_TAG, MPI_COMM_WORLD, &msg_pending, &status);
  }
  if (terminated && !checkForCodeInList(termination_codes, -1)) {
    mode=1;
    pingback_termination_codes[0]=terminated_id;
    for (int i=1;i<total_ranks;i++) pingback_termination_codes[i]=-2;
    for (int i=1;i<total_ranks;i++) {
      MPI_Send(NULL, 0, MPI_INT, i, MPI_TERMINATION_TAG, MPI_COMM_WORLD);
    }
  }
}

/**
* Compares the two termination lists, the ones we get as the code runs and the other the ping-back.
*/
bool MPI_P2P_Messaging::compareTerminationRanks() {
  for (int i=0;i<total_ranks;i++) {
    if (termination_codes[i] != pingback_termination_codes[i]) return false;
  }
  return true;
}

/**
* Checks whether a specific code is in a list of integers or not. This is useful for checking for
* termination sentinel or awaiting value
*/
bool MPI_P2P_Messaging::checkForCodeInList(int * codes_to_check, int failure_code) {
  for (int i=0;i<total_ranks;i++) {
    if (codes_to_check[i] == failure_code) return true;
  }
  return false;
}
