#include "../par-res-kern_general.h"
#include "edat.h"

#define ROOT_PROCESS 0

#if DOUBLE
  #define DTYPE     double
  #define EDAT_DTYPE EDAT_DOUBLE
  #define EPSILON   1.e-8
  #define COEFX     1.0
  #define COEFY     1.0
  #define FSTR      "%lf"
#else
  #define DTYPE     float
  #define EDAT_DTYPE EDAT_FLOAT
  #define EPSILON   0.0001f
  #define COEFX     1.0f
  #define COEFY     1.0f
  #define FSTR      "%f"
#endif

/* define shorthand for indexing multi-dimensional arrays with offsets           */
#define INDEXIN(i,j)  (i+RADIUS+(j+RADIUS)*(width+2*RADIUS))
/* need to add offset of RADIUS to j to account for ghost points                 */
#define IN(i,j)       in[INDEXIN(i-istart,j-jstart)]
#define INDEXOUT(i,j) (i+(j)*(width))
#define OUT(i,j)      out[INDEXOUT(i-istart,j-jstart)]
#define WEIGHT(ii,jj) weight[ii+RADIUS][jj+RADIUS]

#define UP 0
#define RIGHT 1
#define DOWN 2
#define LEFT 3

#ifndef RADIUS
#define RADIUS 2
#endif

static void displayPreamble(int, int, int, int, int, int);
static void errorCheckParameters(int, int, int, long, int, int, int);
static void initialise_in_out_arrays(DTYPE ** RESTRICT, DTYPE ** RESTRICT, int, int, int, int, int, int, int);
static void initialise_stencil_weights(DTYPE [2*RADIUS+1][2*RADIUS+1]);
static void allocate_comms_buffers(DTYPE**, DTYPE**, DTYPE**, DTYPE**, int, int, int);
static void displayResults(int, DTYPE, int, double, int);
static void prepareCommBuffer(DTYPE *, DTYPE *, int);
static void readCommBuffer(DTYPE *, DTYPE *, int);
static void complete_run(EDAT_Event*, int);
static void compute_kernel(EDAT_Event*, int);
static void firstHaloSwap(EDAT_Event*, int);

int Num_procsx, Num_procsy, my_IDx, my_IDy, right_nbr, left_nbr, top_nbr, bottom_nbr, n, width, height, istart, iend, jstart, jend, iterations,
  num_neighbours;
long nsquare;
DTYPE weight[2*RADIUS+1][2*RADIUS+1];

int main(int argc, char ** argv) {
  int    Num_procs;       /* number of ranks                                     */
  int    my_ID;           /* MPI rank                                            */
  int    iter, leftover;  /* dummies                   */
  DTYPE * RESTRICT in, * RESTRICT out;
  DTYPE * comm_buffer_up, * comm_buffer_down, * comm_buffer_left, * comm_buffer_right;
  /*******************************************************************************
  ** Initialize the MPI environment
  ********************************************************************************/
  task_ptr_t task_array[2] = {complete_run, compute_kernel};
  edatInit(&argc, &argv, NULL, task_array, 2);
  my_ID=edatGetRank();
  Num_procs=edatGetNumRanks();

  /*******************************************************************************
  ** process, test, and broadcast input parameters
  ********************************************************************************/

  iterations = atoi(*++argv);
  n = atoi(*++argv);
  nsquare = (long) n * (long) n;

  errorCheckParameters(my_ID, argc, iterations, nsquare, Num_procs, RADIUS, n);

  /* determine best way to create a 2D grid of ranks (closest to square)     */
  factor(Num_procs, &Num_procsx, &Num_procsy);

  my_IDx = my_ID%Num_procsx;
  my_IDy = my_ID/Num_procsx;
  /* compute neighbors; don't worry about dropping off the edges of the grid */
  right_nbr  = my_ID+1;
  left_nbr   = my_ID-1;
  top_nbr    = my_ID+Num_procsx;
  bottom_nbr = my_ID-Num_procsx;

  if (my_ID == ROOT_PROCESS) {
    displayPreamble(Num_procs, n, RADIUS, Num_procsx, Num_procsy, iterations);
  }

  /* compute amount of space required for input and solution arrays             */
  width = n/Num_procsx;
  leftover = n%Num_procsx;
  if (my_IDx < leftover) {
    istart = (width+1) * my_IDx;
    iend = istart + width;
  } else {
    istart = (width+1) * leftover + width * (my_IDx-leftover);
    iend = istart + width - 1;
  }

  width = iend - istart + 1;
  if (width == 0) {
    printf("ERROR: rank %d has no work to do\n", my_ID);
    exit(EXIT_FAILURE);
  }

  height = n/Num_procsy;
  leftover = n%Num_procsy;
  if (my_IDy<leftover) {
    jstart = (height+1) * my_IDy;
    jend = jstart + height;
  } else {
    jstart = (height+1) * leftover + height * (my_IDy-leftover);
    jend = jstart + height - 1;
  }

  height = jend - jstart + 1;
  if (height == 0) {
    printf("ERROR: rank %d has no work to do\n", my_ID);
    exit(EXIT_FAILURE);
  }

  if (width < RADIUS || height < RADIUS) {
    printf("ERROR: rank %d has work tile smaller then stencil radius\n", my_ID);
    exit(EXIT_FAILURE);
  }

  initialise_in_out_arrays(&in, &out, my_ID, width, height, jstart, jend, istart, iend);
  initialise_stencil_weights(weight);

  if (Num_procs > 1) {
    allocate_comms_buffers(&comm_buffer_up, &comm_buffer_down, &comm_buffer_left, &comm_buffer_right, width, height, my_ID);

    num_neighbours=0;
    if (my_IDy < Num_procsy-1) num_neighbours++;
    if (my_IDy > 0) num_neighbours++;
    if (my_IDx > 0) num_neighbours++;
    if (my_IDx < Num_procsx-1) num_neighbours++;

    switch (num_neighbours) {
      case 1:
        // I have one neighbour! Where are they?
        if (my_IDy < Num_procsy-1) {
          // above!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", top_nbr, "buffer", EDAT_SELF, "halosend");
          edatScheduleTask(firstHaloSwap, 2, EDAT_SELF, "init_in", top_nbr, "init_buffer");
          prepareCommBuffer(comm_buffer_up, in, UP);
          edatFireEvent(comm_buffer_up, EDAT_DTYPE, RADIUS*width, top_nbr, "init_buffer");
        } else if (my_IDy > 0) {
          // below!!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", bottom_nbr, "buffer", EDAT_SELF, "halosend");
          edatScheduleTask(firstHaloSwap, 2, EDAT_SELF, "init_in", bottom_nbr, "init_buffer");
          prepareCommBuffer(comm_buffer_down, in, DOWN);
          edatFireEvent(comm_buffer_down, EDAT_DTYPE, RADIUS*width, bottom_nbr, "init_buffer");
        } else if (my_IDx < Num_procsx-1) {
          // to the right!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", right_nbr, "buffer", EDAT_SELF, "halosend");
          edatScheduleTask(firstHaloSwap, 2, EDAT_SELF, "init_in", bottom_nbr, "buffer");
          prepareCommBuffer(comm_buffer_right, in, RIGHT);
          edatFireEvent(comm_buffer_right, EDAT_DTYPE, RADIUS*height, right_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
        } else if (my_IDx > 0) {
          // to the left!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", left_nbr, "buffer", EDAT_SELF, "halosend");
          edatScheduleTask(firstHaloSwap, 2, EDAT_SELF, "init_in", bottom_nbr, "buffer");
          prepareCommBuffer(comm_buffer_left, in, LEFT);
          edatFireEvent(comm_buffer_left, EDAT_DTYPE, RADIUS*height, left_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
        } else {
          // problem
          printf("ERROR: Rank %d could not map its neighbour\n", my_ID);
          exit(EXIT_FAILURE);
        }
        break;
      case 2:
        // I have two neighbours! Where are they?
        if (my_IDy < Num_procsy-1 && my_IDy > 0) {
          // above and below!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", top_nbr, "buffer", EDAT_SELF, "halosend", bottom_nbr, "buffer", EDAT_SELF, "halosend");
          prepareCommBuffer(comm_buffer_up, in, UP);
          prepareCommBuffer(comm_buffer_down, in, DOWN);
          edatFireEvent(comm_buffer_up, EDAT_DTYPE, RADIUS*width, top_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_down, EDAT_DTYPE, RADIUS*width, bottom_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
        } else if (my_IDx > 0 && my_IDx < Num_procsx-1) {
          // to the left and to the right!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", left_nbr, "buffer", EDAT_SELF, "halosend", right_nbr, "buffer", EDAT_SELF, "halosend");
          prepareCommBuffer(comm_buffer_left, in, LEFT);
          prepareCommBuffer(comm_buffer_right, in, RIGHT);
          edatFireEvent(comm_buffer_left, EDAT_DTYPE, RADIUS*height, left_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_right, EDAT_DTYPE, RADIUS*height, right_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
        } else if (my_IDy < Num_procsy-1 && my_IDx < Num_procsx-1) {
          // above and to the right!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", top_nbr, "buffer", EDAT_SELF, "halosend", right_nbr, "buffer", EDAT_SELF, "halosend");
          prepareCommBuffer(comm_buffer_up, in, UP);
          prepareCommBuffer(comm_buffer_right, in, RIGHT);
          edatFireEvent(comm_buffer_up, EDAT_DTYPE, RADIUS*width, top_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_right, EDAT_DTYPE, RADIUS*height, right_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
        } else if (my_IDy < Num_procsy-1 && my_IDx > 0) {
          // above and to the left!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", top_nbr, "buffer", EDAT_SELF, "halosend", left_nbr, "buffer", EDAT_SELF, "halosend");
          prepareCommBuffer(comm_buffer_up, in, UP);
          prepareCommBuffer(comm_buffer_left, in, LEFT);
          edatFireEvent(comm_buffer_up, EDAT_DTYPE, RADIUS*width, top_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_left, EDAT_DTYPE, RADIUS*height, left_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
        } else if (my_IDy > 0 && my_IDx < Num_procsx-1) {
          // below and to the right!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", bottom_nbr, "buffer", EDAT_SELF, "halosend", right_nbr, "buffer", EDAT_SELF, "halosend");
          prepareCommBuffer(comm_buffer_down, in, DOWN);
          prepareCommBuffer(comm_buffer_right, in, RIGHT);
          edatFireEvent(comm_buffer_down, EDAT_DTYPE, RADIUS*width, bottom_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_right, EDAT_DTYPE, RADIUS*height, right_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
        } else if (my_IDy > 0 && my_IDx > 0) {
          // below and to the left!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", bottom_nbr, "buffer", EDAT_SELF, "halosend", left_nbr, "buffer", EDAT_SELF, "halosend");
          prepareCommBuffer(comm_buffer_down, in, DOWN);
          prepareCommBuffer(comm_buffer_left, in, LEFT);
          edatFireEvent(comm_buffer_down, EDAT_DTYPE, RADIUS*width, bottom_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_left, EDAT_DTYPE, RADIUS*height, left_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
        } else {
          // problem
          printf("ERROR: Rank %d could not map its neighbours\n", my_ID);
          exit(EXIT_FAILURE);
        }
        break;
      case 3:
        if (my_IDy < Num_procsy-1 && my_IDy > 0 && my_IDx < Num_procsx-1) {
          // above, below, and to the right!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", top_nbr, "buffer", EDAT_SELF, "halosend", bottom_nbr, "buffer", EDAT_SELF, "halosend", right_nbr, "buffer", EDAT_SELF, "halosend");
          prepareCommBuffer(comm_buffer_up, in, UP);
          prepareCommBuffer(comm_buffer_down, in, DOWN);
          prepareCommBuffer(comm_buffer_right, in, RIGHT);
          edatFireEvent(comm_buffer_up, EDAT_DTYPE, RADIUS*width, top_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_down, EDAT_DTYPE, RADIUS*width, bottom_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_right, EDAT_DTYPE, RADIUS*height, right_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
        } else if (my_IDy < Num_procsy-1 && my_IDy > 0 && my_IDx > 0) {
          // above, below, and to the left!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", top_nbr, "buffer", EDAT_SELF, "halosend", bottom_nbr, "buffer", EDAT_SELF, "halosend", left_nbr, "buffer", EDAT_SELF, "halosend");
          prepareCommBuffer(comm_buffer_up, in, UP);
          prepareCommBuffer(comm_buffer_down, in, DOWN);
          prepareCommBuffer(comm_buffer_left, in, LEFT);
          edatFireEvent(comm_buffer_up, EDAT_DTYPE, RADIUS*width, top_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_down, EDAT_DTYPE, RADIUS*width, bottom_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_left, EDAT_DTYPE, RADIUS*height, left_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
        } else if (my_IDy < Num_procsy-1 && my_IDx > 0 && my_IDx < Num_procsx-1) {
          // above, to the left, and to the right!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", top_nbr, "buffer", EDAT_SELF, "halosend", left_nbr, "buffer", EDAT_SELF, "halosend", right_nbr, "buffer", EDAT_SELF, "halosend");
          prepareCommBuffer(comm_buffer_up, in, UP);
          prepareCommBuffer(comm_buffer_left, in, LEFT);
          prepareCommBuffer(comm_buffer_right, in, RIGHT);
          edatFireEvent(comm_buffer_up, EDAT_DTYPE, RADIUS*width, top_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_left, EDAT_DTYPE, RADIUS*height, left_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_right, EDAT_DTYPE, RADIUS*height, right_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
        } else if (my_IDy > 0 && my_IDx > 0 && my_IDx < Num_procsx-1) {
          // below, to the left, and to the right!
          edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", bottom_nbr, "buffer", EDAT_SELF, "halosend", left_nbr, "buffer", EDAT_SELF, "halosend", right_nbr, "buffer", EDAT_SELF, "halosend");
          prepareCommBuffer(comm_buffer_down, in, DOWN);
          prepareCommBuffer(comm_buffer_left, in, LEFT);
          prepareCommBuffer(comm_buffer_right, in, RIGHT);
          edatFireEvent(comm_buffer_down, EDAT_DTYPE, RADIUS*width, bottom_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_left, EDAT_DTYPE, RADIUS*height, left_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_right, EDAT_DTYPE, RADIUS*height, right_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
        } else {
          // problem
          printf("ERROR: Rank %d could not map its neighbours\n", my_ID);
          exit(EXIT_FAILURE);
        }
        break;
      case 4:
        // surrounded!
        edatSchedulePersistentTask(compute_kernel, 4+num_neighbours*2, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time", top_nbr, "buffer", EDAT_SELF, "halosend", bottom_nbr, "buffer", EDAT_SELF, "halosend", left_nbr, "buffer", EDAT_SELF, "halosend", right_nbr, "buffer", EDAT_SELF, "halosend");
          prepareCommBuffer(comm_buffer_up, in, UP);
          prepareCommBuffer(comm_buffer_down, in, DOWN);
          prepareCommBuffer(comm_buffer_left, in, LEFT);
          prepareCommBuffer(comm_buffer_right, in, RIGHT);
          edatFireEvent(comm_buffer_up, EDAT_DTYPE, RADIUS*width, top_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_down, EDAT_DTYPE, RADIUS*width, bottom_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_left, EDAT_DTYPE, RADIUS*height, left_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
          edatFireEvent(comm_buffer_right, EDAT_DTYPE, RADIUS*height, right_nbr, "buffer");
          edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
        break;
    }
  } else {
    // single process, so no communication
    edatSchedulePersistentTask(compute_kernel, 4, EDAT_SELF, "in", EDAT_SELF, "out", EDAT_SELF, "iterations", EDAT_SELF, "start_time");
  }

  if (my_ID == ROOT_PROCESS) {
    edatScheduleTask(complete_run, 2, EDAT_ALL, "runtime", EDAT_ALL, "localnorm");
  }

  int total_length_in  = (width+2*RADIUS) * (height+2*RADIUS);
  int total_length_out = width * height;
  iter=0;
  edatFireEvent(&iter, EDAT_INT, 1, EDAT_SELF, "iterations");
  edatFireEvent(in, EDAT_DTYPE, total_length_in, EDAT_SELF, "init_in");
  edatFireEvent(out, EDAT_DTYPE, total_length_out, EDAT_SELF, "out");
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "start_time");

  prk_free(in);
  prk_free(out);

  edatFinalise();
  exit(EXIT_SUCCESS);
}

static void complete_run(EDAT_Event * events, int num_events) {
  double overall_time=0.0;
  double overall_norm=0.0;
  int i;
  for (i=0;i<edatGetNumRanks();i++) {
    double specific_time=*((double*) events[edatFindEvent(events, num_events, i, "runtime")].data);
    if (overall_time < specific_time) overall_time=specific_time;
    overall_norm+=*((double*) events[edatFindEvent(events, num_events, i, "localnorm")].data);
  }
  displayResults(ROOT_PROCESS, overall_norm, iterations, overall_time, n);
}

static void compute_kernel(EDAT_Event * events, int num_events) {
  DTYPE * RESTRICT in = (DTYPE*) events[0].data;
  DTYPE * RESTRICT out = (DTYPE*) events[1].data;
  int current_it=*((int*) events[2].data);

  int total_length_in  = (width+2*RADIUS) * (height+2*RADIUS);
  int total_length_out = width * height;

  // Apply the stencil operator
  for (int j=MAX(jstart,RADIUS); j<=MIN(n-RADIUS-1,jend); j++) {
    for (int i=MAX(istart,RADIUS); i<=MIN(n-RADIUS-1,iend); i++) {
      #if LOOPGEN
        #include "loop_body_star.incl"
      #else
        for (int jj=-RADIUS; jj<=RADIUS; jj++) OUT(i,j) += WEIGHT(0,jj)*IN(i,j+jj);
        for (int ii=-RADIUS; ii<0; ii++)       OUT(i,j) += WEIGHT(ii,0)*IN(i+ii,j);
        for (int ii=1; ii<=RADIUS; ii++)       OUT(i,j) += WEIGHT(ii,0)*IN(i+ii,j);
      #endif
    }
  }

  // Add constant to solution to force refresh of neighbor data, if any
  for (int j=jstart; j<=jend; j++) {
    for (int i=istart; i<=iend; i++) {
      IN(i,j)+= 1.0;
    }
  }

  // send halos
  if (my_IDy < Num_procsy-1) {
    DTYPE * comm_buffer_up = (DTYPE*) malloc(RADIUS*width*sizeof(DTYPE));
    prepareCommBuffer(comm_buffer_up, in, UP);
    edatFireEvent(comm_buffer_up, EDAT_DTYPE, RADIUS*width, top_nbr, "buffer");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
    free(comm_buffer_up);
  }
  if (my_IDy > 0) {
    DTYPE * comm_buffer_down = (DTYPE*) malloc(RADIUS*width*sizeof(DTYPE));
    prepareCommBuffer(comm_buffer_down, in, DOWN);
    edatFireEvent(comm_buffer_down, EDAT_DTYPE, RADIUS*width, bottom_nbr, "buffer");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
    free(comm_buffer_down);
  }
  if (my_IDx > 0) {
    DTYPE * comm_buffer_left = (DTYPE*) malloc(RADIUS*height*sizeof(DTYPE));
    prepareCommBuffer(comm_buffer_left, in, LEFT);
    edatFireEvent(comm_buffer_left, EDAT_DTYPE, RADIUS*height, left_nbr, "buffer");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
    free(comm_buffer_left);
  }
  if (my_IDx < Num_procsx-1) {
    DTYPE * comm_buffer_right = (DTYPE*) malloc(RADIUS*height*sizeof(DTYPE));
    prepareCommBuffer(comm_buffer_right, in, RIGHT);
    edatFireEvent(comm_buffer_right, EDAT_DTYPE, RADIUS*height, right_nbr, "buffer");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
    free(comm_buffer_right);
  }

  // update halo
  switch (num_neighbours) {
      case 0:
        break;
      case 1:
        // I have one neighbour! Where are they?
        if (my_IDy < Num_procsy-1) {
          // above!
          DTYPE * buffer = (DTYPE*) events[4].data;
          readCommBuffer(buffer, in, UP);
        } else if (my_IDy > 0) {
          // below!
          DTYPE * buffer = (DTYPE*) events[4].data;
          readCommBuffer(buffer, in, DOWN);
        } else if (my_IDx < Num_procsx-1) {
          // to the right!
          DTYPE * buffer = (DTYPE*) events[4].data;
          readCommBuffer(buffer, in, RIGHT);
        } else if (my_IDx > 0) {
          // to the left!
          DTYPE * buffer = (DTYPE*) events[4].data;
          readCommBuffer(buffer, in, LEFT);
        } else {
          // problem
          printf("ERROR: Could not map my neighbour\n");
          exit(EXIT_FAILURE);
        }
        break;
      case 2:
        // I have two neighbours! Where are they?
        if (my_IDy < Num_procsy-1 && my_IDy > 0) {
          // above and below!
          DTYPE * buffer_up = (DTYPE*) events[4].data;
          DTYPE * buffer_down = (DTYPE*) events[6].data;
          readCommBuffer(buffer_up, in, UP);
          readCommBuffer(buffer_down, in, DOWN);
        } else if (my_IDx > 0 && my_IDx < Num_procsx-1) {
          // to the left and to the right!
          DTYPE * buffer_left = (DTYPE*) events[4].data;
          DTYPE * buffer_right = (DTYPE*) events[6].data;
          readCommBuffer(buffer_left, in, LEFT);
          readCommBuffer(buffer_right, in, RIGHT);
        } else if (my_IDy < Num_procsy-1 && my_IDx < Num_procsx-1) {
          // above and to the right!
          DTYPE * buffer_up = (DTYPE*) events[4].data;
          DTYPE * buffer_right = (DTYPE*) events[6].data;
          readCommBuffer(buffer_up, in, UP);
          readCommBuffer(buffer_right, in, RIGHT);
        } else if (my_IDy < Num_procsy-1 && my_IDx > 0) {
          // above and to the left!
          DTYPE * buffer_up = (DTYPE*) events[4].data;
          DTYPE * buffer_left = (DTYPE*) events[6].data;
          readCommBuffer(buffer_up, in, UP);
          readCommBuffer(buffer_left, in, LEFT);
        } else if (my_IDy > 0 && my_IDx < Num_procsx-1) {
          // below and to the right!
          DTYPE * buffer_down = (DTYPE*) events[4].data;
          DTYPE * buffer_right = (DTYPE*) events[6].data;
          readCommBuffer(buffer_down, in, DOWN);
          readCommBuffer(buffer_right, in, RIGHT);
        } else if (my_IDy > 0 && my_IDx > 0) {
          // below and to the left!
          DTYPE * buffer_down = (DTYPE*) events[4].data;
          DTYPE * buffer_left = (DTYPE*) events[6].data;
          readCommBuffer(buffer_down, in, DOWN);
          readCommBuffer(buffer_left, in, LEFT);
        } else {
          // problem
          printf("ERROR: Could not map my neighbours\n");
          exit(EXIT_FAILURE);
        }
        break;
      case 3:
        if (my_IDy < Num_procsy-1 && my_IDy > 0 && my_IDx < Num_procsx-1) {
          // above, below, and to the right!
          DTYPE * buffer_up = (DTYPE*) events[4].data;
          DTYPE * buffer_down = (DTYPE*) events[6].data;
          DTYPE * buffer_right = (DTYPE*) events[8].data;
          readCommBuffer(buffer_up, in, UP);
          readCommBuffer(buffer_down, in, DOWN);
          readCommBuffer(buffer_right, in, RIGHT);
        } else if (my_IDy < Num_procsy-1 && my_IDy > 0 && my_IDx > 0) {
          // above, below, and to the left!
          DTYPE * buffer_up = (DTYPE*) events[4].data;
          DTYPE * buffer_down = (DTYPE*) events[6].data;
          DTYPE * buffer_left = (DTYPE*) events[8].data;
          readCommBuffer(buffer_up, in, UP);
          readCommBuffer(buffer_down, in, DOWN);
          readCommBuffer(buffer_left, in, LEFT);
        } else if (my_IDy < Num_procsy-1 && my_IDx > 0 && my_IDx < Num_procsx-1) {
          // above, to the left, and to the right!
          DTYPE * buffer_up = (DTYPE*) events[4].data;
          DTYPE * buffer_left = (DTYPE*) events[6].data;
          DTYPE * buffer_right = (DTYPE*) events[8].data;
          readCommBuffer(buffer_up, in, UP);
          readCommBuffer(buffer_left, in, LEFT);
          readCommBuffer(buffer_right, in, RIGHT);
        } else if (my_IDy > 0 && my_IDx > 0 && my_IDx < Num_procsx-1) {
          // below, to the left, and to the right!
          DTYPE * buffer_down = (DTYPE*) events[4].data;
          DTYPE * buffer_left = (DTYPE*) events[6].data;
          DTYPE * buffer_right = (DTYPE*) events[8].data;
          readCommBuffer(buffer_down, in, DOWN);
          readCommBuffer(buffer_left, in, LEFT);
          readCommBuffer(buffer_right, in, RIGHT);
        } else {
          // problem
          printf("ERROR: Could not map my neighbours\n");
          exit(EXIT_FAILURE);
        }
        break;
      case 4:
        // surrounded!
        {
          DTYPE * buffer_up = (DTYPE*) events[4].data;
          DTYPE * buffer_down = (DTYPE*) events[6].data;
          DTYPE * buffer_left = (DTYPE*) events[8].data;
          DTYPE * buffer_right = (DTYPE*) events[10].data;
          readCommBuffer(buffer_up, in, UP);
          readCommBuffer(buffer_down, in, DOWN);
          readCommBuffer(buffer_left, in, LEFT);
          readCommBuffer(buffer_right, in, RIGHT);
        }
        break;
    }

  current_it++;
  if (current_it<=iterations) {
    edatFireEvent(&current_it, EDAT_INT, 1, EDAT_SELF, "iterations");

    double runtime;
    if (current_it == 1) {
      runtime=wtime();
    } else {
      runtime=*((double*) events[3].data);
    }
    edatFireEvent(in, EDAT_DTYPE, total_length_in, EDAT_SELF, "in");
    edatFireEvent(out, EDAT_DTYPE, total_length_out, EDAT_SELF, "out");
    edatFireEvent(&runtime, EDAT_DOUBLE, 1, EDAT_SELF, "start_time");

  } else {
    double runtime=wtime() - *((double*) events[3].data);
    edatFireEvent(&runtime, EDAT_DOUBLE, 1, ROOT_PROCESS, "runtime");
      /* compute L1 norm in parallel                                                */
    DTYPE local_norm = (DTYPE) 0.0;
    for (int j=MAX(jstart,RADIUS); j<=MIN(n-RADIUS-1,jend); j++) {
      for (int i=MAX(istart,RADIUS); i<=MIN(n-RADIUS-1,iend); i++) {
        local_norm += (DTYPE)ABS(OUT(i,j));
      }
    }
    edatFireEvent(&local_norm, EDAT_DOUBLE, 1, ROOT_PROCESS, "localnorm");
  }

}

static void displayResults(int my_ID, DTYPE norm, int iterations, double stencil_time, int n) {
  DTYPE f_active_points = (DTYPE) (n-2*RADIUS)*(DTYPE) (n-2*RADIUS);

  DTYPE reference_norm = (DTYPE) 0.0;
  norm /= f_active_points;
  if (RADIUS > 0) {
    reference_norm = (DTYPE) (iterations+1) * (COEFX + COEFY);
  }
  if (ABS(norm-reference_norm) > EPSILON) {
    fprintf(stderr, "ERROR: L1 norm = "FSTR", Reference L1 norm = "FSTR"\n", norm, reference_norm);
    exit(EXIT_FAILURE);
  } else {
     printf("Solution validates\n");
#if VERBOSE
    printf("Reference L1 norm = "FSTR", L1 norm = "FSTR"\n", reference_norm, norm);
#endif
  }

  /* flops/stencil: 2 flops (fma) for each point in the stencil,
      plus one flop for the update of the input of the array        */
  int stencil_size = 4*RADIUS+1;
  DTYPE flops = (DTYPE) (2*stencil_size+1) * f_active_points;
  double avgtime = stencil_time/iterations;
  printf("Rate (MFlops/s): "FSTR"  Avg time (s): %lf\n", 1.0E-06 * flops/avgtime, avgtime);
}

static void initialise_in_out_arrays(DTYPE  ** RESTRICT in_x, DTYPE  ** RESTRICT out_x, int my_ID, int width, int height, int jstart, int jend, int istart, int iend) {
  long total_length_in  = (long) (width+2*RADIUS)*(long) (height+2*RADIUS)*sizeof(DTYPE);
  long total_length_out = (long) width* (long) height*sizeof(DTYPE);

  *in_x = (DTYPE *) prk_malloc(total_length_in);
  *out_x = (DTYPE *) prk_malloc(total_length_out);
  if (!*in_x || !*out_x) {
    printf("ERROR: rank %d could not allocate space for input/output array\n", my_ID);
    exit(EXIT_FAILURE);
  }

  DTYPE * RESTRICT in = *in_x;
  DTYPE * RESTRICT out = *out_x;

  /* intialize the input and output arrays                                     */
  for (int j=jstart; j<=jend; j++) {
    for (int i=istart; i<=iend; i++) {
      IN(i,j)  = COEFX*i+COEFY*j;
      OUT(i,j) = (DTYPE)0.0;
    }
  }
}

static void allocate_comms_buffers(DTYPE **comm_buffer_up, DTYPE **comm_buffer_down, DTYPE **comm_buffer_left, DTYPE **comm_buffer_right, int width, int height, int my_ID) {
  *comm_buffer_up = (DTYPE *) prk_malloc(4*sizeof(DTYPE) * RADIUS * width);
  if (!*comm_buffer_up) {
    printf("ERROR: Rank %d could not allocated comm_buffer_up\n", my_ID);
    exit(EXIT_FAILURE);
  }
  *comm_buffer_down = (DTYPE *) prk_malloc(4*sizeof(DTYPE) * RADIUS * width);
  if (!*comm_buffer_down) {
    printf("ERROR: Rank %d could not allocated comm_buffer_down\n", my_ID);
    exit(EXIT_FAILURE);
  }
  *comm_buffer_left = (DTYPE *) prk_malloc(4*sizeof(DTYPE) * RADIUS * height);
  if (!*comm_buffer_left) {
    printf("ERROR: Rank %d could not allocated comm_buffer_left\n", my_ID);
    exit(EXIT_FAILURE);
  }
  *comm_buffer_right = (DTYPE *) prk_malloc(4*sizeof(DTYPE) * RADIUS * height);
  if (!*comm_buffer_right) {
    printf("ERROR: Rank %d could not allocated comm_buffer_right\n", my_ID);
    exit(EXIT_FAILURE);
  }
}

static void errorCheckParameters(int my_ID, int argc, int iterations, long nsquare, int num_procs, int radius, int n) {
  if (my_ID == ROOT_PROCESS) {
    printf("Parallel Research Kernels version %s\n", PRKVERSION);
    printf("EDAT stencil execution on 2D grid\n");
  }
#if !STAR
  if (my_ID == ROOT_PROCESS) fprintf(stderr, "ERROR: Compact stencil not supported\n");
  exit(EXIT_FAILURE);
#endif
  if (argc != 3){
    if (my_ID == ROOT_PROCESS) fprintf(stderr, "Usage: stencil <# iterations> <array dimension> \n");
    exit(EXIT_FAILURE);
  }

  if (iterations < 1){
    if (my_ID == ROOT_PROCESS) fprintf(stderr, "ERROR: iterations must be >= 1 : %d \n", iterations);
    exit(EXIT_FAILURE);
  }

  if (nsquare < num_procs){
    if (my_ID == ROOT_PROCESS) fprintf(stderr, "ERROR: grid size %ld must be at least # ranks: %d\n", nsquare, num_procs);
    exit(EXIT_FAILURE);
  }

  if (radius < 0) {
    if (my_ID == ROOT_PROCESS) fprintf(stderr, "ERROR: Stencil radius %d should be non-negative\n", radius);
    exit(EXIT_FAILURE);
  }

  if (2*radius +1 > n) {
    if (my_ID == ROOT_PROCESS) fprintf(stderr, "ERROR: Stencil radius %d exceeds grid size %d\n", radius, n);
    exit(EXIT_FAILURE);
  }
}

static void initialise_stencil_weights(DTYPE weight[2*RADIUS+1][2*RADIUS+1]) {
  /* fill the stencil weights to reflect a discrete divergence operator         */
  for (int jj=-RADIUS; jj<=RADIUS; jj++) {
    for (int ii=-RADIUS; ii<=RADIUS; ii++){
      WEIGHT(ii,jj) = (DTYPE) 0.0;
    }
  }

  for (int ii=1; ii<=RADIUS; ii++) {
    WEIGHT(0, ii) = WEIGHT( ii,0) =  (DTYPE) (1.0/(2.0*ii*RADIUS));
    WEIGHT(0,-ii) = WEIGHT(-ii,0) = -(DTYPE) (1.0/(2.0*ii*RADIUS));
  }
}

static void displayPreamble(int num_procs, int n, int radius, int num_procsx, int num_procsy, int iterations) {
  printf("Number of ranks        = %d\n", num_procs);
  printf("Grid size              = %d\n", n);
  printf("Radius of stencil      = %d\n", radius);
  printf("Tiles in x/y-direction = %d/%d\n", num_procsx, num_procsy);
  printf("Type of stencil        = star\n");
#if DOUBLE
  printf("Data type              = double precision\n");
#else
  printf("Data type              = single precision\n");
#endif
#if LOOPGEN
  printf("Script used to expand stencil loop body\n");
#else
  printf("Compact representation of stencil loop body\n");
#endif
  printf("Number of iterations   = %d\n", iterations);
}

static void prepareCommBuffer(DTYPE * buffer, DTYPE * in, int direction) {
  int kk = 0;
  // fills a buffer before sending
  switch (direction) {
    case UP:
      // up
      for (int j=jend-RADIUS+1; j<=jend; j++) {
        for (int i=istart; i<=iend; i++) {
          buffer[kk++]= IN(i,j);
        }
      }
      break;
    case RIGHT:
      // right
      for (int j=jstart; j<=jend; j++) {
        for (int i=iend-RADIUS+1; i<=iend; i++) {
          buffer[kk++]= IN(i,j);
        }
      }
      break;
    case DOWN:
      // down
      for (int j=jstart; j<=jstart+RADIUS-1; j++) {
        for (int i=istart; i<=iend; i++) {
          buffer[kk++]= IN(i,j);
        }
      }
      break;
    case LEFT:
      // left
      for (int j=jstart; j<=jend; j++) {
        for (int i=istart; i<=istart+RADIUS-1; i++) {
          buffer[kk++]= IN(i,j);
        }
      }
      break;
  }

  return;
}

static void readCommBuffer(DTYPE * buffer, DTYPE * in, int direction) {
  int kk = 0;

  switch (direction) {
    case UP:
      for (int j=jend+1; j<=jend+RADIUS; j++) {
        for (int i=istart; i<=iend; i++) {
          IN(i,j) = buffer[kk++];
        }
      }
      break;
    case RIGHT:
      for (int j=jstart; j<=jend; j++) {
        for (int i=iend+1; i<=iend+RADIUS; i++) {
          IN(i,j) = buffer[kk++];
        }
      }
      break;
    case DOWN:
      for (int j=jstart-RADIUS; j<=jstart-1; j++) {
        for (int i=istart; i<=iend; i++) {
          IN(i,j) = buffer[kk++];
        }
      }
      break;
    case LEFT:
      for (int j=jstart; j<=jend; j++) {
        for (int i=istart-RADIUS; i<=istart-1; i++) {
          IN(i,j) = buffer[kk++];
        }
      }
      break;
  }

  return;
}

static void firstHaloSwap(EDAT_Event * events, int num_events) {
  DTYPE * in = events[0].data;
  DTYPE * recv_buffer = events[1].data;
  int total_length_in  = (width+2*RADIUS) * (height+2*RADIUS);

  if (events[1].metadata.source == top_nbr) {
    readCommBuffer(recv_buffer, in, UP);
    DTYPE * comm_buffer_up = (DTYPE*) malloc(RADIUS*width*sizeof(DTYPE));
    prepareCommBuffer(comm_buffer_up, in, UP);
    edatFireEvent(comm_buffer_up, EDAT_DTYPE, RADIUS*width, top_nbr, "buffer");
    free(comm_buffer_up);
  } else if (events[1].metadata.source == bottom_nbr) {
    readCommBuffer(recv_buffer, in, DOWN);
    DTYPE * comm_buffer_down = (DTYPE*) malloc(RADIUS*width*sizeof(DTYPE));
    prepareCommBuffer(comm_buffer_down, in, DOWN);
    edatFireEvent(comm_buffer_down, EDAT_DTYPE, RADIUS*width, bottom_nbr, "buffer");
    free(comm_buffer_down);
  }
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
  edatFireEvent(in, EDAT_DTYPE, total_length_in, EDAT_SELF, "in");

  return;
}
