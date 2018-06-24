# Getting Started

In this documents folder we have a number of separate file which discuss different aspects of EDAT and the API. 

* <a href="https://github.com/EPCCed/edat/blob/master/docs/concepts.md">Crucial concepts</a> which describes the underlying ideas behind EDAT
* <a href="https://github.com/EPCCed/edat/blob/master/docs/tasks.md">Task scheduling</a> which discusses the scheduling and management of tasks
* <a href="https://github.com/EPCCed/edat/blob/master/docs/events.md">Event firing</a> which describes how to fire events

# Initialisation of EDAT
At the start of your code you should initialise EDAT by calling the _edatInit_ function which has the signature `int edatInit(int * argc, char *** argv, struct edat_struct_configuration*)`. The first argument is the pointer to the number of arguments, the second the pointer to the arguments themselves and the third the pointer to EDAT specific configuration options. This third argument is, in addition to environment variables, how the programmer can configure EDAT and it is fine to pass _NULL_ if no code configuration is desired. 

The _edat_struct_configuration_ structure has three members, _char**_ keys (pointer to strings of keys), _char**_ values (pointer to strings of values) and the integer number of entries in the configuration structure, _num_entries_.
