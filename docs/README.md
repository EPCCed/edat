# Getting Started

In this documents folder we have a number of separate file which discuss different aspects of EDAT and the API. 

* <a href="https://github.com/EPCCed/edat/blob/master/docs/concepts.md">Crucial concepts</a> which describes the underlying ideas behind EDAT
* <a href="https://github.com/EPCCed/edat/blob/master/docs/tasks.md">Task submission</a> which discusses the submission and management of tasks
* <a href="https://github.com/EPCCed/edat/blob/master/docs/events.md">Event firing</a> which describes how to fire events
* <a href="https://github.com/EPCCed/edat/blob/master/docs/in_task_consumption.md">In task consumption</a> of events which allows the programmer to start a task and then consume additional events as it runs
* <a href="https://github.com/EPCCed/edat/blob/master/docs/protecting_shared_data.md">Protecting shared data</a> of multiple tasks running on workers of a process
* <a href="https://github.com/EPCCed/edat/blob/master/docs/configuration.md">Configuration</a> which specifies all the configuration options which can be exported via environment variables or in user code

# Initialisation of EDAT
At the start of your code you should initialise EDAT by calling the _edatInit_ function which has the signature `void edatInit(void)`. It is also possible to initialse EDAT with some specific configuration parameters using the `edatInitWithConfiguration` API call, see <a href="https://github.com/EPCCed/edat/blob/master/docs/configuration.md">configuration</a> for more details.

# Finalisation of EDAT
Once your main function has come to an end you should call _edatFinalise_ which has the API signature `void edatFinalise(void)`. This will put the main thread to sleep (consume no CPU cycles) until termination and may optionally (depending how you have configured EDAT) reuse this main thread as a worker thread to execute tasks upon.

# Getting the rank of a process
A process can call _edatGetRank_ to retrieve its rank, the API call is `int edatGetRank(void)`.

# Getting the total number of processes
A process can call _edatGetNumRanks_ to retrieve the total number of processes executing, the API call is `int edatGetNumRanks(void)`.

# Other language bindings
EDAT is natively callable from C and C++. We have developed bindings for some other languages to enable calling EDAT from a more wide range of codes.

* <a href="https://github.com/EPCCed/edat/blob/master/docs/python.md">Python bindings</a> describes using EDAT in Python codes
* <a href="https://github.com/EPCCed/edat/blob/master/docs/fortran.md">Fortran bindings</a> describes using EDAT in Fortran codes
