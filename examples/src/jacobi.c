#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <time.h>
#include "edat.h"

// Boundary value at the LHS of the bar
#define LEFT_VALUE 1.0
// Boundary value at the RHS of the bar
#define RIGHT_VALUE 10.0
// The maximum number of iterations
#define MAX_ITERATIONS 100000
// How often to report the norm
#define REPORT_NORM_PERIOD 1000
// Pick a processor to act as master
#define MASTER 0

void domainDecomposition(int, int, int, int, int*, int*, int*);
void findMyNeighbours(int, int*, int*, int*);
void initJacobi(int*, int*, int*, int*, double**, double**);
void computeJacobi(double**, double**, int*, int*);
void haloExtract(double*, int*, int*, double*);
double localNorm(double*, int*, int*);

// EDAT tasks
void iterTask(EDAT_Event*, int);
void initialNormTask(EDAT_Event*, int);
void globalNormTask(EDAT_Event*, int);
void normUpdateTask(EDAT_Event*, int);
void localNormTask(EDAT_Event*, int);
void boundaryUpdateTask(EDAT_Event*, int);

// timing
double timeDiff(struct timespec, struct timespec);

int main(int argc, char * argv[])
{
	int num_ranks, my_rank, nx, ny, domain_dims[]={0,0}, local_dims[]={0,0};
	int domain_coords[]={-1,-1}, neighbours[]={INT_MIN, INT_MIN, INT_MIN, INT_MIN};
	int mem_dims[]={0,0}, iter=0;
	double norm=INFINITY, initial_norm=0.0, global_norm=0.0, local_norm=0.0;
	double convergence_accuracy, wall_time;
	double *u_k, *u_kp1, *ptr_to_norm=&norm;
	double *ptr_to_initial=&initial_norm, *ptr_to_global=&global_norm;
	struct timespec start, stop;

	edatInit();

	if (argc != 4) {
		fprintf(stderr, "You must provide three command line arguments, the global size in X, the global size in Y and convergence accuracy\n");
		return -1;
	}
	nx=atoi(argv[1]);
	ny=atoi(argv[2]);
	convergence_accuracy=atof(argv[3]);
	num_ranks = edatGetNumRanks();
	my_rank = edatGetRank();

	domainDecomposition(num_ranks, my_rank, nx, ny, domain_dims, local_dims, domain_coords);

	if (my_rank==0) {
		printf("Global size in X=%d, Global size in Y=%d\n", nx, ny);
		printf("Number of domains in X=%d, Number of domains in Y=%d\n\n", domain_dims[0], domain_dims[1]);
	}

	findMyNeighbours(my_rank, domain_coords, domain_dims, neighbours);

	initJacobi(local_dims, domain_coords, domain_dims, mem_dims, &u_k, &u_kp1);

	local_norm = localNorm(u_k, local_dims, mem_dims);

	// start the clock before we fire the first event
	if (my_rank == MASTER) clock_gettime(CLOCK_MONOTONIC, &start);

	edatFireEvent(&local_norm, EDAT_DOUBLE, 1, MASTER, "iN_local_norm");

	// everybody tasks
	edatSubmitPersistentTask(iterTask, 10, MASTER, "iterate",
		EDAT_SELF, "neighbours", EDAT_SELF, "local_dims", EDAT_SELF, "mem_dims",
		EDAT_SELF, "u_k_addr", EDAT_SELF, "u_kp1_addr", EDAT_SELF, "global_norm_addr",
		EDAT_SELF, "initial_norm_addr", EDAT_SELF, "norm_addr",
		EDAT_SELF, "convergence_accuracy");

	// everybody fire at self
	edatFireEvent(&neighbours, EDAT_INT, 4, EDAT_SELF, "neighbours");
	edatFireEvent(&local_dims, EDAT_INT, 2, EDAT_SELF, "local_dims");
	edatFireEvent(&mem_dims, EDAT_INT, 2, EDAT_SELF, "mem_dims");
	edatFireEvent(&u_k, EDAT_ADDRESS, 1, EDAT_SELF, "u_k_addr");
	edatFireEvent(&u_kp1, EDAT_ADDRESS, 1, EDAT_SELF, "u_kp1_addr");
	edatFireEvent(&ptr_to_global, EDAT_ADDRESS, 1, EDAT_SELF, "global_norm_addr");
	edatFireEvent(&ptr_to_initial, EDAT_ADDRESS, 1, EDAT_SELF, "initial_norm_addr");
	edatFireEvent(&ptr_to_norm, EDAT_ADDRESS, 1, EDAT_SELF, "norm_addr");
	edatFireEvent(&convergence_accuracy, EDAT_DOUBLE, 1, EDAT_SELF, "convergence_accuracy");

	// master tasks
	if (my_rank == MASTER) {
		edatSubmitTask(initialNormTask, 2, EDAT_SELF, "iN_initial_norm_addr",
			EDAT_ALL, "iN_local_norm");
		edatSubmitPersistentTask(globalNormTask, 2, EDAT_SELF, "gN_global_norm_addr",
			EDAT_ALL, "local_norm");
		edatSubmitPersistentTask(normUpdateTask, 5, EDAT_SELF, "nU_initial_norm_addr",
			EDAT_SELF, "new_global", EDAT_SELF, "nU_norm_addr",
			EDAT_SELF, "nU_convergence_accuracy", EDAT_SELF, "iter");

		edatFireEvent(&ptr_to_initial, EDAT_ADDRESS, 1, EDAT_SELF, "iN_initial_norm_addr");
		edatFireEvent(&ptr_to_global, EDAT_ADDRESS, 1, EDAT_SELF, "gN_global_norm_addr");
		edatFireEvent(&ptr_to_norm, EDAT_ADDRESS, 1, EDAT_SELF, "nU_norm_addr");
		edatFireEvent(&convergence_accuracy, EDAT_DOUBLE, 1, EDAT_SELF, "nU_convergence_accuracy");
		edatFireEvent(&iter, EDAT_INT, 1, EDAT_SELF, "iter");
	}

	edatFinalise();
	// stop timer
	if (my_rank == MASTER) {
		clock_gettime(CLOCK_MONOTONIC, &stop);
		wall_time = timeDiff(stop, start);
		printf("Total time = %e seconds.\n", wall_time);
	}

	// no leaks, thanks
	free(u_k);
	free(u_kp1);

	return 0;
}

void domainDecomposition(int num_ranks, int my_rank, int nx, int ny,
	int * domain_dims, int * local_dims, int * domain_coords) {
	/* sets local grid sizes for each rank, and gives each rank its location */
	int Xmax = (int) floor(sqrt(num_ranks));

	domain_dims[0] = num_ranks;
	domain_dims[1] = 1;
	for (int i=Xmax; i>1; i--) {
		if (domain_dims[0] % i == 0) {
			domain_dims[0] /= i;
			domain_dims[1] *= i;
			break;
		}
	}

	domain_coords[0] = my_rank / domain_dims[1];
	domain_coords[1] = my_rank % domain_dims[1];

	local_dims[0] = nx / domain_dims[0];
	if (local_dims[0] * domain_dims[0] < nx) {
		if (domain_coords[0] < nx - local_dims[0] * domain_dims[0]) local_dims[0]++;
	}

	local_dims[1] = ny / domain_dims[1];
	if (local_dims[1] * domain_dims[1] < ny) {
		if (domain_coords[1] < ny - local_dims[1] * domain_dims[1]) local_dims[1]++;
	}

	return;
}

void findMyNeighbours(int my_rank, int * domain_coords, int * domain_dims, int * neighbours) {
	/* determines neighbouring ranks and stores them in int vector neighbours
	 * neighbours[North,East,South,West]
	 * uses <limits.h> INT_MIN as a null value for boundaries
	 * we are ignoring the possibility for periodic boundaries for now
	 */
	if (domain_coords[1] != 0) neighbours[0] = my_rank - 1;
	if (domain_coords[0] != 0) neighbours[1] = my_rank - domain_dims[1];
	if (domain_coords[1] != domain_dims[1]-1) neighbours[2] = my_rank + 1;
	if (domain_coords[0] != domain_dims[0]-1) neighbours[3] = my_rank + domain_dims[1];

	return;
}

void initJacobi(int * local_dims, int * domain_coords, int * domain_dims,
	int * mem_dims, double ** u_k, double ** u_kp1) {
	/* allocate and initialise computational arrays, including halos
	 * set boundary conditions for the appropriate ranks
	 */
	int i, numel;

	// halos
	mem_dims[0] = local_dims[0] + 2;
	mem_dims[1] = local_dims[1] + 2;
	numel = mem_dims[0] * mem_dims[1];

	// allocate
	*u_k = (double*) malloc(sizeof(double) * numel);
	*u_kp1 = (double*) malloc(sizeof(double) * numel);

	// set everything to zero, deal with initial boundaries afterwards
	for (i=0; i<numel; ++i) {
		(*u_k)[i] = 0.0;
	}

	// set initial boundaries at the east and west borders
	if (domain_coords[0] == 0) {
		for (i=mem_dims[0]; i<(numel-mem_dims[0]); i+=mem_dims[0]) {
			(*u_k)[i] = RIGHT_VALUE;
		}
	}

	if (domain_coords[0] == domain_dims[0]-1) {
		for (i=(2*mem_dims[0]-1); i<(numel-mem_dims[0]); i+=mem_dims[0]) {
			(*u_k)[i] = LEFT_VALUE;
		}
	}

	// make u_kp1 an exact copy of u_k
	for (i=0; i<numel; i++) (*u_kp1)[i] = (*u_k)[i];

	return;
}

void computeJacobi(double ** u_k, double ** u_kp1, int * local_dims, int * mem_dims) {
	/* one iteration of the actual computational step
	 * intentionally written to be verbose for now at least
	 */
	int row, col, lindex, northdex, eastdex, southdex, westdex;
	double north, south, east, west, *temp = NULL;

	for (row=1; row<local_dims[1]+1; row++) {
		for(col=1; col<local_dims[0]+1; col++) {
			// linear index to update element
			lindex = row * mem_dims[0] + col;

			// linear indices to neighbouring elements
			northdex = lindex - mem_dims[0];
			eastdex = lindex - 1;
			southdex = lindex + mem_dims[0];
			westdex = lindex + 1;

			// pull out elements
			north = (*u_k)[northdex];
			east = (*u_k)[eastdex];
			south = (*u_k)[southdex];
			west = (*u_k)[westdex];

			// compute new element
			(*u_kp1)[lindex] = 0.25 * (north + east + south + west);
		}
	}

	// swap arrays over
	temp = *u_kp1;
	*u_kp1 = *u_k;
	*u_k = temp;

	return;
}

void haloExtract(double * u_k, int * local_dims, int * mem_dims, double * boundary) {
	/* extract internal boundary of array and store it in a new array,
	 * in preparation for sending to neighbours
	 */
	int i, j=0, numel;
	int north_start, east_start, south_start, west_start;

	numel = mem_dims[0] * mem_dims[1];
	north_start = 1 + mem_dims[0];
	east_start = 1 + mem_dims[0];
	south_start = numel-(2*mem_dims[0])+1;
	west_start = 2*mem_dims[0]-2;

	for (i=north_start; i<north_start+local_dims[0]; i++) {
		boundary[j] = u_k[i];
		j++;
	}
	for (i=east_start; i<numel-mem_dims[0]+1; i+=mem_dims[0]) {
		boundary[j] = u_k[i];
		j++;
	}
	for (i=south_start; i<south_start+local_dims[0]; i++) {
		boundary[j] = u_k[i];
		j++;
	}
	for (i=west_start; i<numel-2; i+=mem_dims[0]) {
		boundary[j] = u_k[i];
		j++;
	}

	return;
}

double localNorm(double * u_k, int * local_dims, int * mem_dims) {
	/* calculate the local norm value */
	int row, col, lindex, northdex, eastdex, southdex, westdex;
	double north, east, south, west, local_norm=0.0;

	for (row=1; row<=local_dims[1]; row++) {
		for(col=1; col<=local_dims[0]; col++) {
			// linear index to update element
			lindex = row * mem_dims[0] + col;

			// linear indices to neighbouring elements
			northdex = lindex - mem_dims[0];
			eastdex = lindex - 1;
			southdex = lindex + mem_dims[0];
			westdex = lindex + 1;

			// pull out elements
			north = u_k[northdex];
			east = u_k[eastdex];
			south = u_k[southdex];
			west = u_k[westdex];

			// calculate local norm
			local_norm += pow((4.0*u_k[lindex] - north - east - south - west), 2);
		}
	}

	return local_norm;
}

void iterTask(EDAT_Event * events, int num_events) {
	/* until the iteration limit is reached this task will carry out the
	 * computational step and submit all necessary tasks for the next iteration
	 * this task requires an event fired by normUpdateTask when the new norm
	 * is greater than the convergence_accuracy
	 */
	int i, boundary_length, neighbour=0;
	double *boundary_start=NULL, *boundary;
	const char *compass[] = {"north", "east", "south", "west"};
	int iter_index = edatFindEvent(events, num_events, MASTER, "iterate");
	int neighbour_index = edatFindEvent(events, num_events, EDAT_SELF, "neighbours");
	int ldims_index = edatFindEvent(events, num_events, EDAT_SELF, "local_dims");
	int mdims_index = edatFindEvent(events, num_events, EDAT_SELF, "mem_dims");
	int u_k_index = edatFindEvent(events, num_events, EDAT_SELF, "u_k_addr");
	int u_kp1_index = edatFindEvent(events, num_events, EDAT_SELF, "u_kp1_addr");
	int gnorm_index = edatFindEvent(events, num_events, EDAT_SELF, "global_norm_addr");
	int inorm_index = edatFindEvent(events, num_events, EDAT_SELF, "initial_norm_addr");
	int norm_index = edatFindEvent(events, num_events, EDAT_SELF, "norm_addr");
	int conv_index = edatFindEvent(events, num_events, EDAT_SELF, "convergence_accuracy");

	int iter = *((int *) events[iter_index].data);
	int *neighbours = (int *) events[neighbour_index].data;
	int *local_dims = (int *) events[ldims_index].data;
	int *mem_dims = (int *) events[mdims_index].data;
	double *u_k = *((double **) events[u_k_index].data);
	double *u_kp1 = *((double **) events[u_kp1_index].data);
	double *ptr_to_global = *((double **) events[gnorm_index].data);
	double *ptr_to_initial = *((double **) events[inorm_index].data);
	double *ptr_to_norm = *((double **) events[norm_index].data);
	double convergence_accuracy = *((double *) events[conv_index].data);

	++iter;

	if (iter < MAX_ITERATIONS) {
		// jacobi computational step -> halo swap -> calculate norm
		// submits a local norm calculation for after the halo swap
		edatSubmitTask(localNormTask, 7, EDAT_SELF, "lnorm_u_k_addr",
			EDAT_SELF, "lnorm_local_dims", EDAT_SELF, "lnorm_mem_dims",
			EDAT_SELF, "north", EDAT_SELF, "east", EDAT_SELF, "south", EDAT_SELF, "west");
		edatFireEvent(local_dims, EDAT_INT, 2, EDAT_SELF, "lnorm_local_dims");
		edatFireEvent(mem_dims, EDAT_INT, 2, EDAT_SELF, "lnorm_mem_dims");

		// perform a jacobi computational step
		computeJacobi(&u_k, &u_kp1, local_dims, mem_dims);
		edatFireEvent(&u_k, EDAT_ADDRESS, 1, EDAT_SELF, "lnorm_u_k_addr");

		// halo swap
		boundary_length = 2 * (local_dims[0] + local_dims[1]);
		boundary_start = (double*) malloc(sizeof(double) * boundary_length);
		haloExtract(u_k, local_dims, mem_dims, boundary_start);
		boundary = boundary_start;

		for (i=0; i<4; ++i) {
			neighbour = neighbours[i];
			if(neighbour != INT_MIN) {
				// send boundary to neighbour
				edatFireEvent(boundary, EDAT_DOUBLE, local_dims[i%2], neighbour, "boundary");
				// expect boundary to be received from neighbour
				edatFireEvent(&neighbour, EDAT_INT, 1, EDAT_SELF, "halo_neighbour");
				edatFireEvent(&i, EDAT_INT, 1, EDAT_SELF, "direction");
				edatFireEvent(local_dims, EDAT_INT, 2, EDAT_SELF, "halo_local_dims");
				edatFireEvent(mem_dims, EDAT_INT, 2, EDAT_SELF, "halo_mem_dims");
				edatFireEvent(&u_k, EDAT_ADDRESS, 1, EDAT_SELF, "halo_u_k_addr");
				edatSubmitTask(boundaryUpdateTask, 6, neighbour, "boundary", EDAT_SELF, "halo_neighbour", EDAT_SELF, "direction", EDAT_SELF, "halo_local_dims", EDAT_SELF, "halo_mem_dims", EDAT_SELF, "halo_u_k_addr");
			} else {
				edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, compass[i]);
			}
			boundary += local_dims[i%2];
		}

		// have master fire non-synching events for norm update
		if (edatGetRank() == MASTER) {
			edatFireEvent(&ptr_to_global, EDAT_ADDRESS, 1, EDAT_SELF, "gN_global_norm_addr");
			edatFireEvent(&ptr_to_initial, EDAT_ADDRESS, 1, EDAT_SELF, "nU_initial_norm_addr");
			edatFireEvent(&ptr_to_norm, EDAT_ADDRESS, 1, EDAT_SELF, "nU_norm_addr");
			edatFireEvent(&convergence_accuracy, EDAT_DOUBLE, 1, EDAT_SELF, "nU_convergence_accuracy");
			edatFireEvent(&iter, EDAT_INT, 1, EDAT_SELF, "iter");
		}

		// everybody fire non-synching events to self in case of next iteration
		edatFireEvent(neighbours, EDAT_INT, 4, EDAT_SELF, "neighbours");
		edatFireEvent(local_dims, EDAT_INT, 2, EDAT_SELF, "local_dims");
		edatFireEvent(mem_dims, EDAT_INT, 2, EDAT_SELF, "mem_dims");
		edatFireEvent(&ptr_to_global, EDAT_ADDRESS, 1, EDAT_SELF, "global_norm_addr");
		edatFireEvent(&ptr_to_initial, EDAT_ADDRESS, 1, EDAT_SELF, "initial_norm_addr");
		edatFireEvent(&ptr_to_norm, EDAT_ADDRESS, 1, EDAT_SELF, "norm_addr");
		edatFireEvent(&convergence_accuracy, EDAT_DOUBLE, 1, EDAT_SELF, "convergence_accuracy");
		edatFireEvent(&u_k, EDAT_ADDRESS, 1, EDAT_SELF, "u_k_addr");
		edatFireEvent(&u_kp1, EDAT_ADDRESS, 1, EDAT_SELF, "u_kp1_addr");
	} else {
		if (edatGetRank() == MASTER) printf("\nIteration limit reached. Calculation failed.\n");
	}

	free(boundary_start);
	return;
}

void initialNormTask(EDAT_Event * events, int num_events) {
	/* The relative norm which is checked for convergence is relative to this
	 * value. This tasks runs one-time only, but needs to have happened before
	 * the first computational iteration.
	 */
	int i;
	double tmpnorm=0.0;
	int inorm_index = edatFindEvent(events, num_events, EDAT_SELF, "iN_initial_norm_addr");
	double * initial_norm = *((double **) events[inorm_index].data);

	for (i=0; i<num_events; ++i) {
		if(i != inorm_index) tmpnorm += *((double *) events[i].data);
	}

	*initial_norm = sqrt(tmpnorm);
	edatFireEvent(&initial_norm, EDAT_ADDRESS, 1, EDAT_SELF, "nU_initial_norm_addr");
	edatFireEvent(initial_norm, EDAT_DOUBLE, 1, EDAT_SELF, "new_global");

	return;
}

void globalNormTask(EDAT_Event * events, int num_events) {
	/* collates local norm from all ranks, and uses them to calculate the new
	 * global norm
	 */
	int i;
	double tmpnorm=0.0;
	int gnorm_index = edatFindEvent(events, num_events, EDAT_SELF, "gN_global_norm_addr");
	double * global_norm = *((double **) events[gnorm_index].data);

	for (i=0; i<num_events; ++i) {
		if(i != gnorm_index) {
			tmpnorm += *((double *) events[i].data);
		}
	}

	*global_norm = sqrt(tmpnorm);

	edatFireEvent(global_norm, EDAT_DOUBLE, 1, EDAT_SELF, "new_global");

	return;
}

void normUpdateTask(EDAT_Event * events, int num_events) {
	/* Collects a new global norm, and uses it to calculate the new relative norm.
	 * If the new relative norm is above convergence_accuracy an event is fired
	 * to trigger the next iteration.
	 */
	int norm_index = edatFindEvent(events, num_events, EDAT_SELF, "nU_norm_addr");
	int inorm_index = edatFindEvent(events, num_events, EDAT_SELF, "nU_initial_norm_addr");
	int gnorm_index = edatFindEvent(events, num_events, EDAT_SELF, "new_global");
	int conv_index = edatFindEvent(events, num_events, EDAT_SELF, "nU_convergence_accuracy");
	int iter_index = edatFindEvent(events, num_events, EDAT_SELF, "iter");

	double * norm = *((double **) events[norm_index].data);
	double * initial_norm = *((double **) events[inorm_index].data);
	double global_norm = *((double *) events[gnorm_index].data);
	double convergence_accuracy = *((double *) events[conv_index].data);
	int iter = *((int *) events[iter_index].data);

	*norm = global_norm / *initial_norm;

	if (*norm < convergence_accuracy) {
		printf("Convergence reached. Terminated on %d iterations. Relative norm = %e.\n", iter, *norm);
	} else {
		if (iter % REPORT_NORM_PERIOD == 0) printf("Iteration = %d, Norm = %e\n", iter, *norm);
		edatFireEvent(&iter, EDAT_INT, 1, EDAT_ALL, "iterate");
	}

	return;
}

void localNormTask(EDAT_Event * events, int num_events) {
	/* Calculates the local norm, and then fires it to MASTER so it can be
	 * reduced and the new global norm calculated. Requires confirmation in the
	 * form of four events that a halo swap has been completed
	 */
	int u_k_index = edatFindEvent(events, num_events, EDAT_SELF, "lnorm_u_k_addr");
	int ldims_index = edatFindEvent(events, num_events, EDAT_SELF, "lnorm_local_dims");
	int mdims_index = edatFindEvent(events, num_events, EDAT_SELF, "lnorm_mem_dims");

	double * u_k = *((double **) events[u_k_index].data);
	int *local_dims = (int *) events[ldims_index].data;
	int *mem_dims = (int *) events[mdims_index].data;

	double local_norm = localNorm(u_k, local_dims, mem_dims);

	edatFireEvent(&local_norm, EDAT_DOUBLE, 1, MASTER, "local_norm");

	return;
}

void boundaryUpdateTask(EDAT_Event * events, int num_events) {
	/* Receives halos from relevant neighbours and updates them in the local array. */
	int neighbour_index = edatFindEvent(events, num_events, EDAT_SELF, "halo_neighbour");
	int dir_index = edatFindEvent(events, num_events, EDAT_SELF, "direction");
	int ldims_index = edatFindEvent(events, num_events, EDAT_SELF, "halo_local_dims");
	int mdims_index = edatFindEvent(events, num_events, EDAT_SELF, "halo_mem_dims");
	int u_k_index = edatFindEvent(events, num_events, EDAT_SELF, "halo_u_k_addr");
	const char *compass[] = {"north", "east", "south", "west"};

	int neighbour = *((int *) events[neighbour_index].data);
	int direction = *((int *) events[dir_index].data);
	int *local_dims = (int *) events[ldims_index].data;
	int *mem_dims = (int *) events[mdims_index].data;
	double *u_k = *((double **) events[u_k_index].data);

	int boundary_index = edatFindEvent(events, num_events, neighbour, "boundary");
	double *boundary = (double *) events[boundary_index].data;

	int i, j=0, start=-1, stop=-1, iter=1;
	// selects the correct loop criteria for the boundary elements in u_k
	switch(direction) {
		case 0 :  // north boundary
			start = 1;
			stop = 1 + local_dims[0];
			iter = 1;
			break;

		case 1 :  // east boundary
			start = mem_dims[0];
			stop = mem_dims[0] * (mem_dims[1] - 1);
			iter = mem_dims[0];
			break;

		case 2 :  // south_boundary
			start = mem_dims[0] * (mem_dims[1] - 1) + 1;
			stop = start + local_dims[0];
			iter = 1;
			break;

		case 3 :  // west_boundary
			start = 2*mem_dims[0] - 1;
			stop = mem_dims[0] * mem_dims[1] - 1;
			iter = mem_dims[0];
			break;

		default : // something went wrong...
			fprintf(stderr, "[%d] boundaryUpdateTask failed to find the direction and set loop criteria. direction=%d, dir_index=%d\n", edatGetRank(), direction, dir_index);
	}

	for(i=start; i<stop; i+=iter) {
		u_k[i] = boundary[j];
		++j;
	}

	// let the iterJacobi task know this boundary has been updated
	edatFireEvent(NULL, EDAT_NOTYPE, 0, EDAT_SELF, compass[direction]);

	return;
}

double timeDiff(struct timespec stop, struct timespec start) {
	long int dt, stop_ns, start_ns;

	stop_ns = (long int) stop.tv_sec * 1E9 + stop.tv_nsec;
	start_ns = (long int) start.tv_sec * 1E9 + start.tv_nsec;
	dt = stop_ns - start_ns;

	return (double) 1E-9 * dt;
}
