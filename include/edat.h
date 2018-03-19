#ifndef SRC_EDAT_H_
#define SRC_EDAT_H_

#ifdef __cplusplus
extern "C" {
#endif

#define EDAT_NOTYPE 0
#define EDAT_INT 1
#define EDAT_FLOAT 2
#define EDAT_DOUBLE 3
#define EDAT_BYTE 4
#define EDAT_ADDRESS 5

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

int edatInit(int *, char ***);
int edatFinalise(void);
int edatGetRank();
int edatGetNumRanks();
int edatScheduleTask(void (*)(EDAT_Event*, int), int, ...);
int edatSchedulePersistentTask(void (*)(EDAT_Event*, int), int, ...);
int edatFireEvent(void*, int, int, int, const char *);
int edatFireEventWithReflux(void*, int, int, int, const char *, void (*)(EDAT_Event*, int));


#ifdef __cplusplus
}
#endif

#endif /* SRC_EDAT_H_ */
