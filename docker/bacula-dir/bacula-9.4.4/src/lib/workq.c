/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2018 Kern Sibbald

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
 *
 * Example:
 *
 * static workq_t job_wq;    define work queue
 *
 *  Initialize queue
 *  if ((stat = workq_init(&job_wq, max_workers, job_thread)) != 0) {
 *     berrno be;
 *     Emsg1(M_ABORT, 0, "Could not init job work queue: ERR=%s\n", be.bstrerror(errno));
 *   }
 *
 *  Add an item to the queue
 *  if ((stat = workq_add(&job_wq, (void *)jcr)) != 0) {
 *      berrno be;
 *      Emsg1(M_ABORT, 0, "Could not add job to work queue: ERR=%s\n", be.bstrerror(errno));
 *   }
 *
 *  Wait for all queued work to be completed
 *  if ((stat = workq_wait_idle(&job_wq, (void *)jcr)) != 0) {
 *     berrno be;
 *     Emsg1(M_ABORT, 0, "Could not wait for idle: ERR=%s\n", be.bstrerror(errno));
 *  }
 *
 *  Terminate the queue
 *  workq_destroy(workq_t *wq);
 *
 */

#include "bacula.h"
#include "jcr.h"

/* Forward referenced functions */
extern "C" void *workq_server(void *arg);

/*
 * Initialize a work queue
 *
 *  Returns: 0 on success
 *           errno on failure
 */
int workq_init(workq_t *wq, int threads, void *(*engine)(void *arg))
{
   int stat;

   if ((stat = pthread_attr_init(&wq->attr)) != 0) {
      return stat;
   }
   if ((stat = pthread_attr_setdetachstate(&wq->attr, PTHREAD_CREATE_DETACHED)) != 0) {
      pthread_attr_destroy(&wq->attr);
      return stat;
   }
   if ((stat = pthread_mutex_init(&wq->mutex, NULL)) != 0) {
      pthread_attr_destroy(&wq->attr);
      return stat;
   }
   if ((stat = pthread_cond_init(&wq->work, NULL)) != 0) {
      pthread_mutex_destroy(&wq->mutex);
      pthread_attr_destroy(&wq->attr);
      return stat;
   }
   if ((stat = pthread_cond_init(&wq->idle, NULL)) != 0) {
      pthread_mutex_destroy(&wq->mutex);
      pthread_attr_destroy(&wq->attr);
      pthread_cond_destroy(&wq->work);
      return stat;
   }
   wq->quit = 0;
   wq->first = wq->last = NULL;
   wq->max_workers = threads;         /* max threads to create */
   wq->num_workers = 0;               /* no threads yet */
   wq->num_running = 0;               /* no running threads */
   wq->idle_workers = 0;              /* no idle threads */
   wq->engine = engine;               /* routine to run */
   wq->valid = WORKQ_VALID;
   return 0;
}

/*
 * Destroy a work queue
 *
 * Returns: 0 on success
 *          errno on failure
 */
int workq_destroy(workq_t *wq)
{
   int stat, stat1, stat2, stat3;

  if (wq->valid != WORKQ_VALID) {
     return EINVAL;
  }
  P(wq->mutex);
  wq->valid = 0;                      /* prevent any more operations */

  /*
   * If any threads are active, wake them
   */
  if (wq->num_workers > 0) {
     wq->quit = 1;
     if (wq->idle_workers) {
        if ((stat = pthread_cond_broadcast(&wq->work)) != 0) {
           V(wq->mutex);
           return stat;
        }
     }
     while (wq->num_workers > 0) {
        if ((stat = pthread_cond_wait(&wq->work, &wq->mutex)) != 0) {
           V(wq->mutex);
           return stat;
        }
     }
  }
  V(wq->mutex);
  stat  = pthread_mutex_destroy(&wq->mutex);
  stat1 = pthread_cond_destroy(&wq->work);
  stat2 = pthread_attr_destroy(&wq->attr);
  stat3 = pthread_cond_destroy(&wq->idle);
  if (stat != 0) return stat;
  if (stat1 != 0) return stat1;
  if (stat2 != 0) return stat2;
  if (stat3 != 0) return stat3;
  return 0;
}

/*
 * Wait for work to terminate
 *
 * Returns: 0 on success
 *          errno on failure
 */
int workq_wait_idle(workq_t *wq)
{
   int stat;

  if (wq->valid != WORKQ_VALID) {
     return EINVAL;
  }
  P(wq->mutex);

  /* While there is work, wait */
  while (wq->num_running || wq->first != NULL) {
     if ((stat = pthread_cond_wait(&wq->idle, &wq->mutex)) != 0) {
        V(wq->mutex);
        return stat;
     }
  }
  V(wq->mutex);
  return 0;
}



/*
 *  Add work to a queue
 *    wq is a queue that was created with workq_init
 *    element is a user unique item that will be passed to the
 *        processing routine
 *    work_item will get internal work queue item -- if it is not NULL
 *    priority if non-zero will cause the item to be placed on the
 *        head of the list instead of the tail.
 */
int workq_add(workq_t *wq, void *element, workq_ele_t **work_item, int priority)
{
   int stat=0;
   workq_ele_t *item;
   pthread_t id;

   Dmsg0(1400, "workq_add\n");
   if (wq->valid != WORKQ_VALID) {
      return EINVAL;
   }

   if ((item = (workq_ele_t *)malloc(sizeof(workq_ele_t))) == NULL) {
      return ENOMEM;
   }
   item->data = element;
   item->next = NULL;
   P(wq->mutex);

   Dmsg0(1400, "add item to queue\n");
   if (priority) {
      /* Add to head of queue */
      if (wq->first == NULL) {
         wq->first = item;
         wq->last = item;
      } else {
         item->next = wq->first;
         wq->first = item;
      }
   } else {
      /* Add to end of queue */
      if (wq->first == NULL) {
         wq->first = item;
      } else {
         wq->last->next = item;
      }
      wq->last = item;
   }

   /* if any threads are idle, wake one */
   if (wq->idle_workers > 0) {
      Dmsg0(1400, "Signal worker\n");
      if ((stat = pthread_cond_broadcast(&wq->work)) != 0) {
         V(wq->mutex);
         return stat;
      }
   } else if (wq->num_workers < wq->max_workers) {
      Dmsg0(1400, "Create worker thread\n");
      /* No idle threads so create a new one */
      set_thread_concurrency(wq->max_workers + 1);
      if ((stat = pthread_create(&id, &wq->attr, workq_server, (void *)wq)) != 0) {
         V(wq->mutex);
         return stat;
      }
      wq->num_workers++;
   }
   V(wq->mutex);
   Dmsg0(1400, "Return workq_add\n");
   /* Return work_item if requested */
   if (work_item) {
      *work_item = item;
   }
   return stat;
}

/*
 *  Remove work from a queue
 *    wq is a queue that was created with workq_init
 *    work_item is an element of work
 *
 *   Note, it is "removed" by immediately calling a processing routine.
 *    if you want to cancel it, you need to provide some external means
 *    of doing so.
 */
int workq_remove(workq_t *wq, workq_ele_t *work_item)
{
   int stat, found = 0;
   pthread_t id;
   workq_ele_t *item, *prev;

   Dmsg0(1400, "workq_remove\n");
   if (wq->valid != WORKQ_VALID) {
      return EINVAL;
   }

   P(wq->mutex);

   for (prev=item=wq->first; item; item=item->next) {
      if (item == work_item) {
         found = 1;
         break;
      }
      prev = item;
   }
   if (!found) {
      return EINVAL;
   }

   /* Move item to be first on list */
   if (wq->first != work_item) {
      prev->next = work_item->next;
      if (wq->last == work_item) {
         wq->last = prev;
      }
      work_item->next = wq->first;
      wq->first = work_item;
   }

   /* if any threads are idle, wake one */
   if (wq->idle_workers > 0) {
      Dmsg0(1400, "Signal worker\n");
      if ((stat = pthread_cond_broadcast(&wq->work)) != 0) {
         V(wq->mutex);
         return stat;
      }
   } else {
      Dmsg0(1400, "Create worker thread\n");
      /* No idle threads so create a new one */
      set_thread_concurrency(wq->max_workers + 1);
      if ((stat = pthread_create(&id, &wq->attr, workq_server, (void *)wq)) != 0) {
         V(wq->mutex);
         return stat;
      }
      wq->num_workers++;
   }
   V(wq->mutex);
   Dmsg0(1400, "Return workq_remove\n");
   return stat;
}


/*
 * This is the worker thread that serves the work queue.
 * In due course, it will call the user's engine.
 */
extern "C"
void *workq_server(void *arg)
{
   struct timespec timeout;
   workq_t *wq = (workq_t *)arg;
   workq_ele_t *we;
   int stat, timedout;

   Dmsg0(1400, "Start workq_server\n");
   P(wq->mutex);
   set_jcr_in_tsd(INVALID_JCR);

   for (;;) {
      struct timeval tv;
      struct timezone tz;

      Dmsg0(1400, "Top of for loop\n");
      timedout = 0;
      Dmsg0(1400, "gettimeofday()\n");
      gettimeofday(&tv, &tz);
      timeout.tv_nsec = 0;
      timeout.tv_sec = tv.tv_sec + 2;

      while (wq->first == NULL && !wq->quit) {
         /*
          * Wait 2 seconds, then if no more work, exit
          */
         Dmsg0(1400, "pthread_cond_timedwait()\n");
         stat = pthread_cond_timedwait(&wq->work, &wq->mutex, &timeout);
         Dmsg1(1400, "timedwait=%d\n", stat);
         if (stat == ETIMEDOUT) {
            timedout = 1;
            break;
         } else if (stat != 0) {
            /* This shouldn't happen */
            Dmsg0(1400, "This shouldn't happen\n");
            wq->num_workers--;
            V(wq->mutex);
            return NULL;
         }
      }
      we = wq->first;
      if (we != NULL) {
         wq->first = we->next;
         if (wq->last == we) {
            wq->last = NULL;
         }
         wq->num_running++;
         V(wq->mutex);
         /* Call user's routine here */
         Dmsg0(1400, "Calling user engine.\n");
         wq->engine(we->data);
         Dmsg0(1400, "Back from user engine.\n");
         free(we);                    /* release work entry */
         Dmsg0(1400, "relock mutex\n");
         P(wq->mutex);
         wq->num_running--;
         Dmsg0(1400, "Done lock mutex\n");
      }
      if (wq->first == NULL && !wq->num_running) {
          pthread_cond_broadcast(&wq->idle);
      }
      /*
       * If no more work request, and we are asked to quit, then do it
       */
      if (wq->first == NULL && wq->quit) {
         wq->num_workers--;
         if (wq->num_workers == 0) {
            Dmsg0(1400, "Wake up destroy routine\n");
            /* Wake up destroy routine if he is waiting */
            pthread_cond_broadcast(&wq->work);
         }
         Dmsg0(1400, "Unlock mutex\n");
         V(wq->mutex);
         Dmsg0(1400, "Return from workq_server\n");
         return NULL;
      }
      Dmsg0(1400, "Check for work request\n");
      /*
       * If no more work requests, and we waited long enough, quit
       */
      Dmsg1(1400, "wq->first==NULL = %d\n", wq->first==NULL);
      Dmsg1(1400, "timedout=%d\n", timedout);
      if (wq->first == NULL && timedout) {
         Dmsg0(1400, "break big loop\n");
         wq->num_workers--;
         break;
      }
      Dmsg0(1400, "Loop again\n");
   } /* end of big for loop */

   Dmsg0(1400, "unlock mutex\n");
   V(wq->mutex);
   Dmsg0(1400, "End workq_server\n");
   return NULL;
}


//=================================================
#ifdef TEST_PROGRAM

#define TEST_SLEEP_TIME_IN_SECONDS 3
#define TEST_MAX_NUM_WORKERS 5
#define TEST_NUM_WORKS 10


void *callback(void *ctx)
{
   JCR* jcr = (JCR*)ctx;

   if (jcr)
   {
      Jmsg1(jcr, M_INFO, 0, _("workq_test: thread %d : now starting work....\n"), (int)pthread_self());
      sleep(TEST_SLEEP_TIME_IN_SECONDS);
      Jmsg1(jcr, M_INFO, 0, _("workq_test: thread %d : ...work completed.\n"), (int)pthread_self());
   }
   return NULL;
}


char *configfile = NULL;
//STORES *me = NULL;                    /* our Global resource */
bool forge_on = false;                /* proceed inspite of I/O errors */
pthread_mutex_t device_release_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t wait_device_release = PTHREAD_COND_INITIALIZER;

int main (int argc, char *argv[])
{
   pthread_attr_t attr;

   void * start_heap = sbrk(0);
   (void)start_heap;

   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");
   init_stack_dump();
   my_name_is(argc, argv, "workq_test");
   init_msg(NULL, NULL);
   daemon_start_time = time(NULL);
   set_thread_concurrency(150);
   lmgr_init_thread(); /* initialize the lockmanager stack */
   pthread_attr_init(&attr);

   int stat(-1);
   berrno be;

   workq_t queue;
   /* Start work queues */
   if ((stat = workq_init(&queue, TEST_MAX_NUM_WORKERS, callback)) != 0)
   {
      be.set_errno(stat);
      Emsg1(M_ABORT, 0, _("Could not init work queue: ERR=%s\n"), be.bstrerror());
   }

   /* job1 is created and pseudo-submits some work to the work queue*/
   JCR *jcr1 = new_jcr(sizeof(JCR), NULL);
   jcr1->JobId = 1;
   workq_ele_t * ret(0);
   for (int w=0; w<TEST_NUM_WORKS; ++w)
   {
      if ((stat = workq_add(&queue, jcr1, &ret, 0)) != 0)
      {
         be.set_errno(stat);
         Emsg1(M_ABORT, 0, _("Could not add work to queue: ERR=%s\n"), be.bstrerror());
      }
   }

   JCR *jcr2 = new_jcr(sizeof(JCR), NULL);
   jcr2->JobId = 2;
   for (int w=0; w<TEST_NUM_WORKS; ++w)
   {
      if ((stat = workq_add(&queue, jcr2, &ret, 0)) != 0)
      {
         be.set_errno(stat);
         Emsg1(M_ABORT, 0, _("Could not add work to queue: ERR=%s\n"), be.bstrerror());
      }
   }

   printf("--------------------------------------------------------------\n");
   printf("Start workq_wait_idle ....\n");
   if ((stat = workq_wait_idle(&queue)) != 0)
   {
      be.set_errno(stat);
      Emsg1(M_ABORT, 0, _("Waiting for workq to be empty: ERR=%s\n"), be.bstrerror());
   }
   printf("... workq_wait_idle completed.\n");
   printf("--------------------------------------------------------------\n");

   printf("Start workq_destroy ....\n");
   if ((stat = workq_destroy(&queue)) != 0)
   {
      be.set_errno(stat);
      Emsg1(M_ABORT, 0, _("Error in workq_destroy: ERR=%s\n"), be.bstrerror());
   }
   printf("... workq_destroy completed.\n");

   return 0;

}

#endif
