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
 * Watchdog timer routines
 *
 *    Kern Sibbald, December MMII
 *
*/

enum {
   TYPE_CHILD = 1,
   TYPE_PTHREAD,
   TYPE_BSOCK
};

#define TIMEOUT_SIGNAL SIGUSR2

struct s_watchdog_t {
   bool one_shot;
   utime_t interval;
   void (*callback)(struct s_watchdog_t *wd);
   void (*destructor)(struct s_watchdog_t *wd);
   void *data;
   /* Private data below - don't touch outside of watchdog.c */
   dlink link;
   utime_t next_fire;
};
typedef struct s_watchdog_t watchdog_t;

/* Exported globals */
extern utime_t DLL_IMP_EXP watchdog_time;             /* this has granularity of SLEEP_TIME */
extern utime_t DLL_IMP_EXP watchdog_sleep_time;      /* examine things every 60 seconds */
