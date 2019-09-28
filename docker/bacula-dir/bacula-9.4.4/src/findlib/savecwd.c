/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2015 Kern Sibbald

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
 *  Kern Sibbald, August MMVII
 *
 */

#include "bacula.h"
#include "savecwd.h"

/*
 * Attempt to save the current working directory by various means so that
 *  we can optimize code by doing a cwd and then restore the cwd.
 */

#ifdef HAVE_FCHDIR
static bool fchdir_failed = false;          /* set if we get a fchdir failure */
#else
static bool fchdir_failed = true;           /* set if we get a fchdir failure */
#endif

/*
 * Save current working directory.
 * Returns: true if OK
 *          false if failed
 */
bool saveCWD::save(JCR *jcr)
{
   release();                                /* clean up */
   if (!fchdir_failed) {
      m_fd = open(".", O_RDONLY);
      if (m_fd < 0) {
         berrno be;
         Jmsg1(jcr, M_ERROR, 0, _("Cannot open current directory: ERR=%s\n"), be.bstrerror());
         m_saved = false;
         return false;
      }
   }

   if (fchdir_failed) {
      POOLMEM *buf = get_memory(5000);
      m_cwd = (POOLMEM *)getcwd(buf, sizeof_pool_memory(buf));
      if (m_cwd == NULL) {
         berrno be;
         Jmsg1(jcr, M_ERROR, 0, _("Cannot get current directory: ERR=%s\n"), be.bstrerror());
         free_pool_memory(buf);
         m_saved = false;
         return false;
      }
   }
   m_saved = true;
   return true;
}

/*
 * Restore previous working directory.
 * Returns: true if OK
 *          false if failed
 */
bool saveCWD::restore(JCR *jcr)
{
   if (!m_saved) {
      return true;
   }
   m_saved = false;
   if (m_fd >= 0) {
      if (fchdir(m_fd) != 0) {
         berrno be;
         Jmsg1(jcr, M_ERROR, 0, _("Cannot reset current directory: ERR=%s\n"), be.bstrerror());
         close(m_fd);
         m_fd = -1;
         fchdir_failed = true;
         chdir("/");                  /* punt */
         return false;
      }
      return true;
   }
   if (chdir(m_cwd) < 0) {
      berrno be;
      Jmsg1(jcr, M_ERROR, 0, _("Cannot reset current directory: ERR=%s\n"), be.bstrerror());
      chdir("/");
      free_pool_memory(m_cwd);
      m_cwd = NULL;
      return false;
   }
   return true;
}

void saveCWD::release()
{
   if (!m_saved) {
      return;
   }
   m_saved = false;
   if (m_fd >= 0) {
      close(m_fd);
      m_fd = -1;
   }
   if (m_cwd) {
      free_pool_memory(m_cwd);
      m_cwd = NULL;
   }
}
