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

#ifndef __BACL_H_
#define __BACL_H_

/* check if ACL support is enabled */
#if defined(HAVE_ACL)

/*
 * Return value status enumeration
 * You have an error when value is less then zero.
 * You have a positive status when value is not negative
 * (greater or equal to zero).
 */
enum bRC_BACL {
   bRC_BACL_inval = -3,       // input data invalid
   bRC_BACL_fatal = -2,       // a fatal error
   bRC_BACL_error = -1,       // standard error
   bRC_BACL_ok    = 0,        // success
   bRC_BACL_skip  = 1,        // processing should skip current runtime
   bRC_BACL_cont  = 2         // processing should skip current element
                              // and continue with next one
};

/*
 * We support the following types of ACLs
 */
typedef enum {
   BACL_TYPE_NONE             = 0,
   BACL_TYPE_ACCESS           = 1,
   BACL_TYPE_DEFAULT          = 2,
   BACL_TYPE_DEFAULT_DIR      = 3,
   BACL_TYPE_EXTENDED         = 4,
   BACL_TYPE_NFS4             = 5,
   BACL_TYPE_PLUGIN           = 6
} BACL_type;

/*
 * Flags which control what ACL engine to use for backup/restore
 */
#define BACL_FLAG_NONE        0
#define BACL_FLAG_NATIVE      0x01
#define BACL_FLAG_AFS         0x02
#define BACL_FLAG_PLUGIN      0x04

/*
 * Ensure we have none
 */
#ifndef ACL_TYPE_NONE
#define ACL_TYPE_NONE         0x0
#endif

/*
 * Basic ACL class which is a foundation for any other OS specific implementation.
 *
 * This class cannot be used directly as it is an abstraction class with a lot
 * of virtual methods laying around. As a basic class it has all public API
 * available for backup and restore functionality. As a bonus it handles all
 * ACL generic functions and OS independent API, i.e. for AFS ACL or Plugins ACL
 * (future functionality).
 */
class BACL {
private:
   bool acl_ena;
   uint32_t flags;
   uint32_t current_dev;
   POOLMEM *content;
   uint32_t content_len;
   uint32_t acl_nr_errors;
   const int *acl_streams;
   const int *default_acl_streams;
   const char **xattr_skiplist;
   const char **xattr_acl_skiplist;

   void init();

   /**
    * Perform OS specific ACL backup.
    * in:
    *    jcr - Job Control Record
    *    ff_pkt - file to backup information rector
    * out:
    *    bRC_BACL_ok - backup performed without problems
    *    any other - some error occurred
    */
   virtual bRC_BACL os_backup_acl (JCR *jcr, FF_PKT *ff_pkt){return bRC_BACL_fatal;};

   /**
    * Perform OS specific ACL restore. Runtime is called only when stream is supported by OS.
    * in:
    *    jcr - Job Control Record
    *    ff_pkt - file to backup information rector
    * out:
    *    bRC_BACL_ok - backup performed without problems
    *    any other - some error occurred
    */
   virtual bRC_BACL os_restore_acl (JCR *jcr, int stream, char *content, uint32_t length){return bRC_BACL_fatal;};

   /**
    * Low level OS specific runtime to get ACL data from file. The ACL data is set in internal content buffer.
    *
    * in:
    *    jcr - Job Control Record
    *    bacltype - the acl type to restore
    * out:
    *    bRC_BACL_ok -
    *    bRC_BACL_error/fatal - an error or fatal error occurred
    */
   virtual bRC_BACL os_get_acl (JCR *jcr, BACL_type bacltype){return bRC_BACL_fatal;};

   /**
    * Low level OS specific runtime to set ACL data on file.
    *
    * in:
    *    jcr - Job Control Record
    *    bacltype - the acl type to restore
    *    content - a buffer with data to restore
    *    length - a data restore length
    * out:
    *    bRC_BACL_ok -
    *    bRC_BACL_error/fatal - an error or fatal error occurred
    */
   virtual bRC_BACL os_set_acl (JCR *jcr, BACL_type bacltype, char *content, uint32_t length){return bRC_BACL_fatal;};

   void inc_acl_errors(){ acl_nr_errors++;};
   bRC_BACL check_dev (JCR *jcr);
   void check_dev (JCR *jcr, uint32_t dev);

public:
   BACL ();
   virtual ~BACL();

   /* enable/disable functionality */
   void enable_acl();
   void disable_acl();

   /*
    * public methods used outside the class or derivatives
    */
   bRC_BACL backup_acl (JCR *jcr, FF_PKT *ff_pkt);
   bRC_BACL restore_acl (JCR *jcr, int stream, char *content, uint32_t content_length);

   /* utility functions */
   inline uint32_t get_acl_nr_errors(){ return acl_nr_errors;};
   void set_acl_streams (const int *pacl, const int *pacl_def);
   inline void clear_flag (uint32_t flag){ flags &= ~flag;};
   inline void set_flag (uint32_t flag){ flags |= flag;};
   POOLMEM * set_content (char *text);
   POOLMEM * set_content(char *data, int len);
   inline POOLMEM * get_content (void){ return content;};
   inline uint32_t get_content_size (void){ return sizeof_pool_memory(content);};
   inline uint32_t get_content_len (void){ return content_len;};

   /* sending data to the storage */
   bRC_BACL send_acl_stream (JCR *jcr, int stream);

   /* generic functions */
   bRC_BACL generic_backup_acl (JCR *jcr, FF_PKT *ff_pkt);
   bRC_BACL generic_restore_acl (JCR *jcr, int stream);
   bRC_BACL afs_backup_acl (JCR *jcr, FF_PKT *ff_pkt);
   bRC_BACL afs_restore_acl (JCR *jcr, int stream);
   bRC_BACL backup_plugin_acl (JCR *jcr, FF_PKT *ff_pkt);
   bRC_BACL restore_plugin_acl (JCR *jcr);
};

void *new_bacl();

#endif /* HAVE_ACL */

#endif /* __BACL_H_ */
