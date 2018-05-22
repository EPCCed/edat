#ifndef SRC_METRICS_H
#define SRC_METRICS_H

#include "edat.h"
#include <string>
#include <mutex>
#include <map>
#include <chrono>
#include <queue>

using ns = std::chrono::duration<double,std::nano>;

void metricsInit(void);

struct Timings {
  int num_events = 0;
  ns min = ns::max();
  ns max = ns::min();
  ns sum = ns::zero();
  ns avg = ns::zero();
  std::queue<std::chrono::steady_clock::time_point> start_times;
};

class EDAT_Metrics {
private:
  const int RANK = edatGetRank();
  std::mutex event_times_mutex;
  std::map<std::string,Timings> event_times;
  void process();
  void writeOut();

public:
  void timerStart(std::string);
  void timerStop(std::string);
  void finalise();
};

namespace metrics {
  extern EDAT_Metrics * METRICS;
}

#endif
