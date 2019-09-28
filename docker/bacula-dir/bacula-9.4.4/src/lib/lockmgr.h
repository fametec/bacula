/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2017 Kern Sibbald

   The original author of Bacula is Kern Sibbald, with contributions
   from many others, a complete list can be found in the file AUTHORS.

   You may use this file and others of this release according to the
   license defined in the LICENSE file, which includes the Affero General
   Public License, v3.0 ("AGPLv3") and some additional permissions and
   terms pursuant to its AGPLv3 Section 7.

   This notice must be preserved when any source code is 
   conveyed and/or propagated.

   Bacula(R) is a registered trademark of Kern Sibbald.
*/

#ifndef LOCKMGR_H
#define LOCKMGR_H 1

#include "mutex_list.h"     /* Manage mutex with priority in a central place */

/*
 * P and V op that don't use the lock manager (for memory allocation or on
 * win32)
 */
void lmgr_p(pthread_mutex_t *m);
void lmgr_v(pthread_mutex_t *m);

/*
 * Get integer thread id
 */
intptr_t bthread_get_thread_id();

/* 
 * Set the Thread Id of the current thread to limit I/O operations
 */
int bthread_change_uid(uid_t uid, gid_t gid);

#ifdef USE_LOCKMGR

typedef struct bthread_mutex_t
{
   pthread_mutex_t mutex;
   int priority;
} bthread_mutex_t;

/*
 * We decide that a thread won't lock more than LMGR_MAX_LOCK at the same time
 */
#define LMGR_MAX_LOCK 32

int bthread_cond_wait_p(pthread_cond_t *cond,
                        bthread_mutex_t *mutex,
                        const char *file="*unknown*", int line=0);

int bthread_cond_timedwait_p(pthread_cond_t *cond,
                             bthread_mutex_t *mutex,
                             const struct timespec * abstime,
                             const char *file="*unknown*", int line=0);

/* Same with real pthread_mutex_t */
int bthread_cond_wait_p(pthread_cond_t *cond,
                        pthread_mutex_t *mutex,
                        const char *file="*unknown*", int line=0);

int bthread_cond_timedwait_p(pthread_cond_t *cond,
                             pthread_mutex_t *mutex,
                             const struct timespec * abstime,
                             const char *file="*unknown*", int line=0);

/* Replacement of pthread_mutex_lock()  but with real pthread_mutex_t */
int bthread_mutex_lock_p(pthread_mutex_t *m,
                         const char *file="*unknown*", int line=0);

/* Replacement for pthread_mutex_unlock() but with real pthread_mutex_t */
int bthread_mutex_unlock_p(pthread_mutex_t *m,
                           const char *file="*unknown*", int line=0);

/* Replacement of pthread_mutex_lock() */
int bthread_mutex_lock_p(bthread_mutex_t *m,
                         const char *file="*unknown*", int line=0);

/* Replacement of pthread_mutex_unlock() */
int bthread_mutex_unlock_p(bthread_mutex_t *m,
                           const char *file="*unknown*", int line=0);

/*  Test if this mutex is locked by the current thread
 *     0 - not locked by the current thread
 *     1 - locked by the current thread
 */
int lmgr_mutex_is_locked(void *m);

/*
 * Use them when you want use your lock yourself (ie rwlock)
 */

/* Call before requesting the lock */
void lmgr_pre_lock(void *m, int prio=0,
                   const char *file="*unknown*", int line=0);

/* Call after getting it */
void lmgr_post_lock();

/* Same as pre+post lock */
void lmgr_do_lock(void *m, int prio=0,
                  const char *file="*unknown*", int line=0);

/* Call just before releasing the lock */
void lmgr_do_unlock(void *m);

/* We use C++ mangling to make integration eaysier */
int pthread_mutex_init(bthread_mutex_t *m, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(bthread_mutex_t *m);

void bthread_mutex_set_priority(bthread_mutex_t *m, int prio);

/*
 * Each thread have to call this function to put a lmgr_thread_t object
 * in the stack and be able to call mutex_lock/unlock
 */
void lmgr_init_thread();

/*
 * Know if the current thread is registred (used when we
 * do not control thread creation)
 */
bool lmgr_thread_is_initialized();

/*
 * Call this function at the end of the thread
 */
void lmgr_cleanup_thread();

/*
 * Call this at the end of the program, it will release the
 * global lock manager
 */
void lmgr_cleanup_main();

/*
 * Dump each lmgr_thread_t object to stdout
 */
void lmgr_dump();

/*
 * Search a deadlock
 */
bool lmgr_detect_deadlock();

/* Bit flags */
#define LMGR_EVENT_NONE    0
#define LMGR_EVENT_DUP     1       /* use strdup() to copy the comment (will set FREE) */
#define LMGR_EVENT_FREE    2       /* use free() when overwriting/deleting the comment */
#define LMGR_EVENT_INVALID 4       /* Used to mark the record invalid */

/*
 * Add event to the thread event list
 */
void lmgr_add_event_p(const char *comment, intptr_t user_data, int32_t flags, const char *file, int32_t line);
#define lmgr_add_event(c, u) lmgr_add_event_p(c, u, 0, __FILE__, __LINE__)
#define lmgr_add_event_flag(c, u, f) lmgr_add_event_p(c, u, (f), __FILE__, __LINE__)

/*
 * Search a deadlock after a fatal signal
 * no lock are granted, so the program must be
 * stopped.
 */
bool lmgr_detect_deadlock_unlocked();

/*
 * This function will run your thread with lmgr_init_thread() and
 * lmgr_cleanup_thread().
 */
int lmgr_thread_create(pthread_t *thread,
                       const pthread_attr_t *attr,
                       void *(*start_routine)(void*), void *arg);

/*
 * Can use SAFEKILL to check if the argument is a valid threadid
 */
int bthread_kill(pthread_t thread, int sig,
                 const char *file="*unknown*", int line=0);

#define BTHREAD_MUTEX_NO_PRIORITY      {PTHREAD_MUTEX_INITIALIZER, 0}
#define BTHREAD_MUTEX_INITIALIZER      BTHREAD_MUTEX_NO_PRIORITY

/* Define USE_LOCKMGR_PRIORITY to detect mutex wrong order */
#ifdef USE_LOCKMGR_PRIORITY
# define BTHREAD_MUTEX_PRIORITY(p)       {PTHREAD_MUTEX_INITIALIZER, p}
#else
# define BTHREAD_MUTEX_PRIORITY(p)       BTHREAD_MUTEX_NO_PRIORITY
#endif

#define bthread_mutex_lock(x)      bthread_mutex_lock_p(x, __FILE__, __LINE__)
#define bthread_mutex_unlock(x)    bthread_mutex_unlock_p(x, __FILE__, __LINE__)
#define bthread_cond_wait(x,y)     bthread_cond_wait_p(x,y, __FILE__, __LINE__)
#define bthread_cond_timedwait(x,y,z) bthread_cond_timedwait_p(x,y,z, __FILE__, __LINE__)

/*
 * Define LOCKMGR_COMPLIANT to use real pthread functions
 */
#define real_P(x) lmgr_p(&(x))
#define real_V(x) lmgr_v(&(x))

#ifdef LOCKMGR_COMPLIANT
# define P(x)  lmgr_p(&(x))
# define pP(x) lmgr_p(x)
# define V(x)  lmgr_v(&(x))
# define pV(x) lmgr_v(x)
#else
# define P(x)  bthread_mutex_lock_p(&(x), __FILE__, __LINE__)
# define pP(x) bthread_mutex_lock_p((x), __FILE__, __LINE__)
# define V(x)  bthread_mutex_unlock_p(&(x), __FILE__, __LINE__)
# define pV(x) bthread_mutex_unlock_p((x), __FILE__, __LINE__)
# define pthread_create(a, b, c, d)      lmgr_thread_create(a,b,c,d)
# define pthread_mutex_lock(x)           bthread_mutex_lock(x)
# define pthread_mutex_unlock(x)         bthread_mutex_unlock(x)
# define pthread_cond_wait(x,y)          bthread_cond_wait(x,y)
# define pthread_cond_timedwait(x,y,z)   bthread_cond_timedwait(x,y,z)

# ifdef USE_LOCKMGR_SAFEKILL
#  define pthread_kill(a,b)      bthread_kill((a),(b), __FILE__, __LINE__)
# endif
#endif

#else   /* !USE_LOCKMGR */

# define lmgr_detect_deadlock()
# define lmgr_add_event_p(c, u, f, l)
# define lmgr_add_event(c, u)
# define lmgr_dump()
# define lmgr_thread_is_initialized() (1)
# define lmgr_init_thread()
# define lmgr_cleanup_thread()
# define lmgr_pre_lock(m, prio, f, l)
# define lmgr_post_lock()
# define lmgr_do_lock(m, prio, f, l)
# define lmgr_do_unlock(m)
# define lmgr_cleanup_main()
# define bthread_mutex_set_priority(a,b)
# define bthread_mutex_lock(a)           pthread_mutex_lock(a)
# define bthread_mutex_lock_p(a, f, l)   pthread_mutex_lock(a)
# define bthread_mutex_unlock(a)         pthread_mutex_unlock(a)
# define bthread_mutex_unlock_p(a, f, l) pthread_mutex_unlock(a)
# define lmgr_cond_wait(a,b)             pthread_cond_wait(a,b)
# define lmgr_cond_timedwait(a,b,c)      pthread_cond_timedwait(a,b,c)
# define bthread_mutex_t                 pthread_mutex_t
# define P(x)  lmgr_p(&(x))
# define pP(x) lmgr_p((x))
# define V(x)  lmgr_v(&(x))
# define pV(x) lmgr_v((x))
# define BTHREAD_MUTEX_PRIORITY(p)      PTHREAD_MUTEX_INITIALIZER
# define BTHREAD_MUTEX_NO_PRIORITY      PTHREAD_MUTEX_INITIALIZER
# define BTHREAD_MUTEX_INITIALIZER      PTHREAD_MUTEX_INITIALIZER
# define lmgr_mutex_is_locked(m)        (1)
# define bthread_cond_wait_p(w, x, y, z) pthread_cond_wait(w,x)
#endif  /* USE_LOCKMGR */

/* a very basic lock_guard implementation :
 * Lock_guard is mostly usefull to garanty mutex unlocking. Also, it's exception safe. 
 * usage example:
 * void foobar()
 * {
 *    lock_guard protector(m_mutex); // m_mutex is locked
 *    // the following section is protected until the function exits and/or returns
 *
 *    if (case == TRUE)
 *    { 
 *       return; // when returning, m_mutex is unlocked
 *    }
 *    .
 *    .  
 *    .
 *
 *    // when the method exits, m_mutex is unlocked.
 * }
 */
class lock_guard
{
public:
   
   pthread_mutex_t &m_mutex; /* the class keeps a reference on the mutex*/

   explicit lock_guard(pthread_mutex_t &mutex) : m_mutex(mutex)
   {
      P(m_mutex); /* constructor locks the mutex*/
   }
   
   ~lock_guard()
   {
      V(m_mutex); /* destructor unlocks the mutex*/
   }
};

#endif  /* LOCKMGR_H */
