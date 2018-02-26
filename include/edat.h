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

struct edat_struct_metadata {
  int data_type, number_elements, source;
  const char *unique_id;
};

typedef struct edat_struct_metadata EDAT_Metadata;

int edatInit(int *, char ***);
int edatFinalise(void);
int edatGetRank();
int edatGetNumRanks();
int edatScheduleTask(void (*)(void *, EDAT_Metadata), char*);
int edatFireEvent(void*, int, int, int, const char *);


#ifdef __cplusplus
}
#endif

#endif /* SRC_EDAT_H_ */
