# Python bindings
EDAT comes with Python bindings which means that users can call EDAT from their Python code seamlessly. The Python module can be found at `include\edat.py`. Whilst many of the EDAT calls are very similar, this page details the differences between Python EDAT and the other documentation found here.

## Initialising EDAT via Python
The same `edatInit` call is used to initialise EDAT and there are two possible, optional, parameters that can be supplied. Firstly a dictionary of configuration options can be provided by the user, for instance `edatInit(configuration={'EDAT_REPORT_WORKER_MAPPING':'true'})` (it is of-course fine to omit the _configuration_ naming if you stick to parameter ordering.) The second possible parameter is the location of the EDAT library (libedat.so), by default it will use your local directory as the location of the library but this can be set explicitly via the _libraryPath_ option, for instance `edatInit(libraryPath="../../../libedat.so")`
