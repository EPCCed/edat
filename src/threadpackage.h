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

#ifndef SRC_THREADPACKAGE_H_
#define SRC_THREADPACKAGE_H_

#include <thread>
#include <condition_variable>
#include <mutex>

class ThreadPackage {
  std::thread * thread;
  std::thread::id threadId;
  std::mutex * m;
  std::condition_variable * cv;
  std::unique_lock<std::mutex> my_lock;
  bool completed, abort_thread;

public:
  ThreadPackage(std::thread * tp) : thread(tp), m(new std::mutex()), cv(new std::condition_variable()), completed(false), abort_thread(false) { }
  ThreadPackage(std::thread::id aId) : thread(NULL), threadId(aId), m(new std::mutex()), cv(new std::condition_variable()), completed(false), abort_thread(false) { }
  ThreadPackage() : thread(NULL), m(new std::mutex()), cv(new std::condition_variable()), completed(false), abort_thread(false) { }

  void attachThread(std::thread*, int);
  bool doesMatch(std::thread::id);
  void pause();
  void resume();
  bool shouldAbort() { return abort_thread; }
  void abort();
};

#endif /* SRC_THREADPACKAGE_H_ */
