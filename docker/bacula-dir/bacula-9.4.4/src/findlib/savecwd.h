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

#ifndef _SAVECWD_H
#define _SAVECWD_H 1

class saveCWD {
   bool m_saved;                   /* set if we should do chdir i.e. save_cwd worked */
   int m_fd;                       /* fd of current dir before chdir */
   char *m_cwd;                    /* cwd before chdir if fd fchdir() works */

public:
   saveCWD() { m_saved=false; m_fd=-1; m_cwd=NULL; };
   ~saveCWD() { release(); };
   bool save(JCR *jcr);
   bool restore(JCR *jcr);
   void release();
   bool is_saved() { return m_saved; };
};

#endif /* _SAVECWD_H */
