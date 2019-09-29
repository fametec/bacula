/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2017 Kern Sibbald

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
 *
 * A specialized class to handle ACL in Bacula Enterprise.
 * The runtime consist of two parts:
 * 1. OS independent class: BACL
 * 2. OS dependent subclass: BACL_*
 *
 * OS dependent subclasses are available for the following OS:
 *   - Darwin (OSX)
 *   - FreeBSD (POSIX and NFSv4/ZFS acls)
 *   - Linux
 *   - Solaris (POSIX and NFSv4/ZFS acls)
 *
 * OS dependent subclasses in progress:
 *   - AIX (pre-5.3 and post 5.3 acls, acl_get and aclx_get interface)
 *   - HPUX
 *   - IRIX
 *   - Tru64
 *
 * OS independent class support AFS acls using the pioctl interface.
 *
 * ACLs are saved in OS native text format provided by acl(3) API and uses
 * different streams for all different platforms.
 * Above behavior is a backward compatibility with previous Bacula implementation
 * we need to maintain.
 *
 * During OS specific implementation of BACL you need to implement a following methods:
 *
 * [bacl] - indicates bacl function/method to call
 * [os] - indicates OS specific function, which could be different on specific OS
 *        (we use a Linux API calls as an example)
 *
 * ::os_get_acl(JCR *jcr, BACL_type bacltype)
 *
 *   1. get binary form of the acl - acl_get_file[os]
 *   2. check if acl is trivial if required - call acl_issimple[bacl]
 *   3. translate binary form into text representation - acl_to_text[os]
 *   4. save acl text into content - set_content[bacl]
 *   5. if acl not supported on filesystem - call clear_flag(BACL_FLAG_NATIVE)[bacl]
 *
 * ::os_backup_acl (JCR *jcr, FF_PKT *ff_pkt)
 *
 *   1. call os_get_acl[bacl] for all supported ACL_TYPES
 *   2. call send_acl_stream[bacl] for all supported ACL_STREAMS
 *
 * ::os_set_acl(JCR *jcr, BACL_type bacltype, char *content, uint32_t length)
 *
 *   1. prepare acl binary form from text representation stored in content - acl_from_text[os]
 *   2. set acl on file - acl_set_file[os]
 *   3. if acl not supported on filesystem, clear_flag(BACL_FLAG_NATIVE)
 *
 * ::os_restore_acl (JCR *jcr, int stream, char *content, uint32_t length)
 *
 *   1. call os_set_acl for all supported ACL_TYPES
 */

#include "bacula.h"
#include "filed.h"
#include "fd_plugins.h"

/* check if ACL support is enabled */
#if defined(HAVE_ACL)

/*
 * This is a constructor of the base BACL class which is OS independent
 *
 * - for initialization it uses ::init()
 *
 */
BACL::BACL (){
   init();
};

/*
 * This is a destructor of the BACL class
 */
BACL::~BACL (){
   free_pool_memory(content);
};

/*
 * Initialization routine
 * - initializes all variables to required status
 * - allocates required memory
 */
void BACL::init(){
#if defined(HAVE_ACL)
   acl_ena = TRUE;
#else
   acl_ena = FALSE;
#endif

   /* generic variables */
   flags = BACL_FLAG_NONE;
   current_dev = 0;
   content = get_pool_memory(PM_BSOCK);   /* it is better to have a 4k buffer */
   content_len = 0;
   acl_nr_errors = 0;
   acl_streams = NULL;
   default_acl_streams = NULL;
};

/*
 * Enables ACL handling in runtime, could be disabled with disable_acl
 *    when ACL is not configured then cannot change status
 */
void BACL::enable_acl(){
#if defined(HAVE_ACL)
   acl_ena = TRUE;
#endif
};

/*
 * Disables ACL handling in runtime, could be enabled with enable_acl
 *    when ACL is configured
 */
void BACL::disable_acl(){
   acl_ena = FALSE;
};

/*
 * Copies a text into a content variable and sets a content_len respectively
 *
 * in:
 *    text - a standard null terminated string
 * out:
 *    pointer to content variable to use externally
 */
POOLMEM * BACL::set_content(char *text){
   content_len = pm_strcpy(&content, text);
   if (content_len > 0){
      /* count the nul terminated char */
      content_len++;
   }
   // Dmsg2(400, "BACL::set_content: %p %i\n", text, content_len);
   return content;
};

/*
 * Copies a data with length of len into a content variable
 *
 * in:
 *    data - data pointer to copy into content buffer
 * out:
 *    pointer to content variable to use externally
 */
POOLMEM * BACL::set_content(char *data, int len){
   content_len = pm_memcpy(&content, data, len);
   return content;
};

/*
 * Check if we changed the device,
 * if so setup a flags
 *
 * in:
 *    jcr - Job Control Record
 * out:
 *    bRC_BACL_ok - change of device checked and finish successful
 *    bRC_BACL_error - encountered error
 *    bRC_BACL_skip - cannot verify device - no file found
 *    bRC_BACL_inval - invalid input data
 */
bRC_BACL BACL::check_dev (JCR *jcr){

   int lst;
   struct stat st;

   /* sanity check of input variables */
   if (jcr == NULL || jcr->last_fname == NULL){
      return bRC_BACL_inval;
   }

   lst = lstat(jcr->last_fname, &st);
   switch (lst){
      case -1: {
         berrno be;
         switch (errno){
         case ENOENT:
            return bRC_BACL_skip;
         default:
            Mmsg2(jcr->errmsg, _("Unable to stat file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
            Dmsg2(100, "Unable to stat file \"%s\": ERR=%s\n", jcr->last_fname, be.bstrerror());
            return bRC_BACL_error;
         }
         break;
      }
      case 0:
         break;
   }

   check_dev(jcr, st.st_dev);

   return bRC_BACL_ok;
};

/*
 * Check if we changed the device, if so setup a flags
 *
 * in:
 *    jcr - Job Control Record
 * out:
 *    internal flags status set
 */
void BACL::check_dev (JCR *jcr, uint32_t dev){

   /* sanity check of input variables */
   if (jcr == NULL || jcr->last_fname == NULL){
      return;
   }

   if (current_dev != dev){
      flags = BACL_FLAG_NONE;
#if defined(HAVE_AFS_ACL)
      /* handle special fs: AFS */
      if (fstype_equals(jcr->last_fname, "afs")){
         set_flag(BACL_FLAG_AFS);
      } else {
         set_flag(BACL_FLAG_NATIVE);
      }
#else
      set_flag(BACL_FLAG_NATIVE);
#endif
      current_dev = dev;
   }
};

/*
 * It sends a stream located in this->content to Storage Daemon, so the main Bacula
 * backup loop is free from this. It sends a header followed by data.
 *
 * in:
 *    jcr - Job Control Record
 *    stream - a stream number to save
 * out:
 *    bRC_BACL_inval - when supplied variables are incorrect
 *    bRC_BACL_fatal - when we can't send data to the SD
 *    bRC_BACL_ok - send finish without errors
 */
bRC_BACL BACL::send_acl_stream(JCR *jcr, int stream){

   BSOCK * sd;
   POOLMEM * msgsave;
#ifdef FD_NO_SEND_TEST
   return bRC_BACL_ok;
#endif

   /* sanity check of input variables */
   if (jcr == NULL || jcr->store_bsock == NULL){
      return bRC_BACL_inval;
   }
   if (content_len <= 0){
      return bRC_BACL_ok;
   }

   sd = jcr->store_bsock;
   /* send header */
   if (!sd->fsend("%ld %d 0", jcr->JobFiles, stream)){
      Jmsg1(jcr, M_FATAL, 0, _("Network send error to SD. ERR=%s\n"), sd->bstrerror());
      return bRC_BACL_fatal;
   }

   /* send the buffer to the storage daemon */
   Dmsg1(400, "Backing up ACL: %i\n", content_len);
#if 0
   POOL_MEM tmp(PM_FNAME);
   pm_memcpy(tmp, content, content_len);
   Dmsg2(400, "Backing up ACL: (%i) <%s>\n", strlen(tmp.addr()), tmp.c_str());
#endif
   msgsave = sd->msg;
   sd->msg = content;
   sd->msglen = content_len;
   if (!sd->send()){
      sd->msg = msgsave;
      sd->msglen = 0;
      Jmsg1(jcr, M_FATAL, 0, _("Network send error to SD. ERR=%s\n"), sd->bstrerror());
      return bRC_BACL_fatal;
   }

   jcr->JobBytes += sd->msglen;
   sd->msg = msgsave;
   if (!sd->signal(BNET_EOD)){
      Jmsg1(jcr, M_FATAL, 0, _("Network send error to SD. ERR=%s\n"), sd->bstrerror());
      return bRC_BACL_fatal;
   }

   Dmsg1(200, "ACL of file: %s successfully backed up!\n", jcr->last_fname);
   return bRC_BACL_ok;
};

/*
 * The main public backup method for ACL
 *
 * in:
 *    jcr - Job Control Record
 *    ff_pkt - file backup record
 * out:
 *    bRC_BACL_fatal - when ACL backup is not compiled in Bacula
 *    bRC_BACL_ok - backup finish without problems
 *    bRC_BACL_error - when you can't backup acl data because some error
 */
bRC_BACL BACL::backup_acl (JCR *jcr, FF_PKT *ff_pkt)
{
#if !defined(HAVE_ACL) && !defined(HAVE_AFS_ACL)
   Jmsg(jcr, M_FATAL, 0, "ACL backup requested but not configured in Bacula.\n");
   return bRC_BACL_fatal;
#else
   /* sanity check of input variables and verify if engine is enabled */
   if (acl_ena && jcr != NULL && ff_pkt != NULL){
      /* acl engine enabled, proceed */
      bRC_BACL rc;

      jcr->errmsg[0] = 0;
      /* check if we have a plugin generated backup */
      if (ff_pkt->cmd_plugin){
         rc = backup_plugin_acl(jcr, ff_pkt);
      } else {
         /* Check for aclsupport flag and no acl request for link */
         if (!(ff_pkt->flags & FO_ACL && ff_pkt->type != FT_LNK)){
            return bRC_BACL_ok;
         }

         check_dev(jcr, ff_pkt->statp.st_dev);

#if defined(HAVE_AFS_ACL)
         if (flags & BACL_FLAG_AFS){
            Dmsg0(400, "make AFS ACL call\n");
            rc = afs_backup_acl(jcr, ff_pkt);
            goto bail_out;
         }
#endif

#if defined(HAVE_ACL)
         if (flags & BACL_FLAG_NATIVE){
            Dmsg0(400, "make Native ACL call\n");
            rc = os_backup_acl(jcr, ff_pkt);
         } else {
            /* skip acl backup */
            return bRC_BACL_ok;
         }
#endif
      }
#if defined(HAVE_AFS_ACL)
   bail_out:
#endif
      if (rc == bRC_BACL_error){
         if (acl_nr_errors < ACL_MAX_ERROR_PRINT_PER_JOB){
            if (!jcr->errmsg[0]){
               Jmsg(jcr, M_WARNING, 0, "No OS ACL configured.\n");
            } else {
               Jmsg(jcr, M_WARNING, 0, "%s", jcr->errmsg);
            }
            inc_acl_errors();
         }
         return bRC_BACL_ok;
      }
      return rc;
   }
   return bRC_BACL_ok;
#endif
};

/*
 * The main public restore method for ACL
 *
 * in:
 *    jcr - Job Control Record
 *    stream - a backup stream type number to restore_acl
 *    data - a pointer to the data stream to restore
 *    length - a data stream length
 * out:
 *    bRC_BACL_fatal - when ACL restore is not compiled in Bacula
 *    bRC_BACL_ok - restore finish without problems
 *    bRC_BACL_error - when you can't restore a stream because some error
 */
bRC_BACL BACL::restore_acl (JCR *jcr, int stream, char *data, uint32_t length)
{
#if !defined(HAVE_ACL) && !defined(HAVE_AFS_ACL)
   Jmsg(jcr, M_FATAL, 0, "ACL restore requested but not configured in Bacula.\n");
   return bRC_BACL_fatal;
#else
   /* sanity check of input variables and verify if engine is enabled */
   if (acl_ena && jcr != NULL && data != NULL){
      /* acl engine enabled, proceed */
      int a;
      bRC_BACL rc;

      /* check_dev supported on real fs only */
      if (stream != STREAM_XACL_PLUGIN_ACL){
         rc = check_dev(jcr);

         switch (rc){
            case bRC_BACL_skip:
               return bRC_BACL_ok;
            case bRC_BACL_ok:
               break;
            default:
               return rc;
         }
      }

      /* copy a data into a content buffer */
      set_content(data, length);

      switch (stream){
#if defined(HAVE_AFS_ACL)
         case STREAM_BACL_AFS_TEXT:
            if (flags & BACL_FLAG_AFS){
               return afs_restore_acl(jcr, stream);
            } else {
               /*
                * Increment error count but don't log an error again for the same filesystem.
                */
               inc_acl_errors();
               return bRC_BACL_ok;
            }
#endif
#if defined(HAVE_ACL)
         case STREAM_UNIX_ACCESS_ACL:
         case STREAM_UNIX_DEFAULT_ACL:
            if (flags & BACL_FLAG_NATIVE){
               return os_restore_acl(jcr, stream, content, content_len);
            } else {
               inc_acl_errors();
               return bRC_BACL_ok;
            }
            break;
         case STREAM_XACL_PLUGIN_ACL:
            return restore_plugin_acl(jcr);
         default:
            if (flags & BACL_FLAG_NATIVE){
               Dmsg0(400, "make Native ACL call\n");
               for (a = 0; acl_streams[a] > 0; a++){
                  if (acl_streams[a] == stream){
                     return os_restore_acl(jcr, stream, content, content_len);
                  }
               }
               for (a = 0; default_acl_streams[a] > 0; a++){
                  if (default_acl_streams[a] == stream){
                     return os_restore_acl(jcr, stream, content, content_len);
                  }
               }
            } else {
               inc_acl_errors();
               return bRC_BACL_ok;
            }
            break;
#else
         default:
            break;
#endif
      }
      /* cannot find a valid stream to support */
      Qmsg2(jcr, M_WARNING, 0, _("Can't restore ACLs of %s - incompatible acl stream encountered - %d\n"), jcr->last_fname, stream);
      return bRC_BACL_error;
   }
   return bRC_BACL_ok;
#endif
};

/*
 * Performs a generic ACL backup using OS specific methods for
 * getting acl data from file
 *
 * in:
 *    jcr - Job Control Record
 *    ff_pkt - file to backup control package
 * out:
 *    bRC_BACL_ok - backup of acl's was successful
 *    bRC_BACL_fatal - was an error during acl backup
 */
bRC_BACL BACL::generic_backup_acl (JCR *jcr, FF_PKT *ff_pkt)
{
   /* sanity check of input variables */
   if (jcr == NULL || ff_pkt == NULL){
      return bRC_BACL_inval;
   }

   if (os_get_acl(jcr, BACL_TYPE_ACCESS) == bRC_BACL_fatal){
      /* XXX: check if os_get_acl return fatal and decide what to do when error is returned */
      return bRC_BACL_fatal;
   }

   if (content_len > 0){
      if (send_acl_stream(jcr, acl_streams[0]) == bRC_BACL_fatal){
         return bRC_BACL_fatal;
      }
   }

   if (ff_pkt->type == FT_DIREND){
      if (os_get_acl(jcr, BACL_TYPE_DEFAULT) == bRC_BACL_fatal){
         return bRC_BACL_fatal;
      }
      if (content_len > 0){
         if (send_acl_stream(jcr, default_acl_streams[0]) == bRC_BACL_fatal){
            return bRC_BACL_fatal;
         }
      }
   }
   return bRC_BACL_ok;
};

/*
 * Performs a generic ACL restore using OS specific methods for
 * setting acl data on file.
 *
 * in:
 *    jcr - Job Control Record
 *    stream - a stream number to restore
 * out:
 *    bRC_BACL_ok - restore of acl's was successful
 *    bRC_BACL_error - was an error during acl restore
 *    bRC_BACL_fatal - was a fatal error during acl restore or input data
 *                     is invalid
 */
bRC_BACL BACL::generic_restore_acl (JCR *jcr, int stream){

   unsigned int count;

   /* sanity check of input variables */
   if (jcr == NULL){
      return bRC_BACL_inval;
   }

   switch (stream){
      case STREAM_UNIX_ACCESS_ACL:
         return os_set_acl(jcr, BACL_TYPE_ACCESS, content, content_len);
      case STREAM_UNIX_DEFAULT_ACL:
         return os_set_acl(jcr, BACL_TYPE_DEFAULT, content, content_len);
      default:
         for (count = 0; acl_streams[count] > 0; count++){
            if (acl_streams[count] == stream){
               return os_set_acl(jcr, BACL_TYPE_ACCESS, content, content_len);
            }
         }
         for (count = 0; default_acl_streams[count] > 0; count++){
            if (default_acl_streams[count] == stream){
               return os_set_acl(jcr, BACL_TYPE_DEFAULT, content, content_len);
            }
         }
         break;
   }
   return bRC_BACL_error;
};

/*
 * Perform a generic ACL backup using a plugin. It calls the plugin API to
 * get required acl data from plugin.
 *
 * in:
 *    jcr - Job Control Record
 *    ff_pkt - file to backup control package
 * out:
 *    bRC_BACL_ok - backup of acls was successful
 *    bRC_BACL_fatal - was an error during acl backup
 */
bRC_BACL BACL::backup_plugin_acl (JCR *jcr, FF_PKT *ff_pkt)
{
   int status;
   char *data;

   /* sanity check of input variables */
   if (jcr == NULL || ff_pkt == NULL){
      return bRC_BACL_inval;
   }

   while ((status = plugin_backup_acl(jcr, ff_pkt, &data)) > 0){
      /* data is a plugin buffer which contains data to backup
       * and status is a length of the buffer when > 0 */
      set_content(data, status);
      if (send_acl_stream(jcr, STREAM_XACL_PLUGIN_ACL) == bRC_BACL_fatal){
         return bRC_BACL_fatal;
      }
   }
   if (status < 0){
      /* error */
      return bRC_BACL_error;
   }

   return bRC_BACL_ok;
};

/*
 * Perform a generic ACL restore using a plugin. It calls the plugin API to
 * send acl data to plugin.
 *
 * in:
 *    jcr - Job Control Record
 *    stream - a stream number to restore
 * out:
 *    bRC_BACL_ok - restore of acls was successful
 *    bRC_BACL_error - was an error during acls restore
 *    bRC_BACL_fatal - was a fatal error during acl restore or input data
 *                     is invalid
 */
bRC_BACL BACL::restore_plugin_acl (JCR *jcr)
{
   /* sanity check of input variables */
   if (jcr == NULL){
      return bRC_BACL_inval;
   }

   if (!plugin_restore_acl(jcr, content, content_len)){
      /* error */
      return bRC_BACL_error;
   }

   return bRC_BACL_ok;
}

/*
 * Initialize variables acl_streams and default_acl_streams for a specified OS.
 * The rutine should be called from object instance constructor
 *
 * in:
 *    pacl - acl streams supported for specific OS
 *    pacl_def - default (directory) acl streams supported for specific OS
 */
void BACL::set_acl_streams (const int *pacl, const int *pacl_def){

   acl_streams = pacl;
   default_acl_streams = pacl_def;
};

#if defined(HAVE_AFS_ACL)
#if defined(HAVE_AFS_AFSINT_H) && defined(HAVE_AFS_VENUS_H)
#include <afs/afsint.h>
#include <afs/venus.h>
#else
#error "configure failed to detect availability of afs/afsint.h and/or afs/venus.h"
#endif

/*
 * External references to functions in the libsys library function not in current include files.
 */
extern "C" {
long pioctl(char *pathp, long opcode, struct ViceIoctl *blobp, int follow);
}

/*
 * Backup ACL data of AFS
 *
 * in:
 *    jcr - Job Control Record
 *    ff_pkt - file backup record
 * out:
 *    bRC_BACL_inval - input variables are invalid (NULL)
 *    bRC_BACL_ok - backup finish without problems
 *    bRC_BACL_error - when you can't backup acl data because some error
 */
bRC_BACL BACL::afs_backup_acl (JCR *jcr, FF_PKT *ff_pkt){

   int rc;
   struct ViceIoctl vip;
   char data[BUFSIZ];

   /* sanity check of input variables */
   if (jcr == NULL || ff_pkt == NULL){
      return bRC_BACL_inval;
   }

   /* AFS ACLs can only be set on a directory, so no need to try other files */
   if (ff_pkt->type != FT_DIREND){
      return bRC_BACL_ok;
   }

   vip.in = NULL;
   vip.in_size = 0;
   vip.out = data;
   vip.out_size = BUFSIZE;
   memset(data, 0, BUFSIZE);

   if ((rc = pioctl(jcr->last_fname, VIOCGETAL, &vip, 0)) < 0){
      berrno be;

      Mmsg2(jcr->errmsg, _("pioctl VIOCGETAL error on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
      Dmsg2(100, "pioctl VIOCGETAL error file=%s ERR=%s\n", jcr->last_fname, be.bstrerror());
      return bRC_BACL_error;
   }
   set_content(data);
   return send_acl_stream(jcr, STREAM_BACL_AFS_TEXT);
};

/*
 * Restore ACL data of AFS
 * in:
 *    jcr - Job Control Record
 *    stream - a backup stream type number to restore_acl
 * out:
 *    bRC_BACL_inval - input variables are invalid (NULL)
 *    bRC_BACL_ok - backup finish without problems
 *    bRC_BACL_error - when you can't backup acl data because some error
 */
bRC_BACL BACL::afs_restore_acl (JCR *jcr, int stream){

   int rc;
   struct ViceIoctl vip;

   /* sanity check of input variables */
   if (jcr == NULL || ff_pkt == NULL){
      return bRC_BACL_inval;
   }

   vip.in = content;
   vip.in_size = content_len;
   vip.out = NULL;
   vip.out_size = 0;

   if ((rc = pioctl(jcr->last_fname, VIOCSETAL, &vip, 0)) < 0){
      berrno be;

      Mmsg2(jcr->errmsg, _("pioctl VIOCSETAL error on file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
      Dmsg2(100, "pioctl VIOCSETAL error file=%s ERR=%s\n", jcr->last_fname, be.bstrerror());

      return bRC_BACL_error;
   }
   return bRC_BACL_ok;
};
#endif /* HAVE_AFS_ACL */

#include "bacl_osx.h"
#include "bacl_linux.h"
#include "bacl_freebsd.h"
#include "bacl_solaris.h"
// #include "bacl_aix.h"

/*
 * Creating the correct instance of the BACL for a supported OS
 */
void *new_bacl()
{
#if   defined(HAVE_DARWIN_OS)
   return new BACL_OSX();
#elif defined(HAVE_LINUX_OS)
   return new BACL_Linux();
#elif defined(HAVE_FREEBSD_OS)
   return new BACL_FreeBSD();
#elif defined(HAVE_HURD_OS)
   return new BACL_Hurd();
#elif defined(HAVE_AIX_OS)
   return new BACL_AIX();
#elif defined(HAVE_IRIX_OS)
   return new BACL_IRIX();
#elif defined(HAVE_OSF1_OS)
   return new BACL_OSF1();
#elif defined(HAVE_SUN_OS)
   return new BACL_Solaris();
#else
   return NULL;
#endif
};

#endif /* HAVE_ACL */
