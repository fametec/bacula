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
 * Major refactoring of XATTR code written by:
 *
 *  Radosław Korzeniewski, MMXVI
 *  radoslaw@korzeniewski.net, radekk@inteos.pl
 *  Inteos Sp. z o.o. http://www.inteos.pl/
 *
 *
 * A specialized class to handle XATTR in Bacula Enterprise.
 * The runtime consist of two parts:
 * 1. OS independent class: BXATTR
 * 2. OS dependent subclass: BXATTR_*
 *
 * OS dependent subclasses are available for the following OS:
 *   - Darwin (OSX)
 *   - FreeBSD
 *   - Linux
 *   - Solaris
 *
 * OS depended subclasses in progress:
 *   - AIX (pre-5.3 and post 5.3 acls, acl_get and aclx_get interface)
 *   - HPUX
 *   - IRIX
 *   - Tru64
 *
 * XATTRs are saved in OS independent format (Bacula own) and uses different streams
 * for all different platforms. In theory it is possible to restore XATTRs from
 * particular OS on different OS platform. But this functionality is not available.
 * The behavior above is backward compatibility with previous Bacula implementation
 * we need to maintain.
 *
 * During OS specific implementation of BXATTR you need to implement a following methods:
 *
 * [bxattr] - indicates bxattr function/method to call
 * [os] - indicates OS specific function, which could be different on specific OS
 *        (we use a Linux api calls as an example)
 *
 * ::os_get_xattr_names (JCR *jcr, int namespace, POOLMEM ** pxlist, uint32_t * xlen)
 *
 *    1. get a size of the extended attributes list for the file - llistxattr[os]
 *       in most os'es it is required to have a sufficient space for attributes list
 *       and we wont allocate too much and too low space
 *    2. allocate the buffer of required space
 *    3. get an extended attributes list for file - llistxattr[os]
 *    4. return allocated space buffer in pxlist and length of the buffer in xlen
 *
 * ::os_get_xattr_value (JCR *jcr, char * name, char ** pvalue, uint32_t * plen)
 *
 *    1. get a size of the extended attribute value for the file - lgetxattr[os]
 *       in most os'es it is required to have a sufficient space for attribute value
 *       and we wont allocate too much and too low space
 *    2. allocate the buffer of required space
 *    3. get an extended attribute value for file - lgetxattr[os]
 *    4. return allocated space buffer in pvalue and length of the buffer in plen
 *
 * ::os_backup_xattr (JCR *jcr, FF_PKT *ff_pkt)
 *
 *    1. get a list of extended attributes (name and value) for a file; in most implementations
 *       it require to get a separate list of attributes names and separate values for every name,
 *       so it is:
 *       1A. get a list of xattr attribute names available on file - os_get_xattr_names[bxattr]
 *       1B. for every attribute name get a value - os_get_xattr_value[bxattr]
 *          You should skip some OS specific attributes like ACL attributes or NFS4; you can use
 *          check_xattr_skiplists[bxattr] for this
 *       1C. build a list [type alist] of name/value pairs stored in BXATTR_xattr struct
 *    2. if the xattr list is not empty then serialize the list using serialize_xattr_stream[bxattr]
 *    3. call send_xattr_stream[bxattr]
 *
 * ::os_set_xattr (JCR *jcr, BXATTR_xattr *xattr)
 *
 *    1. set xattr on file using name/value in xattr - lsetxattr[os]
 *    2. if xattr not supported on filesystem - call clear_flag(BXATTR_FLAG_NATIVE)[bxattr]
 *
 * ::os_restore_xattr (JCR *jcr, int stream, char *content, uint32_t length)
 *
 *    1. unserialize backup stream
 *    2. for every extended attribute restored call os_set_xattr[bxattr] to set this attribute on file
 */

#include "bacula.h"
#include "filed.h"
#include "fd_plugins.h"

/* check if XATTR support is enabled */
#if defined(HAVE_XATTR)

/*
 * This is a constructor of the base BXATTR class which is OS independent
 *
 * - for initialization it uses ::init()
 *
 */
BXATTR::BXATTR (){
   init();
};

/*
 * This is a destructor of the BXATTR class
 */
BXATTR::~BXATTR (){
   free_pool_memory(content);
};

/*
 * Initialization routine
 * - initializes all variables to required status
 * - allocates required memory
 */
void BXATTR::init(){

#if defined(HAVE_XATTR)
   xattr_ena = TRUE;
#else
   xattr_ena = FALSE;
#endif

   /* generic variables */
   flags = BXATTR_FLAG_NONE;
   current_dev = 0;
   content = get_pool_memory(PM_BSOCK);   /* it is better to have a 4k buffer */
   content_len = 0;
   xattr_nr_errors = 0;
   xattr_streams = NULL;
   xattr_skiplist = NULL;
   xattr_acl_skiplist = NULL;
};

/*
 * Enables XATTR handling in runtime, could be disabled with disable_xattr
 *    when XATTR is not configured then cannot change status
 */
void BXATTR::enable_xattr(){
#ifdef HAVE_XATTR
   xattr_ena = TRUE;
#endif
};

/*
 * Disables XATTR handling in runtime, could be enabled with enable_xattr
 *    when XATTR is configured
 */
void BXATTR::disable_xattr(){
   xattr_ena = FALSE;
};

/*
 * Copies a text into a content variable and sets a content_len respectively
 *
 * in:
 *    text - a standard null terminated string
 * out:
 *    pointer to content variable to use externally
 */
POOLMEM * BXATTR::set_content(char *text){
   content_len = pm_strcpy(&content, text);
   if (content_len > 0){
      /* count the nul terminated char */
      content_len++;
   }
   // Dmsg2(400, "BXATTR::set_content: %p %i\n", text, content_len);
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
POOLMEM * BXATTR::set_content(char *data, int len){
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
 *    bRC_BXATTR_ok - change of device checked and finish successful
 *    bRC_BXATTR_error - encountered error
 *    bRC_BXATTR_skip - cannot verify device - no file found
 *    bRC_BXATTR_inval - invalid input data
 */
bRC_BXATTR BXATTR::check_dev (JCR *jcr){

   int lst;
   struct stat st;

   /* sanity check of input variables */
   if (jcr == NULL || jcr->last_fname == NULL){
      return bRC_BXATTR_inval;
   }

   lst = lstat(jcr->last_fname, &st);
   switch (lst){
      case -1: {
         berrno be;
         switch (errno){
         case ENOENT:
            return bRC_BXATTR_skip;
         default:
            Mmsg2(jcr->errmsg, _("Unable to stat file \"%s\": ERR=%s\n"), jcr->last_fname, be.bstrerror());
            Dmsg2(100, "Unable to stat file \"%s\": ERR=%s\n", jcr->last_fname, be.bstrerror());
            return bRC_BXATTR_error;
         }
         break;
      }
      case 0:
         break;
   }

   check_dev(jcr, st.st_dev);

   return bRC_BXATTR_ok;
};

/*
 * Check if we changed the device, if so setup a flags
 *
 * in:
 *    jcr - Job Control Record
 * out:
 *    internal flags status set
 */
void BXATTR::check_dev (JCR *jcr, uint32_t dev){

   /* sanity check of input variables */
   if (jcr == NULL || jcr->last_fname == NULL){
      return;
   }

   if (current_dev != dev){
      flags = BXATTR_FLAG_NONE;
      set_flag(BXATTR_FLAG_NATIVE);
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
 *    bRC_BXATTR_inval - when supplied variables are incorrect
 *    bRC_BXATTR_fatal - when we can't send data to the SD
 *    bRC_BXATTR_ok - send finish without errors
 */
bRC_BXATTR BXATTR::send_xattr_stream(JCR *jcr, int stream){

   BSOCK * sd;
   POOLMEM * msgsave;
#ifdef FD_NO_SEND_TEST
   return bRC_BXATTR_ok;
#endif

   /* sanity check of input variables */
   if (jcr == NULL || jcr->store_bsock == NULL){
      return bRC_BXATTR_inval;
   }
   if (content_len <= 0){
      return bRC_BXATTR_ok;
   }

   sd = jcr->store_bsock;
   /* send header */
   if (!sd->fsend("%ld %d 0", jcr->JobFiles, stream)){
      Jmsg1(jcr, M_FATAL, 0, _("Network send error to SD. ERR=%s\n"), sd->bstrerror());
      return bRC_BXATTR_fatal;
   }

   /* send the buffer to the storage daemon */
   Dmsg1(400, "Backing up XATTR: %i\n", content_len);
#if 0
   POOL_MEM tmp(PM_FNAME);
   pm_memcpy(tmp, content, content_len);
   Dmsg2(400, "Backing up XATTR: (%i) <%s>\n", strlen(tmp.addr()), tmp.c_str());
#endif
   msgsave = sd->msg;
   sd->msg = content;
   sd->msglen = content_len;
   if (!sd->send()){
      sd->msg = msgsave;
      sd->msglen = 0;
      Jmsg1(jcr, M_FATAL, 0, _("Network send error to SD. ERR=%s\n"), sd->bstrerror());
      return bRC_BXATTR_fatal;
   }

   jcr->JobBytes += sd->msglen;
   sd->msg = msgsave;
   if (!sd->signal(BNET_EOD)){
      Jmsg1(jcr, M_FATAL, 0, _("Network send error to SD. ERR=%s\n"), sd->bstrerror());
      return bRC_BXATTR_fatal;
   }
   Dmsg1(200, "XATTR of file: %s successfully backed up!\n", jcr->last_fname);
   return bRC_BXATTR_ok;
};

/*
 * The main public backup method for XATTR
 *
 * in:
 *    jcr - Job Control Record
 *    ff_pkt - file backup record
 * out:
 *    bRC_BXATTR_fatal - when XATTR backup is not compiled in Bacula
 *    bRC_BXATTR_ok - backup finish without problems
 *    bRC_BXATTR_error - when you can't backup xattr data because some error
 */
bRC_BXATTR BXATTR::backup_xattr (JCR *jcr, FF_PKT *ff_pkt){

#if !defined(HAVE_XATTR)
   Jmsg(jcr, M_FATAL, 0, "XATTR backup requested but not configured in Bacula.\n");
   return bRC_BXATTR_fatal;
#else
   /* sanity check of input variables and verify if engine is enabled */
   if (xattr_ena && jcr != NULL && ff_pkt != NULL){
      /* xattr engine enabled, proceed */
      bRC_BXATTR rc;

      jcr->errmsg[0] = 0;
      /* check if we have a plugin generated backup */
      if (ff_pkt->cmd_plugin){
         rc = backup_plugin_xattr(jcr, ff_pkt);
      } else {
         /* Check for xattrsupport flag */
         if (!(ff_pkt->flags & FO_XATTR && !ff_pkt->cmd_plugin)){
            return bRC_BXATTR_ok;
         }

         check_dev(jcr, ff_pkt->statp.st_dev);

         if (flags & BXATTR_FLAG_NATIVE){
            Dmsg0(400, "make Native XATTR call\n");
            rc = os_backup_xattr(jcr, ff_pkt);
         } else {
            /* skip xattr backup */
            return bRC_BXATTR_ok;
         }

      }

      if (rc == bRC_BXATTR_error){
         if (xattr_nr_errors < XATTR_MAX_ERROR_PRINT_PER_JOB){
            if (!jcr->errmsg[0]){
               Jmsg(jcr, M_WARNING, 0, "No OS XATTR configured.\n");
            } else {
               Jmsg(jcr, M_WARNING, 0, "%s", jcr->errmsg);
            }
            inc_xattr_errors();
         }
         return bRC_BXATTR_ok;
      }
      return rc;
   }
   return bRC_BXATTR_ok;
#endif
};

/*
 * The main public restore method for XATTR
 *
 * in:
 *    jcr - Job Control Record
 *    stream - a backup stream type number to restore_acl
 *    data - a potinter to the data stream to restore
 *    length - a data stream length
 * out:
 *    bRC_BXATTR_fatal - when XATTR restore is not compiled in Bacula
 *    bRC_BXATTR_ok - restore finish without problems
 *    bRC_BXATTR_error - when you can't restore a stream because some error
 */
bRC_BXATTR BXATTR::restore_xattr (JCR *jcr, int stream, char *data, uint32_t length){

#if !defined(HAVE_XATTR)
   Jmsg(jcr, M_FATAL, 0, "XATTR retore requested but not configured in Bacula.\n");
   return bRC_BXATTR_fatal;
#else
   /* sanity check of input variables and verify if engine is enabled */
   if (xattr_ena && jcr != NULL && data != NULL){
      /* xattr engine enabled, proceed */
      int a;
      bRC_BXATTR rc;

      /* check_dev supported on real fs only */
      if (stream != STREAM_XACL_PLUGIN_XATTR){
         rc = check_dev(jcr);

         switch (rc){
            case bRC_BXATTR_skip:
               return bRC_BXATTR_ok;
            case bRC_BXATTR_ok:
               break;
            default:
               return rc;
         }
      }

      /* copy a data into a content buffer */
      set_content(data, length);

      switch (stream){
         case STREAM_XACL_PLUGIN_XATTR:
            return restore_plugin_xattr(jcr);
         default:
            if (flags & BXATTR_FLAG_NATIVE){
               for (a = 0; xattr_streams[a] > 0; a++){
                  if (xattr_streams[a] == stream){
                     Dmsg0(400, "make Native XATTR call\n");
                     return os_restore_xattr(jcr, stream, content, content_len);
                  }
               }
            } else {
               inc_xattr_errors();
               return bRC_BXATTR_ok;
            }
      }
      /* cannot find a valid stream to support */
      Qmsg2(jcr, M_WARNING, 0, _("Can't restore Extended Attributes of %s - incompatible xattr stream encountered - %d\n"), jcr->last_fname, stream);
      return bRC_BXATTR_error;
   }
   return bRC_BXATTR_ok;
#endif
};

/*
 * Checks if supplied xattr attribute name is indicated on OS specific lists
 *
 * in:
 *    jcr - Job Control Record
 *    ff_pkt - file to backup control package
 *    name - a name of the attribute to check
 * out:
 *    TRUE - the attribute name is found on OS specific skip lists and should be skipped during backup
 *    FALSE - the attribute should be saved on backup stream
 */
bool BXATTR::check_xattr_skiplists (JCR *jcr, FF_PKT *ff_pkt, char * name){

   bool skip = FALSE;
   int count;

   /* sanity check of input variables */
   if (jcr == NULL || ff_pkt == NULL || name ==  NULL){
      return false;
   }

   /*
    * On some OSes you also get the acls in the extented attribute list.
    * So we check if we are already backing up acls and if we do we
    * don't store the extended attribute with the same info.
    */
   if (ff_pkt->flags & FO_ACL){
      for (count = 0; xattr_acl_skiplist[count] != NULL; count++){
         if (bstrcmp(name, xattr_acl_skiplist[count])){
            skip = true;
            break;
         }
      }
   }
   /* on some OSes we want to skip certain xattrs which are in the xattr_skiplist array. */
   if (!skip){
      for (count = 0; xattr_skiplist[count] != NULL; count++){
         if (bstrcmp(name, xattr_skiplist[count])){
            skip = true;
            break;
         }
      }
   }

   return skip;
};


/*
 * Performs generic XATTR backup using OS specific methods for
 * getting xattr data from files - os_get_xattr_names and os_get_xattr_value
 *
 * in:
 *    jcr - Job Control Record
 *    ff_pkt - file to backup control package
 * out:
 *    bRC_BXATTR_ok - xattr backup ok or no xattr to backup found
 *    bRC_BXATTR_error/fatal - an error or fatal error occurred
 *    bRC_BXATTR_inval - input variables was invalid
 */
bRC_BXATTR BXATTR::generic_backup_xattr (JCR *jcr, FF_PKT *ff_pkt){

   bRC_BXATTR rc;
   POOLMEM *xlist;
   uint32_t xlen;
   char *name;
   uint32_t name_len;
   POOLMEM *value;
   uint32_t value_len;
   bool skip;
   alist *xattr_list = NULL;
   int xattr_count = 0;
   uint32_t len = 0;
   BXATTR_xattr *xattr;

   /* sanity check of input variables */
   if (jcr == NULL || ff_pkt == NULL){
      return bRC_BXATTR_inval;
   }

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

   /* follow the list of xattr names and get the values
    * TODO: change a standard NULL-terminated list of names into alist of structures */
   for (name = xlist; (name - xlist) + 1 < xlen; name = strchr(name, '\0') + 1){

      name_len = strlen(name);
      skip =  check_xattr_skiplists(jcr, ff_pkt, name);
      if (skip || name_len == 0){
         Dmsg1(100, "Skipping xattr named \"%s\"\n", name);
         continue;
      }

      /* value is allocated as POOLMEM by os_get_xattr_value */
      rc = os_get_xattr_value(jcr, name, &value, &value_len);
      switch (rc){
         case bRC_BXATTR_ok:
            /* it's ok, so go further */
            break;
         case bRC_BXATTR_skip:
            /* no xattr available, so skip rest of it */
            free_pool_memory(xlist);
            return bRC_BXATTR_ok;
         default:
            /* error / fatal */
            free_pool_memory(xlist);
            return rc;
      }

      /*
       * we have a name of the extended attribute in the name variable
       * and value of the extended attribute in the value variable
       * so we need to build a list
       */
      xattr = (BXATTR_xattr*)malloc(sizeof(BXATTR_xattr));
      xattr->name_len = name_len;
      xattr->name = name;
      xattr->value_len = value_len;
      xattr->value = value;
      /*       magic              name_len          name        value_len       value */
      len += sizeof(uint32_t) + sizeof(uint32_t) + name_len + sizeof(uint32_t) + value_len;

      if (xattr_list == NULL){
         xattr_list = New(alist(10, not_owned_by_alist));
      }
      xattr_list->append(xattr);
      xattr_count++;
   }
   if (xattr_count > 0){
      /* serialize the stream */
      rc = serialize_xattr_stream(jcr, len, xattr_list);
      if (rc != bRC_BXATTR_ok){
         Mmsg(jcr->errmsg, _("Failed to serialize extended attributes on file \"%s\"\n"), jcr->last_fname);
         Dmsg1(100, "Failed to serialize extended attributes on file \"%s\"\n", jcr->last_fname);
         goto bailout;
      } else {
         /* send data to SD */
         rc = send_xattr_stream(jcr, xattr_streams[0]);
      }
   } else {
      rc = bRC_BXATTR_ok;
   }

bailout:
   /* free allocated data */
   if (xattr_list != NULL){
      foreach_alist(xattr, xattr_list){
         if (xattr == NULL){
            break;
         }
         if (xattr->value){
            free_pool_memory(xattr->value);
         }
         free(xattr);
      }
      delete xattr_list;
   }
   if (xlist != NULL){
      free_pool_memory(xlist);
   }

   return rc;
};

/*
 * Performs a generic XATTR restore using OS specific methods for
 * setting XATTR data on file.
 *
 * in:
 *    jcr - Job Control Record
 *    stream - a stream number to restore
 * out:
 *    bRC_BXATTR_ok - restore of acl's was successful
 *    bRC_BXATTR_error - was an error during xattr restore
 *    bRC_BXATTR_fatal - was a fatal error during xattr restore
 *    bRC_BXATTR_inval - input variables was invalid
 */
bRC_BXATTR BXATTR::generic_restore_xattr (JCR *jcr, int stream){

   bRC_BXATTR rc = bRC_BXATTR_ok;
   alist *xattr_list;
   BXATTR_xattr *xattr;

   /* sanity check of input variables */
   if (jcr == NULL){
      return bRC_BXATTR_inval;
   }

   /* empty list */
   xattr_list = New(alist(10, not_owned_by_alist));

   /* unserialize data */
   unserialize_xattr_stream(jcr, content, content_len, xattr_list);

   /* follow the list to set all attributes */
   foreach_alist(xattr, xattr_list){
      rc = os_set_xattr(jcr, xattr);
      if (rc != bRC_BXATTR_ok){
         Dmsg2(100, "Failed to set extended attribute %s on file \"%s\"\n", xattr->name, jcr->last_fname);
         goto bailout;
      }
   }

bailout:
   /* free allocated data */
   if (xattr_list != NULL){
      foreach_alist(xattr, xattr_list){
         if (xattr == NULL){
            break;
         }
         if (xattr->name){
            free(xattr->name);
         }
         if (xattr->value){
            free(xattr->value);
         }
         free(xattr);
      }
      delete xattr_list;
   }
   return rc;
};

/*
 * Perform a generic XATTR backup using a plugin. It calls the plugin API to
 * get required xattr data from plugin.
 *
 * in:
 *    jcr - Job Control Record
 *    ff_pkt - file to backup control package
 * out:
 *    bRC_BXATTR_ok - backup of xattrs was successful
 *    bRC_BXATTR_fatal - was an error during xattr backup
 */
bRC_BXATTR BXATTR::backup_plugin_xattr (JCR *jcr, FF_PKT *ff_pkt)
{
   int status;
   char *data;

   /* sanity check of input variables */
   if (jcr == NULL || ff_pkt == NULL){
      return bRC_BXATTR_inval;
   }

   while ((status = plugin_backup_xattr(jcr, ff_pkt, &data)) > 0){
      /* data is a plugin buffer which contains data to backup
       * and status is a length of the buffer when > 0 */
      set_content(data, status);
      if (send_xattr_stream(jcr, STREAM_XACL_PLUGIN_XATTR) == bRC_BXATTR_fatal){
         return bRC_BXATTR_fatal;
      }
   }
   if (status < 0){
      /* error */
      return bRC_BXATTR_error;
   }

   return bRC_BXATTR_ok;
};

/*
 * Perform a generic XATTR restore using a plugin. It calls the plugin API to
 * send acl data to plugin.
 *
 * in:
 *    jcr - Job Control Record
 *    stream - a stream number to restore
 * out:
 *    bRC_BXATTR_ok - restore of xattrs was successful
 *    bRC_BXATTR_error - was an error during xattrs restore
 *    bRC_BXATTR_fatal - was a fatal error during xattrs restore or input data
 *                     is invalid
 */
bRC_BXATTR BXATTR::restore_plugin_xattr (JCR *jcr)
{
   /* sanity check of input variables */
   if (jcr == NULL){
      return bRC_BXATTR_inval;
   }

   if (!plugin_restore_xattr(jcr, content, content_len)){
      /* error */
      return bRC_BXATTR_error;
   }

   return bRC_BXATTR_ok;
}

/*
 * Initialize a variable xattr_streams for a specified OS.
 * The rutine should be called from object instance constructor
 *
 * in:
 *    pxattr - xattr streams supported for specific OS
 */
void BXATTR::set_xattr_streams (const int *pxattr){

   xattr_streams = pxattr;
};

/*
 * Initialize variables xattr_skiplist and xattr_acl_skiplist for a specified OS.
 * The rutine should be called from object instance constructor
 *
 * in:
 *    pxattr - xattr skip list for specific OS
 *    pxattr_acl - xattr acl names skip list for specific OS
 */
void BXATTR::set_xattr_skiplists (const char **pxattr, const char **pxattr_acl){

   xattr_skiplist = pxattr;
   xattr_acl_skiplist = pxattr_acl;
};

/*
 * Serialize the XATTR stream which will be saved into archive. Serialization elements cames from
 * a list and for backward compatibility we produce the same stream as prievous Bacula versions.
 *
 * serialized stream consists of the following elements:
 *    magic - A magic string which makes it easy to detect any binary incompatabilites
 *             required for backward compatibility
 *    name_len - The length of the following xattr name
 *    name - The name of the extended attribute
 *    value_len - The length of the following xattr data
 *    value - The actual content of the extended attribute only if value_len is greater then zero
 *
 * in:
 *    jcr - Job Control Record
 *    len - expected serialize length
 *    list - a list of xattr elements to serialize
 * out:
 *    bRC_BXATTR_ok - when serialization was perfect
 *    bRC_BXATTR_inval - when we have invalid variables
 *    bRC_BXATTR_error - illegal attribute name
 */
bRC_BXATTR BXATTR::serialize_xattr_stream(JCR *jcr, uint32_t len, alist *list){

   ser_declare;
   BXATTR_xattr *xattr;

   /* sanity check of input variables */
   if (jcr == NULL || list == NULL){
      return bRC_BXATTR_inval;
   }

   /* we serialize data direct to content buffer, so check if data fits */
   content = check_pool_memory_size(content, len + 20);
   ser_begin(content, len + 20);

   foreach_alist(xattr, list){
      if (xattr == NULL){
         break;
      }
      /*
       * serialize data
       *
       * we have to start with the XATTR_MAGIC for backward compatibility (the magic is silly)
       */
      ser_uint32(XATTR_MAGIC);
      /* attribute name length and name itself */
      if (xattr->name_len > 0 && xattr->name){
         ser_uint32(xattr->name_len);
         ser_bytes(xattr->name, xattr->name_len);
      } else {
         /* error - name cannot be empty */
         Mmsg0(jcr->errmsg, _("Illegal empty xattr attribute name\n"));
         Dmsg0(100, "Illegal empty xattr attribute name\n");
         return bRC_BXATTR_error;
      }
      /* attibute value length and value itself */
      ser_uint32(xattr->value_len);
      if (xattr->value_len > 0 && xattr->value){
         ser_bytes(xattr->value, xattr->value_len);
         Dmsg3(100, "Backup xattr named %s, value %*.s\n", xattr->name, xattr->value_len, xattr->value);
      } else {
         Dmsg1(100, "Backup empty xattr named %s\n", xattr->name);
      }
   }

   ser_end(content, len + 20);
   content_len = ser_length(content);

   return bRC_BXATTR_ok;
};

/*
 * Unserialize XATTR stream on *content and produce a xattr *list which contain
 * key => value pairs
 *
 * in:
 *    jcr - Job Control Record
 *    content - a stream content to unserialize
 *    length - a content length
 *    list - a pointer to the xattr list to populate
 * out:
 *    bRC_BXATTR_ok - when unserialize was perfect
 *    bRC_BXATTR_inval - when we have invalid variables
 *    list - key/value pairs populated xattr list
 */
bRC_BXATTR BXATTR::unserialize_xattr_stream(JCR *jcr, char *content, uint32_t length, alist *list){

   unser_declare;
   uint32_t magic;
   BXATTR_xattr *xattr;

   /* sanity check of input variables */
   if (jcr == NULL || content == NULL || list == NULL){
      return bRC_BXATTR_inval;
   }

   unser_begin(content, length);
   while (unser_length(content) < length){
      /*
       * Sanity check of correct stream magic number
       * Someone was too paranoid to implement this kind of verification in original Bacula code
       * Unfortunate for backward compatibility we have to follow this insane implementation
       *
       * XXX: design a new xattr stream format
       */
      unser_uint32(magic);
      if (magic != XATTR_MAGIC){
         Mmsg(jcr->errmsg, _("Illegal xattr stream, no XATTR_MAGIC on file \"%s\"\n"), jcr->last_fname);
         Dmsg1(100, "Illegal xattr stream, no XATTR_MAGIC on file \"%s\"\n", jcr->last_fname);
         return bRC_BXATTR_error;
      }
      /* first attribute name length */
      xattr = (BXATTR_xattr *)malloc(sizeof(BXATTR_xattr));
      unser_uint32(xattr->name_len);
      if (xattr->name_len == 0){
         /* attribute name cannot be empty */
         Mmsg(jcr->errmsg, _("Illegal xattr stream, xattr name length <= 0 on file \"%s\"\n"), jcr->last_fname);
         Dmsg1(100, "Illegal xattr stream, xattr name length <= 0 on file \"%s\"\n", jcr->last_fname);
         free(xattr);
         return bRC_BXATTR_error;
      }
      /* followed by attribute name itself */
      xattr->name = (char *)malloc(xattr->name_len + 1);
      unser_bytes(xattr->name, xattr->name_len);
      xattr->name[xattr->name_len] = '\0';
      /* attribute value */
      unser_uint32(xattr->value_len);
      if (xattr->value_len > 0){
         /* we have a value */
         xattr->value = (char *)malloc(xattr->value_len + 1);
         unser_bytes(xattr->value, xattr->value_len);
         xattr->value[xattr->value_len] = '\0';
         Dmsg3(100, "Restoring xattr named %s, value %.*s\n", xattr->name, xattr->value_len, xattr->value);
      } else {
         /* value is empty */
         xattr->value = NULL;
         Dmsg1(100, "Restoring empty xattr named %s\n", xattr->name);
      }
      list->append(xattr);
   }
   unser_end(content, length);

   return bRC_BXATTR_ok;
};

#include "bxattr_osx.h"
#include "bxattr_linux.h"
#include "bxattr_freebsd.h"
#include "bxattr_solaris.h"
// #include "bxattr_aix.h"

/*
 * Creating the current instance of the BXATTR for a supported OS
 */
void *new_bxattr()
{
#if   defined(HAVE_DARWIN_OS)
   return new BXATTR_OSX();
#elif defined(HAVE_LINUX_OS)
   return new BXATTR_Linux();
#elif defined(HAVE_FREEBSD_OS)
   return new BXATTR_FreeBSD();
#elif defined(HAVE_HURD_OS)
   return new BXATTR_Hurd();
#elif defined(HAVE_AIX_OS)
   return new BXATTR_AIX();
#elif defined(HAVE_IRIX_OS)
   return new BXATTR_IRIX();
#elif defined(HAVE_OSF1_OS)
   return new BXATTR_OSF1();
#elif defined(HAVE_SUN_OS)
   return new BXATTR_Solaris();
#else
   return NULL;
#endif
};

#endif /* HAVE_XATTR */
