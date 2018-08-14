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
void edatRestart(void);
void edatPauseMainThread(void);
void edatScheduleTask_f(void (*)(EDAT_Event*, int), const char*, int, int**, char**, bool, bool);
void edatLockComms(void);
void edatUnlockComms(void);
void edatInitialiseWithCommunicator(int);

#ifdef __cplusplus
}
#endif

#endif /* SRC_EDAT_DEBUG_H_ */
