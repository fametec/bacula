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
/*
 * Bacula work queue routines. Permits passing work to
 *  multiple threads.
 *
 *  Kern Sibbald, January MMI
 *
 *  This code adapted from "Programming with POSIX Threads", by
 *    David R. Butenhof
 */

#ifndef __WORKQ_H
#define __WORKQ_H 1

/*
 * Structure to keep track of work queue request
 */
typedef struct workq_ele_tag {
   struct workq_ele_tag *next;
   void                 *data;
} workq_ele_t;

/*
 * Structure describing a work queue
 */
typedef struct workq_tag {
   pthread_mutex_t   mutex;           /* queue access control */
   pthread_cond_t    work;            /* wait for work */
   pthread_cond_t    idle;            /* wait for idle */
   pthread_attr_t    attr;            /* create detached threads */
   workq_ele_t       *first, *last;   /* work queue */
   int               valid;           /* queue initialized */
   int               quit;            /* workq should quit */
   int               max_workers;     /* max threads */
   int               num_workers;     /* current threads */
   int               idle_workers;    /* idle threads */
   int               num_running;     /* Number of jobs running */
   void             *(*engine)(void *arg); /* user engine */
} workq_t;

#define WORKQ_VALID  0xdec1992

extern int workq_init(
              workq_t *wq,
              int     threads,        /* maximum threads */
              void   *(*engine)(void *)   /* engine routine */
                    );
extern int workq_destroy(workq_t *wq);
extern int workq_add(workq_t *wq, void *element, workq_ele_t **work_item, int priority);
extern int workq_remove(workq_t *wq, workq_ele_t *work_item);
extern int workq_wait_idle(workq_t *wq);


#endif /* __WORKQ_H */
