#include "../par-res-kern_general.h"
#include <mpi.h>

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
static void errorCheckParameters(int, int, long, int, int, int);
static void initialise_in_out_arrays(DTYPE ** RESTRICT, DTYPE ** RESTRICT, int, int, int, int, int, int, int);
static void initialise_stencil_weights(DTYPE [2*RADIUS+1][2*RADIUS+1]);
static void allocate_comms_buffers(DTYPE**, DTYPE**, DTYPE**, DTYPE**, DTYPE**, DTYPE**, DTYPE**, DTYPE**, int, int, int);
static void displayResults(int, DTYPE, int, double, int);

int main(int argc, char ** argv) {

  int    Num_procs;       /* number of ranks                                     */
  int    Num_procsx, Num_procsy; /* number of ranks in each coord direction      */
  int    my_ID;           /* MPI rank                                            */
  int    my_IDx, my_IDy;  /* coordinates of rank in rank grid                    */
  int    right_nbr;       /* global rank of right neighboring tile               */
  int    left_nbr;        /* global rank of left neighboring tile                */
  int    top_nbr;         /* global rank of top neighboring tile                 */
  int    bottom_nbr;      /* global rank of bottom neighboring tile              */
  DTYPE *top_buf_out;     /* communication buffer                                */
  DTYPE *top_buf_in;      /*       "         "                                   */
  DTYPE *bottom_buf_out;  /*       "         "                                   */
  DTYPE *bottom_buf_in;   /*       "         "                                   */
  DTYPE *right_buf_out;   /*       "         "                                   */
  DTYPE *right_buf_in;    /*       "         "                                   */
  DTYPE *left_buf_out;    /*       "         "                                   */
  DTYPE *left_buf_in;     /*       "         "                                   */
  int    n, width, height;/* linear global and local grid dimension              */
  long   nsquare;         /* total number of grid points                         */
  int    iter, leftover;  /* dummies                   */
  int    istart, iend;    /* bounds of grid tile assigned to calling rank        */
  int    jstart, jend;    /* bounds of grid tile assigned to calling rank        */
  DTYPE  norm,            /* L1 norm of solution                                 */
         local_norm;
  int    iterations;      /* number of times to run the algorithm                */
  double local_stencil_time,/* timing parameters                                 */
         stencil_time;
  DTYPE  * RESTRICT in;   /* input grid values                                   */
  DTYPE  * RESTRICT out;  /* output grid values                                  */
  DTYPE  weight[2*RADIUS+1][2*RADIUS+1]; /* weights of points in the stencil     */
  MPI_Request request[8];

  /*******************************************************************************
  ** Initialize the MPI environment
  ********************************************************************************/
  MPI_Init(&argc,&argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &my_ID);
  MPI_Comm_size(MPI_COMM_WORLD, &Num_procs);

  /*******************************************************************************
  ** process, test, and broadcast input parameters
  ********************************************************************************/

  if (argc != 3){
    if (my_ID == ROOT_PROCESS) fprintf(stderr, "Usage: stencil <# iterations> <array dimension> \n");
    exit(EXIT_FAILURE);
  }

  iterations = atoi(*++argv);
  n = atoi(*++argv);
  nsquare = (long) n * (long) n;

  errorCheckParameters(my_ID, iterations, nsquare, Num_procs, RADIUS, n);

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
    allocate_comms_buffers(&top_buf_out, &top_buf_in, &bottom_buf_out, &bottom_buf_in, &right_buf_out, &right_buf_in, &left_buf_out, &left_buf_in, width, height, my_ID);
  }

  for (iter = 0; iter<=iterations; iter++) {
    /* start timer after a warmup iteration */
    if (iter == 1) {
      MPI_Barrier(MPI_COMM_WORLD);
      local_stencil_time = wtime();
    }

    /* need to fetch ghost point data from neighbors in y-direction                 */
    if (my_IDy < Num_procsy-1) {
      MPI_Irecv(top_buf_in, RADIUS*width, MPI_DTYPE, top_nbr, 101, MPI_COMM_WORLD, &(request[1]));
      for (int kk=0,j=jend-RADIUS+1; j<=jend; j++) {
        for (int i=istart; i<=iend; i++) {
          top_buf_out[kk++]= IN(i,j);
        }
      }
      MPI_Isend(top_buf_out, RADIUS*width,MPI_DTYPE, top_nbr, 99, MPI_COMM_WORLD, &(request[0]));
    }
    if (my_IDy > 0) {
      MPI_Irecv(bottom_buf_in,RADIUS*width, MPI_DTYPE, bottom_nbr, 99, MPI_COMM_WORLD, &(request[3]));
      for (int kk=0,j=jstart; j<=jstart+RADIUS-1; j++) {
        for (int i=istart; i<=iend; i++) {
          bottom_buf_out[kk++]= IN(i,j);
        }
      }
      MPI_Isend(bottom_buf_out, RADIUS*width,MPI_DTYPE, bottom_nbr, 101, MPI_COMM_WORLD, &(request[2]));
    }
    if (my_IDy < Num_procsy-1) {
      MPI_Wait(&(request[0]), MPI_STATUS_IGNORE);
      MPI_Wait(&(request[1]), MPI_STATUS_IGNORE);
      for (int kk=0,j=jend+1; j<=jend+RADIUS; j++) {
        for (int i=istart; i<=iend; i++) {
          IN(i,j) = top_buf_in[kk++];
        }
      }
    }
    if (my_IDy > 0) {
      MPI_Wait(&(request[2]), MPI_STATUS_IGNORE);
      MPI_Wait(&(request[3]), MPI_STATUS_IGNORE);
      for (int kk=0,j=jstart-RADIUS; j<=jstart-1; j++) {
        for (int i=istart; i<=iend; i++) {
          IN(i,j) = bottom_buf_in[kk++];
        }
      }
    }

    /* need to fetch ghost point data from neighbors in x-direction                 */
    if (my_IDx < Num_procsx-1) {
      MPI_Irecv(right_buf_in, RADIUS*height, MPI_DTYPE, right_nbr, 1010, MPI_COMM_WORLD, &(request[1+4]));
      for (int kk=0,j=jstart; j<=jend; j++) {
        for (int i=iend-RADIUS+1; i<=iend; i++) {
          right_buf_out[kk++]= IN(i,j);
        }
      }
      MPI_Isend(right_buf_out, RADIUS*height, MPI_DTYPE, right_nbr, 990, MPI_COMM_WORLD, &(request[0+4]));
    }
    if (my_IDx > 0) {
      MPI_Irecv(left_buf_in, RADIUS*height, MPI_DTYPE, left_nbr, 990, MPI_COMM_WORLD, &(request[3+4]));
      for (int kk=0,j=jstart; j<=jend; j++) {
        for (int i=istart; i<=istart+RADIUS-1; i++) {
          left_buf_out[kk++]= IN(i,j);
        }
      }
      MPI_Isend(left_buf_out, RADIUS*height, MPI_DTYPE, left_nbr, 1010, MPI_COMM_WORLD, &(request[2+4]));
    }
    if (my_IDx < Num_procsx-1) {
      MPI_Wait(&(request[0+4]), MPI_STATUS_IGNORE);
      MPI_Wait(&(request[1+4]), MPI_STATUS_IGNORE);
      for (int kk=0,j=jstart; j<=jend; j++) {
        for (int i=iend+1; i<=iend+RADIUS; i++) {
          IN(i,j) = right_buf_in[kk++];
        }
      }
    }
    if (my_IDx > 0) {
      MPI_Wait(&(request[2+4]), MPI_STATUS_IGNORE);
      MPI_Wait(&(request[3+4]), MPI_STATUS_IGNORE);
      for (int kk=0,j=jstart; j<=jend; j++) {
        for (int i=istart-RADIUS; i<=istart-1; i++) {
          IN(i,j) = left_buf_in[kk++];
        }
      }
    }

    /* Apply the stencil operator */
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

    /* add constant to solution to force refresh of neighbor data, if any */
    for (int j=jstart; j<=jend; j++) {
      for (int i=istart; i<=iend; i++) {
        IN(i,j)+= 1.0;
      }
    }

  } /* end of iterations                                                   */

  local_stencil_time = wtime() - local_stencil_time;
  MPI_Reduce(&local_stencil_time, &stencil_time, 1, MPI_DOUBLE, MPI_MAX, ROOT_PROCESS, MPI_COMM_WORLD);

  /* compute L1 norm in parallel                                                */
  local_norm = (DTYPE) 0.0;
  for (int j=MAX(jstart,RADIUS); j<=MIN(n-RADIUS-1,jend); j++) {
    for (int i=MAX(istart,RADIUS); i<=MIN(n-RADIUS-1,iend); i++) {
      local_norm += (DTYPE)ABS(OUT(i,j));
    }
  }


  norm = (DTYPE) 0.0;
  MPI_Reduce(&local_norm, &norm, 1, MPI_DTYPE, MPI_SUM, ROOT_PROCESS, MPI_COMM_WORLD);

  if (my_ID == ROOT_PROCESS) displayResults(my_ID, norm, iterations, stencil_time, n);

  MPI_Finalize();
  exit(EXIT_SUCCESS);
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

static void allocate_comms_buffers(DTYPE **top_buf_out, DTYPE **top_buf_in, DTYPE **bottom_buf_out, DTYPE **bottom_buf_in,
                                   DTYPE **right_buf_out, DTYPE **right_buf_in, DTYPE **left_buf_out, DTYPE **left_buf_in,
                                   int width, int height, int my_ID) {
  *top_buf_out = (DTYPE *) prk_malloc(4*sizeof(DTYPE)*RADIUS*width);
  if (!*top_buf_out) {
    printf("ERROR: Rank %d could not allocated comm buffers for y-direction\n", my_ID);
    exit(EXIT_FAILURE);
  }
  *top_buf_in     = *top_buf_out +   RADIUS*width;
  *bottom_buf_out = *top_buf_out + 2*RADIUS*width;
  *bottom_buf_in  = *top_buf_out + 3*RADIUS*width;

  *right_buf_out  = (DTYPE *) prk_malloc(4*sizeof(DTYPE)*RADIUS*height);
  if (!*right_buf_out) {
    printf("ERROR: Rank %d could not allocated comm buffers for x-direction\n", my_ID);
    exit(EXIT_FAILURE);
  }
  *right_buf_in   = *right_buf_out +   RADIUS*height;
  *left_buf_out   = *right_buf_out + 2*RADIUS*height;
  *left_buf_in    = *right_buf_out + 3*RADIUS*height;
}

static void errorCheckParameters(int my_ID, int iterations, long nsquare, int num_procs, int radius, int n) {
  if (my_ID == ROOT_PROCESS) {
    printf("Parallel Research Kernels version %s\n", PRKVERSION);
    printf("MPI stencil execution on 2D grid\n");
  }
#if !STAR
  if (my_ID == ROOT_PROCESS) fprintf(stderr, "ERROR: Compact stencil not supported\n");
  exit(EXIT_FAILURE);
#endif
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
