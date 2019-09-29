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
 * Collection of Bacula Storage daemon locking software
 *
 *  Kern Sibbald, June 2007
 *
 */

#include "bacula.h"                   /* pull in global headers */
#include "stored.h"                   /* pull in Storage Deamon headers */

#ifdef SD_DEBUG_LOCK
const int dbglvl = DT_LOCK|30;
#else
const int dbglvl = DT_LOCK|50;
#endif


/*
 *
 * The Storage daemon has three locking concepts that must be
 *   understood:
 *
 *  1. dblock    blocking the device, which means that the device
 *               is "marked" in use.  When setting and removing the
                 block, the device is locked, but after dblock is
                 called the device is unlocked.
 *  2. Lock()    simple mutex that locks the device structure. A Lock
 *               can be acquired while a device is blocked if it is not
 *               locked.
 *  3. rLock(locked)  "recursive" Lock, when means that a Lock (mutex)
 *               will be acquired on the device if it is not blocked
 *               by some other thread. If the device was blocked by
 *               the current thread, it will acquire the lock.
 *               If some other thread has set a block on the device,
 *               this call will wait until the device is unblocked.
 *               Can be called with locked true, which means the
 *               Lock is already set
 *
 *  A lock is normally set when modifying the device structure.
 *  A rLock is normally acquired when you want to block the device
 *    i.e. it will wait until the device is not blocked.
 *  A block is normally set during long operations like writing to
 *    the device.
 *  If you are writing the device, you will normally block and
 *    lock it.
 *  A lock cannot be violated. No other thread can touch the
 *    device while a lock is set.
 *  When a block is set, every thread accept the thread that set
 *    the block will block if rLock is called.
 *  A device can be blocked for multiple reasons, labeling, writing,
 *    acquiring (opening) the device, waiting for the operator, unmounted,
 *    ...
 *  Under certain conditions the block that is set on a device can be
 *    stolen and the device can be used by another thread. For example,
 *    a device is blocked because it is waiting for the operator to
 *    mount a tape.  The operator can then unmount the device, and label
 *    a tape, re-mount it, give back the block, and the job will continue.
 *
 *
 * Functions:
 *
 *   DEVICE::Lock()   does P(m_mutex)     (in dev.h)
 *   DEVICE::Unlock() does V(m_mutex)
 *
 *   DEVICE::rLock(locked) allows locking the device when this thread
 *                     already has the device blocked.
 *                    if (!locked)
 *                       Lock()
 *                    if blocked and not same thread that locked
 *                       pthread_cond_wait
 *                    leaves device locked
 *
 *   DEVICE::rUnlock() unlocks but does not unblock
 *                    same as Unlock();
 *
 *   DEVICE::dblock(why)  does
 *                    rLock();         (recursive device lock)
 *                    block_device(this, why)
 *                    rUnlock()
 *
 *   DEVICE::dunblock does
 *                    Lock()
 *                    unblock_device()
 *                    Unlock()
 *
 *   block_device() does  (must be locked and not blocked at entry)
 *                    set blocked status
 *                    set our pid
 *
 *   unblock_device() does (must be blocked at entry)
 *                        (locked on entry)
 *                        (locked on exit)
 *                    set unblocked status
 *                    clear pid
 *                    if waiting threads
 *                       pthread_cond_broadcast
 *
 *   obtain_device_block() does (must be locked and blocked at entry)
 *                    save status
 *                    set new blocked status
 *                    set new pid
 *
 *   give_back_device_block() does (must be blocked and locked)
 *                    reset blocked status
 *                    save previous blocked
 *                    reset pid
 *                    if waiting threads
 *                       pthread_cond_broadcast
 *
 */

void DEVICE::dblock(int why)
{
   rLock(false);              /* need recursive lock to block */
   block_device(this, why);
   rUnlock();
}

void DEVICE::dunblock(bool locked)
{
   if (!locked) {
      Lock();
   }
   unblock_device(this);
   Unlock();
}



/*
 * Debug DEVICE locks  N.B.
 *
 */

#ifdef DEV_DEBUG_LOCK

void DEVICE::dbg_Lock(const char *file, int line)
{
   Dmsg4(sd_dbglvl, "Lock %s from %s:%d precnt=%d\n", device->hdr.name, file, line, m_count);
   bthread_mutex_lock_p(&m_mutex, file, line);
   m_pid = pthread_self();
   m_count++;
}

void DEVICE::dbg_Unlock(const char *file, int line)
{
   m_count--;
   clear_thread_id(m_pid);
   Dmsg4(sd_dbglvl, "Unlock %s from %s:%d postcnt=%d\n", device->hdr.name, file, line, m_count);
   bthread_mutex_unlock_p(&m_mutex, file, line);
}

void DEVICE::dbg_rUnlock(const char *file, int line)
{
   Dmsg2(sd_dbglvl, "rUnlock from %s:%d\n", file, line);
   dbg_Unlock(file, line);
}

#else

/*
 * DEVICE locks  N.B.
 *
 */


void DEVICE::rUnlock()
{
   Unlock();
}

void DEVICE::Lock()
{
   P(m_mutex);
}

void DEVICE::Unlock()
{
   V(m_mutex);
}

#endif /* DEV_DEBUG_LOCK */

/*
 * This is a recursive lock that checks if the device is blocked.
 *
 * When blocked is set, all threads EXCEPT thread with id no_wait_id
 * must wait. The no_wait_id thread is out obtaining a new volume
 * and preparing the label.
 */
#ifdef DEV_DEBUG_LOCK
void DEVICE::dbg_rLock(const char *file, int line, bool locked)
{
   Dmsg3(sd_dbglvl, "Enter rLock blked=%s from %s:%d\n", print_blocked(),
         file, line);
   if (!locked) {
      /* lockmgr version of P(m_mutex) */
      Dmsg4(sd_dbglvl, "Lock %s in rLock %s from %s:%d\n",
         device->hdr.name, print_blocked(), file, line);
      bthread_mutex_lock_p(&m_mutex, file, line);
      m_count++;
   }

   if (blocked() && !pthread_equal(no_wait_id, pthread_self())) {
      num_waiting++;             /* indicate that I am waiting */
      while (blocked()) {
         int stat;
#ifndef HAVE_WIN32
         /* thread id on Win32 may be a struct */
         Dmsg5(sd_dbglvl, "Blocked by %d %s in rLock blked=%s no_wait=%p me=%p\n",
            blocked_by, device->hdr.name, print_blocked(), no_wait_id, pthread_self());
#endif
         if ((stat = bthread_cond_wait_p(&this->wait, &m_mutex, file, line)) != 0) {
            berrno be;
            this->dbg_Unlock(file, line);
            Emsg1(M_ABORT, 0, _("pthread_cond_wait failure. ERR=%s\n"),
               be.bstrerror(stat));
         }
      }
      num_waiting--;             /* no longer waiting */
   }
}
#else /* DEV_DEBUG_LOCK */

void DEVICE::rLock(bool locked)
{
   if (!locked) {
      Lock();
      m_count++;
   }

   if (blocked() && !pthread_equal(no_wait_id, pthread_self())) {
      num_waiting++;             /* indicate that I am waiting */
      while (blocked()) {
         int stat;
#ifndef HAVE_WIN32
         /* thread id on Win32 may be a struct */
         Dmsg5(sd_dbglvl, "Blocked by %d rLock %s blked=%s no_wait=%p me=%p\n",
            blocked_by, device->hdr.name, print_blocked(), no_wait_id, pthread_self());
#endif
         if ((stat = pthread_cond_wait(&this->wait, &m_mutex)) != 0) {
            berrno be;
            this->Unlock();
            Emsg1(M_ABORT, 0, _("pthread_cond_wait failure. ERR=%s\n"),
               be.bstrerror(stat));
         }
      }
      num_waiting--;             /* no longer waiting */
   }
}

#endif  /* DEV_DEBUG_LOCK */

#ifdef SD_DEBUG_LOCK

void DEVICE::dbg_Lock_acquire(const char *file, int line)
{
   Dmsg2(sd_dbglvl, "Lock_acquire from %s:%d\n", file, line);
   bthread_mutex_lock_p(&acquire_mutex, file, line);
}

void DEVICE::dbg_Unlock_acquire(const char *file, int line)
{
   Dmsg2(sd_dbglvl, "Unlock_acquire from %s:%d\n", file, line);
   bthread_mutex_unlock_p(&acquire_mutex, file, line);
}

void DEVICE::dbg_Lock_read_acquire(const char *file, int line)
{
   Dmsg2(sd_dbglvl, "Lock_read_acquire from %s:%d\n", file, line);
   bthread_mutex_lock_p(&read_acquire_mutex, file, line);
}

void DEVICE::dbg_Unlock_read_acquire(const char *file, int line)
{
   Dmsg2(sd_dbglvl, "Unlock_read_acquire from %s:%d\n", file, line);
   bthread_mutex_unlock_p(&read_acquire_mutex, file, line);
}

void DEVICE::dbg_Lock_VolCatInfo(const char *file, int line)
{
   bthread_mutex_lock_p(&volcat_mutex, file, line);
}

void DEVICE::dbg_Unlock_VolCatInfo(const char *file, int line)
{
   bthread_mutex_unlock_p(&volcat_mutex, file, line);
}

#else

void DEVICE::Lock_acquire()
{
   P(acquire_mutex);
}

void DEVICE::Unlock_acquire()
{
   V(acquire_mutex);
}

void DEVICE::Lock_read_acquire()
{
   P(read_acquire_mutex);
}

void DEVICE::Unlock_read_acquire()
{
   V(read_acquire_mutex);
}

void DEVICE::Lock_VolCatInfo()
{
   P(volcat_mutex);
}

void DEVICE::Unlock_VolCatInfo()
{
   V(volcat_mutex);
}



#endif

/* Main device access control */
int DEVICE::init_mutex()
{
   return pthread_mutex_init(&m_mutex, NULL);
}

/* Mutex around the freespace command */
int DEVICE::init_freespace_mutex()
{
   return pthread_mutex_init(&freespace_mutex, NULL);
}

/* Write device acquire mutex */
int DEVICE::init_acquire_mutex()
{
   return pthread_mutex_init(&acquire_mutex, NULL);
}

/* Read device acquire mutex */
int DEVICE::init_read_acquire_mutex()
{
   return pthread_mutex_init(&read_acquire_mutex, NULL);
}

/* VolCatInfo mutex */
int DEVICE::init_volcat_mutex()
{
   return pthread_mutex_init(&volcat_mutex, NULL);
}

/* dcrs mutex */
int DEVICE::init_dcrs_mutex()
{
   return pthread_mutex_init(&dcrs_mutex, NULL);
}

/* Set order in which device locks must be acquired */
void DEVICE::set_mutex_priorities()
{
   /* Ensure that we respect this order in P/V operations */
   bthread_mutex_set_priority(&m_mutex,       PRIO_SD_DEV_ACCESS);
   bthread_mutex_set_priority(&spool_mutex,   PRIO_SD_DEV_SPOOL);
   bthread_mutex_set_priority(&acquire_mutex, PRIO_SD_DEV_ACQUIRE);
}

int DEVICE::next_vol_timedwait(const struct timespec *timeout)
{
   return pthread_cond_timedwait(&wait_next_vol, &m_mutex, timeout);
}


/*
 * Block all other threads from using the device
 *  Device must already be locked.  After this call,
 *  the device is blocked to any thread calling dev->rLock(),
 *  but the device is not locked (i.e. no P on device).  Also,
 *  the current thread can do slip through the dev->rLock()
 *  calls without blocking.
 */
void _block_device(const char *file, int line, DEVICE *dev, int state)
{
   ASSERT2(dev->blocked() == BST_NOT_BLOCKED, "Block request of device already blocked");
   dev->set_blocked(state);           /* make other threads wait */
   dev->no_wait_id = pthread_self();  /* allow us to continue */
   dev->blocked_by = get_jobid_from_tsd();
   Dmsg4(sd_dbglvl, "Blocked %s %s from %s:%d\n",
      dev->device->hdr.name, dev->print_blocked(), file, line);
}

/*
 * Unblock the device, and wake up anyone who went to sleep.
 * Enter: device locked
 * Exit:  device locked
 */
void _unblock_device(const char *file, int line, DEVICE *dev)
{
   Dmsg4(sd_dbglvl, "Unblocked %s %s from %s:%d\n", dev->device->hdr.name,
      dev->print_blocked(), file, line);
   ASSERT2(dev->blocked(), "Unblock request of device not blocked");
   dev->set_blocked(BST_NOT_BLOCKED);
   dev->blocked_by = 0;
   clear_thread_id(dev->no_wait_id);
   if (dev->num_waiting > 0) {
      pthread_cond_broadcast(&dev->wait); /* wake them up */
   }
}

static pthread_mutex_t block_mutex = PTHREAD_MUTEX_INITIALIZER;
/*
 * Enter and leave with device locked
 *
 * Note: actually this routine:
 *   returns true if it can either set or steal the device block
 *   returns false if it cannot block the device
 */
bool DEVICE::_obtain_device_block(const char *file, int line,
                                  bsteal_lock_t *hold, int retry, int state)
{
   int ret;
   int r = retry;

   if (!can_obtain_block() && !pthread_equal(no_wait_id, pthread_self())) {
      num_waiting++;             /* indicate that I am waiting */
      while ((retry == 0 || r-- > 0) && !can_obtain_block()) {
         if ((ret = bthread_cond_wait_p(&wait, &m_mutex, file, line)) != 0) {
            berrno be;
            Emsg1(M_ABORT, 0, _("pthread_cond_wait failure. ERR=%s\n"),
                  be.bstrerror(ret));
         }
      }
      num_waiting--;             /* no longer waiting */
   }

   P(block_mutex);
   Dmsg4(sd_dbglvl, "Steal lock %s old=%s from %s:%d\n",
      device->hdr.name, print_blocked(), file, line);

   if (!can_obtain_block() && !pthread_equal(no_wait_id, pthread_self())) { 
      V(block_mutex);
      return false;
   }
   hold->dev_blocked = blocked();
   hold->dev_prev_blocked = dev_prev_blocked;
   hold->no_wait_id = no_wait_id;
   hold->blocked_by = blocked_by;
   set_blocked(state);
   Dmsg1(sd_dbglvl, "steal block. new=%s\n", print_blocked());
   no_wait_id = pthread_self();
   blocked_by = get_jobid_from_tsd();
   V(block_mutex);
   return true;
}

/*
 * Enter with device blocked and locked by us
 * Exit with device locked, and blocked by previous owner
 */
void _give_back_device_block(const char *file, int line,
                             DEVICE *dev, bsteal_lock_t *hold)
{
   Dmsg4(sd_dbglvl, "Return lock %s old=%s from %s:%d\n",
      dev->device->hdr.name, dev->print_blocked(), file, line);
   P(block_mutex);
   dev->set_blocked(hold->dev_blocked);
   dev->dev_prev_blocked = hold->dev_prev_blocked;
   dev->no_wait_id = hold->no_wait_id;
   dev->blocked_by = hold->blocked_by;
   Dmsg1(sd_dbglvl, "return lock. new=%s\n", dev->print_blocked());
   if (dev->num_waiting > 0) {
      pthread_cond_broadcast(&dev->wait); /* wake them up */
   }
   V(block_mutex);
}

const char *DEVICE::print_blocked() const
{
   switch (m_blocked) {
   case BST_NOT_BLOCKED:
      return "BST_NOT_BLOCKED";
   case BST_UNMOUNTED:
      return "BST_UNMOUNTED";
   case BST_WAITING_FOR_SYSOP:
      return "BST_WAITING_FOR_SYSOP";
   case BST_DOING_ACQUIRE:
      return "BST_DOING_ACQUIRE";
   case BST_WRITING_LABEL:
      return "BST_WRITING_LABEL";
   case BST_UNMOUNTED_WAITING_FOR_SYSOP:
      return "BST_UNMOUNTED_WAITING_FOR_SYSOP";
   case BST_MOUNT:
      return "BST_MOUNT";
   case BST_DESPOOLING:
      return "BST_DESPOOLING";
   case BST_RELEASING:
      return "BST_RELEASING";
   default:
      return _("unknown blocked code");
   }
}


/*
 * Check if the device is blocked or not
 */
bool DEVICE::is_device_unmounted()
{
   bool stat;

   int blk = blocked();
   stat = (blk == BST_UNMOUNTED) ||
          (blk == BST_UNMOUNTED_WAITING_FOR_SYSOP);
   return stat;
}
