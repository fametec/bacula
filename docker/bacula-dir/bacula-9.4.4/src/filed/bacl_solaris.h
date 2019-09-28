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
 * Major refactoring of ACL code written by:
 *
 *  Radosław Korzeniewski, MMXVI
 *  radoslaw@korzeniewski.net, radekk@inteos.pl
 *  Inteos Sp. z o.o. http://www.inteos.pl/
 *
 */

#ifndef __BACL_Solaris_H_
#define __BACL_Solaris_H_

#if defined(HAVE_SUN_OS)

/* check if ACL support is enabled */
#if defined(HAVE_ACL)

#ifdef HAVE_SYS_ACL_H
#include <sys/acl.h>
#else
#error "configure failed to detect availability of sys/acl.h"
#endif

/*
 * As the header acl.h doesn't seem to define this one we need to.
 */
extern "C" {
int acl_type(acl_t *);
char *acl_strerror(int);
};

/*
 *
 */
#if defined(HAVE_EXTENDED_ACL)
#if !defined(_SYS_ACL_IMPL_H)
typedef enum acl_type {
   ACLENT_T = 0,
   ACE_T = 1
} acl_type_t;
#endif
#endif

/*
 *
 *
 */
class BACL_Solaris : public BACL {
private:
   alist * cache;
   bRC_BACL os_backup_acl (JCR *jcr, FF_PKT *ff_pkt);
   bRC_BACL os_restore_acl (JCR *jcr, int stream, char *content, uint32_t length);
   bRC_BACL os_get_acl(JCR *jcr, int *stream);
   bRC_BACL os_set_acl(JCR *jcr, int stream, char *content, uint32_t length);
   /* requires acl.h available */
   bRC_BACL check_bacltype (JCR *jcr, int name);
public:
   BACL_Solaris ();
   ~BACL_Solaris ();
};

#endif /* HAVE_ACL */

#endif /* HAVE_SUN_OS */

#endif /* __BACL_Solaris_H_ */
