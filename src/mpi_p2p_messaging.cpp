#include "mpi_p2p_messaging.h"
#include "misc.h"
#include "scheduler.h"
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>
#include <mutex>

#define MPI_TAG 16384
#define SEND_PROGRESS_PERIOD 10

void MPI_P2P_Messaging::initMPI() {
  int is_mpi_init, provided;
  MPI_Initialized(&is_mpi_init);
  if (is_mpi_init) {
    mpiInitHere = false;
    MPI_Query_thread(&provided);
    if (provided != MPI_THREAD_MULTIPLE && provided != MPI_THREAD_SERIALIZED) {
      raiseError("You must initialise MPI in thread serialised or multiple, or let NDM do this for you");
    }
    protectMPI = provided == MPI_THREAD_SERIALIZED;
  } else {
    mpiInitHere = true;
    MPI_Init_thread(NULL, NULL, MPI_THREAD_SERIALIZED, &provided);
    protectMPI = true;
  }
  MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &total_ranks);
}

void MPI_P2P_Messaging::fireEvent(void * data, int data_count, int data_type, int target, const char * uuid) {
  int uuid_len=strlen(uuid);
  int type_element_size=getTypeSize(data_type);
  int packet_size=(type_element_size * data_count) + (sizeof(int) * 3) + uuid_len + 1;
  char * buffer = (char*) malloc(packet_size);
  memcpy(buffer, &data_type, sizeof(int));
  memcpy(&buffer[4], &my_rank, sizeof(int));
  memcpy(&buffer[8], &uuid_len, sizeof(int));
  memcpy(&buffer[12], uuid, sizeof(char) * (uuid_len + 1));
  if (data != NULL) memcpy(&buffer[(12 + uuid_len + 1)], data, type_element_size * data_count);
  MPI_Request request;
  MPI_Isend(buffer, packet_size, MPI_BYTE, target, MPI_TAG, MPI_COMM_WORLD, &request);
  std::lock_guard<std::mutex> out_sendReq_lock(outstandingSendRequests_mutex);
  outstandingSendRequests.insert(std::pair<MPI_Request, char*>(request, buffer));
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
      for (int i=0;i<out_count;i++) {
        auto it = outstandingSendRequests.find(storedReqHandles[returnIndicies[i]]);
        if (it != outstandingSendRequests.end()) {
          free(it->second);
          outstandingSendRequests.erase(it);
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
      MPI_Get_count(&message_status, MPI_BYTE, &message_size);
      buffer = (char*)malloc(message_size);
      MPI_Recv(buffer, message_size, MPI_BYTE, message_status.MPI_SOURCE, MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      int data_type = *((int*)buffer);
      int source_pid = *((int*)&buffer[4]);
      int unique_id_length = strlen(&buffer[12]);
      int data_size = message_size - (12 + unique_id_length + 1);
      if (data_size > 0) {
        data_buffer = (char*)malloc(data_size);
        memcpy(data_buffer, &buffer[12 + unique_id_length + 1], data_size);
      } else {
        data_buffer = NULL;
      }
      SpecificEvent* event=new SpecificEvent(source_pid, data_size, data_type, std::string(&buffer[12]), data_buffer);
      scheduler.registerEvent(event);
      free(buffer);
    }
  }
}
