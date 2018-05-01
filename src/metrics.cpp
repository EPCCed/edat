#include "metrics.h"
#include <iostream>
#include <sstream>
#include <map>
#include <queue>
#include <string>
#include <chrono>

namespace metrics {
  EDAT_Metrics * METRICS;
}

void metricsInit(void) {
  // create an EDAT_Metrics object which everyone can access
  metrics::METRICS = new EDAT_Metrics();

  return;
}

void EDAT_Metrics::timerStart(std::string event_name) {
  // if emplace finds that the map already has an entry with the key event_name
  // it will just do nothing, so this is safe
  Timings walltimes = Timings();
  event_times.emplace(event_name, walltimes);

  event_times.at(event_name).num_events++;

  event_times.at(event_name).start_times.push(std::chrono::steady_clock::now());

  return;
}

void EDAT_Metrics::timerStop(std::string event_name) {
  // we use a FIFO queue held in the map to store timer start times
  // this means we assume that an event with the same name will not start and
  // finish before this call to timerStop has had chance to pop its start time
  // - this may or may not be a safe assumption
  std::chrono::steady_clock::time_point t_start = event_times.at(event_name).start_times.front();
  event_times.at(event_name).start_times.pop();
  std::chrono::steady_clock::time_point t_stop = std::chrono::steady_clock::now();

  ns delta_t = t_stop - t_start;

  if (delta_t > event_times.at(event_name).max) event_times.at(event_name).max = delta_t;
  if (delta_t < event_times.at(event_name).min) event_times.at(event_name).min = delta_t;
  event_times.at(event_name).sum += delta_t;

  return;
}

void EDAT_Metrics::process() {
  // at the moment this general sounding memeber function just finishes
  // calculating the average
  std::map<std::string,Timings>::iterator iter;

  for (iter=event_times.begin(); iter!=event_times.end(); ++iter) {
    iter->second.avg = iter->second.sum / iter->second.num_events;
  }

  return;
}

void EDAT_Metrics::writeOut() {
  // we use the stringstream in order to try and prevent processes printing over
  // one another
  std::stringstream buffer;
  std::map<std::string,Timings>::iterator event;

  std::chrono::duration<double> average;
  std::chrono::duration<double> min;
  std::chrono::duration<double> max;
  std::chrono::duration<double> sum;

  buffer.precision(4);
  buffer << std::scientific;
  buffer << "RANK [" << RANK << "] Walltimes (s)\n" << "EVENT\tCOUNT\tMEAN\tMIN\tMAX\tSUM\n";
  for (event=event_times.begin(); event!=event_times.end(); ++event) {
    sum = std::chrono::duration_cast<std::chrono::duration<double>>(event->second.sum);
    average = std::chrono::duration_cast<std::chrono::duration<double>>(event->second.avg);
    min = std::chrono::duration_cast<std::chrono::duration<double>>(event->second.min);
    max = std::chrono::duration_cast<std::chrono::duration<double>>(event->second.max);

    buffer << event->first << "\t" << event->second.num_events << "\t"
      << average.count() << "\t" << min.count() << "\t" << max.count()
      << "\t" << sum.count() << "\n";
  }
  std::cout << buffer.str() << std::endl;

  return;
}

void EDAT_Metrics::finalise() {
  // process, report results, and delete the metrics::METRICS object
  this->process();
  this->writeOut();

  delete this;

  return;
}
