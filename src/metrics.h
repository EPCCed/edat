#ifndef SRC_METRICS_H
#define SRC_METRICS_H

#include "edat.h"
#include "configuration.h"
#include <string>
#include <mutex>
#include <map>
#include <vector>
#include <chrono>

using ns = std::chrono::duration<long long int,std::nano>;

struct Timings {
  int num_events = 0;
  ns min = ns::max();
  ns max = ns::min();
  ns sum = ns::zero();
  ns avg = ns::zero();
  std::map<unsigned long int,std::chrono::steady_clock::time_point> start_times;
};

class EDAT_Metrics {
private:
  Configuration & configuration;
  int num_threads;
  unsigned long int edat_timer_key;
  std::mutex event_times_mutex;
  std::map<std::string,Timings> event_times;
  std::vector<ns> thread_active;
  std::vector<double> thread_active_pc;
  int task_time_bins[10] = {0};
  unsigned long int getTimerKey(void);
  void process(void);
  void writeOut(void);

public:
  EDAT_Metrics(Configuration & aconfig);
  void edatTimerStart(void);
  unsigned long int timerStart(std::string);
  void timerStop(std::string, unsigned long int);
  void threadReport(int myThreadId, ns active_time);
  void finalise(void);
};

namespace metrics {
  extern EDAT_Metrics * METRICS;
}

#endif
