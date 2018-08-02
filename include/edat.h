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
void edatScheduleTask(void (*)(EDAT_Event*, int), int, ...);
void edatScheduleNamedTask(void (*)(EDAT_Event*, int), const char*, int, ...);
void edatSchedulePersistentTask(void (*)(EDAT_Event*, int), int, ...);
void edatSchedulePersistentGreedyTask(void (*)(EDAT_Event*, int), int, ...);
void edatSchedulePersistentNamedTask(void (*)(EDAT_Event*, int), const char*, int, ...);
void edatSchedulePersistentNamedGreedyTask(void (*)(EDAT_Event*, int), const char*, int, ...);
int edatIsTaskScheduled(const char*);
int edatDescheduleTask(const char*);
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
