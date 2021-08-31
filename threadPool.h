//Omer Mizrachi 313263493

#ifndef OS_EX4__THREADPOOL_H_
#define OS_EX4__THREADPOOL_H_

#include "osqueue.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/* added this after checking the real value of the constant.
 * U2 didnt allow me to use that imported constant for some reason */
#define PTHREAD_MUTEX_ERRORCHECK 2

/* Thread Pool Structs*/
typedef struct thread_pool {
    OSQueue *tasks;
    pthread_t *threads;
    pthread_mutex_t tasks_mtx;
    pthread_mutex_t threads_mtx;
    pthread_cond_t ready_cond; // flags task is ready
    size_t threads_count;
    size_t active_threads; // number of active threads
    size_t ready_tasks; // number of tasks ready to process
    int running; // flags thread pool is running
    int still_processing;
}ThreadPool;
typedef struct task{
    void(*func)(void*);
    void* args;
}Task;

/* Error Handling */
typedef enum error_code {
    MallocErr = -100,
    MutexInitErr,
    MutexDestroyErr,
    MutexLockErr,
    MutexUnlockErr,
    MutexAttrInitErr,
    MutexAttrDestroyErr,
    MutexAttrSetTypeErr,
    PthreadCreateErr,
    PthreadJoinErr,
    PthreadCancelErr, // not in use, keep for possible future use?
    CondInitErr,
    CondDestroyErr,
    CondSignalErr,
    CondWaitErr,

    PoolNullWarning,
    PoolNullErr,
    TpDestroyCalled = -1,
    Ok = 0
}error;

/* Thread Pool Functions */
int tpInsertTask(ThreadPool* threadPool, void(*computeFunc)(void*), void* param); // return 0 for success or -1 if tpDestoy was called.
ThreadPool* tpCreate(int numOfThread);
void tpDestroy(ThreadPool* threadPool, int shouldWaitForTasks);
void ProcessAllTasks(ThreadPool* pool); // the main algorithm for each thread to run
void JoinAllThreads(ThreadPool* pool);

/* Thread Pool Helper Functions */
void SelfBlockUntilTask(ThreadPool* pool); // waits until new task or pool is being destroyed
void ProcessTask(Task* toProcess);
void StartThreads(ThreadPool* pool, void*(*start_func)(void*));

/* Error Handling & Wrapper Functions.
 * Mose functions (except first 2) just wrap
 * An existing function and adds error handling */
void HandleError(error error, ThreadPool* pool); // maybe add should exit flag?
void MutexLock(ThreadPool* pool, pthread_mutex_t *mtx);
void MutexUnlock(ThreadPool* pool, pthread_mutex_t *mtx);
void CondSignal(ThreadPool* pool, pthread_cond_t *cond);
void CondWait(ThreadPool* pool, pthread_cond_t *cond, pthread_mutex_t *mtx);

/* Task Functions */
Task* CreateTask(); // not in use, made for symmetry originally..
void DestroyTask(Task* toDestroy);


#endif //OS_EX4__THREADPOOL_H_
