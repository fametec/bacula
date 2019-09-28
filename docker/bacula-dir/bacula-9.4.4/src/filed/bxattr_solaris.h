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

#ifndef __BXATTR_Solaris_H_
#define __BXATTR_Solaris_H_

#if defined(HAVE_SUN_OS)

/* check if XATTR support is enabled */
#if defined(HAVE_XATTR)

/*
 *
 */
#if defined(HAVE_SYS_ATTR_H)
#include <sys/attr.h>
#elif defined(HAVE_ATTR_H)
#include <attr.h>
#endif

/*
 * Required for XATTR/ACL backup
 */
#ifdef HAVE_SYS_ACL_H
#include <sys/acl.h>
bool acl_is_trivial(int count, aclent_t *entries);
#endif

/*
 * Cache structure in alist
 */
struct BXATTR_Solaris_Cache {
   ino_t inode;
   char * name;
};

/*
 * This is a Solaris specific XATTR implementation.
 *
 * Solaris extended attributes were introduced in Solaris 9
 * by PSARC 1999/209
 *
 * Solaris extensible attributes were introduced in OpenSolaris
 * by PSARC 2007/315 Solaris extensible attributes are also
 * sometimes called extended system attributes.
 *
 * man fsattr(5) on Solaris gives a wealth of info. The most
 * important bits are:
 *
 * Attributes are logically supported as files within the  file
 * system.   The  file  system  is  therefore augmented with an
 * orthogonal name space of file attributes. Any file  (includ-
 * ing  attribute files) can have an arbitrarily deep attribute
 * tree associated with it. Attribute values  are  accessed  by
 * file descriptors obtained through a special attribute inter-
 * face.  This logical view of "attributes as files" allows the
 * leveraging  of  existing file system interface functionality
 * to support the construction, deletion, and  manipulation  of
 * attributes.
 *
 * The special files  "."  and  ".."  retain  their  accustomed
 * semantics within the attribute hierarchy.  The "." attribute
 * file refers to the current directory and the ".."  attribute
 * file  refers to the parent directory.  The unnamed directory
 * at the head of each attribute tree is considered the "child"
 * of  the  file it is associated with and the ".." file refers
 * to the associated file.  For  any  non-directory  file  with
 * attributes,  the  ".." entry in the unnamed directory refers
 * to a file that is not a directory.
 *
 * Conceptually, the attribute model is fully general. Extended
 * attributes  can  be  any  type of file (doors, links, direc-
 * tories, and so forth) and can even have their own attributes
 * (fully  recursive).   As a result, the attributes associated
 * with a file could be an arbitrarily deep directory hierarchy
 * where each attribute could have an equally complex attribute
 * tree associated with it.  Not all implementations  are  able
 * to,  or  want to, support the full model. Implementation are
 * therefore permitted to reject operations that are  not  sup-
 * ported.   For  example,  the implementation for the UFS file
 * system allows only regular files as attributes (for example,
 * no sub-directories) and rejects attempts to place attributes
 * on attributes.
 *
 * The following list details the operations that are  rejected
 * in the current implementation:
 *
 * link                     Any attempt to create links between
 *                          attribute  and  non-attribute space
 *                          is rejected  to  prevent  security-
 *                          related   or   otherwise  sensitive
 *                          attributes from being exposed,  and
 *                          therefore  manipulable,  as regular
 *                          files.
 *
 * rename                   Any  attempt  to   rename   between
 *                          attribute  and  non-attribute space
 *                          is rejected to prevent  an  already
 *                          linked  file from being renamed and
 *                          thereby circumventing the link res-
 *                          triction above.
 *
 * mkdir, symlink, mknod    Any  attempt  to  create  a   "non-
 *                          regular" file in attribute space is
 *                          rejected to reduce the  functional-
 *                          ity,  and  therefore  exposure  and
 *                          risk, of  the  initial  implementa-
 *                          tion.
 *
 * The entire available name space has been allocated to  "gen-
 * eral use" to bring the implementation in line with the NFSv4
 * draft standard [NFSv4]. That standard defines "named  attri-
 * butes"  (equivalent  to Solaris Extended Attributes) with no
 * naming restrictions.  All Sun  applications  making  use  of
 * opaque extended attributes will use the prefix "SUNW".
 */
class BXATTR_Solaris : public BXATTR {
private:
   alist * cache;
   bRC_BXATTR os_backup_xattr (JCR *jcr, FF_PKT *ff_pkt);
   bRC_BXATTR os_restore_xattr (JCR *jcr, int stream, char *content, uint32_t length);
   bRC_BXATTR os_get_xattr_names (JCR *jcr, POOLMEM **list, uint32_t *length);
   bRC_BXATTR os_get_xattr_value (JCR *jcr, char * name, char ** pvalue, uint32_t * plen);
   bRC_BXATTR os_set_xattr (JCR *jcr, bool extended, char *content, uint32_t length);
   bRC_BXATTR os_get_xattr_acl(JCR *jcr, int fd, char **buffer, char* attrname);
   bRC_BXATTR os_set_xattr_acl(JCR *jcr, int fd, char *name, char *acltext);
   inline char * find_xattr_cache(JCR *jcr, ino_t ino, char * name);
   inline void delete_xattr_cache();
public:
   BXATTR_Solaris ();
   ~BXATTR_Solaris ();
};

#endif /* HAVE_XATTR */

#endif /* HAVE_SUN_OS */

#endif /* __BXATTR_Solaris_H_ */
