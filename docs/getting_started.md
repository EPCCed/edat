# Getting Started

In this documents folder we have a number of separate file which discuss different aspects of EDAT and the API. 

* <a href="https://github.com/EPCCed/edat/blob/master/docs/concepts.md">Crucial concepts</a> which describes the underlying ideas behind EDAT
* <a href="https://github.com/EPCCed/edat/blob/master/docs/tasks.md">Task scheduling</a> which discusses the scheduling and management of tasks
* <a href="https://github.com/EPCCed/edat/blob/master/docs/events.md">Event firing</a> which describes how to fire events

# Initialisation of EDAT
At the start of your code you should initialise EDAT by calling the _edatInit_ function which has the signature `int edatInit(int * argc, char *** argv, struct edat_struct_configuration*)`. The first argument is the pointer to the number of arguments, the second the pointer to the arguments themselves and the third the pointer to EDAT specific configuration options. This third argument is, in addition to environment variables, how the programmer can configure EDAT and it is fine to pass _NULL_ if no code configuration is desired. 

The _edat_struct_configuration_ structure has three members, _char**_ keys (pointer to strings of keys), _char**_ values (pointer to strings of values) and the integer number of entries in the configuration structure, _num_entries_.

# Finalisation of EDAT
Once your main function has come to an end you should call _edatFinalise_ which has the API signature `int edatFinalise(void)`. This will put the main thread to sleep (consume no CPU cycles) until termination and may optionally (depending how you have configured EDAT) reuse this main thread as a worker thread to execute tasks upon.

# Getting the rank of a process
A process can call _edatGetRank_ to retrieve its rank, the API call is `int edatGetRank(void)`.

# Getting the total number of processes
A process can call _edatGetNumRanks_ to retrieve the total number of processes executing, the API call is `int edatGetNumRanks(void)`.

# Getting the local id of a worker thread
A task executing on a worker thread can obtain the local id of that worker thread via the _edatGetThread_ function, the API call is `int edatGetThread(void)`

# Getting the total number of local worker threads
A process can obtain the total number of available (idle or busy) local worker threads via the _edatGetNumThreads_ function, the API call is `int edatGetNumThreads(void)`.
