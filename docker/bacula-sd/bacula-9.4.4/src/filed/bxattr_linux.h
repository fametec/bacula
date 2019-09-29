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
/**
 * Major refactoring of XATTR code written by:
 *
 *  Radosław Korzeniewski, MMXVI
 *  radoslaw@korzeniewski.net, radekk@inteos.pl
 *  Inteos Sp. z o.o. http://www.inteos.pl/
 *
 */

#ifndef __BXATTR_LINUX_H_
#define __BXATTR_LINUX_H_

#if defined(HAVE_LINUX_OS)
#include <sys/types.h>

/* check if XATTR support is enabled */
#if defined(HAVE_XATTR)

/* verify a support for XATTR on build os */
#if !defined(HAVE_LLISTXATTR) || !defined(HAVE_LGETXATTR) || !defined(HAVE_LSETXATTR)
#error "Missing full support for the XATTR functions."
#endif

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#else
#error "Missing sys/xattr.h header file"
#endif

/*
 *
 *
 */
class BXATTR_Linux : public BXATTR {
private:
   bRC_BXATTR os_backup_xattr (JCR *jcr, FF_PKT *ff_pkt);
   bRC_BXATTR os_restore_xattr (JCR *jcr, int stream, char *content, uint32_t length);
   bRC_BXATTR os_get_xattr_names (JCR *jcr, POOLMEM **list, uint32_t *length);
   bRC_BXATTR os_get_xattr_value (JCR *jcr, char * name, char ** pvalue, uint32_t * plen);
   bRC_BXATTR os_set_xattr (JCR *jcr, FF_PKT *ff_pkt, char *list, uint32_t length);
   bRC_BXATTR os_set_xattr (JCR *jcr, BXATTR_xattr *xattr);
public:
   BXATTR_Linux ();
};

#endif /* HAVE_XATTR */

#endif /* HAVE_LINUX_OS */

#endif /* __BXATTR_LINUX_H_ */
