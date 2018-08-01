import sys
sys.path.append("../../../include")
from edat import *

def my_function(events, num_events):
  print(events[0].get_data())

params = {'EDAT_REPORT_WORKER_MAPPING':'true'}
edatInit(params, libraryPath="../../../libedat.so")
print("Hello: "+str(edatGetRank()))
if edatGetRank() == 0:
  edatScheduleTask(my_function, 2, 1, "test", 1, "hello")
  print("Scheduled")
else:
  data=22
  edatFireEvent(data, EDAT_INT, 1, 0, "test")
  edatFireEvent(None, EDAT_NOTYPE, 0, 0, "hello")
  print("Fired")
edatFinalise()
