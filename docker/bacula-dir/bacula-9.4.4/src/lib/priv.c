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

#include "bacula.h"

#undef ENABLE_KEEP_READALL_CAPS_SUPPORT
#if defined(HAVE_SYS_PRCTL_H) && defined(HAVE_SYS_CAPABILITY_H) && \
   defined(HAVE_PRCTL) && defined(HAVE_SETREUID) && defined(HAVE_LIBCAP)
# include <sys/prctl.h>
# include <sys/capability.h>
# if defined(PR_SET_KEEPCAPS)
#  define ENABLE_KEEP_READALL_CAPS_SUPPORT
# endif
#endif

#ifdef HAVE_AIX_OS
# ifndef _AIX51
extern "C" int initgroups(const char *,int);
# endif
#endif

/*
 * Lower privileges by switching to new UID and GID if non-NULL.
 * If requested, keep readall capabilities after switch.
 */
void drop(char *uname, char *gname, bool keep_readall_caps)
{
#if   defined(HAVE_PWD_H) && defined(HAVE_GRP_H)
   struct passwd *passw = NULL;
   struct group *group = NULL;
   gid_t gid;
   uid_t uid;
   char username[1000];

   Dmsg2(900, "uname=%s gname=%s\n", uname?uname:"NONE", gname?gname:"NONE");
   if (!uname && !gname) {
      return;                            /* Nothing to do */
   }

   if (uname) {
      if ((passw = getpwnam(uname)) == NULL) {
         berrno be;
         Emsg2(M_ERROR_TERM, 0, _("Could not find userid=%s: ERR=%s\n"), uname,
            be.bstrerror());
      }
   } else {
      if ((passw = getpwuid(getuid())) == NULL) {
         berrno be;
         Emsg1(M_ERROR_TERM, 0, _("Could not find password entry. ERR=%s\n"),
            be.bstrerror());
      } else {
         uname = passw->pw_name;
      }
   }
   /* Any OS uname pointer may get overwritten, so save name, uid, and gid */
   bstrncpy(username, uname, sizeof(username));
   uid = passw->pw_uid;
   gid = passw->pw_gid;
   if (gname) {
      if ((group = getgrnam(gname)) == NULL) {
         berrno be;
         Emsg2(M_ERROR_TERM, 0, _("Could not find group=%s: ERR=%s\n"), gname,
            be.bstrerror());
      }
      gid = group->gr_gid;
   }
   if (initgroups(username, gid)) {
      berrno be;
      if (gname) {
         Emsg3(M_ERROR_TERM, 0, _("Could not initgroups for group=%s, userid=%s: ERR=%s\n"),
            gname, username, be.bstrerror());
      } else {
         Emsg2(M_ERROR_TERM, 0, _("Could not initgroups for userid=%s: ERR=%s\n"),
            username, be.bstrerror());
      }
   }
   if (gname) {
      if (setgid(gid)) {
         berrno be;
         Emsg2(M_ERROR_TERM, 0, _("Could not set group=%s: ERR=%s\n"), gname,
            be.bstrerror());
      }
   }
   if (keep_readall_caps) {
#ifdef ENABLE_KEEP_READALL_CAPS_SUPPORT
      cap_t caps;

      if (prctl(PR_SET_KEEPCAPS, 1)) {
         berrno be;
         Emsg1(M_ERROR_TERM, 0, _("prctl failed: ERR=%s\n"), be.bstrerror());
      }
      if (setreuid(uid, uid)) {
         berrno be;
         Emsg1(M_ERROR_TERM, 0, _("setreuid failed: ERR=%s\n"), be.bstrerror());
      }
      if (!(caps = cap_from_text("cap_dac_read_search=ep"))) {
         berrno be;
         Emsg1(M_ERROR_TERM, 0, _("cap_from_text failed: ERR=%s\n"), be.bstrerror());
      }
      if (cap_set_proc(caps) < 0) {
         berrno be;
         Emsg1(M_ERROR_TERM, 0, _("cap_set_proc failed: ERR=%s\n"), be.bstrerror());
      }
      cap_free(caps);
#else
      Emsg0(M_ERROR_TERM, 0, _("Keep readall caps not implemented this OS or missing libraries.\n"));
#endif
   } else if (setuid(uid)) {
      berrno be;
      Emsg1(M_ERROR_TERM, 0, _("Could not set specified userid: %s\n"), username);
   }
#endif
}
