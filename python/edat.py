from ctypes import *
edatlib = cdll.LoadLibrary('../libedat.so')

EDAT_NOTYPE=0
EDAT_INT=1
EDAT_FLOAT=2
EDAT_DOUBLE=3
EDAT_BYTE=4
EDAT_ADDRESS=5
EDAT_LONG=6

EDAT_ALL=-1
EDAT_ANY=-2
EDAT_SELF=-3

class EDAT_Metadata(Structure):
  _fields_ = [("data_type", c_int), ("number_elements", c_int), ("source", c_int), ("event_id", c_char_p)]

class EDAT_Event(Structure):
  _fields_ = [("data", c_void_p), ("metadata", EDAT_Metadata)]

  def get_raw_data(self, data_type=None):
    if data_type == None: data_type=self.metadata.data_type
    if (data_type == EDAT_NOTYPE):
      return None
    elif (data_type == EDAT_INT):
      return cast(self.data, POINTER(c_int))
    elif (data_type == EDAT_FLOAT):
      return cast(self.data, POINTER(c_float))
    elif (data_type == EDAT_DOUBLE):
      return cast(self.data, POINTER(c_double))
    elif (data_type == EDAT_BYTE):
      return cast(self.data, POINTER(c_byte))
    elif (data_type == EDAT_LONG):
      return cast(self.data, POINTER(c_long))
    print("EDAT Python error: Data type not recognised")
    return None

  def get_data(self, data_type=None):
    raw_data=self.get_raw_data(data_type)
    if raw_data == None: return []
    data=[]
    for i in range(0, self.metadata.number_elements):
      data.append(raw_data[i])
    return data

TASKFUNCTION = CFUNCTYPE(None, POINTER(EDAT_Event), c_int)
edatlib.edatFireEvent.argtypes = [c_void_p, c_int, c_int, c_int, c_char_p]

def _packageEventData(data, data_type, data_count):
  if (data_type == EDAT_ADDRESS):
    print("EDAT Python error: Passing an address is not supported in the Python bindings")

  storage_type=None
  if (data_type == EDAT_INT):
    storage_type=c_int
  elif (data_type == EDAT_FLOAT):
    storage_type=c_float
  elif (data_type == EDAT_DOUBLE):
    storage_type=c_double
  elif (data_type == EDAT_BYTE):
    storage_type=c_byte
  elif (data_type == EDAT_LONG):
    storage_type=c_long

  if (storage_type != None):
    return byref((storage_type * data_count)(data))
  else:
    return None

def edatInit():
  return edatlib.edatInit(None, None, None)

def edatFinalise():
  return edatlib.edatFinalise()

def edatRestart():
  return edatlib.edatRestart()

def edatPauseMainThread():
  return edatlib.edatPauseMainThread()

def edatGetRank():
  return edatlib.edatGetRank()

def edatGetNumRanks():
  return edatlib.edatGetNumRanks()

def edatGetNumThreads():
  return edatlib.edatGetNumThreads()

def edatGetThread():
  return edatlib.edatGetThread()

def edatScheduleTask(fn, num_events, *args):
  task_fn=TASKFUNCTION(fn)
  return edatlib.edatScheduleTask(task_fn, num_events, *args)

def edatScheduleNamedTask(fn, task_name, num_events, *args):
  task_fn=TASKFUNCTION(fn)
  return edatlib.edatScheduleNamedTask(task_fn, task_name, num_events, *args)

def edatSchedulePersistentTask(fn, num_events, *args):
  task_fn=TASKFUNCTION(fn)
  return edatlib.edatSchedulePersistentTask(task_fn, num_events, *args)

def edatSchedulePersistentNamedTask(fn, task_name, num_events, *args):
  task_fn=TASKFUNCTION(fn)
  return edatlib.edatSchedulePersistentTask(task_fn, task_name, num_events, *args)

def edatIsTaskScheduled(task_name):
  return edatlib.edatIsTaskScheduled(task_name)

def edatDescheduleTask(task_name):
  return edatlib.edatDescheduleTask(task_name)

def edatFireEvent(data, data_type, data_count, target, event_id):
  return edatlib.edatFireEvent(_packageEventData(data, data_type, data_count), data_type, data_count, target, event_id)

def edatFirePersistentEvent(data, data_type, data_count, target, event_id):
  return edatlib.edatFirePersistentEvent(_packageEventData(data, data_type, data_count), data_type, data_count, target, event_id)
