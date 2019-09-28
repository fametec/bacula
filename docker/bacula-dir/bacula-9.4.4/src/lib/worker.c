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
 * Bacula worker class. It permits creating a worker thread,
 *  then sending data via a fifo queue to it.
 *
 *  Kern Sibbald, August 2014
 *
 */

#define LOCKMGR_COMPLIANT
#include "bacula.h"
#include "worker.h"

int worker::init(int fifo_size)
{
   int stat;

   if ((stat = pthread_mutex_init(&mutex, NULL)) != 0) {
      return stat;
   }
   if ((stat = pthread_mutex_init(&fmutex, NULL)) != 0) {
      pthread_mutex_destroy(&mutex);
      return stat;
   }
   if ((stat = pthread_cond_init(&full_wait, NULL)) != 0) {
      pthread_mutex_destroy(&mutex);
      pthread_mutex_destroy(&fmutex);
      return stat;
   }
   if ((stat = pthread_cond_init(&empty_wait, NULL)) != 0) {
      pthread_cond_destroy(&full_wait);
      pthread_mutex_destroy(&mutex);
      pthread_mutex_destroy(&fmutex);
      return stat;
   }
   if ((stat = pthread_cond_init(&m_wait, NULL)) != 0) {
      pthread_cond_destroy(&empty_wait);
      pthread_cond_destroy(&full_wait);
      pthread_mutex_destroy(&mutex);
      pthread_mutex_destroy(&fmutex);
      return stat;
   }
   valid = WORKER_VALID;
   fifo = New(flist(fifo_size));
   fpool = New(alist(fifo_size + 2, false));
   worker_running = false;
   set_wait_state();
   return 0;
}

/*
 * Handle cleanup when the lock is released.
 */
static void worker_cleanup(void *arg)
{
   worker *wrk = (worker *)arg;
   wrk->release_lock();
}


void worker::release_lock()
{
   pthread_mutex_unlock(&mutex);
}


void worker::set_wait_state()
{
   m_state = WORKER_WAIT;
}

void worker::set_run_state()
{
   if (is_quit_state()) return;
   m_state = WORKER_RUN;
   if (worker_waiting) {
      pthread_cond_signal(&m_wait);
   }
}
      
void worker::set_quit_state()
{
   P(mutex);
   m_state = WORKER_QUIT;
   pthread_cond_signal(&m_wait);
   pthread_cond_signal(&empty_wait);
   V(mutex);
}


/* Empty the fifo putting in free pool */
void worker::discard_queue()
{
  void *item;

  P(mutex);
  P(fmutex);
  while ((item = fifo->dequeue())) {
     fpool->push(item);
  }
  V(fmutex);
  V(mutex);
}

/*
 * Destroy a read/write lock
 *
 * Returns: 0 on success
 *          errno on failure
 */
int worker::destroy()
{
   int stat, stat1, stat2, stat3, stat4;
   POOLMEM *item;

   m_state = WORKER_QUIT;
   pthread_cond_signal(&m_wait);
   pthread_cond_signal(&empty_wait);

   P(fmutex);
   /* Release free pool */
   while ((item = (POOLMEM *)fpool->pop())) {
      free_pool_memory(item);
   }
   V(fmutex);
   fpool->destroy();   
   free(fpool);

   /* Release work queue */
   while ((item = (POOLMEM *)fifo->dequeue())) {
      free_pool_memory(item);
   }
   valid = 0;
   worker_running = false;

   fifo->destroy();
   free(fifo);

   stat  = pthread_mutex_destroy(&mutex);
   stat1 = pthread_mutex_destroy(&fmutex);
   stat2 = pthread_cond_destroy(&full_wait);
   stat3 = pthread_cond_destroy(&empty_wait);
   stat4 = pthread_cond_destroy(&m_wait);
   if (stat != 0) return stat;
   if (stat1 != 0) return stat1;
   if (stat2 != 0) return stat2;
   if (stat3 != 0) return stat3;
   if (stat4 != 0) return stat4;
   return 0;
}


/* Start the worker thread */
int worker::start(void *(*auser_sub)(void *), void *auser_ctx)
{
   int stat;
   int i;
   if (valid != WORKER_VALID) {
      return EINVAL;
   }
   user_sub = auser_sub;
   user_ctx = auser_ctx;
   if ((stat = pthread_create(&worker_id, NULL, user_sub, this) != 0)) {
      return stat;
   }
   /* Wait for thread to start, but not too long */
   for (i=0; i<100 && !is_running(); i++) {
      bmicrosleep(0, 5000);
   }
   set_run_state();
   return 0;
}

/* Wait for the worker thread to empty the queue */
void worker::wait_queue_empty()
{
   if (is_quit_state()) {
      return;
   }
   P(mutex);
   while (!empty() && !is_quit_state()) {
      pthread_cond_wait(&empty_wait, &mutex);
   }
   V(mutex);
   return;
}

/* Wait for the main thread to release us */
void worker::wait()
{
   P(mutex);
   pthread_cleanup_push(worker_cleanup, (void *)this);
   while (is_wait_state() && !is_quit_state()) {
      worker_waiting = true;
      pthread_cond_signal(&m_wait);
      pthread_cond_wait(&m_wait, &mutex);
   }
   pthread_cleanup_pop(0);
   worker_waiting = false;
   V(mutex);
}

/* Stop the worker thread */
int worker::stop()
{
   if (valid != WORKER_VALID) {
      return EINVAL;
   }
   m_state = WORKER_QUIT;
   pthread_cond_signal(&m_wait);
   pthread_cond_signal(&empty_wait);

   if (!pthread_equal(worker_id, pthread_self())) {
      pthread_cancel(worker_id);
      pthread_join(worker_id, NULL);
   }
   return 0;
}


/*
 * Queue an item for the worker thread. Called by main thread.
 */
bool worker::queue(void *item)
{
   bool was_empty = false;;

   if (valid != WORKER_VALID || is_quit_state()) {
      return EINVAL;
   }
   P(mutex);
   done = false;
   //pthread_cleanup_push(worker_cleanup, (void *)this);
   while (full() && !is_quit_state()) {
      pthread_cond_wait(&full_wait, &mutex);
   }
   //pthread_cleanup_pop(0);
   /* Maybe this should be worker_running */
   was_empty = empty();
   if (!fifo->queue(item)) {
      /* Since we waited for !full this cannot happen */
      V(mutex);
      ASSERT2(1, "Fifo queue failed.\n");
   }
   if (was_empty) {
      pthread_cond_signal(&empty_wait);
   }
   m_state = WORKER_RUN;
   if (worker_waiting) {
      pthread_cond_signal(&m_wait);
   }
   V(mutex);
   return 1;
}

/*
 * Wait for work to complete
 */
void worker::finish_work()
{
   P(mutex);
   while (!empty() && !is_quit_state()) {
      pthread_cond_wait(&empty_wait, &mutex);
   }
   done = true;                       /* Tell worker that work is done */
   m_state = WORKER_WAIT;             /* force worker into wait state */
   V(mutex);                          /* pause for state transition */
   if (waiting_on_empty) pthread_cond_signal(&empty_wait);
   P(mutex);
   /* Wait until worker in wait state */
   while (!worker_waiting && !is_quit_state()) {
      if (waiting_on_empty) pthread_cond_signal(&empty_wait);
      pthread_cond_wait(&m_wait, &mutex);
   }
   V(mutex);   
   discard_queue();
}

/*
 * Dequeue a work item. Called by worker thread.
 */
void *worker::dequeue()
{
   bool was_full = false;;
   void *item = NULL;

   if (valid != WORKER_VALID || done || is_quit_state()) {
      return NULL;
   }
   P(mutex);
   //pthread_cleanup_push(worker_cleanup, (void *)this);
   while (empty() && !done && !is_quit_state()) {
      waiting_on_empty = true;
      pthread_cond_wait(&empty_wait, &mutex);
   }
   waiting_on_empty = false;
   //pthread_cleanup_pop(0);
   was_full = full();
   item = fifo->dequeue();
   if (was_full) {
      pthread_cond_signal(&full_wait);
   }
   if (empty()) {
      pthread_cond_signal(&empty_wait);
   }
   V(mutex);
   return item;
}

/*
 * Pop a free buffer from the list, if one exists.
 *  Called by main thread to get a free buffer.
 *  If none exists (NULL returned), it must allocate
 *  one.
 */
void *worker::pop_free_buffer()
{
   void *free_buf;

   P(fmutex);
   free_buf = fpool->pop();
   V(fmutex);
   return free_buf;
}

/*
 * Once a work item (buffer) has been processed by the
 *  worker thread, it will put it on the free buffer list
 *  (fpool).
 */
void worker::push_free_buffer(void *buf)
{
   P(fmutex);
   fpool->push(buf);
   V(fmutex);
}


//=================================================

#ifdef TEST_PROGRAM

void *worker_prog(void *wctx)
{
   POOLMEM *buf;
   worker *wrk = (worker *)wctx;

   wrk->set_running();

   while (!wrk->is_quit_state()) {
      if (wrk->is_wait_state()) {
         wrk->wait();
         continue;
      }   
      buf = (POOLMEM *)wrk->dequeue();
      if (!buf) {
         printf("worker: got null stop\n");
         return NULL;
      }
      printf("ctx=%lld worker: %s\n", (long long int)wrk->get_ctx(), buf);
      wrk->push_free_buffer(buf);
   }
   printf("worker: asked to stop");
   return NULL;
}

int main(int argc, char *argv[])
{
   POOLMEM *buf;
   int i;
   worker *wrk;
   void *ctx;

   wrk = New(worker(10));
   ctx = (void *)1;
   wrk->start(worker_prog, ctx);

   for (i=1; i<=40; i++) {
      buf = (POOLMEM *)wrk->pop_free_buffer();
      if (!buf) {
         buf = get_pool_memory(PM_BSOCK);
         printf("Alloc %p\n", buf);
      }
      sprintf(buf, "This is item %d", i);
      wrk->queue(buf);
      //printf("back from queue %d\n", i);
   }
   wrk->wait_queue_empty();
   wrk->set_wait_state();
   printf("======\n");
   for (i=1; i<=5; i++) {
      buf = (POOLMEM *)wrk->pop_free_buffer();
      if (!buf) {
         buf = get_pool_memory(PM_BSOCK);
         printf("Alloc %p\n", buf);
      }
      sprintf(buf, "This is item %d", i);
      wrk->queue(buf);
      //printf("back from queue %d\n", i);
   }
   wrk->set_run_state();
   for (i=6; i<=40; i++) {
      buf = (POOLMEM *)wrk->pop_free_buffer();
      if (!buf) {
         buf = get_pool_memory(PM_BSOCK);
         printf("Alloc %p\n", buf);
      }
      sprintf(buf, "This is item %d", i);
      wrk->queue(buf);
      //printf("back from queue %d\n", i);
   }
   wrk->wait_queue_empty();
   wrk->stop();
   wrk->destroy();
   free(wrk);
   
   close_memory_pool();
   sm_dump(false);       /* test program */
}
#endif
