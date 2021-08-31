//Omer Mizrachi 313263493

#include "threadPool.h"

/* ThreadPool Related */
ThreadPool* tpCreate(int numOfThreads) {
    pthread_mutexattr_t mtx_attr;
    /* Thread Pool */
    ThreadPool *result = (ThreadPool *) malloc(sizeof(ThreadPool));
    if (result == NULL) {
        HandleError(MallocErr, result);
        return NULL; // HandleError will exit anyway, to shut the compiler.
    }

    /* Threads Array */
    result->threads = (pthread_t *) malloc(sizeof(pthread_t) * numOfThreads);
    if (result->threads == NULL) {
        HandleError(MallocErr, result);
    }

    /* Tasks List */
    result->tasks = osCreateQueue();
    if (result->tasks == NULL) {
        HandleError(MallocErr, result);
    }

    /* MutexAttr */
    if (pthread_mutexattr_init(&mtx_attr)) {
        HandleError(MutexAttrInitErr, result);
    }
    if (pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_ERRORCHECK)){
        HandleError(MutexAttrSetTypeErr, result);
    }

    /* Mutex & Cond */
    if (pthread_mutex_init(&(result->tasks_mtx),&mtx_attr)) {
        HandleError(MutexInitErr, result);
    }
    if (pthread_mutex_init(&(result->threads_mtx),&mtx_attr)) {
        HandleError(MutexInitErr, result);
    }
    if (pthread_cond_init(&(result->ready_cond), NULL)) {
        HandleError(CondInitErr, result);
    }
    if (pthread_mutexattr_destroy(&mtx_attr)) {
        HandleError(MutexAttrDestroyErr, result);
    }

    /* Init Values */
    result->threads_count = numOfThreads;
    result->still_processing = 1;
    result->active_threads = 0;
    result->ready_tasks = 0;
    result->running = 1;

    /* Create threads & start waiting for tasks */
    StartThreads(result, (void *(*)(void *)) ProcessAllTasks);

    return result;
}
void StartThreads(ThreadPool* pool, void*(*start_func)(void*)) {
    size_t i;

    for(i=0; i<pool->threads_count; ++i)
        if(pthread_create(&(pool->threads[i]), NULL, start_func, pool))
            HandleError(PthreadCreateErr, pool);
}
int tpInsertTask(ThreadPool* threadPool, void(*computeFunc)(void*), void* param) {
    Task* toAdd;

    if(threadPool == NULL) {
        HandleError(PoolNullWarning, threadPool);
        return PoolNullWarning;
    }

    MutexLock(threadPool, &(threadPool->tasks_mtx)); // lock
    /* Pool not being destroyed */
    if(!threadPool->running) {
        MutexUnlock(threadPool, &(threadPool->tasks_mtx));
        return TpDestroyCalled;
    }

    /* Prepare Task */
    toAdd = malloc(sizeof(Task));
    if(toAdd == NULL) {
        HandleError(MallocErr, threadPool);
        return MallocErr;
    }
    toAdd->func = computeFunc;
    toAdd->args = param;

    /* Insert it */
    ++threadPool->ready_tasks;
    osEnqueue(threadPool->tasks, toAdd);
    MutexUnlock(threadPool, &(threadPool->tasks_mtx)); // unlock

    /* Notify new task is ready */
    CondSignal(threadPool, &(threadPool->ready_cond));
    return 0;
}
void ProcessAllTasks(ThreadPool* pool) {
    Task *task;

    /*Good explanation about pthread_cond_signal & pthread_cond_wait at:
     * https://stackoverflow.com/questions/16522858/understanding-of-pthread-cond-wait-and-pthread-cond-signal */
    while(1) {
        /* Lock and wait for task (block) */
        MutexLock(pool, &(pool->tasks_mtx));
        SelfBlockUntilTask(pool);

        /* Pool being destroyed AND no more tasks OR force exit (should'nt wait) */
        if(!pool->running && (osIsQueueEmpty(pool->tasks) || !pool->still_processing)) {

            /* If there's a thread blocked, wake him up so he will exit */
            CondSignal(pool, &(pool->ready_cond));
            MutexUnlock(pool, &(pool->tasks_mtx));
            break;
        }

        /* Take a task */
        task = osDequeue(pool->tasks);
        MutexUnlock(pool, &(pool->tasks_mtx));

        /* Update active threads counter & process task */
        MutexLock(pool, &(pool->threads_mtx));
        ++(pool->active_threads);
        MutexUnlock(pool, &(pool->threads_mtx));

        ProcessTask(task);

        MutexLock(pool, &(pool->threads_mtx));
        --(pool->active_threads);
        MutexUnlock(pool, &(pool->threads_mtx));
    }
}
void tpDestroy(ThreadPool* threadPool, int shouldWaitForTasks) {
    if(threadPool == NULL)
        return HandleError(PoolNullErr, threadPool);

    MutexLock(threadPool, &(threadPool->tasks_mtx));
    threadPool->running = 0;
    threadPool->still_processing = shouldWaitForTasks;
    MutexUnlock(threadPool, &(threadPool->tasks_mtx));

    JoinAllThreads(threadPool);

    /* Free\Release All */
    osDestroyQueue(threadPool->tasks);
    free(threadPool->threads);
    if(pthread_mutex_destroy(&(threadPool->tasks_mtx)) != 0) {
        HandleError(MutexDestroyErr, threadPool);
    }
    if(pthread_mutex_destroy(&(threadPool->threads_mtx)) != 0) {
        HandleError(MutexDestroyErr, threadPool);
    }
    if(pthread_cond_destroy(&(threadPool->ready_cond)) != 0) {
        HandleError(CondDestroyErr, threadPool);
    }
    free(threadPool);
}
void JoinAllThreads(ThreadPool* pool) {
    size_t i;
    /* Join all threads that are not done */
    for(i=0; i<pool->threads_count; ++i)
        if(pthread_join(pool->threads[i], NULL) != 0)
            HandleError(PthreadJoinErr, pool);
}

/* Thread Pool Helpers */
void SelfBlockUntilTask(ThreadPool* pool) {
    while(osIsQueueEmpty(pool->tasks) && pool->running)
        CondWait(pool,&(pool->ready_cond), &(pool->tasks_mtx));
}
void ProcessTask(Task* toProcess) {
    if(toProcess != NULL && toProcess->func != NULL) {
        toProcess->func(toProcess->args);
        DestroyTask(toProcess);
    }
}

/* Error Handling Related */
void HandleError(error error, ThreadPool* pool) {
    int shouldExit=0;

    switch(error) {
        case MallocErr:{
            perror("Error in system call: malloc\n");
            shouldExit=1;
            break;
        }
        case MutexInitErr:{
            perror("Error in system call: pthread_mutex_init\n");
            shouldExit=1;
            break;
        }
        case MutexDestroyErr:{
            perror("Error in system call: pthread_mutex_destroy\n");
            shouldExit=1;
            break;
        }
        case MutexLockErr:{
            perror("Error in system call: pthread_mutex_lock\n");
            shouldExit=1;
            break;
        }
        case MutexUnlockErr:{
            perror("Error in system call: pthread_mutex_unlock\n");
            shouldExit=1;
            break;
        }
        case PthreadCreateErr:{
            perror("Error in system call: pthread_create\n");
            shouldExit=1;
            break;
        }
        case PthreadJoinErr:{
            perror("Error in system call: pthread_join\n");
            shouldExit=1;
            break;
        }
        case PthreadCancelErr:{ //not in use, possible future use?
            perror("Error in system call: pthread_cancel\n");
            shouldExit=1;
            break;
        }
        case CondSignalErr:{
            perror("Error in system call: pthread_cond_signal\n");
            shouldExit=1;
            break;
        }
        case CondWaitErr:{
            perror("Error in system call: pthread_cond_wait\n");
            shouldExit=1;
            break;
        }
        case PoolNullWarning:{
            perror("Debug: Can't insert task the pool is NULL\n");//debug
            shouldExit=0;
            break;
        }
        case PoolNullErr:{
            perror("Debug: Can't destroy a NULL pool\n");//debug
            shouldExit=1;
            break;
        }
        case MutexAttrInitErr: {
            perror("Error in system call: pthread_mutexattr_init\n");
            shouldExit=1;
            break;
        }
        case MutexAttrDestroyErr: {
            perror("Error in system call: pthread_mutexattr_destroy\n");
            shouldExit=1;
            break;
        }
        case MutexAttrSetTypeErr: {
            perror("Error in system call: pthread_mutexattr_settype\n");
            shouldExit=1;
            break;
        }
        case CondInitErr: {
            perror("Error in system call: pthread_cond_init\n");
            shouldExit=1;
            break;
        }
        case CondDestroyErr: {
            perror("Error in system call: pthread_cond_destroy\n");
            shouldExit=1;
            break;
        }
        default:{
            perror("Debug: Unexpected error occurred\n"); //debug
            shouldExit=1;
            break;
        }
    }
    if(!shouldExit || pool == NULL)
        return;

    /* Case tpDestroy was not called by now
     * O.W: Pool destroy called already, so even if user
     * has requested to wait on remaining tasks - force exit will occur
     * and the remaining tasks will not be processed (except for those who
     * have already been claimed by a thread) */
    if(pool->running) {
        pool->running = 0;
        tpDestroy(pool, 0);
    } else {
        pool->still_processing = 0;
    }
}
void MutexLock(ThreadPool* pool, pthread_mutex_t *mtx) {
    if(pthread_mutex_lock(mtx) != 0)
        HandleError(MutexLockErr, pool);
}
void MutexUnlock(ThreadPool* pool, pthread_mutex_t *mtx) {
    if(pthread_mutex_unlock(mtx) != 0)
        HandleError(MutexUnlockErr, pool);
}
void CondSignal(ThreadPool* pool, pthread_cond_t *cond) {
    if(pthread_cond_signal(cond) != 0)
        HandleError(CondSignalErr, pool);
}
void CondWait(ThreadPool* pool, pthread_cond_t *cond, pthread_mutex_t *mtx) {
    if(pthread_cond_wait(cond, mtx) != 0)
        HandleError(CondWaitErr, pool);
}

/* Task Related */
Task* CreateTask() {
    return malloc(sizeof(Task));
} // made it for symmetry
void DestroyTask(Task* toDestroy) {
    if(toDestroy == NULL)
        return;
    free(toDestroy);
}
