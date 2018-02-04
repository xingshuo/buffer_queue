#ifndef _BQLOCK_H_
#define _BQLOCK_H_

#include <pthread.h>
#include <assert.h>

#define BQ_LOCK_INIT(q) bqlock_init(&(q)->lock);
#define BQ_LOCK(q) bqlock_lock(&(q)->lock);
#define BQ_UNLOCK(q) bqlock_unlock(&(q)->lock);
#define BQ_LOCK_FREE(q) bqlock_destroy(&(q)->lock);

struct bqlock {
    pthread_mutex_t lock;
    pthread_mutexattr_t attr;
};

static inline void
bqlock_init(struct bqlock *lock) {
    assert(pthread_mutexattr_init(&lock->attr) == 0);
    pthread_mutexattr_settype(&lock->attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&lock->lock, &lock->attr);
}

static inline void
bqlock_lock(struct bqlock *lock) {
    pthread_mutex_lock(&lock->lock);
}

static inline int
bqlock_trylock(struct bqlock *lock) {
    return pthread_mutex_trylock(&lock->lock) == 0;
}

static inline void
bqlock_unlock(struct bqlock *lock) {
    pthread_mutex_unlock(&lock->lock);
}

static inline void
bqlock_destroy(struct bqlock *lock) {
    pthread_mutex_destroy(&lock->lock);
    pthread_mutexattr_destroy(&lock->attr);
}

#endif