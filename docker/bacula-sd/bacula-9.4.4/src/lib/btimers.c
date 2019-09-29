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
 * Process and thread timer routines, built on top of watchdogs.
 *
 *    Nic Bellamy <nic@bellamy.co.nz>, October 2004.
 *
*/

#include "bacula.h"
#include "jcr.h"

const int dbglvl = 900;

/* Forward referenced functions */
static void stop_btimer(btimer_t *wid);
static btimer_t *btimer_start_common(uint32_t wait);

/* Forward referenced callback functions */
static void callback_child_timer(watchdog_t *self);
static void callback_thread_timer(watchdog_t *self);
#ifdef xxx
static void destructor_thread_timer(watchdog_t *self);
static void destructor_child_timer(watchdog_t *self);
#endif

/*
 * Start a timer on a child process of pid, kill it after wait seconds.
 *
 *  Returns: btimer_t *(pointer to btimer_t struct) on success
 *           NULL on failure
 */
btimer_t *start_child_timer(JCR *jcr, pid_t pid, uint32_t wait)
{
   btimer_t *wid;

   wid = btimer_start_common(wait);
   if (wid == NULL) {
      return NULL;
   }
   wid->type = TYPE_CHILD;
   wid->pid = pid;
   wid->killed = false;
   wid->jcr = jcr;

   wid->wd->callback = callback_child_timer;
   wid->wd->one_shot = false;
   wid->wd->interval = wait;
   register_watchdog(wid->wd);

   Dmsg3(dbglvl, "Start child timer %p, pid %d for %d secs.\n", wid, pid, wait);
   return wid;
}

/*
 * Stop child timer
 */
void stop_child_timer(btimer_t *wid)
{
   if (wid == NULL) {
      return;
   }
   Dmsg2(dbglvl, "Stop child timer %p pid %d\n", wid, wid->pid);
   stop_btimer(wid);
}

#ifdef xxx
static void destructor_child_timer(watchdog_t *self)
{
   btimer_t *wid = (btimer_t *)self->data;
   free(wid->wd);
   free(wid);
}
#endif

static void callback_child_timer(watchdog_t *self)
{
   btimer_t *wid = (btimer_t *)self->data;

   if (!wid->killed) {
      /* First kill attempt; try killing it softly (kill -SONG) first */
      wid->killed = true;

      Dmsg2(dbglvl, "watchdog %p term PID %d\n", self, wid->pid);

      /* Kill -TERM the specified PID, and reschedule a -KILL for 5 seconds
       * later. (Warning: this should let dvd-writepart enough time to term
       * and kill growisofs, which takes 3 seconds, so the interval must not
       * be less than 5 seconds)
       */
      kill(wid->pid, SIGTERM);
      self->interval = 10;
   } else {
      /* This is the second call - terminate with prejudice. */
      Dmsg2(dbglvl, "watchdog %p kill PID %d\n", self, wid->pid);

      kill(wid->pid, SIGKILL);

      /* Setting one_shot to true before we leave ensures we don't get
       * rescheduled.
       */
      self->one_shot = true;
   }
}

/*
 * Start a timer on a thread. kill it after wait seconds.
 *
 *  Returns: btimer_t *(pointer to btimer_t struct) on success
 *           NULL on failure
 */
btimer_t *start_thread_timer(JCR *jcr, pthread_t tid, uint32_t wait)
{
   btimer_t *wid;
   wid = btimer_start_common(wait);
   if (wid == NULL) {
      Dmsg1(dbglvl, "start_thread_timer return NULL from common. wait=%d.\n", wait);
      return NULL;
   }
   wid->type = TYPE_PTHREAD;
   wid->tid = tid;
   wid->jcr = jcr;

   wid->wd->callback = callback_thread_timer;
   wid->wd->one_shot = true;
   wid->wd->interval = wait;
   register_watchdog(wid->wd);

   Dmsg3(dbglvl, "Start thread timer %p tid %p for %d secs.\n", wid, tid, wait);

   return wid;
}

/*
 * Start a timer on a BSOCK. kill it after wait seconds.
 *
 *  Returns: btimer_t *(pointer to btimer_t struct) on success
 *           NULL on failure
 */
btimer_t *_start_bsock_timer(BSOCK *bsock, uint32_t wait)
{
   btimer_t *wid;
   if (wait <= 0) {                 /* wait should be > 0 */
      return NULL;
   }
   wid = btimer_start_common(wait);
   if (wid == NULL) {
      return NULL;
   }
   wid->type = TYPE_BSOCK;
   wid->tid = pthread_self();
   wid->bsock = bsock;
   wid->jcr = bsock->jcr();

   wid->wd->callback = callback_thread_timer;
   wid->wd->one_shot = true;
   wid->wd->interval = wait;
   register_watchdog(wid->wd);

   Dmsg4(dbglvl, "Start bsock timer %p tid=%p for %d secs at %d\n", wid,
         wid->tid, wait, time(NULL));

   return wid;
}

/*
 * Start a timer on a BSOCK. kill it after wait seconds.
 *
 *  Returns: btimer_t *(pointer to btimer_t struct) on success
 *           NULL on failure
 */
btimer_t *start_bsock_timer(BSOCKCORE *bsock, uint32_t wait)
{
   return _start_bsock_timer((BSOCK*)bsock, wait);
};

/*
 * Start a timer on a BSOCK. kill it after wait seconds.
 *
 *  Returns: btimer_t *(pointer to btimer_t struct) on success
 *           NULL on failure
 */
btimer_t *start_bsock_timer(BSOCK *bsock, uint32_t wait)
{
   return _start_bsock_timer(bsock, wait);
};

/*
 * Stop bsock timer
 */
void stop_bsock_timer(btimer_t *wid)
{
   if (wid == NULL) {
      return;
   }
   Dmsg3(dbglvl, "Stop bsock timer %p tid=%p at %d.\n", wid, wid->tid, time(NULL));
   stop_btimer(wid);
}


/*
 * Stop thread timer
 */
void stop_thread_timer(btimer_t *wid)
{
   if (wid == NULL) {
      return;
   }
   Dmsg2(dbglvl, "Stop thread timer %p tid=%p.\n", wid, wid->tid);
   stop_btimer(wid);
}

#ifdef xxx
static void destructor_thread_timer(watchdog_t *self)
{
   btimer_t *wid = (btimer_t *)self->data;
   free(wid->wd);
   free(wid);
}
#endif

static void callback_thread_timer(watchdog_t *self)
{
   btimer_t *wid = (btimer_t *)self->data;

   Dmsg4(dbglvl, "thread timer %p kill %s tid=%p at %d.\n", self,
      wid->type == TYPE_BSOCK ? "bsock" : "thread", wid->tid, time(NULL));
   if (wid->jcr) {
      Dmsg2(dbglvl, "killed jid=%u Job=%s\n", wid->jcr->JobId, wid->jcr->Job);
   }

   if (wid->type == TYPE_BSOCK && wid->bsock) {
      wid->bsock->set_timed_out();
   }
   pthread_kill(wid->tid, TIMEOUT_SIGNAL);
}

static btimer_t *btimer_start_common(uint32_t wait)
{
   btimer_t *wid = (btimer_t *)malloc(sizeof(btimer_t));

   wid->wd = new_watchdog();
   if (wid->wd == NULL) {
      free(wid);
      return NULL;
   }
   wid->wd->data = wid;
   wid->killed = false;

   return wid;
}

/*
 * Stop btimer
 */
static void stop_btimer(btimer_t *wid)
{
   if (wid == NULL) {
      Emsg0(M_ABORT, 0, _("stop_btimer called with NULL btimer_id\n"));
   }
   unregister_watchdog(wid->wd);
   free(wid->wd);
   free(wid);
}
