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
 * Bacula worker thread class
 *
 *  Kern Sibbald, August 2014
 *
 */

#ifndef __WORKER_H
#define __WORKER_H 1

enum WORKER_STATE {
   WORKER_WAIT,
   WORKER_RUN,
   WORKER_QUIT
};

class worker : public SMARTALLOC {
private:
   pthread_mutex_t   mutex;           /* main fifo mutex */
   pthread_mutex_t   fmutex;          /* free pool mutex */
   pthread_cond_t    full_wait;       /* wait because full */
   pthread_cond_t    empty_wait;      /* wait because empty */
   pthread_cond_t    m_wait;          /* wait state */
   pthread_t         worker_id;       /* worker's thread id */
   void        *(*user_sub)(void *);  /* user subroutine */
   void             *user_ctx;        /* user context */
   flist            *fifo;            /* work fifo */
   alist            *fpool;           /* free pool */
   int               valid;           /* set when valid */
   WORKER_STATE      m_state;         /* worker state */
   bool              worker_running;  /* set when worker running */
   bool              worker_waiting;  /* set when worker is waiting */
   bool              done;            /* work done */
   bool              waiting_on_empty; /* hung waiting on empty queue */


public:
   worker(int fifo_size = 10);
   ~worker();
   int init(int fifo_size = 10);
   int destroy();

   bool queue(void *item);
   void *dequeue();
   int start(void *(*sub)(void *), void *wctx);  /* Start worker */
   void wait_queue_empty();           /* Main thread wait for fifo to be emptied */
   void discard_queue();              /* Discard the fifo queue */
   int stop();                        /* Stop worker */
   void wait();                       /* Wait for main thread to release us */
   void set_wait_state();
   void set_run_state();
   void set_quit_state();
   void finish_work();

   void *pop_free_buffer();
   void push_free_buffer(void *buf);

   inline void set_running() { worker_running = true; };
   inline bool is_running() const { return worker_running; };
   inline void *get_ctx() const { return user_ctx; };
   
   void release_lock();               /* Cleanup release lock */

   inline bool empty() const { return fifo->empty(); };
   inline bool full() const { return fifo->full(); };
   inline int size() const { return fifo->size(); };
   inline bool is_quit_state() const { return m_state == WORKER_QUIT; };
   inline bool is_wait_state() const { return m_state == WORKER_WAIT; };
};


inline worker::worker(int fifo_size)
{
   init(fifo_size);
}

inline worker::~worker()
{
   destroy();
}


#define WORKER_VALID  0xfadbec

#endif /* __WORKER_H */
