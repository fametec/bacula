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
#include "bacl_solaris.h"

#if defined(HAVE_SUN_OS)

/* check if ACL support is enabled */
#if defined(HAVE_ACL)

/*
 * Define the supported ACL streams for this OS
 */
static const int os_acl_streams[] = {
   STREAM_XACL_SOLARIS_POSIX,
   STREAM_XACL_SOLARIS_NFS4,
   0
};

static const int os_default_acl_streams[] = {
   0
};

/*
 * OS specific constructor
 */
BACL_Solaris::BACL_Solaris(){

   set_acl_streams(os_acl_streams, os_default_acl_streams);
   cache = NULL;
};

/*
 * OS specific destructor
 */
BACL_Solaris::~BACL_Solaris(){};

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
 */
bRC_BACL BACL_Solaris::check_bacltype (JCR *jcr, int name){

   int rc = 0;

   rc = pathconf(jcr->last_fname, name);
   switch (rc){
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
         /* No support for ACLs */
         clear_flag(BACL_FLAG_NATIVE);
         set_content(NULL);
         return bRC_BACL_skip;
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
bRC_BACL BACL_Solaris::os_backup_acl (JCR *jcr, FF_PKT *ff_pkt){

   bRC_BACL rc;
   int stream;

   /*
    * See if filesystem supports acls.
    */
   rc = check_bacltype(jcr, _PC_ACL_ENABLED);
   switch (rc){
      case bRC_BACL_ok:
         break;
      case bRC_BACL_skip:
         return bRC_BACL_ok;
      default:
         /* errors */
         return rc;
   }

   rc = os_get_acl(jcr, &stream);
   switch (rc){
      case bRC_BACL_ok:
         if (get_content_len() > 0){
            if (send_acl_stream(jcr, stream) == bRC_BACL_fatal){
               return bRC_BACL_fatal;
            }
         }
         break;
      default:
         return rc;
   }

   return bRC_BACL_ok;
};

/*
 * Perform OS specific ACL restore
 *
 * in/out - check API at bacl.h
 */
bRC_BACL BACL_Solaris::os_restore_acl (JCR *jcr, int stream, char *content, uint32_t length){

   int aclrc = 0;

   switch (stream){
      case STREAM_UNIX_ACCESS_ACL:
      case STREAM_XACL_SOLARIS_POSIX:
      case STREAM_XACL_SOLARIS_NFS4:
         aclrc = pathconf(jcr->last_fname, _PC_ACL_ENABLED);
         break;
      default:
         return bRC_BACL_error;
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
         Mmsg(jcr->errmsg, _("Trying to restore acl on file \"%s\" on filesystem without acl support\n"), jcr->last_fname);
         return bRC_BACL_error;
      default:
         break;
   }

   Dmsg2(400, "restore acl stream %i on file: %s\n", stream, jcr->last_fname);
   switch (stream){
      case STREAM_XACL_SOLARIS_POSIX:
         if ((aclrc & (_ACL_ACLENT_ENABLED | _ACL_ACE_ENABLED)) == 0){
            Mmsg(jcr->errmsg, _("Trying to restore POSIX acl on file \"%s\" on filesystem without aclent acl support\n"), jcr->last_fname);
            return bRC_BACL_error;
         }
         break;
      case STREAM_XACL_SOLARIS_NFS4:
         if ((aclrc & _ACL_ACE_ENABLED) == 0){
            Mmsg(jcr->errmsg, _("Trying to restore NFSv4 acl on file \"%s\" on filesystem without ace acl support\n"), jcr->last_fname);
            return bRC_BACL_error;
         }
         break;
      default:
         break;
   }

   return os_set_acl(jcr, stream, content, length);
};

/*
 * Low level OS specific runtime to get ACL data from file. The ACL data is set in internal content buffer
 *
 * in/out - check API at bacl.h
 */
bRC_BACL BACL_Solaris::os_get_acl(JCR *jcr, int *stream){

   int flags;
   acl_t *aclp;
   char *acl_text;
   bRC_BACL rc = bRC_BACL_ok;

   if (!stream){
      return bRC_BACL_fatal;
   }

   if (acl_get(jcr->last_fname, ACL_NO_TRIVIAL, &aclp) != 0){
      /* we've got some error */
      berrno be;
      switch (errno){
      case ENOENT:
         /* file does not exist */
         return bRC_BACL_ok;
      default:
         Mmsg2(jcr->errmsg, _("acl_get error on file \"%s\": ERR=%s\n"), jcr->last_fname, acl_strerror(errno));
         Dmsg2(100, "acl_get error file=%s ERR=%s\n", jcr->last_fname, acl_strerror(errno));
         return bRC_BACL_error;
      }
   }

   if (!aclp){
      /*
       * The ACLs simply reflect the (already known) standard permissions
       * So we don't send an ACL stream to the SD.
       */
      set_content(NULL);
      return bRC_BACL_ok;
   }

#if defined(ACL_SID_FMT)
   /* new format flag added in newer Solaris versions */
   flags = ACL_APPEND_ID | ACL_COMPACT_FMT | ACL_SID_FMT;
#else
   flags = ACL_APPEND_ID | ACL_COMPACT_FMT;
#endif /* ACL_SID_FMT */

   if ((acl_text = acl_totext(aclp, flags)) != NULL){
      set_content(acl_text);
      switch (acl_type(aclp)){
         case ACLENT_T:
            *stream = STREAM_XACL_SOLARIS_POSIX;
            Dmsg1(500, "found acl SOLARIS_POSIX: %s\n", acl_text);
            break;
         case ACE_T:
            *stream = STREAM_XACL_SOLARIS_NFS4;
            Dmsg1(500, "found acl SOLARIS_NFS4: %s\n", acl_text);
            break;
         default:
            rc = bRC_BACL_error;
            break;
      }
      actuallyfree(acl_text);
      acl_free(aclp);
   }
   return rc;
};

/*
 * Low level OS specific runtime to set ACL data on file
 *
 * in/out - check API at bacl.h
 */
bRC_BACL BACL_Solaris::os_set_acl(JCR *jcr, int stream, char *content, uint32_t length){

   int rc;
   acl_t *aclp;

   if ((rc = acl_fromtext(content, &aclp)) != 0){
      Mmsg2(jcr->errmsg, _("acl_fromtext error on file \"%s\": ERR=%s\n"), jcr->last_fname, acl_strerror(rc));
      Dmsg3(100, "acl_fromtext error acl=%s file=%s ERR=%s\n", content, jcr->last_fname, acl_strerror(rc));
      return bRC_BACL_error;
   }

   switch (stream){
      case STREAM_XACL_SOLARIS_POSIX:
         if (acl_type(aclp) != ACLENT_T){
            Mmsg(jcr->errmsg, _("wrong encoding of acl type in acl stream on file \"%s\"\n"), jcr->last_fname);
            return bRC_BACL_error;
         }
         break;
      case STREAM_XACL_SOLARIS_NFS4:
         if (acl_type(aclp) != ACE_T){
            Mmsg(jcr->errmsg, _("wrong encoding of acl type in acl stream on file \"%s\"\n"), jcr->last_fname);
            return bRC_BACL_error;
         }
         break;
      default:
         break;
   }

   if ((rc = acl_set(jcr->last_fname, aclp)) == -1 && jcr->last_type != FT_LNK){
      switch (errno){
         case ENOENT:
            acl_free(aclp);
            return bRC_BACL_ok;
         default:
            Mmsg2(jcr->errmsg, _("acl_set error on file \"%s\": ERR=%s\n"), jcr->last_fname, acl_strerror(rc));
            Dmsg3(100, "acl_set error acl=%s file=%s ERR=%s\n", content, jcr->last_fname, acl_strerror(rc));
            acl_free(aclp);
            return bRC_BACL_error;
      }
   }

   acl_free(aclp);
   return bRC_BACL_ok;
};

#endif /* HAVE_ACL */

#endif /* HAVE_SUN_OS */
