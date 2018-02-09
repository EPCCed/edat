#ifndef SRC_EDAT_H_
#define SRC_EDAT_H_

#ifdef __cplusplus
extern "C" {
#endif

struct edat_struct_metadata {
  int data_type, number_elements, source, my_rank;
  const char *unique_id;
};

typedef struct edat_struct_metadata EDAT_Metadata;

int edatInit(int *, char ***);
int edatFinalise(void);
int edatScheduleTask(void (*)(void *, EDAT_Metadata), char*);


#ifdef __cplusplus
}
#endif

#endif /* SRC_EDAT_H_ */
