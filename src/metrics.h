#ifndef SRC_METRICS_H
#define SRC_METRICS_H

#include "edat.h"
#include <string>
#include <mutex>
#include <map>
#include <chrono>

using ns = std::chrono::duration<long long int,std::nano>;

void metricsInit(void);

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
  const int RANK = edatGetRank();
  std::mutex event_times_mutex;
  std::map<std::string,Timings> event_times;
  int task_time_bins[10] = {0};
  unsigned long int getTimerKey(void);
  void process(void);
  void writeOut(void);

public:
  unsigned long int timerStart(std::string);
  void timerStop(std::string, unsigned long int);
  void finalise(void);
};

namespace metrics {
  extern EDAT_Metrics * METRICS;
}

#endif
