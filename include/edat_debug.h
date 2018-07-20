#ifndef SRC_EDAT_DEBUG_H_
#define SRC_EDAT_DEBUG_H_

/*
* This file contains EDAT "debug" functionality that is accessible from codes using this library. We don't advise end users to use this, but
* instead these calls are useful for debugging EDAT and some of the synthetic benchmarks.
*/

#ifdef __cplusplus
extern "C" {
#endif

int edatGetNumWorkers(void);
int edatGetWorker(void);
int edatGetNumActiveWorkers(void);
int edatRestart(void);
int edatPauseMainThread(void);

#ifdef __cplusplus
}
#endif

#endif /* SRC_EDAT_DEBUG_H_ */
