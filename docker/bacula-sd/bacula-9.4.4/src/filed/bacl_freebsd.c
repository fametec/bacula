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

#include "bacula.h"
#include "filed.h"
#include "bacl_freebsd.h"

#if defined(HAVE_FREEBSD_OS)

/* check if ACL support is enabled */
#if defined(HAVE_ACL)

/*
 * Define the supported ACL streams for this OS
 */
static const int os_acl_streams[] = {
   STREAM_XACL_FREEBSD_ACCESS,
   STREAM_XACL_FREEBSD_NFS4,
   0
};

static const int os_default_acl_streams[] = {
   STREAM_XACL_FREEBSD_DEFAULT,
   0
};

/*
 * OS specific constructor
 */
BACL_FreeBSD::BACL_FreeBSD(){

   set_acl_streams(os_acl_streams, os_default_acl_streams);
};

/*
 * Translates Bacula internal acl representation into acl type
 *
 * in:
 *    bacltype - internal Bacula acl type (BACL_type)
 * out:
 *    acl_type_t - os dependent acl type
 *    when failed - ACL_TYPE_NONE is returned
 */
acl_type_t BACL_FreeBSD::get_acltype(BACL_type bacltype){

   acl_type_t acltype;

   switch (bacltype){
#ifdef HAVE_ACL_TYPE_NFS4
      case BACL_TYPE_NFS4:
         acltype = ACL_TYPE_NFS4;
         break;
#endif
      case BACL_TYPE_ACCESS:
         acltype = ACL_TYPE_ACCESS;
         break;
      case BACL_TYPE_DEFAULT:
         acltype = ACL_TYPE_DEFAULT;
         break;
      default:
         /*
          * sanity check for acl's not supported by OS
          */
         acltype = (acl_type_t)ACL_TYPE_NONE;
         break;
   }
   return acltype;
};

/*
 * Counts a number of acl entries
 *
 * in:
 *    acl - acl object
 * out:
 *    int - number of entries in acl object
 *    when no acl entry available or any error then return zero '0'
 */
int BACL_FreeBSD::acl_nrentries(acl_t acl){

   int nr = 0;
   acl_entry_t aclentry;
   int rc;

   rc = acl_get_entry(acl, ACL_FIRST_ENTRY, &aclentry);
   while (rc == 1){
      nr++;
      rc = acl_get_entry(acl, ACL_NEXT_ENTRY, &aclentry);
   }

   return nr;
};

/*
 * Checks if acl is simple.
 *
 * acl is simple if it has only the following entries:
 * "user::",
 * "group::",
 * "other::"
 *
 * in:
 *    acl - acl object
 * out:
 *    true - when acl object is simple
 *    false - when acl object is not simple
 */
bool BACL_FreeBSD::acl_issimple(acl_t acl){

   acl_entry_t aclentry;
   acl_tag_t acltag;
   int rc;

   rc = acl_get_entry(acl, ACL_FIRST_ENTRY, &aclentry);
   while (rc == 1){
      if (acl_get_tag_type(aclentry, &acltag) < 0){
         return true;
      }
      /*
       * Check for ACL_USER_OBJ, ACL_GROUP_OBJ or ACL_OTHER to find out.
       */
      if (acltag != ACL_USER_OBJ &&
          acltag != ACL_GROUP_OBJ &&
          acltag != ACL_OTHER){
         return false;
      }
      rc = acl_get_entry(acl, ACL_NEXT_ENTRY, &aclentry);
   }
   return true;
};

/*
 * Checks if ACL's are available for a specified file
 *
 * in:
 *    jcr - Job Control Record
 *    name - specifies the system variable to be queried
 * out:
 *    bRC_BACL_ok - check successful, lets setup bacltype variable
 *    bRC_BACL_error -  in case of error
 *    bRC_BACL_skip - you should skip all other routine
 *    bRC_BACL_cont - you need to continue your routine
 */
bRC_BACL BACL_FreeBSD::check_bacltype (JCR *jcr, int name){

   int aclrc = 0;

   aclrc = pathconf(jcr->last_fname, name);
   switch (aclrc){
      case -1: {
         /* some error check why */
         berrno be;
         if (errno == ENOENT){
            /* file does not exist skip it */
            return bRC_BACL_skip;
         } else {
            Mmsg2(jcr->errmsg, _("pathconf error on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
            Dmsg2(100, "pathconf error file=%s ERR=%s\n", jcr->last_fname, be.bstrerror());
            return bRC_BACL_error;
         }
      }
      case 0:
         /* continue the routine */
         return bRC_BACL_cont;
      default:
         break;
   }
   return bRC_BACL_ok;
};

/*
 * Perform OS specific ACL backup
 *
 * in/out - check API at bacl.h
 */
bRC_BACL BACL_FreeBSD::os_backup_acl (JCR *jcr, FF_PKT *ff_pkt){

   bRC_BACL rc;
   BACL_type bacltype = BACL_TYPE_NONE;

#if defined(_PC_ACL_NFS4)
   /*
    * Check if filesystem supports NFS4 acls.
    */
   rc = check_bacltype(jcr, _PC_ACL_NFS4);
   switch (rc){
      case bRC_BACL_ok:
         bacltype = BACL_TYPE_NFS4;
         break;
      case bRC_BACL_skip:
         return bRC_BACL_ok;
      case bRC_BACL_cont:
         break;
      default:
         /* errors */
         return rc;
   }
#endif
   if (bacltype == BACL_TYPE_NONE){
      /*
       * Check if filesystem supports POSIX acls.
       */
      rc = check_bacltype(jcr, _PC_ACL_EXTENDED);
      switch (rc){
         case bRC_BACL_ok:
            bacltype = BACL_TYPE_ACCESS;
            break;
         case bRC_BACL_skip:
            return bRC_BACL_ok;
         case bRC_BACL_cont:
            break;
         default:
            /* errors */
            return rc;
      }
   }

   /* no ACL's available for file, so skip this filesystem */
   if (bacltype == BACL_TYPE_NONE){
      clear_flag(BACL_FLAG_NATIVE);
      /*
       * it is a bit of hardcore to clear a poolmemory with a NULL pointer,
       * but it is working, hehe :)
       * you may ask why it is working? it is simple, a pm_strcpy function is handling
       * a null pointer with a substitiution of empty string.
       */
      set_content(NULL);
      return bRC_BACL_ok;
   }

   switch (bacltype){
      case BACL_TYPE_NFS4:
         /*
          * Read NFS4 ACLs
          */
         if (os_get_acl(jcr, BACL_TYPE_NFS4) == bRC_BACL_fatal)
            return bRC_BACL_fatal;

         if (get_content_len() > 0){
            if (send_acl_stream(jcr, STREAM_XACL_FREEBSD_NFS4) == bRC_BACL_fatal)
               return bRC_BACL_fatal;
         }
         break;
      case BACL_TYPE_ACCESS:
         /*
          * Read access ACLs
          */
         if (os_get_acl(jcr, BACL_TYPE_ACCESS) == bRC_BACL_fatal)
            return bRC_BACL_fatal;

         if (get_content_len() > 0){
            if (send_acl_stream(jcr, STREAM_XACL_FREEBSD_ACCESS) == bRC_BACL_fatal)
               return bRC_BACL_fatal;
         }

         /*
          * Directories can have default ACLs too
          */
         if (ff_pkt->type == FT_DIREND){
            if (os_get_acl(jcr, BACL_TYPE_DEFAULT) == bRC_BACL_fatal)
               return bRC_BACL_fatal;
            if (get_content_len() > 0){
               if (send_acl_stream(jcr, STREAM_XACL_FREEBSD_DEFAULT) == bRC_BACL_fatal)
                  return bRC_BACL_fatal;
            }
         }
         break;
      default:
         break;
   }

   return bRC_BACL_ok;
};

/*
 * Perform OS specific ACL restore
 *
 * in/out - check API at bacl.h
 */
bRC_BACL BACL_FreeBSD::os_restore_acl (JCR *jcr, int stream, char *content, uint32_t length){

   int aclrc = 0;
   const char *acl_type_name;

   switch (stream){
      case STREAM_UNIX_ACCESS_ACL:
      case STREAM_XACL_FREEBSD_ACCESS:
      case STREAM_UNIX_DEFAULT_ACL:
      case STREAM_XACL_FREEBSD_DEFAULT:
         aclrc = pathconf(jcr->last_fname, _PC_ACL_EXTENDED);
         acl_type_name = "POSIX";
         break;
      case STREAM_XACL_FREEBSD_NFS4:
#if defined(_PC_ACL_NFS4)
         aclrc = pathconf(jcr->last_fname, _PC_ACL_NFS4);
#endif
         acl_type_name = "NFS4";
         break;
      default:
         acl_type_name = "unknown";
         break;
   }

   switch (aclrc){
      case -1: {
         berrno be;

         switch (errno){
            case ENOENT:
               return bRC_BACL_ok;
            default:
               Mmsg2(jcr->errmsg, _("pathconf error on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
               Dmsg3(100, "pathconf error acl=%s file=%s ERR=%s\n", content, jcr->last_fname, be.bstrerror());
               return bRC_BACL_error;
         }
      }
      case 0:
         clear_flag(BACL_FLAG_NATIVE);
         Mmsg2(jcr->errmsg, _("Trying to restore acl on file \"%s\" on filesystem without %s acl support\n"), jcr->last_fname, acl_type_name);
         return bRC_BACL_error;
      default:
         break;
   }

   switch (stream){
      case STREAM_UNIX_ACCESS_ACL:
      case STREAM_XACL_FREEBSD_ACCESS:
         return os_set_acl(jcr, BACL_TYPE_ACCESS, content, length);
      case STREAM_UNIX_DEFAULT_ACL:
      case STREAM_XACL_FREEBSD_DEFAULT:
         return os_set_acl(jcr, BACL_TYPE_DEFAULT, content, length);
      case STREAM_XACL_FREEBSD_NFS4:
         return os_set_acl(jcr, BACL_TYPE_NFS4, content, length);
      default:
         break;
   }
   return bRC_BACL_error;
};

/*
 * Low level OS specific runtime to get ACL data from file.
 * The ACL data is set in internal content buffer
 *
 * in/out - check API at bacl.h
 */
bRC_BACL BACL_FreeBSD::os_get_acl(JCR *jcr, BACL_type bacltype){

   acl_t acl;
   acl_type_t acltype;
   char *acltext;
   bRC_BACL rc = bRC_BACL_ok;

   acltype = get_acltype(bacltype);
   acl = acl_get_file(jcr->last_fname, acltype);

   if (acl){
      Dmsg1(400, "OS_ACL read from file: %s\n",jcr->last_fname);
      if (acl_nrentries(acl) == 0){
         goto bail_out;
      }

      /* check for simple ACL which correspond to standard permissions only */
      if (bacltype == BACL_TYPE_ACCESS && acl_issimple(acl)){
         goto bail_out;
      }

#if defined(_PC_ACL_NFS4)
      if (bacltype == BACL_TYPE_NFS4){
         int trivial;
         if (acl_is_trivial_np(acl, &trivial) == 0){
            if (trivial == 1){
               goto bail_out;
            }
         }
      }
#endif

      if ((acltext = acl_to_text(acl, NULL)) != NULL){
         set_content(acltext);
         acl_free(acl);
         acl_free(acltext);
         return bRC_BACL_ok;
      }

      berrno be;

      Mmsg2(jcr->errmsg, _("acl_to_text error on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
      Dmsg2(100, "acl_to_text error file=%s ERR=%s\n", jcr->last_fname, be.bstrerror());

      rc = bRC_BACL_error;

   } else {
      berrno be;

      switch (errno){
      case EOPNOTSUPP:
         /* fs does not support acl, skip it */
         Dmsg0(400, "Wow, ACL is not supported on this filesystem\n");
         clear_flag(BACL_FLAG_NATIVE);
         break;
      case ENOENT:
         break;
      default:
         /* Some real error */
         Mmsg2(jcr->errmsg, _("acl_get_file error on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
         Dmsg2(100, "acl_get_file error file=%s ERR=%s\n", jcr->last_fname, be.bstrerror());
         rc = bRC_BACL_error;
         break;
      }
   }

bail_out:
   if (acl){
      acl_free(acl);
   }
   /*
    * it is a bit of hardcore to clear a pool memory with a NULL pointer,
    * but it is working, hehe :)
    * you may ask why it is working? it is simple, a pm_strcpy function is handling
    * a null pointer with a substitution of empty string.
    */
   set_content(NULL);
   return rc;
};

/*
 * Low level OS specific runtime to set ACL data on file
 *
 * in/out - check API at bacl.h
 */
bRC_BACL BACL_FreeBSD::os_set_acl(JCR *jcr, BACL_type bacltype, char *content, uint32_t length){

   acl_t acl;
   acl_type_t acltype;

   acltype = get_acltype(bacltype);
   if (acltype == ACL_TYPE_DEFAULT && length == 0){
      /* delete ACl from file when no acl data available for default acl's */
      if (acl_delete_def_file(jcr->last_fname) == 0){
         return bRC_BACL_ok;
      }

      berrno be;
      switch (errno){
         case ENOENT:
            return bRC_BACL_ok;
         case ENOTSUP:
            /*
             * If the filesystem reports it doesn't support acl's we clear the
             * BACL_FLAG_NATIVE flag so we skip ACL restores on all other files
             * on the same filesystem. The BACL_FLAG_NATIVE flag gets set again
             * when we change from one filesystem to an other.
             */
            clear_flag(BACL_FLAG_NATIVE);
            Mmsg(jcr->errmsg, _("acl_delete_def_file error on file \"%s\": filesystem doesn't support ACLs\n"), jcr->last_fname);
            return bRC_BACL_error;
         default:
            Mmsg2(jcr->errmsg, _("acl_delete_def_file error on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
            return bRC_BACL_error;
      }
   }

   acl = acl_from_text(content);
   if (acl == NULL){
      berrno be;

      Mmsg2(jcr->errmsg, _("acl_from_text error on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
      Dmsg3(100, "acl_from_text error acl=%s file=%s ERR=%s\n", content, jcr->last_fname, be.bstrerror());
      return bRC_BACL_error;
   }

   /*
    * Restore the ACLs, but don't complain about links which really should
    * not have attributes, and the file it is linked to may not yet be restored.
    * This is only true for the old acl streams as in the new implementation we
    * don't save acls of symlinks (which cannot have acls anyhow)
    */
   if (acl_set_file(jcr->last_fname, acltype, acl) != 0 && jcr->last_type != FT_LNK){
      berrno be;
      switch (errno){
      case ENOENT:
         acl_free(acl);
         return bRC_BACL_ok;
      case ENOTSUP:
         /*
          * If the filesystem reports it doesn't support ACLs we clear the
          * BACL_FLAG_NATIVE flag so we skip ACL restores on all other files
          * on the same filesystem. The BACL_FLAG_NATIVE flag gets set again
          * when we change from one filesystem to an other.
          */
         clear_flag(BACL_FLAG_NATIVE);
         Mmsg(jcr->errmsg, _("acl_set_file error on file \"%s\": filesystem doesn't support ACLs\n"), jcr->last_fname);
         Dmsg2(100, "acl_set_file error acl=%s file=%s filesystem doesn't support ACLs\n", content, jcr->last_fname);
         acl_free(acl);
         return bRC_BACL_error;
      default:
         Mmsg2(jcr->errmsg, _("acl_set_file error on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
         Dmsg3(100, "acl_set_file error acl=%s file=%s ERR=%s\n", content, jcr->last_fname, be.bstrerror());
         acl_free(acl);
         return bRC_BACL_error;
      }
   }
   acl_free(acl);
   return bRC_BACL_ok;
};

#endif /* HAVE_ACL */

#endif /* HAVE_FREEBSD_OS */
