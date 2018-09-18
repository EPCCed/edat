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

#include <thread>
#include <condition_variable>
#include <mutex>
#include "threadpackage.h"
#include "misc.h"

/**
* Attaches a thread if the core id is not -1 then map the thread to the specific core.
* This is useful as we might create multiple threads (for instance when others are paused
* and want the core mapping to be unchanged.)
*/
void ThreadPackage::attachThread(std::thread* tp, int core_id) {
  this->thread=tp;
  if (core_id != -1) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    int rc = pthread_setaffinity_np(thread->native_handle(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) raiseError("Error setting pthread affinity in mapping threads to cores");
  }
}

/**
* Determines whether a specific thread ID matches the thread represented by this package or not
*/
bool ThreadPackage::doesMatch(std::thread::id checkingThreadId) {
  if (thread != NULL) {
    return thread->get_id() == checkingThreadId;
  } else {
    return threadId == checkingThreadId;
  }
}

void ThreadPackage::abort() {
  abort_thread=true;
  resume();
}

/**
* Pauses the thread
*/
void ThreadPackage::pause() {
  std::unique_lock<std::mutex> lck=std::unique_lock<std::mutex>(*m);
  if (!completed) cv->wait(lck, [this]{return (this->completed);});
  completed=false;
}

/**
* Resumes the thread
*/
void ThreadPackage::resume() {
  std::unique_lock<std::mutex> lck=std::unique_lock<std::mutex>(*m);
  completed=true;
  cv->notify_one();
}
