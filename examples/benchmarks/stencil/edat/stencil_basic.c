#include "../par-res-kern_general.h"
#include "edat.h"

#define ROOT_PROCESS 0

#if DOUBLE
  #define DTYPE     double
  #define MPI_DTYPE MPI_DOUBLE
  #define EPSILON   1.e-8
  #define COEFX     1.0
  #define COEFY     1.0
  #define FSTR      "%lf"
#else
  #define DTYPE     float
  #define MPI_DTYPE MPI_FLOAT
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

static void displayPreamble(int, int, int, int, int, int);
static void errorCheckParameters(int, int, int, long, int, int, int);
static void initialise_in_out_arrays(DTYPE ** RESTRICT, DTYPE ** RESTRICT, int, int, int, int, int, int, int);
static void initialise_stencil_weights(DTYPE [2*RADIUS+1][2*RADIUS+1]);
static void allocate_comms_buffers(DTYPE**, DTYPE**, DTYPE**, DTYPE**, int, int, int);
static void displayResults(int, DTYPE, int, double, int);
static void complete_run(EDAT_Event*, int);
static void compute_kernel(EDAT_Event*, int);
static void halo_swap_from_up(EDAT_Event*, int);
static void halo_swap_to_up(EDAT_Event*, int);
static void halo_swap_from_down(EDAT_Event*, int);
static void halo_swap_to_down(EDAT_Event*, int);
static void halo_swap_from_left(EDAT_Event*, int);
static void halo_swap_to_left(EDAT_Event*, int);
static void halo_swap_from_right(EDAT_Event*, int);
static void halo_swap_to_right(EDAT_Event*, int);

int Num_procsx, Num_procsy, my_IDx, my_IDy, right_nbr, left_nbr, top_nbr, bottom_nbr, n, width, height, istart, iend, jstart, jend, iterations,
  num_neighbours;
DTYPE * comm_buffer_up, * comm_buffer_down, * comm_buffer_left, * comm_buffer_right;
long nsquare;
DTYPE * RESTRICT in, * RESTRICT out;
DTYPE weight[2*RADIUS+1][2*RADIUS+1];

int main(int argc, char ** argv) {
  int    Num_procs;       /* number of ranks                                     */
  int    my_ID;           /* MPI rank                                            */
  int    iter, leftover;  /* dummies                   */
  /*******************************************************************************
  ** Initialize the MPI environment
  ********************************************************************************/
  task_ptr_t task_array[10] = {complete_run, compute_kernel, halo_swap_from_up, halo_swap_to_up, halo_swap_from_down, halo_swap_to_down, halo_swap_from_left, halo_swap_to_left, halo_swap_from_right, halo_swap_to_right};
  edatInit(&argc, &argv, NULL, task_array, 10);
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

  num_neighbours=0;
  if (my_IDy < Num_procsy-1) num_neighbours++;
  if (my_IDy > 0) num_neighbours++;
  if (my_IDx > 0) num_neighbours++;
  if (my_IDx < Num_procsx-1) num_neighbours++;

  if (Num_procs > 1) {
    allocate_comms_buffers(&comm_buffer_up, &comm_buffer_down, &comm_buffer_left, &comm_buffer_right, width, height, my_ID);
  }

  if (my_ID == ROOT_PROCESS) {
    edatScheduleTask(complete_run, 2, EDAT_ALL, "runtime", EDAT_ALL, "localnorm");
  }

  if (my_IDy < Num_procsy-1) {
    edatSchedulePersistentTask(halo_swap_from_up, 2, top_nbr, "buffer", EDAT_SELF, "gethalo_up");
    edatSchedulePersistentTask(halo_swap_to_up, 1, EDAT_SELF, "haloswap_up");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_up");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_up");
  }

  if (my_IDy > 0) {
    edatSchedulePersistentTask(halo_swap_from_down, 2, bottom_nbr, "buffer", EDAT_SELF, "gethalo_down");
    edatSchedulePersistentTask(halo_swap_to_down, 1, EDAT_SELF, "haloswap_down");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_down");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_down");
  }

  if (my_IDx > 0) {
    edatSchedulePersistentTask(halo_swap_from_left, 2, left_nbr, "buffer", EDAT_SELF, "gethalo_left");
    edatSchedulePersistentTask(halo_swap_to_left, 1, EDAT_SELF, "haloswap_left");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_left");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_left");
  }

  if (my_IDx < Num_procsx-1) {
    edatSchedulePersistentTask(halo_swap_from_right, 2, right_nbr, "buffer", EDAT_SELF, "gethalo_right");
    edatSchedulePersistentTask(halo_swap_to_right, 1, EDAT_SELF, "haloswap_right");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_right");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_right");
  }

  switch(num_neighbours) {
  case 0:
    edatSchedulePersistentTask(compute_kernel, 2+(num_neighbours*2), EDAT_SELF, "iterations", EDAT_SELF, "start_time");
    break;
  case 1:
    edatSchedulePersistentTask(compute_kernel, 2+(num_neighbours*2), EDAT_SELF, "iterations", EDAT_SELF, "start_time", EDAT_SELF, "halorecv",
                               EDAT_SELF, "halosend");
    break;
  case 2:
    edatSchedulePersistentTask(compute_kernel, 2+(num_neighbours*2), EDAT_SELF, "iterations", EDAT_SELF, "start_time", EDAT_SELF, "halorecv",
                               EDAT_SELF, "halosend", EDAT_SELF, "halorecv", EDAT_SELF, "halosend");
    break;
  case 3:
    edatSchedulePersistentTask(compute_kernel, 2+(num_neighbours*2), EDAT_SELF, "iterations", EDAT_SELF, "start_time", EDAT_SELF, "halorecv",
                               EDAT_SELF, "halosend", EDAT_SELF, "halorecv", EDAT_SELF, "halosend", EDAT_SELF, "halorecv", EDAT_SELF, "halosend");
    break;
  case 4:
    edatSchedulePersistentTask(compute_kernel, 2+(num_neighbours*2), EDAT_SELF, "iterations", EDAT_SELF, "start_time", EDAT_SELF, "halorecv",
                               EDAT_SELF, "halosend", EDAT_SELF, "halorecv", EDAT_SELF, "halosend", EDAT_SELF, "halorecv", EDAT_SELF, "halosend",
                               EDAT_SELF, "halorecv", EDAT_SELF, "halosend");
    break;
  }

  iter=0;
  edatFireEvent(&iter, EDAT_INT, 1, EDAT_SELF, "iterations");
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "start_time");

  edatFinalise();
  exit(EXIT_SUCCESS);
}

static void halo_swap_from_up(EDAT_Event * events, int num_events) {
  double * buffer = (double*) events[0].data;

  for (int kk=0,j=jend+1; j<=jend+RADIUS; j++) {
    for (int i=istart; i<=iend; i++) {
      IN(i,j) = buffer[kk++];
    }
  }
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halorecv");
}

static void halo_swap_to_up(EDAT_Event * events, int num_events) {
  for (int kk=0,j=jend-RADIUS+1; j<=jend; j++) {
    for (int i=istart; i<=iend; i++) {
      comm_buffer_up[kk++]= IN(i,j);
    }
  }
  edatFireEvent(comm_buffer_up, EDAT_DOUBLE, RADIUS*width, top_nbr, "buffer");
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
}

static void halo_swap_from_down(EDAT_Event * events, int num_events) {
  double * buffer = (double*) events[0].data;

  for (int kk=0,j=jstart-RADIUS; j<=jstart-1; j++) {
    for (int i=istart; i<=iend; i++) {
      IN(i,j) = buffer[kk++];
    }
  }
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halorecv");
}

static void halo_swap_to_down(EDAT_Event * events, int num_events) {
  for (int kk=0,j=jstart; j<=jstart+RADIUS-1; j++) {
    for (int i=istart; i<=iend; i++) {
      comm_buffer_down[kk++]= IN(i,j);
    }
  }
  edatFireEvent(comm_buffer_down, EDAT_DOUBLE, RADIUS*width, bottom_nbr, "buffer");
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
}

static void halo_swap_from_left(EDAT_Event * events, int num_events) {
  double * buffer = (double*) events[0].data;

  for (int kk=0,j=jstart; j<=jend; j++) {
    for (int i=istart-RADIUS; i<=istart-1; i++) {
      IN(i,j) = buffer[kk++];
    }
  }
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halorecv");
}

static void halo_swap_to_left(EDAT_Event * events, int num_events) {
  for (int kk=0,j=jstart; j<=jend; j++) {
    for (int i=istart; i<=istart+RADIUS-1; i++) {
      comm_buffer_left[kk++]= IN(i,j);
    }
  }
  edatFireEvent(comm_buffer_left, EDAT_DOUBLE, RADIUS*height, left_nbr, "buffer");
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
}

static void halo_swap_from_right(EDAT_Event * events, int num_events) {
  double * buffer = (double*) events[0].data;

  for (int kk=0,j=jstart; j<=jend; j++) {
    for (int i=iend+1; i<=iend+RADIUS; i++) {
      IN(i,j) = buffer[kk++];
    }
  }
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halorecv");
}

static void halo_swap_to_right(EDAT_Event * events, int num_events) {
  for (int kk=0,j=jstart; j<=jend; j++) {
    for (int i=iend-RADIUS+1; i<=iend; i++) {
      comm_buffer_right[kk++]= IN(i,j);
    }
  }
  edatFireEvent(comm_buffer_right, EDAT_DOUBLE, RADIUS*height, right_nbr, "buffer");
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
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
  int current_it=*((int*) events[0].data);

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

  if (my_IDy < Num_procsy-1) {
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_up");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_up");
  }
  if (my_IDy > 0) {
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_down");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_down");
  }
  if (my_IDx > 0) {
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_left");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_left");
  }
  if (my_IDx < Num_procsx-1) {
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_right");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_right");
  }

  current_it++;
  if (current_it<=iterations) {
    edatFireEvent(&current_it, EDAT_INT, 1, EDAT_SELF, "iterations");

    double runtime;
    if (current_it == 1) {
      runtime=wtime();
    } else {
      runtime=*((double*) events[1].data);
    }
    edatFireEvent(&runtime, EDAT_DOUBLE, 1, EDAT_SELF, "start_time");
  } else {
    double runtime=wtime() - *((double*) events[1].data);
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
