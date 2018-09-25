# Python bindings
EDAT comes with Python bindings which means that users can call EDAT from their Python code seamlessly. The Python module can be found at `include\edat.py`. Whilst many of the EDAT calls are very similar, this page details the differences between Python EDAT and the other documentation found here.

## Initialising EDAT via Python
The same `edatInit` call is used to initialise EDAT and there are two possible, optional, parameters that can be supplied. Firstly a dictionary of configuration options can be provided by the user, for instance `edatInit(configuration={'EDAT_REPORT_WORKER_MAPPING':'true'})` (it is of-course fine to omit the _configuration_ naming if you stick to parameter ordering.) The second possible parameter is the location of the EDAT library (libedat.so), by default it will use your local directory as the location of the library but this can be set explicitly via the _libraryPath_ option, for instance `edatInit(libraryPath="../../../libedat.so")`

```python
from edat import *

params = {'EDAT_REPORT_WORKER_MAPPING':'true'}
edatInit(params, libraryPath="../../../libedat.so")
print("Hello: "+str(edatGetRank()))
edatFinalise()
```
## Accessing event data
When a task executes and you want to interact with the input events, the metadata can be accessed as usual. For the data itself you can access it directly via the _data_ member but we suggest the _get_data_ method which will package the data up into the appropriate type and return this seamlessly, cutting down on the boilerplate required in your code.

```python
from edat import *

def my_task(events, num_events):
  print(events[0].get_data())

edatInit(libraryPath="../../../libedat.so")
if edatGetRank() == 0:
  edatSubmitTask(my_task, 2, 1, "test", 1, "hello")
else:
  data=22
  edatFireEvent(data, EDAT_INT, 1, 0, "test")
  edatFireEvent(None, EDAT_NOTYPE, 0, 0, "hello")
edatFinalise()
```

In this example the task running on a worker of process 0 will display the data associated with the input event. This task is fired twice, firstly with some integer data and secondly without any payload data. Hence in the first case the integer _22_ will be displayed and the second instane of the task displays _None_.
