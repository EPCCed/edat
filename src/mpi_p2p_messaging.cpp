#include "mpi_p2p_messaging.h"
#include "misc.h"
#include "scheduler.h"
#include <string.h>
#include <stdlib.h>
#include <mpi.h>

#define MPI_TAG 16384

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

void MPI_P2P_Messaging::finalise() {
  continue_polling=false;
  Messaging::finalise();
  if (mpiInitHere) MPI_Finalize();
}

void MPI_P2P_Messaging::runPollForEvents() {
  int pending_message, message_size;
  char* buffer, *data_buffer;
  MPI_Status message_status;
  while (continue_polling) {
    fireASingleLocalEvent();
    MPI_Iprobe(MPI_ANY_SOURCE, MPI_TAG, MPI_COMM_WORLD, &pending_message, &message_status);
    if (pending_message) {
      MPI_Get_count(&message_status, MPI_BYTE, &message_size);
      buffer = (char*)malloc(message_size);
      MPI_Recv(buffer, message_size, MPI_BYTE, message_status.MPI_SOURCE, MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      int data_type = *((int*)buffer);
      int source_pid = *((int*)&buffer[4]);
      int unique_id_length = strlen(&buffer[8]);
      int data_size = message_size - (8 + unique_id_length + 1);
      if (data_size > 0) {
        data_buffer = (char*)malloc(data_size);
        memcpy(data_buffer, &buffer[8 + unique_id_length + 1], data_size);
      } else {
        data_buffer = NULL;
      }
      SpecificEvent* event=new SpecificEvent(source_pid, data_size, data_type, std::string(&buffer[8]), data_buffer);
      scheduler.registerEvent(event);
      free(buffer);
    }
  }
}
