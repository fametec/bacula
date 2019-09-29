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

#include "bacula.h"
#include "filed.h"
#include "bxattr_solaris.h"

#if defined(HAVE_SUN_OS)

/* check if XATTR support is enabled */
#if defined(HAVE_XATTR)

#ifdef HAVE_SYS_NVPAIR_H
#include <sys/nvpair.h>
#endif

#if !defined(HAVE_OPENAT) || \
    !defined(HAVE_ATTROPEN) || \
    !defined(HAVE_UNLINKAT) || \
    !defined(HAVE_FCHOWNAT) || \
    !defined(HAVE_FUTIMESAT)
#error "Unable to compile code because of missing openat, attropen, unlinkat, fchownat or futimesat functions"
#endif

/*
 * Define the supported XATTR streams for this OS
 */
static const int os_xattr_streams[] = {
   STREAM_XACL_SOLARIS_XATTR,
#if defined(HAVE_SYS_NVPAIR_H) && defined(_PC_SATTR_ENABLED)
   STREAM_XACL_SOLARIS_SYS_XATTR,
#endif /* defined(HAVE_SYS_NVPAIR_H) && defined(_PC_SATTR_ENABLED) */
   0
};

static const char *os_xattr_skiplist[] = {
   "..",
#if defined(HAVE_SYS_NVPAIR_H) && defined(_PC_SATTR_ENABLED)
   VIEW_READONLY,
#endif /* defined(HAVE_SYS_NVPAIR_H) && defined(_PC_SATTR_ENABLED) */
   NULL
};

static const char *os_xattr_acl_skiplist[] = {
   NULL
};

/* Provide a suplement for Solaris 10 missing linkat function */
#ifndef HAVE_LINKAT
int linkat(int fd1, const char *path1, int fd2, const char *path2, int flag)
{
   int rc;
   
   rc = fchdir(fd1);
   if (rc != 0){
      return rc;
   }
   
   rc = link(path1, path2);
   return rc;
};
#endif
/*
 * OS Specific constructor
 */
BXATTR_Solaris::BXATTR_Solaris(){

   set_xattr_streams(os_xattr_streams);
   set_xattr_skiplists(os_xattr_skiplist, os_xattr_acl_skiplist);
   cache = NULL;
};

/*
 * OS Specific destructor
 */
BXATTR_Solaris::~BXATTR_Solaris(){

   delete_xattr_cache();
};

/*
 * Perform OS specific extended attribute backup
 *
 * in/out - check API at bxattr.h
 *
 * The Solaris implementation of XATTR is very, very different then all other "generic" unix implementations,
 * so the original author of the Bacula XATTR support for Solaris OS decided to totally change the XATTR Stream
 * content, and we need to follow this design to support previous behavior. The stream consist of a number of
 * "files" with STREAM_XACL_SOLARIS_XATTR or STREAM_XACL_SOLARIS_SYS_XATTR stream id. Every singe stream represents
 * a single attribute. The content is a NULL-terminated array with a following data:
 *    <xattr name>\0<encoded stat>\0<acl rendered text>\0<xattr data>
 * when an attribute file has a hardlinked other attributes then a content stream changes a bit into:
 *    <xattr name>\0<encoded stat>\0<target xattr name>\0
 * where:
 *    <xattr name> is an attribute name - a file name in Solaris
 *    <encoded stat> is a standard file stat struct encoded by Bacula (the same encoding goes with a regular file)
 *    <acl rendered text> is a Solaris dependent acltotext data
 *    <xattr data> is the attribute file raw content
 *    <target xattr name> is a name of the first hardlinked attribute file which a current attribute has to linked to
 *
 * The raw content of the attribute is copied into memory before send to the SD and for a very large attribute
 * data can allocate a large amount of additional memory. In most cases it should not be a problem because most
 * xattrs should has a few /hundred/ bytes in size. This is the same behavior as in previous implementation.
 */
bRC_BXATTR BXATTR_Solaris::os_backup_xattr (JCR *jcr, FF_PKT *ff_pkt){

   bRC_BXATTR rc;
   POOLMEM *xlist = NULL;
   uint32_t xlen;
   char *name;
   char *lnkname;
   uint32_t name_len;
   POOLMEM *value = NULL;
   uint32_t value_len;
   char * bxattracl;
   POOLMEM *data = NULL;
   bool skip;
   struct stat st;
   char attribs[MAXSTRING];
   int stream;
   int attrfd;
   int len;
   mode_t stmode;

   /* sanity check of input variables */
   if (jcr == NULL || ff_pkt == NULL){
      return bRC_BXATTR_inval;
   }

   /* check if extended/extensible attributes are present */
   if (pathconf(jcr->last_fname, _PC_XATTR_EXISTS) > 0){
      /* xlist is allocated as POOLMEM by os_get_xattr_names */
      rc = os_get_xattr_names(jcr, &xlist, &xlen);
      switch (rc){
         case bRC_BXATTR_ok:
            /* it's ok, so go further */
            break;
         case bRC_BXATTR_skip:
         case bRC_BXATTR_cont:
            /* no xattr available, so skip rest of it */
            return bRC_BXATTR_ok;
         default:
            return rc;
      }

      data = get_pool_memory(PM_BSOCK);
      /* follow the list of xattr names and get the values */
      for (name = xlist; (unsigned int)(name - xlist) + 1 < xlen; name = strchr(name, '\0') + 1){
         name_len = strlen(name);
         /* skip read-only or other unused attribute names */
         skip =  check_xattr_skiplists(jcr, ff_pkt, name);
         if (skip || name_len == 0){
            Dmsg1(200, "Skipping xattr named \"%s\"\n", name);
            continue;
         }
         Dmsg1(200, "Processing xattr: %s\n", name);
         /* set a correct stream */
         stream = STREAM_XACL_SOLARIS_XATTR;

#if defined(HAVE_SYS_NVPAIR_H) && defined(_PC_SATTR_ENABLED)
         /* check for system attributes name */
         if (bstrcmp(name, VIEW_READWRITE)){
            stream = STREAM_XACL_SOLARIS_SYS_XATTR;
         }
#endif /* HAVE_SYS_NVPAIR_H && _PC_SATTR_ENABLED */

         /* open an attribute descriptor, it will be used for backup */
         attrfd = attropen(jcr->last_fname, name, O_RDONLY);

         /* get the stat of the attribute */
         if (fstat(attrfd, &st) < 0){
            berrno be;

            switch (errno){
               case ENOENT:
                  rc = bRC_BXATTR_ok;
                  goto bailout;
               default:
                  Mmsg3(jcr->errmsg, _("Unable to get status on xattr \"%s\" on file \"%s\": ERR=%s\n"), name, jcr->last_fname, be.bstrerror());
                  Dmsg3(100, "fstat of xattr %s on \"%s\" failed: ERR=%s\n", name, jcr->last_fname, be.bstrerror());
                  rc = bRC_BXATTR_error;
                  goto bailout;
            }
         }

         /* get xattr acl data, but only when it is not trivial acls */
         rc = os_get_xattr_acl(jcr, attrfd, &bxattracl, name);
         if (rc != bRC_BXATTR_ok){
            goto bailout;
         }

         /*
          * Solaris support only S_IFREG and S_IFDIR as an attribute file type, no other types are supported
          * the previous Solaris xattr implementation in Bacula had an unverified and untested code for other
          * types of attribute files which was a nonsense and unnecessarily complicate the code. We decided
          * to remove unsupported code. To check if the current Solaris version support for xattr was extended
          * simply verify a man fsattr(5) for it.
          */
         stmode = st.st_mode & S_IFMT;
         Dmsg1(800, "st.st_mode: 0x%X\n", stmode);
         switch (stmode){
            case S_IFDIR:
               /* we have a struct stat of the attribute so encode it to the buffer
                * we distinguish standard file by setting ino to zero in attribs */
               encode_stat(attribs, &st, sizeof(st), 0, stream);
               /* prepare and save xattr info */
               len = bsnprintf(data, sizeof_pool_memory(data), "%s%c%s%c%s%c", name, 0, attribs, 0, (bxattracl) ? bxattracl : "", 0);
               set_content(data, len);
               break;
            case S_IFREG:
               /* check for hardlinked attributes which Solaris support */
               if (st.st_nlink > 1){
                  /* search for already saved file of the same inode number */
                  lnkname = find_xattr_cache(jcr, st.st_ino, name);
                  if (lnkname != NULL){
                     /* found a previous saved file, link to it and render xattr data for hardlinked attribute */
                     Dmsg0(400, "saving linked attr\n");
                     /* we have a struct stat of the attribute so encode it to the buffer
                      * we distinguish hardlinked file by setting a real ino in attribs */
                     encode_stat(attribs, &st, sizeof(st), st.st_ino, stream);
                     len = bsnprintf(data, sizeof_pool_memory(data), "%s%c%s%c%s%c", name, 0, attribs, 0, lnkname, 0);
                     set_content(data, len);
                     /* content is ready */
                     break;
                  }
               }
               /* value is allocated as POOLMEM by os_get_xattr_value */
               rc = os_get_xattr_value(jcr, name, &value, &value_len);
               switch (rc){
                  case bRC_BXATTR_ok:
                     /* it's ok, so go further */
                     break;
                  case bRC_BXATTR_skip:
                     /* no xattr available, so skip rest of it */
                     rc = bRC_BXATTR_ok;
                     goto bailout;
                  default:
                     /* error / fatal */
                     goto bailout;
               }
               /* we have a struct stat of the attribute so encode it to the buffer
                * we distinguish standard file by setting ino to zero in attribs */
               encode_stat(attribs, &st, sizeof(st), 0, stream);
               /* prepare and save xattr info */
               len = bsnprintf(data, sizeof_pool_memory(data), "%s%c%s%c%s%c", name, 0, attribs, 0, (bxattracl) ? bxattracl : "", 0);
               /* append value data to the end of the xattr info */
               if (value_len > 0){
                  check_pool_memory_size(data, len + value_len);
                  memcpy(data + len, value, value_len);
                  set_content(data, len + value_len);
                  free_pool_memory(value);
                  value = NULL;
               } else {
                  set_content(data, len);
               }
               break;
            default:
               Dmsg3(100, "Unsupported extended attribute type: 0x%X for \"%s\" on file \"%s\"\n", stmode, name, jcr->last_fname);
               Mmsg3(jcr->errmsg, _("Unsupported extended attribute type: 0x%X for \"%s\" on file \"%s\"\n"), stmode, name, jcr->last_fname);
               rc = bRC_BXATTR_error;
               goto bailout;
         }
         if (bxattracl){
            actuallyfree(bxattracl);
            bxattracl = NULL;
         }
         /* send stream to the sd */
         rc = send_xattr_stream(jcr, stream);
         if (rc != bRC_BXATTR_ok){
            Mmsg2(jcr->errmsg, _("Failed to send extended attribute \"%s\" on file \"%s\"\n"), name, jcr->last_fname);
            Dmsg2(100, "Failed to send extended attribute \"%s\" on file \"%s\"\n", name, jcr->last_fname);
            goto bailout;
         }
      }

bailout:
      /* free allocated data: xlist, value (if not freed), data, etc. */
      free_pool_memory(data);
      if (value != NULL){
         free_pool_memory(value);
      }
      if (xlist != NULL){
         // Dmsg1(400, "free xlist: %p\n", xlist);
         free_pool_memory(xlist);
      }
      /* this is a cache for a particular file, so no needed after backup of this file */
      delete_xattr_cache();

      return rc;
   } else {
      Dmsg1(500, "xattr does not exist on: %s\n", jcr->last_fname);
   }
   return bRC_BXATTR_ok;
};

/*
 * BXATTR_Solaris cache is a simple linked list cache of inode number and names used to handle
 * xattr hard linked data. The function is searching for cached entry. When not found it append
 * entry to the cache.
 * in:
 *    jcr - Job Control Record (well, it is not used here)
 *    ino - inode number to compare/search for
 *    name - the name of the current attribute
 * out:
 *    NULL - when entry not found in cache and new entry was added
 *    <str> - a name of the linked entry
 */
inline char * BXATTR_Solaris::find_xattr_cache(JCR *jcr, ino_t ino, char * name){

   BXATTR_Solaris_Cache *entry;

   if (cache != NULL){
      foreach_alist(entry, cache){
         if (entry && entry->inode == ino){
            /* found in cache, return name */
            return entry->name;
         }
      }
   } else {
      cache = New (alist(10, not_owned_by_alist));
   }
   /* not found, so add this one to the cache */
   entry = (BXATTR_Solaris_Cache*) malloc (sizeof(BXATTR_Solaris_Cache));
   entry->inode = ino;
   entry->name = name;
   cache->append(entry);
   return NULL;
}

/*
 * The function deletes a cache
 * in/out - void
 */
inline void BXATTR_Solaris::delete_xattr_cache(){

   BXATTR_Solaris_Cache *entry;

   if (cache != NULL){
      foreach_alist(entry, cache){
         free(entry);
      }
      delete cache;
      cache = NULL;
   }
}

/*
 * Perform OS specific XATTR restore. Runtime is called only when stream is supported by OS.
 *
 * The way Solaris xattr support is designed in Bacula we will have a single attribute restore
 * with every call to this function. So multiple attributes are restored with multiple calls.
 *
 * in/out - check API at bxattr.h
 */
bRC_BXATTR BXATTR_Solaris::os_restore_xattr (JCR *jcr, int stream, char *content, uint32_t length){

   bRC_BXATTR rc = bRC_BXATTR_error;
   bool extended = false;

   /* check input data */
   if (jcr == NULL || content == NULL){
      return bRC_BXATTR_inval;
   }

   Dmsg2(400, "restore xattr stream %i on file: %s\n", stream, jcr->last_fname);
   /* First make sure we can restore xattr on the filesystem */
   switch (stream){
#if defined(HAVE_SYS_NVPAIR_H) && defined(_PC_SATTR_ENABLED)
      case STREAM_XACL_SOLARIS_SYS_XATTR:
         if (pathconf(jcr->last_fname, _PC_SATTR_ENABLED) <= 0){
            Mmsg(jcr->errmsg, _("Failed to restore extensible attributes on file \"%s\"\n"), jcr->last_fname);
            Dmsg1(100, "Unable to restore extensible attributes on file \"%s\", filesystem doesn't support this\n", jcr->last_fname);
            goto bail_out;
         }
         extended = true;
         break;
#endif
      case STREAM_XACL_SOLARIS_XATTR:
         if (pathconf(jcr->last_fname, _PC_XATTR_ENABLED) <= 0){
            Mmsg(jcr->errmsg, _("Failed to restore extended attributes on file \"%s\"\n"), jcr->last_fname);
            Dmsg1(100, "Unable to restore extended attributes on file \"%s\", filesystem doesn't support this\n", jcr->last_fname);
            goto bail_out;
         }
         break;
      default:
         goto bail_out;
   }

   rc = os_set_xattr(jcr, extended, content, length);

bail_out:
   return rc;
};

#ifdef HAVE_ACL
/*
 * See if an acl is a trivial one (e.g. just the stat bits encoded as acl.)
 * There is no need to store those acls as we already store the stat bits too.
 */
bool acl_is_trivial(int count, aclent_t *entries)
{
   int n;
   aclent_t *ace;

   for (n = 0; n < count; n++) {
      ace = &entries[n];

      if (!(ace->a_type == USER_OBJ ||
            ace->a_type == GROUP_OBJ ||
            ace->a_type == OTHER_OBJ ||
            ace->a_type == CLASS_OBJ))
        return false;
   }
   return true;
}
#endif

/*
 * Low level OS specific runtime to get ACL on XATTR. The ACL data is set in supplied buffer
 *
 * in:
 *    jcr - Job Control Record
 *    fd - an opened file descriptor of the saved attribute
 *    buffer - a memory buffer pointer which we will return when render an acl text
 * out:
 *    buffer - NULL when no ACL or any error else the memory with acl representation.
 *    The memory for the external text string is obtained using malloc(3C).
 *    The caller is responsible for freeing the memory upon completion.
 *    bRC_BXATTR_ok - backup acl for extended attribute finish without problems
 *    bRC_BXATTR_error - backup acl unsuccessful
 *    bRC_BXATTR_inval - input variables are invalid (null)
 *
 */
bRC_BXATTR BXATTR_Solaris::os_get_xattr_acl(JCR *jcr, int fd, char **buffer, char *attrname)
{
// a function is valid only when Bacula have a support for ACL
#ifdef HAVE_ACL
   bRC_BXATTR rc = bRC_BXATTR_error;

   /* sanity check of input variables */
   if (jcr == NULL || buffer == NULL || fd < 0){
      return bRC_BXATTR_inval;
   }

#ifdef HAVE_EXTENDED_ACL

   int flags;
   acl_t *aclp = NULL;

   /* check if an attribute has acl on it which we can save */
   if (fpathconf(fd, _PC_ACL_ENABLED) > 0){
      /* check for non trivial acl on the file */
      if (facl_get(fd, ACL_NO_TRIVIAL, &aclp) != 0){
         berrno be;

         switch (errno){
         case ENOENT:
            rc = bRC_BXATTR_ok;
            goto bail_out;
         default:
            Mmsg2(jcr->errmsg, _("Unable to get xattr acl on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
            Dmsg2(100, "facl_get/acl_get of xattr on \"%s\" failed: ERR=%s\n", jcr->last_fname, be.bstrerror());
            goto bail_out;
         }
      }

      if (aclp != NULL){
#if defined(ACL_SID_FMT)
         /* New format flag added in newer Solaris versions. */
         flags = ACL_APPEND_ID | ACL_COMPACT_FMT | ACL_SID_FMT;
#else
         flags = ACL_APPEND_ID | ACL_COMPACT_FMT;
#endif /* ACL_SID_FMT */

         *buffer = acl_totext(aclp, flags);
         Dmsg1(500, "xattr extended acl found: %s\n", *buffer);
         acl_free(aclp);
      } else {
         *buffer = NULL;
      }
   } else {
      *buffer = NULL;
   }
   rc = bRC_BXATTR_ok;
bail_out:

#else /* !HAVE_EXTENDED_ACL */

   int n;
   aclent_t *acls = NULL;

   /* See if this attribute has an ACL */
   n = facl(fd, GETACLCNT, 0, NULL);

   if (n >= MIN_ACL_ENTRIES){
      acls = (aclent_t *)malloc(n * sizeof(aclent_t));
      if (facl(fd, GETACL, n, acls) != n){
         berrno be;

         switch (errno){
         case ENOENT:
            rc = bRC_BXATTR_ok;
            goto bail_out;
         default:
            Mmsg3(jcr->errmsg, _("Unable to get acl on xattr %s on file \"%s\": ERR=%s\n"), attrname, jcr->last_fname, be.bstrerror());
            Dmsg3(100, "facl/acl of xattr %s on \"%s\" failed: ERR=%s\n", attrname, jcr->last_fname, be.bstrerror());
            rc = bRC_BXATTR_error;
            goto bail_out;
         }
      }

      /* See if there is a non trivial acl on the file. */
      if (!acl_is_trivial(n, acls)){
         if ((*buffer = acltotext(acls, n)) == NULL){
            berrno be;

            Mmsg3(jcr->errmsg, _("Unable to get acl text on xattr %s on file \"%s\": ERR=%s\n"), attrname, jcr->last_fname, be.bstrerror());
            Dmsg3(100, "acltotext of xattr %s on \"%s\" failed: ERR=%s\n", attrname, jcr->last_fname, be.bstrerror());
            rc = bRC_BXATTR_error;
         }
         Dmsg1(500, "xattr acl found: %s\n", *buffer);
      } else {
         *buffer = NULL;
      }
bail_out:
     free(acls);
   } else {
      *buffer = NULL;
   }
   rc = bRC_BXATTR_ok;
#endif /* HAVE_EXTENDED_ACL */
   return rc;
#else /* HAVE_ACL */
   return bRC_BXATTR_ok;
#endif /* HAVE_ACL */
}

/*
 * Low level OS specific runtime to set ACL on XATTR. The ACL data is set from supplied text
 *
 * in:
 *    jcr - Job Control Record
 *    fd - an opened file descriptor of the restored attribute
 *    buffer - a pointer to the memory buffer where we will render an acl text
 * out:
 *    bRC_BXATTR_ok - backup acl for extended attribute finish without problems
 *    bRC_BXATTR_inval - input variables are invalid (null)
 *
 */
bRC_BXATTR BXATTR_Solaris::os_set_xattr_acl(JCR *jcr, int fd, char *name, char *acltext)
{
// a function is valid only when Bacula have a support for ACL
#ifdef HAVE_ACL

   bRC_BXATTR rc = bRC_BXATTR_ok;

   /* sanity check of input variables */
   if (jcr == NULL || name == NULL || acltext == NULL || fd < 0){
      return bRC_BXATTR_inval;
   }

#ifdef HAVE_EXTENDED_ACL

   int error;
   acl_t *aclp = NULL;

   if ((error = acl_fromtext(acltext, &aclp)) != 0){
      Mmsg1(jcr->errmsg, _("Unable to convert acl from text on file \"%s\"\n"), jcr->last_fname);
      return bRC_BXATTR_error;
   }

   if (facl_set(fd, aclp) != 0){
      berrno be;

      Mmsg3(jcr->errmsg, _("Unable to restore acl of xattr %s on file \"%s\": ERR=%s\n"), name, jcr->last_fname, be.bstrerror());
      Dmsg3(100, "Unable to restore acl of xattr %s on file \"%s\": ERR=%s\n", name, jcr->last_fname, be.bstrerror());
      rc = bRC_BXATTR_error;
   }

   if (aclp){
      acl_free(aclp);
   }

#else /* !HAVE_EXTENDED_ACL */

   int n;
   aclent_t *acls = NULL;

   acls = aclfromtext(acltext, &n);
   if (acls){
      if (facl(fd, SETACL, n, acls) != 0){
         berrno be;

         Mmsg3(jcr->errmsg, _("Unable to restore acl of xattr %s on file \"%s\": ERR=%s\n"), name, jcr->last_fname, be.bstrerror());
         Dmsg3(100, "Unable to restore acl of xattr %s on file \"%s\": ERR=%s\n", name, jcr->last_fname, be.bstrerror());
         rc = bRC_BXATTR_error;
      }
      actuallyfree(acls);
   }

#endif /* HAVE_EXTENDED_ACL */

   return rc;
#else /* HAVE_ACL */
   return bRC_BXATTR_ok;
#endif /* HAVE_ACL */
};

/*
 * Return a list of xattr names in newly allocated pool memory and a length of the allocated buffer.
 * It allocates a memory with poolmem subroutines every time a function is called, so it must be freed
 * when not needed.
 *
 * in/out - check API at bxattr.h
 */
bRC_BXATTR BXATTR_Solaris::os_get_xattr_names (JCR *jcr, POOLMEM ** pxlist, uint32_t * xlen){

   int xattrdfd;
   DIR *dirp;
   struct dirent *dp;

   int len;
   int slen;
   POOLMEM * list;

   /* check input data */
   if (jcr == NULL || xlen == NULL || pxlist == NULL){
      return bRC_BXATTR_inval;
   }

   /* Open the xattr stream on file */
   Dmsg1(500, "os_get_xattr_names on file: %s\n", jcr->last_fname);
   if ((xattrdfd = attropen(jcr->last_fname, ".", O_RDONLY)) < 0){
      berrno be;

      switch (errno){
         case ENOENT:
            /* no file available, skip it */
            return bRC_BXATTR_skip;
         case EINVAL:
            /* no xattr supported on file skip it */
            return bRC_BXATTR_skip;
         default:
            Mmsg2(jcr->errmsg, _("Unable to open xattr on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
            Dmsg2(100, "Unable to open xattr on file \"%s\": ERR=%s\n", jcr->last_fname, be.bstrerror());
            return bRC_BXATTR_error;
      }
   }

   /* open an extended file directory to read all xattr names */
   if ((dirp = fdopendir(xattrdfd)) == (DIR *)NULL){
      berrno be;

      Mmsg2(jcr->errmsg, _("Unable to list the xattr on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
      Dmsg3(100, "Unable to fdopendir xattr on file \"%s\" using fd %d: ERR=%s\n", jcr->last_fname, xattrdfd, be.bstrerror());
      close(xattrdfd);
      return bRC_BXATTR_error;
   }

   /*
    * allocate memory for the extented attribute list
    * default size is a 4k for PM_BSOCK, which should be sufficient in most cases
    */
   list = get_pool_memory(PM_BSOCK);
   // Dmsg1(400, "allocated xlist: %p\n", list);
   memset(list, 0, sizeof_pool_memory(list));
   bstrncpy(list, ".", sizeof_pool_memory(list));
   len = strlen(list) + 1;

   /* read all directory entries as a xattr names */
   while ((dp = readdir(dirp)) != NULL){

      /* skip '.' as we added it above */
      if (bstrcmp(dp->d_name, ".")){
         continue;
      }

      Dmsg1(500, "Found attribute: %s\n", dp->d_name);
      /* compute a buffer length = string length and nul char */
      slen = strlen (dp->d_name) + 1;
      list = check_pool_memory_size(list, len + slen);
      // Dmsg3(400, "xlist: %p len: %i slen: %i\n", list, len, slen);
      /* copy the name into a list */
      bstrncpy(list + len, dp->d_name, sizeof_pool_memory(list) - len);
      len += slen;
   }
   if (closedir(dirp) < 0){
      berrno be;

      Mmsg2(jcr->errmsg, _("Unable to close xattr list on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
      Dmsg2(100, "Unable to close xattr list on file \"%s\": ERR=%s\n", jcr->last_fname, be.bstrerror());
      return bRC_BXATTR_error;
   }

   *pxlist = list;
   *xlen = len;

   return bRC_BXATTR_ok;
};

/*
 * Return a value of the requested attribute name and a length of the allocated buffer.
 * It allocates a memory with poolmem subroutines every time a function is called, so it must be freed
 * when not needed.
 *
 * in/out - check API at bxattr.h
 */
bRC_BXATTR BXATTR_Solaris::os_get_xattr_value (JCR *jcr, char * name, char ** pvalue, uint32_t * plen){

   int xattrfd;
   int len;
   POOLMEM * value;
   struct stat st;

   /* check input data */
   if (jcr == NULL || name == NULL || plen == NULL || pvalue == NULL){
      return bRC_BXATTR_inval;
   }

   /* Open the xattr on file */
   if ((xattrfd = attropen(jcr->last_fname, name, O_RDONLY)) < 0){
      berrno be;

      switch (errno){
         case ENOENT:
            /* no file available, skip it */
            return bRC_BXATTR_skip;
         case EINVAL:
            /* no xattr supported on file skip it */
            return bRC_BXATTR_skip;
         default:
            Mmsg2(jcr->errmsg, _("Unable to open xattr on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
            Dmsg2(100, "Unable to open xattr on file \"%s\": ERR=%s\n", jcr->last_fname, be.bstrerror());
            return bRC_BXATTR_error;
      }
   }

   /* get some info about extended attribute */
   if (fstat(xattrfd, &st) < 0){
      berrno be;

      switch (errno){
         case ENOENT:
            /* no file available, skip it */
            return bRC_BXATTR_skip;
         default:
            Mmsg3(jcr->errmsg, _("Unable to stat xattr \"%s\" on file \"%s\": ERR=%s\n"), name, jcr->last_fname, be.bstrerror());
            Dmsg3(100, "Unable to stat xattr \"%s\" on file \"%s\": ERR=%s\n", name, jcr->last_fname, be.bstrerror());
            return bRC_BXATTR_error;
      }
   }

   /* default empty value */
   value = NULL;
   len = 0;

   /* only a file has a data/value we should care about */
   if ((st.st_mode & S_IFMT) != S_IFDIR){
      /* get size of the attribute data/value */
      len = lseek(xattrfd, 0, SEEK_END);
      lseek(xattrfd, 0, SEEK_SET);
   }
   if (len > 0){
      /*
       * allocate memory for the extented attribute value
       * default size is a 256B for PM_MESSAGE, so we need to check required size
       */
      value = get_pool_memory(PM_MESSAGE);
      value = check_pool_memory_size(value, len);
      memset(value, 0, len);
      /* read teh data */
      read (xattrfd, value, len);
      close(xattrfd);
   }

   /* setup return data */
   *pvalue = value;
   *plen = len;
   return bRC_BXATTR_ok;
};

/*
 * Low level OS specific runtime to set extended attribute on file
 *
 * in/out - check API at bxattr.h
 */
bRC_BXATTR BXATTR_Solaris::os_set_xattr (JCR *jcr, bool extended, char *content, uint32_t length){

   char *bp;
   char *name;
   char *attribs;
   char *acltext;
   char *lntarget;
   int attrfd = 0;
   int attrdirfd = 0;
   int cnt;
   int len;
   int inum;
   struct stat st;
   struct timeval times[2];
   bRC_BXATTR rc = bRC_BXATTR_ok;
   mode_t stmode;

   /* check input data */
   if (jcr == NULL || content == NULL){
      return bRC_BXATTR_inval;
   }
   /*
    * Parse content stream and extract valuable data.
    * STD/EXT: <xattr name>\0<encoded stat>\0<acl rendered text>\0<xattr data>
    *    where <encoded stat>.inum = 0
    * LNK:     <xattr name>\0<encoded stat>\0<target xattr name>\0
    *    where <encoded stat>.inum != 0
    */
   bp = content;
   if (bp[0] == '/'){
      bp = content + 1;    /* original code saves attribute name with '/' */
   }
   /* attribute name in name variable */
   name = bp;
   len = strlen (bp);
   bp += len + 1;

   /* attribute encoded stat in attribs variable */
   attribs = bp;
   len = strlen (bp);
   bp += len + 1;
   /* decode attributes into st and inum which distinguish between STX/EXT and LNK xattrs */
   decode_stat(attribs, &st, sizeof(st), &inum);

   /* acltext and link target name goes here */
   acltext = lntarget = bp;
   len = strlen (bp);
   /* now 'bp' should have the xattr data */
   bp += len + 1;

   /*
    * Open the xattr on which to restore the xattrs read-only.
    */
   if ((attrdirfd = attropen(jcr->last_fname, ".", O_RDONLY)) < 0){
      berrno be;

      Mmsg2(jcr->errmsg, _("Unable to open file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
      Dmsg2(100, "Unable to open file \"%s\": ERR=%s\n", jcr->last_fname, be.bstrerror());
      rc = bRC_BXATTR_error;
      goto bail_out;
   }

   stmode = st.st_mode & S_IFMT;
   Dmsg1(800, "st.st_mode: 0x%X\n", stmode);
   switch (stmode) {
      case S_IFDIR:
         /* if it is a current dir on file then we can restore acl data only */
         Dmsg1(400, "Processing dir xattr: %s\n", name);
         if (bstrcmp(name, ".")){
            break;
         }
         break;
      case S_IFREG:
         if (inum != 0){
            /* it is a linked attribute, perform a link operation */
            Dmsg2(400, "Processing linked xattr: %s => %s\n", lntarget, name);
            unlinkat(attrdirfd, name, 0);
            if (linkat(attrdirfd, lntarget, attrdirfd, name, 0) < 0){
               berrno be;

               Mmsg4(jcr->errmsg, _("Unable to link xattr %s to %s on file \"%s\": ERR=%s\n"), name, lntarget, jcr->last_fname, be.bstrerror());
               Dmsg4(100, "Unable to link xattr %s to %s on file \"%s\": ERR=%s\n", name, lntarget, jcr->last_fname, be.bstrerror());
               rc = bRC_BXATTR_error;
               goto bail_out;
            }
            goto bail_out;
         } else {
            Dmsg1(400, "Processing xattr: %s\n", name);
            if (!extended){
               unlinkat(attrdirfd, name, 0);
            }
            if ((attrfd = openat(attrdirfd, name, O_CREAT | O_RDWR | O_TRUNC, st.st_mode)) < 0){
               berrno be;

               Mmsg3(jcr->errmsg, _("Unable to open attribute \"%s\" at file \"%s\": ERR=%s\n"), name, jcr->last_fname, be.bstrerror());
               Dmsg3(100, "Unable to open attribute \"%s\" at file \"%s\": ERR=%s\n", name, jcr->last_fname, be.bstrerror());
               rc = bRC_BXATTR_error;
               goto bail_out;
            }
            /* restore any data if are available */
            if (st.st_size > 0){
               cnt = write (attrfd, bp, length - (bp - content) );
               if (cnt < 0){
                  berrno be;

                  Mmsg3(jcr->errmsg, _("Unable to restore data of xattr %s on file \"%s\": ERR=%s\n"), name, jcr->last_fname, be.bstrerror());
                  Dmsg3(100, "Unable to restore data of xattr %s on file \"%s\": ERR=%s\n", name, jcr->last_fname, be.bstrerror());
                  rc = bRC_BXATTR_error;
                  goto bail_out;
               }
            }
         }
         break;
      default:
         Dmsg3(100, "Unsupported extended attribute type: 0x%X for \"%s\" on file \"%s\"\n", stmode, name, jcr->last_fname);
         Mmsg3(jcr->errmsg, _("Unsupported extended attribute type: 0x%X for \"%s\" on file \"%s\"\n"), stmode, name, jcr->last_fname);
         goto bail_out;
   }

   /* file data restored, so setup permissions and acl data */
   if (!extended){
      if (fchownat(attrdirfd, name, st.st_uid, st.st_gid, AT_SYMLINK_NOFOLLOW) < 0){
         berrno be;

         switch (errno){
            case EINVAL:
            case ENOENT:
               break;
            default:
               Mmsg3(jcr->errmsg, _("Unable to restore owner of xattr %s on file \"%s\": ERR=%s\n"), name, jcr->last_fname, be.bstrerror());
               Dmsg3(100, "Unable to restore owner of xattr %s on file \"%s\": ERR=%s\n", name, jcr->last_fname, be.bstrerror());
               rc = bRC_BXATTR_error;
         }
         goto bail_out;
      }
   }

#ifdef HAVE_ACL
   if (strlen(acltext)){
      rc = os_set_xattr_acl(jcr, attrfd, name, acltext);
      if (rc != bRC_BXATTR_ok){
         goto bail_out;
      }
   }
#endif /* HAVE_ACL */

   /* now restore a access and modification time - only for standard attribute */
   if (!extended){
      times[0].tv_sec = st.st_atime;
      times[0].tv_usec = 0;
      times[1].tv_sec = st.st_mtime;
      times[1].tv_usec = 0;

      if (futimesat(attrdirfd, name, times) < 0){
         berrno be;

         Mmsg3(jcr->errmsg, _("Unable to restore filetimes of xattr %s on file \"%s\": ERR=%s\n"), name, jcr->last_fname, be.bstrerror());
         Dmsg3(100, "Unable to restore filetimes of xattr %s on file \"%s\": ERR=%s\n", name, jcr->last_fname, be.bstrerror());
         rc = bRC_BXATTR_error;
         goto bail_out;
      }
   }

bail_out:
   if (attrfd != 0){
      close(attrfd);
   }
   if (attrdirfd != 0){
      close(attrdirfd);
   }
   return rc;
};

#endif /* HAVE_XATTR */

#endif /* HAVE_SUN_OS */
