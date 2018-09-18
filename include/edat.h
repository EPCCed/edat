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

#ifndef SRC_EDAT_H_
#define SRC_EDAT_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EDAT_NOTYPE 0
#define EDAT_NONE 0
#define EDAT_INT 1
#define EDAT_FLOAT 2
#define EDAT_DOUBLE 3
#define EDAT_BYTE 4
#define EDAT_ADDRESS 5
#define EDAT_LONG 6

#define EDAT_ALL -1
#define EDAT_ANY -2
#define EDAT_SELF -3

struct edat_struct_metadata {
  int data_type, number_elements, source;
  char *event_id;
};

typedef struct edat_struct_metadata EDAT_Metadata;

struct edat_struct_event {
  void * data;
  EDAT_Metadata metadata;
};

typedef struct edat_struct_event EDAT_Event;

void edatInit();
void edatInitWithConfiguration(int, char **, char **);
void edatFinalise(void);
int edatGetRank(void);
int edatGetNumRanks(void);
void edatSubmitTask(void (*)(EDAT_Event*, int), int, ...);
void edatSubmitNamedTask(void (*)(EDAT_Event*, int), const char*, int, ...);
void edatSubmitPersistentTask(void (*)(EDAT_Event*, int), int, ...);
void edatSubmitPersistentGreedyTask(void (*)(EDAT_Event*, int), int, ...);
void edatSubmitPersistentNamedTask(void (*)(EDAT_Event*, int), const char*, int, ...);
void edatSubmitPersistentNamedGreedyTask(void (*)(EDAT_Event*, int), const char*, int, ...);
int edatIsTaskSubmitted(const char*);
int edatRemoveTask(const char*);
void edatFireEvent(void*, int, int, int, const char *);
void edatFirePersistentEvent(void*, int, int, int, const char *);
int edatFindEvent(EDAT_Event*, int, int, const char*);
int edatDefineContext(size_t);
void* edatCreateContext(int);
void edatLock(char*);
void edatUnlock(char*);
int edatTestLock(char*);
EDAT_Event* edatWait(int, ...);
EDAT_Event* edatRetrieveAny(int*, int, ...);

#ifdef __cplusplus
}
#endif

#endif /* SRC_EDAT_H_ */
