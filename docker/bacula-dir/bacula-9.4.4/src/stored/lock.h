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
 * Definitions for locking and blocking functions in the SD
 *
 * Kern Sibbald, pulled out of dev.h June 2007
 *
 */


#ifndef __LOCK_H
#define __LOCK_H 1

void    _lock_reservations(const char *file="**Unknown**", int line=0);
void    _unlock_reservations();
void    _lock_volumes(const char *file="**Unknown**", int line=0);
void    _unlock_volumes();

#ifdef  SD_DEBUG_LOCK

#define lock_reservations() \
         do { Dmsg3(sd_dbglvl, "lock_reservations at %s:%d precnt=%d\n", \
              __FILE__, __LINE__, \
              reservations_lock_count); \
              _lock_reservations(__FILE__, __LINE__); \
              Dmsg0(sd_dbglvl, "lock_reservations: got lock\n"); \
         } while (0)
#define unlock_reservations() \
         do { Dmsg3(sd_dbglvl, "unlock_reservations at %s:%d precnt=%d\n", \
              __FILE__, __LINE__, \
              reservations_lock_count); \
                   _unlock_reservations(); } while (0)

#define lock_volumes() \
         do { Dmsg3(sd_dbglvl, "lock_volumes at %s:%d precnt=%d\n", \
              __FILE__, __LINE__, \
              vol_list_lock_count); \
              _lock_volumes(__FILE__, __LINE__); \
              Dmsg0(sd_dbglvl, "lock_volumes: got lock\n"); \
         } while (0)

#define unlock_volumes() \
         do { Dmsg3(sd_dbglvl, "unlock_volumes at %s:%d precnt=%d\n", \
              __FILE__, __LINE__, \
              vol_list_lock_count); \
                   _unlock_volumes(); } while (0)

#else

#define lock_reservations() _lock_reservations(__FILE__, __LINE__)
#define unlock_reservations() _unlock_reservations()
#define lock_volumes() _lock_volumes(__FILE__, __LINE__)
#define unlock_volumes() _unlock_volumes()

#endif

#ifdef DEV_DEBUG_LOCK
#define Lock() dbg_Lock(__FILE__, __LINE__)
#define Unlock() dbg_Unlock(__FILE__, __LINE__)
#define rLock(locked) dbg_rLock(__FILE__, __LINE__, locked)
#define rUnlock() dbg_rUnlock(__FILE__, __LINE__)
#endif

#ifdef SD_DEBUG_LOCK
#define Lock_acquire() dbg_Lock_acquire(__FILE__, __LINE__)
#define Unlock_acquire() dbg_Unlock_acquire(__FILE__, __LINE__)
#define Lock_read_acquire() dbg_Lock_read_acquire(__FILE__, __LINE__)
#define Unlock_read_acquire() dbg_Unlock_read_acquire(__FILE__, __LINE__)
#define Lock_VolCatInfo() dbg_Lock_VolCatInfo(__FILE__, __LINE__)
#define Unlock_VolCatInfo() dbg_Unlock_VolCatInfo(__FILE__, __LINE__)
#endif

#define block_device(d, s)          _block_device(__FILE__, __LINE__, (d), s)
#define unblock_device(d)           _unblock_device(__FILE__, __LINE__, (d))

#define obtain_device_block(d, p, r, s) (d)->_obtain_device_block(__FILE__, __LINE__, (p), (r), (s))
#define give_back_device_block(d, p) _give_back_device_block(__FILE__, __LINE__, (d), (p))

/* m_blocked states (mutually exclusive) */
enum {
   BST_NOT_BLOCKED = 0,               /* not blocked */
   BST_UNMOUNTED,                     /* User unmounted device */
   BST_WAITING_FOR_SYSOP,             /* Waiting for operator to mount tape */
   BST_DOING_ACQUIRE,                 /* Opening/validating/moving tape */
   BST_WRITING_LABEL,                 /* Labeling a tape */
   BST_UNMOUNTED_WAITING_FOR_SYSOP,   /* User unmounted during wait for op */
   BST_MOUNT,                         /* Mount request */
   BST_DESPOOLING,                    /* Despooling -- i.e. multiple writes */
   BST_RELEASING                      /* Releasing the device */
};

typedef struct s_steal_lock {
   pthread_t  no_wait_id;             /* id of no wait thread */
   int        dev_blocked;            /* state */
   int        dev_prev_blocked;       /* previous blocked state */
   uint32_t   blocked_by;             /* previous blocker */
} bsteal_lock_t;

/*
 * Used in unblock() call
 */
enum {
   DEV_LOCKED = true,
   DEV_UNLOCKED = false
};

#endif
