#ifdef __cplusplus
  extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "edat.h"

// Set rank 0 as master
#define MASTER 0
// enable/disbale print-out of the whole matrix at the end
#define WRITEOUT 1

int iMin(int, int);
int iterWorker(const int);
double timeDiff(struct timespec, struct timespec);
void extractBlock(double *, int, int, int, double *);
void updateBlock(double *, int, int, int, double *);

// compiled fortran BLAS routines
extern void dpotrf_(char *, int *, double *, int *, int *);
extern void dtrsm_(char *, char *, char *, char *, int *, int *, const double *, double *, int *, double *, int *);

// EDAT tasks
void dpotrfTask(EDAT_Event*, int);
void dpotrfReturnTask(EDAT_Event*, int);
void dtrsmSchedulerTask(EDAT_Event*, int);
void dtrsmTask(EDAT_Event*, int);
void dtrsmReturnTask(EDAT_Event*, int);

int main(int argc, char * argv[]) {
  int global_size, block_size, num_blocks_1d, num_blocks, i, j;
  double * A = NULL;
  const double one = 1.0;

  if (argc < 2) {
    fprintf(stderr, "usage: %s global_size block_size\n", argv[0]);
    exit(-1);
  }

  sscanf(argv[1], "%d", &global_size);
  sscanf(argv[2], "%d", &block_size);

  edatInit(&argc, &argv);

  const int NUM_RANKS = edatGetNumRanks();
  const int MY_RANK = edatGetRank();

  // the array is square, but we only care about one triangle, so we calculate
  // the number of blocks of the user-defined size down one side, and then
  // how many in total to cover the whole triangle
  num_blocks_1d = global_size / block_size;
  if (num_blocks_1d * block_size < global_size) num_blocks_1d++;
  num_blocks = (num_blocks_1d * num_blocks_1d + num_blocks_1d) / 2;

  // only MASTER gets the whole actual array
  if (MY_RANK == MASTER) {
    int block_id, block_offset;
    int worker;
    int root_block_dims[] = {0,0};

    // allocate the array
    A = (double *) malloc(global_size * global_size * sizeof(double));
    if (A == NULL) {
      fprintf(stderr, "Couldn't allocate space for the array.\n");
      exit(-1);
    }
    // intialise the array upper triangle, fortran routines will consider it
    // a lower triangle array
    for (i = 0; i < global_size; i++) {
      for (j = i; j < global_size; j++) {
        A[i*global_size + j] = one;
      }
      A[i*global_size + i] += global_size;
    }

    // set up an array of pointers to the blocks
    double * blocks[num_blocks];
    block_id = 0;
    for (i = 0; i < num_blocks_1d; i++) {
      for (j = i; j < num_blocks_1d; j++) {
        blocks[block_id] = A + i*global_size*block_size + j*block_size;
        block_id++;
      }
    }

    printf("EDAT Ranks: %d, Global Size: %d, Block size: %d, Number of blocks: %d\n",
            NUM_RANKS, global_size, block_size, num_blocks);

    double * root_block = (double *) malloc(block_size * block_size * sizeof(double));
    int root_block_id = 0;
    for (block_offset = num_blocks_1d; block_offset > 0; block_offset--) {
      if (root_block_id == num_blocks-1) {
        root_block_dims[0] = global_size + block_size - num_blocks_1d * block_size;
        root_block_dims[1] = global_size + block_size - num_blocks_1d * block_size;
      } else {
        root_block_dims[0] = block_size;
        root_block_dims[1] = block_size;
      }

      extractBlock(blocks[root_block_id], root_block_dims[0], root_block_dims[1], global_size, root_block);

      worker = iterWorker(NUM_RANKS);
      edatFireEvent(root_block, EDAT_DOUBLE, root_block_dims[0]*root_block_dims[1], worker, "dpotrf_block");
      edatFireEvent(root_block_dims, EDAT_INT, 2, worker, "dpotrf_block_dims");
      edatFireEvent(&root_block_id, EDAT_INT, 1, worker, "dpotrf_block_id");
      edatFireEvent(&block_offset, EDAT_INT, 1, worker, "block_offset");

      edatScheduleTask(dpotrfReturnTask, 7, worker, "dpotrf_block", worker, "dpotrf_block_id", worker, "dpotrf_block_dims", worker, "dpotrf_block_offset", EDAT_SELF, "global_size", EDAT_SELF, "block_addresses", EDAT_SELF, "num_blocks");
      edatFireEvent(&global_size, EDAT_INT, 1, EDAT_SELF, "global_size");
      edatFireEvent(blocks, EDAT_ADDRESS, num_blocks, EDAT_SELF, "block_addresses");
      edatFireEvent(&num_blocks, EDAT_INT, 1, EDAT_SELF, "num_blocks");

      if (root_block_id < num_blocks-1)  {
        edatScheduleTask(dtrsmSchedulerTask, 7, EDAT_SELF, "dpotrf_completed", EDAT_SELF, "dtrsm_root_dims", EDAT_SELF, "dtrsm_block_addresses", EDAT_SELF, "row_end", EDAT_SELF, "block_size", EDAT_SELF, "global_size", EDAT_SELF, "num_blocks_1d");
        edatFireEvent(blocks, EDAT_ADDRESS, num_blocks, EDAT_SELF, "dtrsm_block_addresses");
        edatFireEvent(&block_size, EDAT_INT, 1, EDAT_SELF, "block_size");
        edatFireEvent(&global_size, EDAT_INT, 1, EDAT_SELF, "global_size");
        edatFireEvent(&num_blocks_1d, EDAT_INT, 1, EDAT_SELF, "num_blocks_1d");
      }

      root_block_id += block_offset;
    }
    free(root_block);

  } else {
    edatSchedulePersistentTask(dpotrfTask, 4, MASTER, "dpotrf_block", MASTER, "dpotrf_block_dims", MASTER, "dpotrf_block_id", MASTER, "block_offset");
    edatSchedulePersistentTask(dtrsmTask, 5, MASTER, "root_block", MASTER, "root_dims", MASTER, "dtrsm_block", MASTER, "dtrsm_dims", MASTER, "dtrsm_block_id");
  }

  edatFinalise();

  if (WRITEOUT) {
    if (MY_RANK == MASTER) {
      i = 0;
      int iSum = 0;
      printf("A (blocks):\n");
      for (int blockdex = 0; blockdex < num_blocks; blockdex++) {
        printf("%3d ", blockdex);
        if (!((blockdex+1+iSum) % num_blocks_1d) && blockdex+1 < num_blocks) {
          printf("\n");
          ++i;
          iSum += i;
          for (j = 0; j < i; j++) printf(" -- ");
        }
      }
      printf("\n");
      printf("A (printed row-major):\n");
      for (i = 0; i < global_size*global_size; i++) {
        printf("%5.2f ", A[i]);
        if (!((i+1) % global_size)) printf("\n");
      }
    }
  }

  free(A);

  return 0;
}

int iMin(int a, int b) {
  if (a > b) {
    return b;
  } else {
    return a;
  }
}

int iterWorker(const int NUM_RANKS) {
  static int worker = 0;

  worker = (worker % (NUM_RANKS-1)) + 1;

  return worker;
}

double timeDiff(struct timespec stop, struct timespec start) {
  long int dt, stop_ns, start_ns;

  stop_ns = (long int) stop.tv_sec * 1E9 + stop.tv_nsec;
  start_ns = (long int) start.tv_sec * 1E9 + start.tv_nsec;
  dt = stop_ns - start_ns;

  return (double) 1E-9 * dt;
}

void extractBlock(double * row_start, int BLOCK_ROWS, int BLOCK_COLS, int GLOBAL_SIZE, double * block) {
  // extract the BLOCK_ROWSxBLOCK_COLS block from location BLOCK_ADDR, and copy
  // in to block
  // here we use C (row-major) index labels
  int index = 0;

  for (int row = 0; row < BLOCK_ROWS; row++) {
    for (int col = 0; col < BLOCK_COLS; col++) {
      block[index] = *(row_start + col);
      index++;
    }
    row_start += GLOBAL_SIZE;
  }

  return;
}

void updateBlock(double * BLOCK, int BLOCK_ROWS, int BLOCK_COLS, int GLOBAL_SIZE, double * array) {
  // copy BLOCK in to array, at block location array, the reverse operation
  // of extractBlock
  int index = 0;

  for (int row = 0; row < BLOCK_ROWS; row++) {
    for (int col = 0; col < BLOCK_COLS; col++) {
      *(array + col) = BLOCK[index];
      index++;
    }
    array += GLOBAL_SIZE;
  }

  return;
}

void dpotrfTask(EDAT_Event * events, int num_events) {
  // call the BLAS cholesky routine on a supplied block, then send it back
  // to master
  int block_index = edatFindEvent(events, num_events, MASTER, "dpotrf_block");
  int dims_index = edatFindEvent(events, num_events, MASTER, "dpotrf_block_dims");
  int id_index = edatFindEvent(events, num_events, MASTER, "dpotrf_block_id");
  int block_offset_index = edatFindEvent(events, num_events, MASTER, "block_offset");
  int info;

  double * block = (double *) events[block_index].data;
  int * block_dims = (int *) events[dims_index].data;
  int block_id = *((int *) events[id_index].data);
  int block_offset = *((int *) events[block_offset_index].data);

  dpotrf_("L", block_dims, block, (block_dims+1), &info);

  edatFireEvent(block, EDAT_DOUBLE, block_dims[0]*block_dims[1], MASTER, "dpotrf_block");
  edatFireEvent(&block_id, EDAT_INT, 1, MASTER, "dpotrf_block_id");
  edatFireEvent(block_dims, EDAT_INT, 2, MASTER, "dpotrf_block_dims");
  edatFireEvent(&block_offset, EDAT_INT, 1, MASTER, "dpotrf_block_offset");

  return;
}

void dpotrfReturnTask(EDAT_Event * events, int num_events) {
  // receive a block which has been supplied to BLAS cholesky routine (dpotrf)
  // and put it back in the main array
  // dtrsm calls can be made using this as the root block once this is completed
  int block_index = edatFindEvent(events, num_events, EDAT_ANY, "dpotrf_block");
  int dims_index = edatFindEvent(events, num_events, EDAT_ANY, "dpotrf_block_dims");
  int id_index = edatFindEvent(events, num_events, EDAT_ANY, "dpotrf_block_id");
  int block_offset_index = edatFindEvent(events, num_events, EDAT_ANY, "dpotrf_block_offset");
  int global_size_index = edatFindEvent(events, num_events, EDAT_SELF, "global_size");
  int addresses_index = edatFindEvent(events, num_events, EDAT_SELF, "block_addresses");
  int num_blocks_index = edatFindEvent(events, num_events, EDAT_SELF, "num_blocks");

  double * block = (double *) events[block_index].data;
  int * block_dims = (int *) events[dims_index].data;
  int block_id = *((int *) events[id_index].data);
  int block_offset = *((int *) events[block_offset_index].data);
  int global_size = *((int *) events[global_size_index].data);
  double ** blocks = (double **) events[addresses_index].data;
  int num_blocks = *((int *) events[num_blocks_index].data);
  int row_end = block_id + block_offset - 1;

  updateBlock(block, block_dims[0], block_dims[1], global_size, blocks[block_id]);

  if (block_id < num_blocks-1) {
    edatFireEvent(&block_id, EDAT_INT, 1, EDAT_SELF, "dpotrf_completed");
    edatFireEvent(block_dims, EDAT_INT, 2, EDAT_SELF, "dtrsm_root_dims");
    edatFireEvent(&row_end, EDAT_INT, 1, EDAT_SELF, "row_end");
  }

  return;
}

void dtrsmSchedulerTask(EDAT_Event * events, int num_events) {
  // receive confirmation that a row is ready for BLAS dtrsm calls, and then
  // distribute blocks along the row
  int root_block_id_index = edatFindEvent(events, num_events, EDAT_SELF, "dpotrf_completed");
  int root_dim_index = edatFindEvent(events, num_events, EDAT_SELF, "dtrsm_root_dims");
  int addresses_index = edatFindEvent(events, num_events, EDAT_SELF, "dtrsm_block_addresses");
  int row_end_index = edatFindEvent(events, num_events, EDAT_SELF, "row_end");
  int block_size_index = edatFindEvent(events, num_events, EDAT_SELF, "block_size");
  int global_size_index = edatFindEvent(events, num_events, EDAT_SELF, "global_size");
  int num_blocks_1d_index = edatFindEvent(events, num_events, EDAT_SELF, "num_blocks_1d");

  int root_block_id = *((int *) events[root_block_id_index].data);
  int * root_dims = (int *) events[root_dim_index].data;
  double ** blocks = (double **) events[addresses_index].data;
  int row_end = *((int *) events[row_end_index].data);
  int block_size = *((int *) events[block_size_index].data);
  int global_size = *((int *) events[global_size_index].data);
  int num_blocks_1d = *((int *) events[num_blocks_1d_index].data);

  const int NUM_RANKS = edatGetNumRanks();
  int worker;
  int num_blocks = (num_blocks_1d * num_blocks_1d + num_blocks_1d) / 2;
  int block_dims[] = {0,0};
  double * root_block = (double *) malloc(block_size * block_size * sizeof(double));
  double * dtrsm_block = (double *) malloc(block_size * block_size * sizeof(double));

  extractBlock(blocks[root_block_id], root_dims[0], root_dims[1], global_size, root_block);

  block_dims[0] = block_size;
  for (int block_id = root_block_id+1; block_id <= row_end; block_id++) {
    if (block_id == row_end) {
      block_dims[1] = global_size + block_size - num_blocks_1d * block_size;
    } else {
      block_dims[1] = block_size;
    }
    extractBlock(blocks[block_id], block_dims[0], block_dims[1], global_size, dtrsm_block);

    worker = iterWorker(NUM_RANKS);
    edatFireEvent(root_block, EDAT_DOUBLE, root_dims[0]*root_dims[1], worker, "root_block");
    edatFireEvent(root_dims, EDAT_INT, 2, worker, "root_dims");
    edatFireEvent(dtrsm_block, EDAT_DOUBLE, block_dims[0]*block_dims[1], worker, "dtrsm_block");
    edatFireEvent(block_dims, EDAT_INT, 2, worker, "dtrsm_dims");
    edatFireEvent(&block_id, EDAT_INT, 1, worker, "dtrsm_block_id");

    edatScheduleTask(dtrsmReturnTask, 5, worker, "dtrsm_block", worker, "dtrsm_dims", worker, "dtrsm_block_id", EDAT_SELF, "global_size", EDAT_SELF, "block_addresses");
    edatFireEvent(&global_size, EDAT_INT, 1, EDAT_SELF, "global_size");
    edatFireEvent(blocks, EDAT_ADDRESS, num_blocks, EDAT_SELF, "block_addresses");
  }
  free(root_block);
  free(dtrsm_block);

  return;
}

void dtrsmTask(EDAT_Event * events, int num_events) {
  // call BLAS dtrsm routine and send the overwritten block back to master
  int root_block_index = edatFindEvent(events, num_events, MASTER, "root_block");
  int root_dims_index = edatFindEvent(events, num_events, MASTER, "root_dims");
  int dtrsm_block_index = edatFindEvent(events, num_events, MASTER, "dtrsm_block");
  int dtrsm_dims_index = edatFindEvent(events, num_events, MASTER, "dtrsm_dims");
  int dtrsm_block_id_index = edatFindEvent(events, num_events, MASTER, "dtrsm_block_id");

  double * A = (double *) events[root_block_index].data;
  int * root_dims = (int *) events[root_dims_index].data;
  double * B = (double *) events[dtrsm_block_index].data;
  int * block_dims = (int *) events[dtrsm_dims_index].data;
  int block_id = *((int *) events[dtrsm_block_id_index].data);

  double one = 1.0;

  dtrsm_("R", "L", "T", "N", (block_dims+1), block_dims, &one, A, (root_dims+1), B, (block_dims+1));

  edatFireEvent(B, EDAT_DOUBLE, block_dims[0]*block_dims[1], MASTER, "dtrsm_block");
  edatFireEvent(block_dims, EDAT_INT, 2, MASTER, "dtrsm_dims");
  edatFireEvent(&block_id, EDAT_INT, 1, MASTER, "dtrsm_block_id");

  return;
}

void dtrsmReturnTask(EDAT_Event * events, int num_events) {
  // receive a block which has been overwritten by dtrsm, and put it back in
  // the main array
  int block_index = edatFindEvent(events, num_events, EDAT_ANY, "dtrsm_block");
  int dims_index = edatFindEvent(events, num_events, EDAT_ANY, "dtrsm_dims");
  int id_index = edatFindEvent(events, num_events, EDAT_ANY, "dtrsm_block_id");
  int global_size_index = edatFindEvent(events, num_events, EDAT_SELF, "global_size");
  int addresses_index = edatFindEvent(events, num_events, EDAT_SELF, "block_addresses");

  double * block = (double *) events[block_index].data;
  int * block_dims = (int *) events[dims_index].data;
  int block_id = *((int *) events[id_index].data);
  int global_size = *((int *) events[global_size_index].data);
  double ** blocks = (double **) events[addresses_index].data;

  updateBlock(block, block_dims[0], block_dims[1], global_size, blocks[block_id]);

  return;
}

#ifdef __cplusplus
  }
#endif
