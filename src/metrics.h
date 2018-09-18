/*
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
*/

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
