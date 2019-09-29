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
  How to use mutex with bad order usage detection
 ------------------------------------------------

 Note: see file mutex_list.h for current mutexes with
       defined priorities.

 Instead of using:
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    P(mutex);
    ..
    V(mutex);

 use:
    bthread_mutex_t mutex = BTHREAD_MUTEX_PRIORITY(1);
    P(mutex);
    ...
    V(mutex);

 Mutex that doesn't need this extra check can be declared as pthread_mutex_t.
 You can use this object on pthread_mutex_lock/unlock/cond_wait/cond_timewait.

 With dynamic creation, you can use:
    bthread_mutex_t mutex;
    pthread_mutex_init(&mutex);
    bthread_mutex_set_priority(&mutex, 10);
    pthread_mutex_destroy(&mutex);

 */

#define LOCKMGR_COMPLIANT
#include "bacula.h"

#undef ASSERT
#define ASSERT(x) if (!(x)) { \
   char *jcr = NULL; \
   Pmsg3(000, _("ASSERT failed at %s:%i: %s\n"), __FILE__, __LINE__, #x); \
   jcr[0] = 0; }

#define ASSERT_p(x,f,l) if (!(x)) {              \
   char *jcr = NULL; \
   Pmsg3(000, _("ASSERT failed at %s:%i: %s \n"), f, l, #x); \
   jcr[0] = 0; }

#define ASSERT2_p(x,m,f,l) if (!(x)) {          \
   char *jcr = NULL; \
   set_assert_msg(f, l, m); \
   Pmsg4(000, _("ASSERT failed at %s:%i: %s (%s)\n"), f, l, #x, m);        \
   jcr[0] = 0; }

/* for lockmgr unit tests we have to clean up developer flags and asserts which breaks our tests */
#ifdef TEST_PROGRAM
#ifdef DEVELOPER
#undef DEVELOPER
#endif
#ifdef ASSERTD
#undef ASSERTD
#define ASSERTD(x, y)
#endif
#endif

/*
  Inspired from
  http://www.cs.berkeley.edu/~kamil/teaching/sp03/041403.pdf

  This lock manager will replace some pthread calls. It can be
  enabled with USE_LOCKMGR

  Some part of the code can't use this manager, for example the
  rwlock object or the smartalloc lib. To disable LMGR, just add
  LOCKMGR_COMPLIANT before the inclusion of "bacula.h"

  cd build/src/lib
  g++ -g -c lockmgr.c -I.. -I../lib -DUSE_LOCKMGR -DTEST_PROGRAM
  g++ -o lockmgr lockmgr.o -lbac -L../lib/.libs -lssl -lpthread

*/

#define DBGLEVEL_EVENT 50

/*
 * pthread_mutex_lock for memory allocator and other
 * parts that are LOCKMGR_COMPLIANT
 */
void lmgr_p(pthread_mutex_t *m)
{
   int errstat;
   if ((errstat=pthread_mutex_lock(m))) {
      berrno be;
      e_msg(__FILE__, __LINE__, M_ABORT, 0, _("Mutex lock failure. ERR=%s\n"),
            be.bstrerror(errstat));
   }
}

void lmgr_v(pthread_mutex_t *m)
{
   int errstat;
   if ((errstat=pthread_mutex_unlock(m))) {
      berrno be;
      e_msg(__FILE__, __LINE__, M_ABORT, 0, _("Mutex unlock failure. ERR=%s\n"),
            be.bstrerror(errstat));
   }
}

#ifdef USE_LOCKMGR

typedef enum
{
   LMGR_WHITE,                  /* never seen */
   LMGR_BLACK,                  /* no loop */
   LMGR_GRAY                    /* already seen */
} lmgr_color_t;

/*
 * Node used by the Lock Manager
 * If the lock is GRANTED, we have mutex -> proc, else it's a proc -> mutex
 * relation.
 *
 * Note, each mutex can be GRANTED once, and each proc can have only one WANTED
 * mutex.
 */
class lmgr_node_t: public SMARTALLOC
{
public:
   dlink link;
   void *node;
   void *child;
   lmgr_color_t seen;

   lmgr_node_t() {
      child = node = NULL;
      seen = LMGR_WHITE;
   }

   lmgr_node_t(void *n, void *c) {
      init(n,c);
   }

   void init(void *n, void *c) {
      node = n;
      child = c;
      seen = LMGR_WHITE;
   }

   void mark_as_seen(lmgr_color_t c) {
      seen = c;
   }

   ~lmgr_node_t() {printf("delete node\n");}
};

typedef enum {
   LMGR_LOCK_EMPTY   = 'E',      /* unused */
   LMGR_LOCK_WANTED  = 'W',      /* before mutex_lock */
   LMGR_LOCK_GRANTED = 'G'       /* after mutex_lock */
} lmgr_state_t;

/*
 * Object associated with each mutex per thread
 */
class lmgr_lock_t: public SMARTALLOC
{
public:
   dlink link;
   void *lock;                  /* Link to the mutex (or any value) */
   lmgr_state_t state;
   int max_priority;
   int priority;                /* Current node priority */

   const char *file;
   int line;

   lmgr_lock_t() {
      lock = NULL;
      state = LMGR_LOCK_EMPTY;
      priority = max_priority = 0;
   }

   lmgr_lock_t(void *l) {
      lock = l;
      state = LMGR_LOCK_WANTED;
   }

   void set_granted() {
      state = LMGR_LOCK_GRANTED;
   }

   ~lmgr_lock_t() {}

};

/*
 * Get the child list, ret must be already allocated
 */
static void search_all_node(dlist *g, lmgr_node_t *v, alist *ret)
{
   lmgr_node_t *n;
   foreach_dlist(n, g) {
      if (v->child == n->node) {
         ret->append(n);
      }
   }
}

static bool visit(dlist *g, lmgr_node_t *v)
{
   bool ret=false;
   lmgr_node_t *n;
   v->mark_as_seen(LMGR_GRAY);

   alist *d = New(alist(5, false)); /* use alist because own=false */
   search_all_node(g, v, d);

   //foreach_alist(n, d) {
   //   printf("node n=%p c=%p s=%c\n", n->node, n->child, n->seen);
   //}

   foreach_alist(n, d) {
      if (n->seen == LMGR_GRAY) { /* already seen this node */
         ret = true;
         goto bail_out;
      } else if (n->seen == LMGR_WHITE) {
         if (visit(g, n)) {
            ret = true;
            goto bail_out;
         }
      }
   }
   v->mark_as_seen(LMGR_BLACK); /* no loop detected, node is clean */
bail_out:
   delete d;
   return ret;
}

static bool contains_cycle(dlist *g)
{
   lmgr_node_t *n;
   foreach_dlist(n, g) {
      if (n->seen == LMGR_WHITE) {
         if (visit(g, n)) {
            return true;
         }
      }
   }
   return false;
}

/****************************************************************/

/* lmgr_thread_event struct, some call can add events, and they will
 * be dumped during a lockdump
 */
typedef struct
{
   int32_t     id;              /* Id of the event */
   int32_t     global_id;       /* Current global id */
   int32_t     flags;           /* Flags for this event */

   int32_t     line;            /* from which line in filename */
   const char *from;            /* From where in the code (filename) */

   char       *comment;         /* Comment */
   intptr_t    user_data;       /* Optionnal user data (will print address) */

}  lmgr_thread_event;

static int32_t global_event_id=0;

static int global_int_thread_id=0; /* Keep an integer for each thread */

/* Keep this number of event per thread */
#ifdef TEST_PROGRAM
# define LMGR_THREAD_EVENT_MAX  15
#else
# define LMGR_THREAD_EVENT_MAX  1024
#endif

#define lmgr_thread_event_get_pos(x)   ((x) % LMGR_THREAD_EVENT_MAX)

class lmgr_thread_t: public SMARTALLOC
{
public:
   dlink link;
   pthread_mutex_t mutex;
   pthread_t       thread_id;
   intptr_t        int_thread_id;
   lmgr_lock_t     lock_list[LMGR_MAX_LOCK];
   int current;
   int max;
   int max_priority;

   lmgr_thread_event events[LMGR_THREAD_EVENT_MAX];
   int event_id;

   lmgr_thread_t() {
      int status;
      if ((status = pthread_mutex_init(&mutex, NULL)) != 0) {
         berrno be;
         Pmsg1(000, _("pthread key create failed: ERR=%s\n"),
                 be.bstrerror(status));
         ASSERT2(0, "pthread_mutex_init failed");
      }
      event_id = 0;
      thread_id = pthread_self();
      current = -1;
      max = 0;
      max_priority = 0;
   }

   /* Add event to the event list of the thread */
   void add_event(const char *comment, intptr_t user_data, int32_t flags,
                  const char *from, int32_t line)
   {
      char *p;
      int32_t oldflags;
      int   i = lmgr_thread_event_get_pos(event_id);

      oldflags = events[i].flags;
      p = events[i].comment;
      events[i].flags = LMGR_EVENT_INVALID;
      events[i].comment = (char *)"*Freed*";

      /* Shared between thread, just an indication about timing */
      events[i].global_id = global_event_id++;
      events[i].id = event_id;
      events[i].line = line;
      events[i].from = from;

      /* It means we are looping over the ring, so we need
       * to check if the memory need to be freed
       */
      if (event_id >= LMGR_THREAD_EVENT_MAX) {
         if (oldflags & LMGR_EVENT_FREE) {
            free(p);
         }
      }

      /* We need to copy the memory */
      if (flags & LMGR_EVENT_DUP) {
         events[i].comment = bstrdup(comment);
         flags |= LMGR_EVENT_FREE; /* force the free */

      } else {
         events[i].comment = (char *)comment;
      }
      events[i].user_data = user_data;
      events[i].flags = flags;  /* mark it valid */
      event_id++;
   }

   void free_event_list() {
      /* We first check how far we go in the event list */
      int max = MIN(event_id, LMGR_THREAD_EVENT_MAX);
      char *p;

      for (int i = 0; i < max ; i++) {
         if (events[i].flags & LMGR_EVENT_FREE) {
            p = events[i].comment;
            events[i].flags = LMGR_EVENT_INVALID;
            events[i].comment = (char *)"*Freed*";
            free(p);
         }
      }
   }

   void print_event(lmgr_thread_event *ev, FILE *fp) {
      if (ev->flags & LMGR_EVENT_INVALID) {
         return;
      }
      fprintf(fp, "    %010d id=%010d %s data=%p at %s:%d\n",
              ev->global_id,
              ev->id,
              NPRT(ev->comment),
              (void *)ev->user_data,
              ev->from,
              ev->line);
   }

   void _dump(FILE *fp) {
#ifdef HAVE_WIN32
      fprintf(fp, "thread_id=%p int_threadid=%p max=%i current=%i\n",
              (void *)(intptr_t)GetCurrentThreadId(), (void *)int_thread_id, max, current);
#else
      fprintf(fp, "threadid=%p max=%i current=%i\n",
              (void *)thread_id, max, current);
#endif
      for(int i=0; i<=current; i++) {
         fprintf(fp, "   lock=%p state=%s priority=%i %s:%i\n",
                 lock_list[i].lock,
                 (lock_list[i].state=='W')?"Wanted ":"Granted",
                 lock_list[i].priority,
                 lock_list[i].file, lock_list[i].line);
      }

      if (debug_flags & DEBUG_PRINT_EVENT) {
         /* Debug events */
         fprintf(fp, "   events:\n");

         /* Display events between (event_id % LMGR_THREAD_EVENT_MAX) and LMGR_THREAD_EVENT_MAX */
         if (event_id > LMGR_THREAD_EVENT_MAX) {
            for (int i = event_id % LMGR_THREAD_EVENT_MAX ; i < LMGR_THREAD_EVENT_MAX ; i++)
            {
               print_event(&events[i], fp);
            }
         }

         /* Display events between 0 and event_id % LMGR_THREAD_EVENT_MAX*/
         for (int i = 0 ;  i < (event_id % LMGR_THREAD_EVENT_MAX) ; i++)
         {
            print_event(&events[i], fp);
         }
      }
   }

   void dump(FILE *fp) {
      lmgr_p(&mutex);
      {
         _dump(fp);
      }
      lmgr_v(&mutex);
   }

   /*
    * Call before a lock operation (mark mutex as WANTED)
    */
   virtual void pre_P(void *m, int priority,
                      const char *f="*unknown*", int l=0)
   {
      int max_prio = max_priority;

      if (chk_dbglvl(DBGLEVEL_EVENT) && debug_flags & DEBUG_MUTEX_EVENT) {
         /* Keep track of this event */
         add_event("P()", (intptr_t)m, 0, f, l);
      }

      /* Fail if too many locks in use */
      ASSERT2_p(current < LMGR_MAX_LOCK, "Too many locks in use", f, l);
      /* Fail if the "current" value is out of bounds */
      ASSERT2_p(current >= -1, "current lock value is out of bounds", f, l);
      lmgr_p(&mutex);
      {
         current++;
         lock_list[current].lock = m;
         lock_list[current].state = LMGR_LOCK_WANTED;
         lock_list[current].file = f;
         lock_list[current].line = l;
         lock_list[current].priority = priority;
         lock_list[current].max_priority = MAX(priority, max_priority);
         max = MAX(current, max);
         max_priority = MAX(priority, max_priority);
      }
      lmgr_v(&mutex);

      /* Fail if we tried to lock a mutex with a lower priority than
       * the current value. It means that you need to lock mutex in a
       * different order to ensure that the priority field is always
       * increasing. The mutex priority list is defined in mutex_list.h.
       *
       * Look the *.lockdump generated to get the list of all mutexes,
       * and where they were granted to find the priority problem.
       */
      ASSERT2_p(!priority || priority >= max_prio,
                "Mutex priority problem found, locking done in wrong order",
                f, l);
   }

   /*
    * Call after the lock operation (mark mutex as GRANTED)
    */
   virtual void post_P() {
      ASSERT2(current >= 0, "Lock stack when negative");
      ASSERT(lock_list[current].state == LMGR_LOCK_WANTED);
      lock_list[current].state = LMGR_LOCK_GRANTED;
   }

   /* Using this function is some sort of bug */
   void shift_list(int i) {
      for(int j=i+1; j<=current; j++) {
         lock_list[i] = lock_list[j];
      }
      if (current >= 0) {
         lock_list[current].lock = NULL;
         lock_list[current].state = LMGR_LOCK_EMPTY;
      }
      /* rebuild the priority list */
      max_priority = 0;
      for(int j=0; j< current; j++) {
         max_priority = MAX(lock_list[j].priority, max_priority);
         lock_list[j].max_priority = max_priority;
      }
   }

   /*
    * Remove the mutex from the list
    */
   virtual void do_V(void *m, const char *f="*unknown*", int l=0) {
      int old_current = current;

      /* Keep track of this event */
      if (chk_dbglvl(DBGLEVEL_EVENT) && debug_flags & DEBUG_MUTEX_EVENT) {
         add_event("V()", (intptr_t)m, 0, f, l);
      }

      ASSERT2_p(current >= 0, "No previous P found, the mutex list is empty", f, l);
      lmgr_p(&mutex);
      {
         if (lock_list[current].lock == m) {
            lock_list[current].lock = NULL;
            lock_list[current].state = LMGR_LOCK_EMPTY;
            current--;
         } else {
            Pmsg3(0, "ERROR: V out of order lock=%p %s:%i dumping locks...\n", m, f, l);
            Pmsg4(000, "  wrong P/V order pos=%i lock=%p %s:%i\n",
                    current, lock_list[current].lock, lock_list[current].file,
                    lock_list[current].line);
            for (int i=current-1; i >= 0; i--) { /* already seen current */
               Pmsg4(000, "  wrong P/V order pos=%i lock=%p %s:%i\n",
                     i, lock_list[i].lock, lock_list[i].file, lock_list[i].line);
               if (lock_list[i].lock == m) {
                  Pmsg3(000, "ERROR: FOUND P for out of order V at pos=%i %s:%i\n", i, f, l);
                  shift_list(i);
                  current--;
                  break;
               }
            }
         }
         /* reset max_priority to the last one */
         if (current >= 0) {
            max_priority = lock_list[current].max_priority;
         } else {
            max_priority = 0;
         }
      }
      lmgr_v(&mutex);
      /* ASSERT2 should be called outside from the mutex lock */
      ASSERT2_p(current != old_current, "V() called without a previous P()", f, l);
   }

   virtual ~lmgr_thread_t() {destroy();}

   void destroy() {
      free_event_list();
      pthread_mutex_destroy(&mutex);
   }
} ;

class lmgr_dummy_thread_t: public lmgr_thread_t
{
   void do_V(void *m, const char *file, int l)  {}
   void post_P()                                {}
   void pre_P(void *m, int priority, const char *file, int l) {}
};

/*
 * LMGR - Lock Manager
 *
 *
 *
 */

pthread_once_t key_lmgr_once = PTHREAD_ONCE_INIT;
static pthread_key_t lmgr_key;  /* used to get lgmr_thread_t object */

static dlist *global_mgr = NULL;  /* used to store all lgmr_thread_t objects */
static pthread_mutex_t lmgr_global_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t undertaker;
static pthread_cond_t undertaker_cond;
static pthread_mutex_t undertaker_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool use_undertaker = true;
static bool do_quit = false;


#define lmgr_is_active() (global_mgr != NULL)

/*
 * Add a new lmgr_thread_t object to the global list
 */
void lmgr_register_thread(lmgr_thread_t *item)
{
   lmgr_p(&lmgr_global_mutex);
   {
      item->int_thread_id = ++global_int_thread_id;
      global_mgr->prepend(item);
   }
   lmgr_v(&lmgr_global_mutex);
}

/*
 * Call this function to cleanup specific lock thread data
 */
void lmgr_unregister_thread(lmgr_thread_t *item)
{
   if (!lmgr_is_active()) {
      return;
   }
   lmgr_p(&lmgr_global_mutex);
   {
      global_mgr->remove(item);
#ifdef DEVELOPER
      for(int i=0; i<=item->current; i++) {
         lmgr_lock_t *lock = &item->lock_list[i];
         if (lock->state == LMGR_LOCK_GRANTED) {
            ASSERT2(0, "Thread is exiting holding locks!!!!");
         }
      }
#endif
   }
   lmgr_v(&lmgr_global_mutex);
}

#ifdef HAVE_WIN32
# define TID int_thread_id
#else
# define TID thread_id
#endif
/*
 * Search for a deadlock when it's secure to walk across
 * locks list. (after lmgr_detect_deadlock or a fatal signal)
 */
bool lmgr_detect_deadlock_unlocked()
{
   bool ret=false;
   lmgr_node_t *node=NULL;
   lmgr_lock_t *lock;
   lmgr_thread_t *item;
   dlist *g = New(dlist(node, &node->link));

   /* First, get a list of all node */
   foreach_dlist(item, global_mgr) {
      for(int i=0; i<=item->current; i++) {
         node = NULL;
         lock = &item->lock_list[i];
         /* Depending if the lock is granted or not, it's a child or a root
          *  Granted:  Mutex  -> Thread
          *  Wanted:   Thread -> Mutex
          *
          * Note: a Mutex can be locked only once, a thread can request only
          * one mutex.
          *
          */
         if (lock->state == LMGR_LOCK_GRANTED) {
            node = New(lmgr_node_t((void*)lock->lock, (void*)item->TID));
         } else if (lock->state == LMGR_LOCK_WANTED) {
            node = New(lmgr_node_t((void*)item->TID, (void*)lock->lock));
         }
         if (node) {
            g->append(node);
         }
      }
   }

   //foreach_dlist(node, g) {
   //   printf("g n=%p c=%p\n", node->node, node->child);
   //}

   ret = contains_cycle(g);
   if (ret) {
      printf("Found a deadlock !!!!\n");
   }

   delete g;
   return ret;
}

/*
 * Search for a deadlock in during the runtime
 * It will lock all thread specific lock manager, nothing
 * can be locked during this check.
 */
bool lmgr_detect_deadlock()
{
   bool ret=false;
   if (!lmgr_is_active()) {
      return ret;
   }

   lmgr_p(&lmgr_global_mutex);
   {
      lmgr_thread_t *item;
      foreach_dlist(item, global_mgr) {
         lmgr_p(&item->mutex);
      }

      ret = lmgr_detect_deadlock_unlocked();

      foreach_dlist(item, global_mgr) {
         lmgr_v(&item->mutex);
      }
   }
   lmgr_v(&lmgr_global_mutex);

   return ret;
}

/*
 * !!! WARNING !!!
 * Use this function is used only after a fatal signal
 * We don't use locking to display the information
 */
void dbg_print_lock(FILE *fp)
{
   fprintf(fp, "Attempt to dump locks\n");
   if (!lmgr_is_active()) {
      return ;
   }
   lmgr_thread_t *item;
   foreach_dlist(item, global_mgr) {
      item->_dump(fp);
   }
}

/*
 * Dump each lmgr_thread_t object
 */
void lmgr_dump()
{
   lmgr_p(&lmgr_global_mutex);
   {
      lmgr_thread_t *item;
      foreach_dlist(item, global_mgr) {
         item->dump(stderr);
      }
   }
   lmgr_v(&lmgr_global_mutex);
}

void cln_hdl(void *a)
{
   lmgr_cleanup_thread();
}

void *check_deadlock(void *)
{
   lmgr_init_thread();
   pthread_cleanup_push(cln_hdl, NULL);

   while (!do_quit) {
      struct timeval tv;
      struct timezone tz;
      struct timespec timeout;

      gettimeofday(&tv, &tz);
      timeout.tv_nsec = 0;
      timeout.tv_sec = tv.tv_sec + 30;

      pthread_mutex_lock(&undertaker_mutex);
      pthread_cond_timedwait(&undertaker_cond, &undertaker_mutex, &timeout);
      pthread_mutex_unlock(&undertaker_mutex);

      if(do_quit) {
         goto bail_out;
      }
   
      if (lmgr_detect_deadlock()) {
         /* If we have information about P()/V(), display them */
         if (debug_flags & DEBUG_MUTEX_EVENT && chk_dbglvl(DBGLEVEL_EVENT)) {
            debug_flags |= DEBUG_PRINT_EVENT;
         }
         lmgr_dump();
         ASSERT2(0, "Lock deadlock");   /* Abort if we found a deadlock */
      }
   }
   
bail_out:
   Dmsg0(100, "Exit check_deadlock.\n");
   pthread_cleanup_pop(1);
   return NULL;
}

/* This object is used when LMGR is not initialized */
static lmgr_dummy_thread_t dummy_lmgr;

/*
 * Retrieve the lmgr_thread_t object from the stack
 */
inline lmgr_thread_t *lmgr_get_thread_info()
{
   if (lmgr_is_active()) {
      return (lmgr_thread_t *)pthread_getspecific(lmgr_key);
   } else {
      return &dummy_lmgr;
   }
}

/*
 * Know if the current thread is registred (used when we
 * do not control thread creation)
 */
bool lmgr_thread_is_initialized()
{
   return pthread_getspecific(lmgr_key) != NULL;
}

/* On windows, the thread id is a struct, and sometime (for debug or openssl),
 * we need a int
 */
intptr_t bthread_get_thread_id()
{
   lmgr_thread_t *self = lmgr_get_thread_info();
   return self->int_thread_id;
}

/*
 * launch once for all threads
 */
void create_lmgr_key()
{
   int status = pthread_key_create(&lmgr_key, NULL);
   if (status != 0) {
      berrno be;
      Pmsg1(000, _("pthread key create failed: ERR=%s\n"),
            be.bstrerror(status));
      ASSERT2(0, "pthread_key_create failed");
   }

   lmgr_thread_t *n=NULL;
   global_mgr = New(dlist(n, &n->link));

   if (use_undertaker) {
      /* Create condwait */
      status = pthread_cond_init(&undertaker_cond, NULL);
      if (status != 0) {
         berrno be;
         Pmsg1(000, _("pthread_cond_init failed: ERR=%s\n"),
               be.bstrerror(status));
         ASSERT2(0, "pthread_cond_init failed");
      }
      status = pthread_create(&undertaker, NULL, check_deadlock, NULL);
      if (status != 0) {
         berrno be;
         Pmsg1(000, _("pthread_create failed: ERR=%s\n"),
               be.bstrerror(status));
         ASSERT2(0, "pthread_create failed");
      }
   }
}

/*
 * Each thread have to call this function to put a lmgr_thread_t object
 * in the stack and be able to call mutex_lock/unlock
 */
void lmgr_init_thread()
{
   int status = pthread_once(&key_lmgr_once, create_lmgr_key);
   if (status != 0) {
      berrno be;
      Pmsg1(000, _("pthread key create failed: ERR=%s\n"),
            be.bstrerror(status));
      ASSERT2(0, "pthread_once failed");
   }
   lmgr_thread_t *l = New(lmgr_thread_t());
   pthread_setspecific(lmgr_key, l);
   lmgr_register_thread(l);
}

/*
 * Call this function at the end of the thread
 */
void lmgr_cleanup_thread()
{
   if (!lmgr_is_active()) {
      return ;
   }
   lmgr_thread_t *self = lmgr_get_thread_info();
   lmgr_unregister_thread(self);
   delete(self);
}

/*
 * This function should be call at the end of the main thread
 * Some thread like the watchdog are already present, so the global_mgr
 * list is never empty. Should carefully clear the memory.
 */
void lmgr_cleanup_main()
{
   dlist *temp;

   if (!global_mgr) {
      return;
   }
   if (use_undertaker) {
      /* Signal to the check_deadlock thread to stop itself */
      pthread_mutex_lock(&undertaker_mutex);
      do_quit = true;
      pthread_cond_signal(&undertaker_cond);
      pthread_mutex_unlock(&undertaker_mutex);
      /* Should avoid memory leak reporting */
      pthread_join(undertaker, NULL);
      pthread_cond_destroy(&undertaker_cond);
   }
   lmgr_cleanup_thread();
   lmgr_p(&lmgr_global_mutex);
   {
      temp = global_mgr;
      global_mgr = NULL;
      delete temp;
   }
   lmgr_v(&lmgr_global_mutex);
}

void lmgr_add_event_p(const char *comment, intptr_t user_data, int32_t flags,
                      const char *file, int32_t line)
{
   lmgr_thread_t *self = lmgr_get_thread_info();
   self->add_event(comment, user_data, flags, file, line);
}

/*
 * Set the priority of the lmgr mutex object
 */
void bthread_mutex_set_priority(bthread_mutex_t *m, int prio)
{
#ifdef USE_LOCKMGR_PRIORITY
   m->priority = prio;
#endif
}

/*
 * Replacement for pthread_mutex_init()
 */
int pthread_mutex_init(bthread_mutex_t *m, const pthread_mutexattr_t *attr)
{
   m->priority = 0;
   return pthread_mutex_init(&m->mutex, attr);
}

/*
 * Replacement for pthread_mutex_destroy()
 */
int pthread_mutex_destroy(bthread_mutex_t *m)
{
   return pthread_mutex_destroy(&m->mutex);
}

/*
 * Replacement for pthread_kill (only with USE_LOCKMGR_SAFEKILL)
 */
int bthread_kill(pthread_t thread, int sig,
                 const char *file, int line)
{
   bool thread_found_in_process=false;
   int ret=-1;
   /* We dont allow to send signal to ourself */
   if (pthread_equal(thread, pthread_self())) {
      ASSERTD(!pthread_equal(thread, pthread_self()), "Wanted to pthread_kill ourself");
      Dmsg3(10, "%s:%d send kill to self thread %p\n", file, line, thread);
      errno = EINVAL;
      return -1;
   }

   /* This loop isn't very efficient with dozens of threads but we don't use
    * signal very much
    */
   lmgr_p(&lmgr_global_mutex);
   {
      lmgr_thread_t *item;
      foreach_dlist(item, global_mgr) {
         if (pthread_equal(thread, item->thread_id)) {
            ret = pthread_kill(thread, sig);
            thread_found_in_process=true;
            break;
         }
      }
   }
   lmgr_v(&lmgr_global_mutex);

   /* Sending a signal to non existing thread can create problem */
   if (!thread_found_in_process) {
      ASSERTD(thread_found_in_process, "Wanted to pthread_kill non-existant thread");
      Dmsg3(10, "%s:%d send kill to non-existant thread %p\n", file, line, thread);
      errno=ECHILD;
   }
   return ret;
}

/*
 * Replacement for pthread_mutex_lock()
 * Returns always ok
 */
int bthread_mutex_lock_p(bthread_mutex_t *m, const char *file, int line)
{
   lmgr_thread_t *self = lmgr_get_thread_info();
   self->pre_P(m, m->priority, file, line);
   lmgr_p(&m->mutex);
   self->post_P();
   return 0;
}

/*
 * Replacement for pthread_mutex_unlock()
 * Returns always ok
 */
int bthread_mutex_unlock_p(bthread_mutex_t *m, const char *file, int line)
{
   lmgr_thread_t *self = lmgr_get_thread_info();
   self->do_V(m, file, line);
   lmgr_v(&m->mutex);
   return 0;
}

/*
 * Replacement for pthread_mutex_lock() but with real pthread_mutex_t
 * Returns always ok
 */
int bthread_mutex_lock_p(pthread_mutex_t *m, const char *file, int line)
{
   lmgr_thread_t *self = lmgr_get_thread_info();
   self->pre_P(m, 0, file, line);
   lmgr_p(m);
   self->post_P();
   return 0;
}

/*
 * Replacement for pthread_mutex_unlock() but with real pthread_mutex_t
 * Returns always ok
 */
int bthread_mutex_unlock_p(pthread_mutex_t *m, const char *file, int line)
{
   lmgr_thread_t *self = lmgr_get_thread_info();
   self->do_V(m, file, line);
   lmgr_v(m);
   return 0;
}


/* TODO: check this
 */
int bthread_cond_wait_p(pthread_cond_t *cond,
                        pthread_mutex_t *m,
                        const char *file, int line)
{
   int ret;
   lmgr_thread_t *self = lmgr_get_thread_info();
   self->do_V(m, file, line);
   ret = pthread_cond_wait(cond, m);
   self->pre_P(m, 0, file, line);
   self->post_P();
   return ret;
}

/* TODO: check this
 */
int bthread_cond_timedwait_p(pthread_cond_t *cond,
                             pthread_mutex_t *m,
                             const struct timespec * abstime,
                             const char *file, int line)
{
   int ret;
   lmgr_thread_t *self = lmgr_get_thread_info();
   self->do_V(m, file, line);
   ret = pthread_cond_timedwait(cond, m, abstime);
   self->pre_P(m, 0, file, line);
   self->post_P();
   return ret;
}

/* TODO: check this
 */
int bthread_cond_wait_p(pthread_cond_t *cond,
                        bthread_mutex_t *m,
                        const char *file, int line)
{
   int ret;
   lmgr_thread_t *self = lmgr_get_thread_info();
   self->do_V(m, file, line);
   ret = pthread_cond_wait(cond, &m->mutex);
   self->pre_P(m, m->priority, file, line);
   self->post_P();
   return ret;
}

/* TODO: check this
 */
int bthread_cond_timedwait_p(pthread_cond_t *cond,
                             bthread_mutex_t *m,
                             const struct timespec * abstime,
                             const char *file, int line)
{
   int ret;
   lmgr_thread_t *self = lmgr_get_thread_info();
   self->do_V(m, file, line);
   ret = pthread_cond_timedwait(cond, &m->mutex, abstime);
   self->pre_P(m, m->priority, file, line);
   self->post_P();
   return ret;
}

/*  Test if this mutex is locked by the current thread
 *  returns:
 *     0 - unlocked
 *     1 - locked by the current thread
 *     2 - locked by an other thread
 */
int lmgr_mutex_is_locked(void *m)
{
   lmgr_thread_t *self = lmgr_get_thread_info();

   for(int i=0; i <= self->current; i++) {
      if (self->lock_list[i].lock == m) {
         return 1;              /* locked by us */
      }
   }

   return 0;                    /* not locked by us */
}

/*
 * Use this function when the caller handle the mutex directly
 *
 * lmgr_pre_lock(m, 10);
 * pthread_mutex_lock(m);
 * lmgr_post_lock(m);
 */
void lmgr_pre_lock(void *m, int prio, const char *file, int line)
{
   lmgr_thread_t *self = lmgr_get_thread_info();
   self->pre_P(m, prio, file, line);
}

/*
 * Use this function when the caller handle the mutex directly
 */
void lmgr_post_lock()
{
   lmgr_thread_t *self = lmgr_get_thread_info();
   self->post_P();
}

/*
 * Do directly pre_P and post_P (used by trylock)
 */
void lmgr_do_lock(void *m, int prio, const char *file, int line)
{
   lmgr_thread_t *self = lmgr_get_thread_info();
   self->pre_P(m, prio, file, line);
   self->post_P();
}

/*
 * Use this function when the caller handle the mutex directly
 */
void lmgr_do_unlock(void *m)
{
   lmgr_thread_t *self = lmgr_get_thread_info();
   self->do_V(m);
}

typedef struct {
   void *(*start_routine)(void*);
   void *arg;
} lmgr_thread_arg_t;

extern "C"
void *lmgr_thread_launcher(void *x)
{
   void *ret=NULL;
   lmgr_init_thread();
   pthread_cleanup_push(cln_hdl, NULL);

   lmgr_thread_arg_t arg;
   lmgr_thread_arg_t *a = (lmgr_thread_arg_t *)x;
   arg.start_routine = a->start_routine;
   arg.arg = a->arg;
   free(a);

   ret = arg.start_routine(arg.arg);
   pthread_cleanup_pop(1);
   return ret;
}

int lmgr_thread_create(pthread_t *thread,
                       const pthread_attr_t *attr,
                       void *(*start_routine)(void*), void *arg)
{
   /* lmgr should be active (lmgr_init_thread() call in main()) */
   ASSERT2(lmgr_is_active(), "Lock manager not active");
   /* Will be freed by the child */
   lmgr_thread_arg_t *a = (lmgr_thread_arg_t*) malloc(sizeof(lmgr_thread_arg_t));
   a->start_routine = start_routine;
   a->arg = arg;
   return pthread_create(thread, attr, lmgr_thread_launcher, a);
}

#else  /* USE_LOCKMGR */

intptr_t bthread_get_thread_id()
{
# ifdef HAVE_WIN32
   return (intptr_t)GetCurrentThreadId();
# else
   return (intptr_t)pthread_self();
# endif
}

/*
 * !!! WARNING !!!
 * Use this function is used only after a fatal signal
 * We don't use locking to display information
 */
void dbg_print_lock(FILE *fp)
{
   Pmsg0(000, "lockmgr disabled\n");
}

#endif  /* USE_LOCKMGR */

#ifdef HAVE_LINUX_OS
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/syscall.h>
#endif

/*
 * Set the Thread Id of the current thread to limit I/O operations
 */
int bthread_change_uid(uid_t uid, gid_t gid)
{
#if defined(HAVE_WIN32) || defined(HAVE_WIN64)
   /* TODO: Check the cygwin code for the implementation of setuid() */
   errno = ENOSYS;
   return -1;

#elif defined(HAVE_LINUX_OS)
   /* It can be also implemented with setfsuid() and setfsgid() */
   int ret=0;
   ret = syscall(SYS_setregid, getgid(), gid);
   if (ret == -1) {
      return -1;
   }
   return syscall(SYS_setreuid, getuid(), uid);

#elif defined(HAVE_PTHREAD_SETUGID_NP)
   return pthread_setugid_np(uid, gid);

#endif
   errno = ENOSYS;
   return -1;
}

#ifndef TEST_PROGRAM
#define TEST_PROGRAM_A
#endif

#ifdef TEST_PROGRAM
#include "bacula.h"
#include "unittests.h"
#include "lockmgr.h"
#undef P
#undef V
#define P(x) bthread_mutex_lock_p(&(x), __FILE__, __LINE__)
#define V(x) bthread_mutex_unlock_p(&(x), __FILE__, __LINE__)
#define pthread_create(a, b, c, d)    lmgr_thread_create(a,b,c,d)

bthread_mutex_t mutex1 = BTHREAD_MUTEX_NO_PRIORITY;
bthread_mutex_t mutex2 = BTHREAD_MUTEX_NO_PRIORITY;
bthread_mutex_t mutex3 = BTHREAD_MUTEX_NO_PRIORITY;
bthread_mutex_t mutex4 = BTHREAD_MUTEX_NO_PRIORITY;
bthread_mutex_t mutex5 = BTHREAD_MUTEX_NO_PRIORITY;
bthread_mutex_t mutex6 = BTHREAD_MUTEX_NO_PRIORITY;
bthread_mutex_t mutex_p1 = BTHREAD_MUTEX_PRIORITY(1);
bthread_mutex_t mutex_p2 = BTHREAD_MUTEX_PRIORITY(2);
bthread_mutex_t mutex_p3 = BTHREAD_MUTEX_PRIORITY(3);
static const char *my_prog;
static bool thevent1ok = false;
static bool thevent2ok = false;

void *self_lock(void *temp)
{
   P(mutex1);
   P(mutex1);
   V(mutex1);

   return NULL;
}

void *nolock(void *temp)
{
   P(mutex2);
   sleep(5);
   V(mutex2);
   return NULL;
}

void *locker(void *temp)
{
   bthread_mutex_t *m = (bthread_mutex_t*) temp;
   P(*m);
   V(*m);
   return NULL;
}

void *rwlocker(void *temp)
{
   brwlock_t *m = (brwlock_t*) temp;
   rwl_writelock(m);
   rwl_writelock(m);

   rwl_writeunlock(m);
   rwl_writeunlock(m);
   return NULL;
}

void *mix_rwl_mutex(void *temp)
{
   brwlock_t *m = (brwlock_t*) temp;
   P(mutex1);
   rwl_writelock(m);
   rwl_writeunlock(m);
   V(mutex1);
   return NULL;
}

void *thuid(void *temp)
{
//   char buf[512];
//   if (restrict_job_permissions("eric", "users", buf, sizeof(buf)) < 0) {
   if (bthread_change_uid(2, 100) == -1) {
      berrno be;
      fprintf(stderr, "Unable to change the uid err=%s\n", be.bstrerror());
   } else {
      fprintf(stderr, "UID set! %d:%d\n", (int)getuid(), (int)getgid());
      mkdir("/tmp/adirectory", 0755);
      system("touch /tmp/afile");
      system("id");
      fclose(fopen("/tmp/aaa", "a"));
   }
   if (bthread_change_uid(0, 0) == -1) {
      berrno be;
      fprintf(stderr, "Unable to change the uid err=%s\n", be.bstrerror());
   } else {
      fprintf(stderr, "UID set! %d:%d\n", (int)getuid(), (int)getgid());
      sleep(5);
      mkdir("/tmp/adirectory2", 0755);
      system("touch /tmp/afile2");
      system("id");
      fclose(fopen("/tmp/aaa2", "a"));
   }

   return NULL;
}

void *th2(void *temp)
{
   P(mutex2);
   P(mutex1);

   lmgr_dump();

   sleep(10);

   V(mutex1);
   V(mutex2);

   lmgr_dump();
   return NULL;
}
void *th1(void *temp)
{
   P(mutex1);
   sleep(2);
   P(mutex2);

   lmgr_dump();

   sleep(10);

   V(mutex2);
   V(mutex1);

   lmgr_dump();
   return NULL;
}

void *thx(void *temp)
{
   int s= 1 + (int) (500.0 * (rand() / (RAND_MAX + 1.0))) + 200;
   P(mutex1);
   bmicrosleep(0,s);
   P(mutex2);
   bmicrosleep(0,s);

   V(mutex2);
   V(mutex1);
   return NULL;
}

void *th3(void *a) {
   while (1) {
      fprintf(stderr, "undertaker sleep()\n");
      sleep(10);
      lmgr_dump();
      if (lmgr_detect_deadlock()) {
         lmgr_dump();
         exit(1);
      }
   }
   return NULL;
}

void *th_prio(void *a) {
   char buf[512];
   bstrncpy(buf, my_prog, sizeof(buf));
   bstrncat(buf, " priority", sizeof(buf));
   intptr_t ret = system(buf);
   return (void*) ret;
}

void *th_event1(void *a) {
   lmgr_thread_t *self = lmgr_get_thread_info();
   for (int i=0; i < 10000; i++) {
      if ((i % 7) == 0) {
         lmgr_add_event_flag("strdup test", i, LMGR_EVENT_DUP);
      } else {
         lmgr_add_event("My comment", i);
      }
   }
   thevent1ok = self->event_id == 10000;
   sleep(5);
   return NULL;
}

void *th_event2(void *a) {
   lmgr_thread_t *self = lmgr_get_thread_info();
   for (int i=0; i < 10000; i++) {
      if ((i % 2) == 0) {
         lmgr_add_event_flag(bstrdup("free test"), i, LMGR_EVENT_FREE);
      } else {
         lmgr_add_event("My comment", i);
      }
   }
   thevent2ok = self->event_id == 10000;
   sleep(5);
   return NULL;
}

/*
 * TODO:
 *  - Must detect multiple lock
 *  - lock/unlock in wrong order
 *  - deadlock with 2 or 3 threads
 */
int main(int argc, char **argv)
{
   Unittests lmgr_test("lockmgr_test", true, argc != 2);
   void *ret=NULL;
   lmgr_thread_t *self;
   pthread_t id1, id2, id3, id4, id5, tab[200];
   bthread_mutex_t bmutex1;
   pthread_mutex_t pmutex2;

   use_undertaker = false;
   my_prog = argv[0];
   self = lmgr_get_thread_info();

   /* below is used for checking forced SIGSEGV in separate process */
   if (argc == 2) {             /* do priority check */
      P(mutex_p2);                /* not permited */
      P(mutex_p1);
      V(mutex_p1);                /* never goes here */
      V(mutex_p2);
      return 0;
   }

   /* workaround for bthread_change_uid() failure for non-root */
   if (getuid() == 0){
      /* we can change uid/git, so proceed the test */
      pthread_create(&id5, NULL, thuid, NULL);
      pthread_join(id5, NULL);
      Pmsg2(0, "UID %d:%d\n", (int)getuid(), (int)getgid());
   } else {
      Pmsg0(0, "Skipped bthread_change_uid() for non-root\n");
   }

   Pmsg0(0, "Starting mutex priority test\n");
   pthread_mutex_init(&bmutex1, NULL);
   bthread_mutex_set_priority(&bmutex1, 10);

   pthread_mutex_init(&pmutex2, NULL);
   P(bmutex1);
   ok(self->max_priority == 10, "Check self max_priority");
   P(pmutex2);
   ok(bmutex1.priority == 10, "Check bmutex_set_priority()");
   V(pmutex2);
   V(bmutex1);
   ok(self->max_priority == 0, "Check self max_priority");

   Pmsg0(0, "Starting self deadlock tests\n");
   pthread_create(&id1, NULL, self_lock, NULL);
   sleep(2);
   ok(lmgr_detect_deadlock(), "Check self deadlock");
   lmgr_v(&mutex1.mutex);                /* a bit dirty */
   pthread_join(id1, NULL);

   Pmsg0(0, "Starting thread kill tests\n");
   pthread_create(&id1, NULL, nolock, NULL);
   sleep(2);
   ok(bthread_kill(id1, SIGUSR2) == 0, "Kill existing thread");
   pthread_join(id1, NULL);
   ok(bthread_kill(id1, SIGUSR2) == -1, "Kill non-existing thread");
   ok(bthread_kill(pthread_self(), SIGUSR2) == -1, "Kill self");

   Pmsg0(0, "Starting thread locks tests\n");
   pthread_create(&id1, NULL, nolock, NULL);
   sleep(2);
   nok(lmgr_detect_deadlock(), "Check for nolock");
   pthread_join(id1, NULL);

   P(mutex1);
   pthread_create(&id1, NULL, locker, &mutex1);
   pthread_create(&id2, NULL, locker, &mutex1);
   pthread_create(&id3, NULL, locker, &mutex1);
   sleep(2);
   nok(lmgr_detect_deadlock(), "Check for multiple lock");
   V(mutex1);
   pthread_join(id1, NULL);
   pthread_join(id2, NULL);
   pthread_join(id3, NULL);

   brwlock_t wr;
   rwl_init(&wr);
   rwl_writelock(&wr);
   rwl_writelock(&wr);
   pthread_create(&id1, NULL, rwlocker, &wr);
   pthread_create(&id2, NULL, rwlocker, &wr);
   pthread_create(&id3, NULL, rwlocker, &wr);
   nok(lmgr_detect_deadlock(), "Check for multiple rwlock");
   rwl_writeunlock(&wr);
   nok(lmgr_detect_deadlock(), "Check for simple rwlock");
   rwl_writeunlock(&wr);
   nok(lmgr_detect_deadlock(), "Check for multiple rwlock");

   pthread_join(id1, NULL);
   pthread_join(id2, NULL);
   pthread_join(id3, NULL);

   rwl_writelock(&wr);
   P(mutex1);
   pthread_create(&id1, NULL, mix_rwl_mutex, &wr);
   nok(lmgr_detect_deadlock(), "Check for mix rwlock/mutex");
   V(mutex1);
   nok(lmgr_detect_deadlock(), "Check for mix rwlock/mutex");
   rwl_writeunlock(&wr);
   nok(lmgr_detect_deadlock(), "Check for mix rwlock/mutex");
   pthread_join(id1, NULL);

   P(mutex5);
   P(mutex6);
   V(mutex5);
   V(mutex6);

   nok(lmgr_detect_deadlock(), "Check for wrong order");

   for(int j=0; j<200; j++) {
      pthread_create(&tab[j], NULL, thx, NULL);
   }
   for(int j=0; j<200; j++) {
      pthread_join(tab[j], NULL);
      if (j%3) { lmgr_detect_deadlock();}
   }
   nok(lmgr_detect_deadlock(), "Check 200 lockers");

   P(mutex4);
   P(mutex5);
   P(mutex6);
   ok(lmgr_mutex_is_locked(&mutex6) == 1, "Check if mutex is locked");
   V(mutex6);
   ok(lmgr_mutex_is_locked(&mutex6) == 0, "Check if mutex is unlocked");
   V(mutex5);
   V(mutex4);

   Pmsg0(0, "Starting threads deadlock tests\n");
   pthread_create(&id1, NULL, th1, NULL);
   sleep(1);
   pthread_create(&id2, NULL, th2, NULL);
   sleep(1);
   ok(lmgr_detect_deadlock(), "Check for deadlock");

   Pmsg0(0, "Starting for max_priority locks tests\n");
   pthread_create(&id3, NULL, th_prio, NULL);
   pthread_join(id3, &ret);
   ok(ret != 0, "Check for priority segfault");

   P(mutex_p1);
   ok(self->max_priority == 1, "Check max_priority 1/4");
   P(mutex_p2);
   ok(self->max_priority == 2, "Check max_priority 2/4");
   P(mutex_p3);
   ok(self->max_priority == 3, "Check max_priority 3/4");
   P(mutex6);
   ok(self->max_priority == 3, "Check max_priority 4/4");
   V(mutex6);
   ok(self->max_priority == 3, "Check max_priority 1/5");
   V(mutex_p3);
   ok(self->max_priority == 2, "Check max_priority 4/5");
   V(mutex_p2);
   ok(self->max_priority == 1, "Check max_priority 4/5");
   V(mutex_p1);
   ok(self->max_priority == 0, "Check max_priority 5/5");


   P(mutex_p1);
   P(mutex_p2);
   P(mutex_p3);
   P(mutex6);
   ok(self->max_priority == 3, "Check max_priority mixed");
   V(mutex_p2);
   ok(self->max_priority == 3, "Check max_priority mixed");
   V(mutex_p1);
   ok(self->max_priority == 3, "Check max_priority mixed");
   V(mutex_p3);
   ok(self->max_priority == 0, "Check max_priority mixed");
   V(mutex6);
   ok(self->max_priority == 0, "Check max_priority mixed");

   P(mutex_p1);
   P(mutex_p2);
   V(mutex_p1);
   V(mutex_p2);

   Pmsg0(0, "Start lmgr_add_even tests\n");
   for (int i=0; i < 10000; i++) {
      if ((i % 7) == 0) {
         lmgr_add_event_flag("xxxxxxxxxxxxxxxx strdup test xxxxxxxxxxxxxxxx", i, LMGR_EVENT_DUP);
      } else {
         lmgr_add_event("My comment", i);
      }
   }
   ok(self->event_id == 10000, "Checking registered events in self");

   pthread_create(&id4, NULL, th_event1, NULL);
   pthread_create(&id5, NULL, th_event2, NULL);

   sleep(2);

   pthread_join(id4, NULL);
   pthread_join(id5, NULL);

   ok(thevent1ok, "Checking registered events in thread1");
   ok(thevent2ok, "Checking registered events in thread2");

   return report();
}
#endif   /* TEST_PROGRAM */
