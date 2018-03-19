# Environment variables

A number of environment variables can be set which will provide specific configuration options to EDAT. These are all exported via the terminal e.g.

```
export VARIABLE_NAME=VALUE
```

### EDAT_NUM_THREADS

**Value type:** An integer

**Description:** This sets the number of worker threads that EDAT will map tasks onto. By default the main program process is not counted in this number and hence an extra thread. The process "thread" will sleep when the *finalise* function is called. So effectively whilst the main program process is active you will have *EDAT_NUM_THREADS + 1* active threads which will then drop down to *EDAT_NUM_THREADS* once this has called *finalise*. 

```
export EDAT_NUM_THREADS=12
```

Will create 12 worker threads which can execute tasks. Any tasks over and above this are then queued up until an idle thread becomes available.

**Default:** 10

### EDAT_PROGRESS_THREAD

**Value type:** A boolean

**Description:** Determines whether a background progress thread should be created to continually poll for arriving events (and hence task progress), the delivery of events and termination. If *true* then this is an extra thread, additional to the worker threads and will run continually and greedily until program termination. If it is configured not to use a background progress thread then instead an idle worker thread (when one is available) will do the polling until it is interupted by a task. In such a case there is a guarantee that if there are any idle worker threads then one of these will poll for tasks, but tasks take priority and hence there will be no polling when all workers are busy.

```
export EDAT_PROGRESS_THREAD=false
```

**Default:** true
