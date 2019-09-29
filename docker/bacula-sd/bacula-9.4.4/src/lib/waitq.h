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
 * Bacula wait queue routines. Permits waiting for something
 *   to be done. I.e. for operator to mount new volume.
 *
 *  Kern Sibbald, March MMI
 *
 *  This code inspired from "Programming with POSIX Threads", by
 *    David R. Butenhof
 *
 */

#ifndef __WAITQ_H
#define __WAITQ_H 1

/*
 * Structure to keep track of wait queue request
 */
typedef struct waitq_ele_tag {
   struct waitq_ele_tag *next;
   int               done_flag;       /* predicate for wait */
   pthread_cont_t    done;            /* wait for completion */
   void             *msg;             /* message to be passed */
} waitq_ele_t;

/*
 * Structure describing a wait queue
 */
typedef struct workq_tag {
   pthread_mutex_t   mutex;           /* queue access control */
   pthread_cond_t    wait_req;        /* wait for OK */
   int               num_msgs;        /* number of waiters */
   waitq_ele_t       *first;          /* wait queue first item */
   waitq_ele_t       *last;           /* wait queue last item */
} workq_t;

extern int waitq_init(waitq_t *wq);
extern int waitq_destroy(waitq_t *wq);
extern int waitq_add(waitq_t *wq, void *msg);

#endif /* __WAITQ_H */
