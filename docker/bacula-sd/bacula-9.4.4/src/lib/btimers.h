/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

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
 *    Nic Bellamy <nic@bellamy.co.nz>, October 2003.
 *
*/

#ifndef __BTIMERS_H_
#define __BTIMERS_H_

struct btimer_t {
   watchdog_t *wd;                    /* Parent watchdog */
   int type;
   bool killed;
   pid_t pid;                         /* process id if TYPE_CHILD */
   pthread_t tid;                     /* thread id if TYPE_PTHREAD */
   BSOCK *bsock;                      /* Pointer to BSOCK */
   JCR *jcr;                          /* Pointer to job control record */
};

#endif /* __BTIMERS_H_ */
