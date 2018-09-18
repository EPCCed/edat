'''
* Copyright (c) 2018, EPCC, The University of Edinburgh
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*   list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its
*    contributors may be used to endorse or promote products derived from
*    this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
'''

from ctypes import *

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

class EDAT_Configuration(Structure):
  _fields_ = [("key", POINTER(c_char_p)), ("value", POINTER(c_char_p)), ("num_entries", c_int)]

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
_edatlib_ = None

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

def edatInit(configuration=None, libraryPath=None):
  global _edatlib_

  if libraryPath==None:
    _edatlib_=cdll.LoadLibrary('libedat.so')
  else:
    _edatlib_=cdll.LoadLibrary(libraryPath)

  _edatlib_.edatFireEvent.argtypes = [c_void_p, c_int, c_int, c_int, c_char_p]

  if (configuration != None):
    keys = (c_char_p * len(configuration))()
    values = (c_char_p * len(configuration))()

    i=0
    for k in configuration:
      keys[i]=k
      values[i]=configuration[k]
      i+=1
    _edatlib_.edatInitWithConfiguration(len(configuration), keys, values)
  else:
    _edatlib_.edatInit()

def edatFinalise():
  _edatlib_.edatFinalise()

def edatRestart():
  _edatlib_.edatRestart()

def edatPauseMainThread():
  _edatlib_.edatPauseMainThread()

def edatGetRank():
  return _edatlib_.edatGetRank()

def edatGetNumRanks():
  return _edatlib_.edatGetNumRanks()

def edatGetNumThreads():
  return _edatlib_.edatGetNumThreads()

def edatGetThread():
  return _edatlib_.edatGetThread()

def edatSubmitTask(fn, num_events, *args):
  task_fn=TASKFUNCTION(fn)
  _edatlib_.edatSubmitTask(task_fn, num_events, *args)

def edatSubmitNamedTask(fn, task_name, num_events, *args):
  task_fn=TASKFUNCTION(fn)
  _edatlib_.edatSubmitNamedTask(task_fn, task_name, num_events, *args)

def edatSubmitPersistentTask(fn, num_events, *args):
  task_fn=TASKFUNCTION(fn)
  _edatlib_.edatSubmitPersistentTask(task_fn, num_events, *args)

def edatSubmitPersistentNamedTask(fn, task_name, num_events, *args):
  task_fn=TASKFUNCTION(fn)
  _edatlib_.edatSubmitPersistentTask(task_fn, task_name, num_events, *args)

def edatIsTaskSubmitted(task_name):
  return _edatlib_.edatIsTaskSubmitted(task_name)

def edatRemoveTask(task_name):
  _edatlib_.edatRemoveTask(task_name)

def edatFireEvent(data, data_type, data_count, target, event_id):
  _edatlib_.edatFireEvent(_packageEventData(data, data_type, data_count), data_type, data_count, target, event_id)

def edatFirePersistentEvent(data, data_type, data_count, target, event_id):
  _edatlib_.edatFirePersistentEvent(_packageEventData(data, data_type, data_count), data_type, data_count, target, event_id)
