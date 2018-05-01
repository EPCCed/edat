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
#define INDEXIN(i,j)  (i+RADIUS+(j+RADIUS)*(context->width+2*RADIUS))
/* need to add offset of RADIUS to j to account for ghost points                 */
#define IN(i,j)       context->in[INDEXIN(i-context->istart,j-context->jstart)]
#define INDEXOUT(i,j) (i+(j)*(context->width))
#define OUT(i,j)      context->out[INDEXOUT(i-context->istart,j-context->jstart)]
#define WEIGHT(ii,jj) context->weight[ii+RADIUS][jj+RADIUS]

struct mycontext {
  int Num_procsx, Num_procsy, my_IDx, my_IDy, right_nbr, left_nbr, top_nbr, bottom_nbr, n, width, height, istart, iend, jstart, jend, iterations,
  num_neighbours;
  DTYPE * comm_buffer_up, * comm_buffer_down, * comm_buffer_left, * comm_buffer_right;
  long nsquare;
  DTYPE * RESTRICT in, * RESTRICT out;
  DTYPE weight[2*RADIUS+1][2*RADIUS+1];
};

static void displayPreamble(int, int, int, int, int, int);
static void errorCheckParameters(int, int, int, long, int, int, int);
static void initialise_in_out_arrays(struct mycontext *, int);
static void initialise_stencil_weights(struct mycontext *);
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

int MYCONTEXT_TYPE;

int main(int argc, char ** argv) {
  int    Num_procs;       /* number of ranks                                     */
  int    my_ID;           /* MPI rank                                            */
  int    iter, leftover;  /* dummies                   */
  /*******************************************************************************
  ** Initialize the MPI environment
  ********************************************************************************/
  edatInit(&argc, &argv);
  my_ID=edatGetRank();
  Num_procs=edatGetNumRanks();

  MYCONTEXT_TYPE=edatDefineContext(sizeof(struct mycontext));
  struct mycontext * context=(struct mycontext*) edatCreateContext(MYCONTEXT_TYPE);
  edatFirePersistentEvent(context, MYCONTEXT_TYPE, 1, EDAT_SELF, "context");


  /*******************************************************************************
  ** process, test, and broadcast input parameters
  ********************************************************************************/

  context->iterations = atoi(*++argv);
  context->n = atoi(*++argv);
  context->nsquare = (long) context->n * (long) context->n;

  errorCheckParameters(my_ID, argc, context->iterations, context->nsquare, Num_procs, RADIUS, context->n);

  /* determine best way to create a 2D grid of ranks (closest to square)     */
  factor(Num_procs, &(context->Num_procsx), &(context->Num_procsy));

  context->my_IDx = my_ID%context->Num_procsx;
  context->my_IDy = my_ID/context->Num_procsx;
  /* compute neighbors; don't worry about dropping off the edges of the grid */
  context->right_nbr  = my_ID+1;
  context->left_nbr   = my_ID-1;
  context->top_nbr    = my_ID+context->Num_procsx;
  context->bottom_nbr = my_ID-context->Num_procsx;

  if (my_ID == ROOT_PROCESS) {
    displayPreamble(Num_procs, context->n, RADIUS, context->Num_procsx, context->Num_procsy, context->iterations);
  }

  /* compute amount of space required for input and solution arrays             */
  context->width = context->n/context->Num_procsx;
  leftover = context->n%context->Num_procsx;
  if (context->my_IDx < leftover) {
    context->istart = (context->width+1) * context->my_IDx;
    context->iend = context->istart + context->width;
  } else {
    context->istart = (context->width+1) * leftover + context->width * (context->my_IDx-leftover);
    context->iend = context->istart + context->width - 1;
  }

  context->width = context->iend - context->istart + 1;
  if (context->width == 0) {
    printf("ERROR: rank %d has no work to do\n", my_ID);
    exit(EXIT_FAILURE);
  }

  context->height = context->n/context->Num_procsy;
  leftover = context->n%context->Num_procsy;
  if (context->my_IDy<leftover) {
    context->jstart = (context->height+1) * context->my_IDy;
    context->jend = context->jstart + context->height;
  } else {
    context->jstart = (context->height+1) * leftover + context->height * (context->my_IDy-leftover);
    context->jend = context->jstart + context->height - 1;
  }

  context->height = context->jend - context->jstart + 1;
  if (context->height == 0) {
    printf("ERROR: rank %d has no work to do\n", my_ID);
    exit(EXIT_FAILURE);
  }

  if (context->width < RADIUS || context->height < RADIUS) {
    printf("ERROR: rank %d has work tile smaller then stencil radius\n", my_ID);
    exit(EXIT_FAILURE);
  }

  initialise_in_out_arrays(context, my_ID);
  initialise_stencil_weights(context);

  context->num_neighbours=0;
  if (context->my_IDy < context->Num_procsy-1) context->num_neighbours++;
  if (context->my_IDy > 0) context->num_neighbours++;
  if (context->my_IDx > 0) context->num_neighbours++;
  if (context->my_IDx < context->Num_procsx-1) context->num_neighbours++;

  if (Num_procs > 1) {
    allocate_comms_buffers(&(context->comm_buffer_up), &(context->comm_buffer_down), &(context->comm_buffer_left),
                           &(context->comm_buffer_right), context->width, context->height, my_ID);
  }

  if (my_ID == ROOT_PROCESS) {
    edatScheduleTask(complete_run, 3, EDAT_SELF, "context", EDAT_ALL, "runtime", EDAT_ALL, "localnorm");
  }

  if (context->my_IDy < context->Num_procsy-1) {
    edatSchedulePersistentTask(halo_swap_from_up, 3, EDAT_SELF, "context", context->top_nbr, "buffer", EDAT_SELF, "gethalo_up");
    edatSchedulePersistentTask(halo_swap_to_up, 2, EDAT_SELF, "context", EDAT_SELF, "haloswap_up");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_up");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_up");
  }

  if (context->my_IDy > 0) {
    edatSchedulePersistentTask(halo_swap_from_down, 3, EDAT_SELF, "context", context->bottom_nbr, "buffer", EDAT_SELF, "gethalo_down");
    edatSchedulePersistentTask(halo_swap_to_down, 2, EDAT_SELF, "context", EDAT_SELF, "haloswap_down");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_down");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_down");
  }

  if (context->my_IDx > 0) {
    edatSchedulePersistentTask(halo_swap_from_left, 3, EDAT_SELF, "context", context->left_nbr, "buffer", EDAT_SELF, "gethalo_left");
    edatSchedulePersistentTask(halo_swap_to_left, 2, EDAT_SELF, "context", EDAT_SELF, "haloswap_left");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_left");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_left");
  }

  if (context->my_IDx < context->Num_procsx-1) {
    edatSchedulePersistentTask(halo_swap_from_right, 3, EDAT_SELF, "context", context->right_nbr, "buffer", EDAT_SELF, "gethalo_right");
    edatSchedulePersistentTask(halo_swap_to_right, 2, EDAT_SELF, "context", EDAT_SELF, "haloswap_right");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_right");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_right");
  }

  switch(context->num_neighbours) {
  case 0:
    edatSchedulePersistentTask(compute_kernel, 3+(context->num_neighbours*2), EDAT_SELF, "context", EDAT_SELF, "iterations", EDAT_SELF, "start_time");
    break;
  case 1:
    edatSchedulePersistentTask(compute_kernel, 3+(context->num_neighbours*2), EDAT_SELF, "context", EDAT_SELF, "iterations", EDAT_SELF, "start_time",
                               EDAT_SELF, "halorecv", EDAT_SELF, "halosend");
    break;
  case 2:
    edatSchedulePersistentTask(compute_kernel, 3+(context->num_neighbours*2), EDAT_SELF, "context", EDAT_SELF, "iterations", EDAT_SELF, "start_time",
                               EDAT_SELF, "halorecv", EDAT_SELF, "halosend", EDAT_SELF, "halorecv", EDAT_SELF, "halosend");
    break;
  case 3:
    edatSchedulePersistentTask(compute_kernel, 3+(context->num_neighbours*2), EDAT_SELF, "context", EDAT_SELF, "iterations", EDAT_SELF, "start_time",
                               EDAT_SELF, "halorecv", EDAT_SELF, "halosend", EDAT_SELF, "halorecv", EDAT_SELF, "halosend", EDAT_SELF, "halorecv",
                               EDAT_SELF, "halosend");
    break;
  case 4:
    edatSchedulePersistentTask(compute_kernel, 3+(context->num_neighbours*2), EDAT_SELF, "context", EDAT_SELF, "iterations", EDAT_SELF, "start_time",
                               EDAT_SELF, "halorecv", EDAT_SELF, "halosend", EDAT_SELF, "halorecv", EDAT_SELF, "halosend", EDAT_SELF, "halorecv",
                               EDAT_SELF, "halosend", EDAT_SELF, "halorecv", EDAT_SELF, "halosend");
    break;
  }

  iter=0;
  edatFireEvent(&iter, EDAT_INT, 1, EDAT_SELF, "iterations");
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "start_time");

  edatFinalise();
  exit(EXIT_SUCCESS);
}

static void halo_swap_from_up(EDAT_Event * events, int num_events) {
  struct mycontext * context=(struct mycontext*) events[0].data;
  double * buffer = (double*) events[1].data;

  for (int kk=0,j=context->jend+1; j<=context->jend+RADIUS; j++) {
    for (int i=context->istart; i<=context->iend; i++) {
      IN(i,j) = buffer[kk++];
    }
  }
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halorecv");
}

static void halo_swap_to_up(EDAT_Event * events, int num_events) {
  struct mycontext * context=(struct mycontext*) events[0].data;

  for (int kk=0,j=context->jend-RADIUS+1; j<=context->jend; j++) {
    for (int i=context->istart; i<=context->iend; i++) {
      context->comm_buffer_up[kk++] = IN(i,j);
    }
  }
  edatFireEvent(context->comm_buffer_up, EDAT_DOUBLE, RADIUS*context->width, context->top_nbr, "buffer");
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
}

static void halo_swap_from_down(EDAT_Event * events, int num_events) {
  struct mycontext * context=(struct mycontext*) events[0].data;
  double * buffer = (double*) events[1].data;

  for (int kk=0,j=context->jstart-RADIUS; j<=context->jstart-1; j++) {
    for (int i=context->istart; i<=context->iend; i++) {
      IN(i,j) = buffer[kk++];
    }
  }
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halorecv");
}

static void halo_swap_to_down(EDAT_Event * events, int num_events) {
  struct mycontext * context=(struct mycontext*) events[0].data;

  for (int kk=0,j=context->jstart; j<=context->jstart+RADIUS-1; j++) {
    for (int i=context->istart; i<=context->iend; i++) {
      context->comm_buffer_down[kk++]= IN(i,j);
    }
  }
  edatFireEvent(context->comm_buffer_down, EDAT_DOUBLE, RADIUS*context->width, context->bottom_nbr, "buffer");
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
}

static void halo_swap_from_left(EDAT_Event * events, int num_events) {
  struct mycontext * context=(struct mycontext*) events[0].data;
  double * buffer = (double*) events[1].data;

  for (int kk=0,j=context->jstart; j<=context->jend; j++) {
    for (int i=context->istart-RADIUS; i<=context->istart-1; i++) {
      IN(i,j) = buffer[kk++];
    }
  }
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halorecv");
}

static void halo_swap_to_left(EDAT_Event * events, int num_events) {
  struct mycontext * context=(struct mycontext*) events[0].data;

  for (int kk=0,j=context->jstart; j<=context->jend; j++) {
    for (int i=context->istart; i<=context->istart+RADIUS-1; i++) {
      context->comm_buffer_left[kk++]= IN(i,j);
    }
  }
  edatFireEvent(context->comm_buffer_left, EDAT_DOUBLE, RADIUS*context->height, context->left_nbr, "buffer");
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
}

static void halo_swap_from_right(EDAT_Event * events, int num_events) {
  struct mycontext * context=(struct mycontext*) events[0].data;
  double * buffer = (double*) events[1].data;

  for (int kk=0,j=context->jstart; j<=context->jend; j++) {
    for (int i=context->iend+1; i<=context->iend+RADIUS; i++) {
      IN(i,j) = buffer[kk++];
    }
  }
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halorecv");
}

static void halo_swap_to_right(EDAT_Event * events, int num_events) {
  struct mycontext * context=(struct mycontext*) events[0].data;

  for (int kk=0,j=context->jstart; j<=context->jend; j++) {
    for (int i=context->iend-RADIUS+1; i<=context->iend; i++) {
      context->comm_buffer_right[kk++]= IN(i,j);
    }
  }
  edatFireEvent(context->comm_buffer_right, EDAT_DOUBLE, RADIUS*context->height, context->right_nbr, "buffer");
  edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "halosend");
}

static void complete_run(EDAT_Event * events, int num_events) {
  struct mycontext * context=(struct mycontext*) events[edatFindEvent(events, num_events, EDAT_SELF, "context")].data;

  double overall_time=0.0;
  double overall_norm=0.0;
  int i;
  for (i=0;i<edatGetNumRanks();i++) {
    double specific_time=*((double*) events[edatFindEvent(events, num_events, i, "runtime")].data);
    if (overall_time < specific_time) overall_time=specific_time;
    overall_norm+=*((double*) events[edatFindEvent(events, num_events, i, "localnorm")].data);
  }
  displayResults(ROOT_PROCESS, overall_norm, context->iterations, overall_time, context->n);
}

static void compute_kernel(EDAT_Event * events, int num_events) {
  int current_it=*((int*) events[1].data);
  struct mycontext * context=(struct mycontext*) events[0].data;

  // Apply the stencil operator
  for (int j=MAX(context->jstart,RADIUS); j<=MIN(context->n-RADIUS-1,context->jend); j++) {
    for (int i=MAX(context->istart,RADIUS); i<=MIN(context->n-RADIUS-1,context->iend); i++) {
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
  for (int j=context->jstart; j<=context->jend; j++) {
    for (int i=context->istart; i<=context->iend; i++) {
      IN(i,j)+= 1.0;
    }
  }

  if (context->my_IDy < context->Num_procsy-1) {
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_up");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_up");
  }
  if (context->my_IDy > 0) {
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_down");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_down");
  }
  if (context->my_IDx > 0) {
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_left");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_left");
  }
  if (context->my_IDx < context->Num_procsx-1) {
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "haloswap_right");
    edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, "gethalo_right");
  }

  current_it++;
  if (current_it<=context->iterations) {
    edatFireEvent(&current_it, EDAT_INT, 1, EDAT_SELF, "iterations");

    double runtime;
    if (current_it == 1) {
      runtime=wtime();
    } else {
      runtime=*((double*) events[2].data);
    }
    edatFireEvent(&runtime, EDAT_DOUBLE, 1, EDAT_SELF, "start_time");
  } else {
    double runtime=wtime() - *((double*) events[2].data);
    edatFireEvent(&runtime, EDAT_DOUBLE, 1, ROOT_PROCESS, "runtime");
      /* compute L1 norm in parallel                                                */
    DTYPE local_norm = (DTYPE) 0.0;
    for (int j=MAX(context->jstart,RADIUS); j<=MIN(context->n-RADIUS-1,context->jend); j++) {
      for (int i=MAX(context->istart,RADIUS); i<=MIN(context->n-RADIUS-1,context->iend); i++) {
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

static void initialise_in_out_arrays(struct mycontext * context, int my_ID) {
  long total_length_in  = (long) (context->width+2*RADIUS)*(long) (context->height+2*RADIUS)*sizeof(DTYPE);
  long total_length_out = (long) context->width* (long) context->height*sizeof(DTYPE);

  context->in = (DTYPE *) prk_malloc(total_length_in);
  context->out = (DTYPE *) prk_malloc(total_length_out);
  if (!(context->in) || !(context->out)) {
    printf("ERROR: rank %d could not allocate space for input/output array\n", my_ID);
    exit(EXIT_FAILURE);
  }

  /* intialize the input and output arrays                                     */
  for (int j=context->jstart; j<=context->jend; j++) {
    for (int i=context->istart; i<=context->iend; i++) {
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

static void initialise_stencil_weights(struct mycontext * context) {
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
