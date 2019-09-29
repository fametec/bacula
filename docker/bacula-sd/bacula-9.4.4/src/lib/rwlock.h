/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

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
/*
 * Bacula Thread Read/Write locking code. It permits
 *  multiple readers but only one writer.
 *
 *  Kern Sibbald, January MMI
 *
 *  This code adapted from "Programming with POSIX Threads", by
 *    David R. Butenhof
 */

#ifndef __RWLOCK_H
#define __RWLOCK_H 1

typedef struct s_rwlock_tag {
   pthread_mutex_t   mutex;
   pthread_cond_t    read;            /* wait for read */
   pthread_cond_t    write;           /* wait for write */
   pthread_t         writer_id;       /* writer's thread id */
   int               priority;        /* used in deadlock detection */
   int               valid;           /* set when valid */
   int               r_active;        /* readers active */
   int               w_active;        /* writers active */
   int               r_wait;          /* readers waiting */
   int               w_wait;          /* writers waiting */
} brwlock_t;

typedef struct s_rwsteal_tag {
   pthread_t         writer_id;       /* writer's thread id */
   int               state;
} brwsteal_t;


#define RWLOCK_VALID  0xfacade

#define RWL_INIIALIZER \
   {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, \
    PTHREAD_COND_INITIALIZER, RWLOCK_VALID, 0, 0, 0, 0}

#define rwl_writelock(x)     rwl_writelock_p((x), __FILE__, __LINE__)

/*
 * read/write lock prototypes
 */
extern int rwl_init(brwlock_t *wrlock, int priority=0);
extern int rwl_destroy(brwlock_t *rwlock);
extern int rwl_readlock(brwlock_t *rwlock);
extern int rwl_readtrylock(brwlock_t *rwlock);
extern int rwl_readunlock(brwlock_t *rwlock);
extern int rwl_writelock_p(brwlock_t *rwlock,
                           const char *file="*unknown*", int line=0);
extern int rwl_writetrylock(brwlock_t *rwlock);
extern int rwl_writeunlock(brwlock_t *rwlock);
extern bool is_rwl_valid(brwlock_t *rwl);

#endif /* __RWLOCK_H */
