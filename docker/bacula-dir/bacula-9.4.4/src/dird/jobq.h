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
 * Bacula job queue routines.
 *
 *  Kern Sibbald, July MMIII
 *
 *  This code adapted from Bacula work queue code, which was
 *    adapted from "Programming with POSIX Threads", by
 *    David R. Butenhof
 */

#ifndef __JOBQ_H
#define __JOBQ_H 1

/*
 * Structure to keep track of job queue request
 */
struct jobq_item_t {
   dlink link;
   JCR *jcr;
};

/*
 * Structure describing a work queue
 */
struct jobq_t {
   pthread_mutex_t   mutex;           /* queue access control */
   pthread_cond_t    work;            /* wait for work */
   pthread_attr_t    attr;            /* create detached threads */
   dlist            *waiting_jobs;    /* list of jobs waiting */
   dlist            *running_jobs;    /* jobs running */
   dlist            *ready_jobs;      /* jobs ready to run */
   int               valid;           /* queue initialized */
   bool              quit;            /* jobq should quit */
   int               max_workers;     /* max threads */
   int               num_workers;     /* current threads */
   int               idle_workers;    /* idle threads */
   void             *(*engine)(void *arg); /* user engine */
};

#define JOBQ_VALID  0xdec1993

extern int jobq_init(
              jobq_t *wq,
              int     threads,            /* maximum threads */
              void   *(*engine)(void *)   /* engine routine */
                    );
extern int jobq_destroy(jobq_t *wq);
extern int jobq_add(jobq_t *wq, JCR *jcr);
extern int jobq_remove(jobq_t *wq, JCR *jcr);

#endif /* __JOBQ_H */
