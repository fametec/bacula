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
 *  daemon.c by Kern Sibbald 2000
 *
 *   This code is inspired by the Prentice Hall book
 *   "Unix Network Programming" by W. Richard Stevens
 *   and later updated from his book "Advanced Programming
 *   in the UNIX Environment"
 *
 * Initialize a daemon process completely detaching us from
 * any terminal processes.
 *
 */


#include "bacula.h"

#ifdef HAVE_GETRLIMIT
#include <sys/resource.h>
#else
/* If not available, use a wrapper that will not use it */
#define getrlimit(a,b) -1
struct rlimit {
   int64_t rlim_max;
};
#endif

void
daemon_start()
{
#ifndef HAVE_WIN32
   int i, fd, next_fd;
   pid_t cpid;
   mode_t oldmask;
#ifdef DEVELOPER
   next_fd = 3;
#else
   next_fd = 0;
#endif
   /*
    *  Become a daemon.
    */
   Dmsg0(900, "Enter daemon_start\n");
   if ( (cpid = fork() ) < 0) {
      berrno be;
      Emsg1(M_ABORT, 0, _("Cannot fork to become daemon: ERR=%s\n"), be.bstrerror());
   } else if (cpid > 0) {
      exit(0);              /* parent exits */
   }

   /* Child continues */
   setsid();

   /* In the PRODUCTION system, we close ALL
    * file descriptors except stdin, stdout, and stderr.
    */
   if (debug_level > 0) {
      next_fd = 3;                    /* don't close debug output */
   }

#if defined(HAVE_FCNTL_F_CLOSEM)
   fcntl(next_fd, F_CLOSEM);
#elif defined(HAVE_CLOSEFROM)
   closefrom(next_fd);
#else
   struct rlimit rl;
   int64_t rlimitResult=0;

   /* Many systems doesn't have the correct system call
    * to determine the FD list to close.
    */
   if (getrlimit(RLIMIT_NOFILE, &rl) == -1) {
      rlimitResult = sysconf(_SC_OPEN_MAX);
   } else {
      rlimitResult = rl.rlim_max;
   }

   for (i=rlimitResult; i >= next_fd; i--) {
      close(i);
   }
#endif

   /* Move to root directory. For debug we stay
    * in current directory so dumps go there.
    */
#ifndef DEBUG
   chdir("/");
#endif

   /*
    * Avoid creating files 666 but don't override any
    * more restrictive mask set by the user.
    */
   oldmask = umask(026);
   oldmask |= 026;
   umask(oldmask);


   /*
    * Make sure we have fd's 0, 1, 2 open
    *  If we don't do this one of our sockets may open
    *  there and if we then use stdout, it could
    *  send total garbage to our socket.
    *
    */
   fd = open("/dev/null", O_RDONLY, 0644);
   if (fd > 2) {
      close(fd);
   } else {
      for(i=1; fd + i <= 2; i++) {
         dup2(fd, fd+i);
      }
   }

#endif /* HAVE_WIN32 */
   Dmsg0(900, "Exit daemon_start\n");
}
