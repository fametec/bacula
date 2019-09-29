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

#ifndef __BXATTR_H_
#define __BXATTR_H_

/* check if XATTR support is enabled */
#if defined(HAVE_XATTR)

/*
 * Magic used in the magic field of the xattr struct.
 * This way we can see if we encounter a valid xattr struct.
 * Used for backward compatibility only.
 */
#define XATTR_MAGIC 0x5C5884

/*
 * Return value status enumeration
 * You have an error when value is less then zero.
 * You have a positive status when value is not negative
 * (greater or equal to zero).
 */
enum bRC_BXATTR {
   bRC_BXATTR_inval = -3,       // input data invalid
   bRC_BXATTR_fatal = -2,       // a fatal error
   bRC_BXATTR_error = -1,       // standard error
   bRC_BXATTR_ok    = 0,        // success
   bRC_BXATTR_skip  = 1,        // processing should skip current runtime
   bRC_BXATTR_cont  = 2         // processing should skip current element
                                // and continue with next one
};

/*
 * Flags which control what XATTR engine
 * to use for backup/restore
 */
#define BXATTR_FLAG_NONE        0
#define BXATTR_FLAG_NATIVE      0x01
#define BXATTR_FLAG_AFS         0x02
#define BXATTR_FLAG_PLUGIN      0x04

/*
 * Extended attribute (xattr) list element.
 *
 * Every xattr consist of a Key=>Value pair where
 * both could be a binary data.
 */
struct BXATTR_xattr {
   uint32_t name_len;
   char *name;
   uint32_t value_len;
   char *value;
};

/*
 * Basic XATTR class which is a foundation for any other OS specific implementation.
 *
 * This class cannot be used directly as it is an abstraction class with a lot of virtual
 * methods laying around. As a basic class it has all public API available for backup and
 * restore functionality. As a bonus it handles all XATTR generic functions and OS
 * independent API, i.e. for AFS XATTR or Plugins XATTR (future functionality).
 */
class BXATTR {
private:
   bool xattr_ena;
   uint32_t flags;
   uint32_t current_dev;
   POOLMEM *content;
   uint32_t content_len;
   uint32_t xattr_nr_errors;
   const int *xattr_streams;
   const char **xattr_skiplist;
   const char **xattr_acl_skiplist;

   void init();

   /**
    * Perform OS specific XATTR backup.
    *
    * in:
    *    jcr - Job Control Record
    *    ff_pkt - file to backup control package
    * out:
    *    bRC_BXATTR_ok - xattr backup ok or no xattr to backup found
    *    bRC_BXATTR_error/fatal - an error or fatal error occurred
    */
   virtual bRC_BXATTR os_backup_xattr (JCR *jcr, FF_PKT *ff_pkt){return bRC_BXATTR_fatal;};

   /**
    * Perform OS specific XATTR restore. Runtime is called only when stream is supported by OS.
    *
    * in:
    *	 jcr - Job Control Record
    *	 stream - backup stream number
    *	 content - a buffer with data to restore
    *	 length - a data restore length
    * out:
    *	 bRC_BXATTR_ok - xattr backup ok or no xattr to backup found
    *	 bRC_BXATTR_error/fatal - an error or fatal error occurred
    */
   virtual bRC_BXATTR os_restore_xattr (JCR *jcr, int stream, char *content, uint32_t length){return bRC_BXATTR_fatal;};

   /**
    * Returns a list of xattr names in newly allocated pool memory and a length of the allocated buffer.
    * It allocates a memory with poolmem subroutines every time a function is called, so it must be freed
    * when not needed. The list of xattr names is returned as an unordered array of NULL terminated
    * character strings (attribute names are separated by NULL characters), like this:
    *  user.name1\0system.name1\0user.name2\0
    * The format of the list is based on standard "llistxattr" function call.
    * TODO: change the format of the list from an array of NULL terminated strings into an alist of structures.
    *
    * in:
    *    jcr - Job Control Record
    *    xlen - non NULL pointer to the uint32_t variable for storing a length of the xattr names list
    *    pxlist - non NULL pointer to the char* variable for allocating a memoty data for xattr names list
    * out:
    *    bRC_BXATTR_ok - we've got a xattr data to backup
    *    bRC_BXATTR_skip - no xattr data available, no fatal error, skip rest of the runtime
    *    bRC_BXATTR_fatal - when required buffers are unallocated
    *    bRC_BXATTR_error - in case of any error
    */
   virtual bRC_BXATTR os_get_xattr_names (JCR *jcr, POOLMEM ** pxlist, uint32_t * xlen){return bRC_BXATTR_fatal;};

   /**
    * Returns a value of the requested attribute name and a length of the allocated buffer.
    * It allocates a memory with poolmem subroutines every time a function is called, so it must be freed
    * when not needed.
    *
    * in:
    *    jcr - Job Control Record
    *    name - a name of the extended attribute
    *    pvalue - the pointer for the buffer with value - it is allocated by function and should be freed when no needed
    *    plen - the pointer for the length of the allocated buffer
    *
    * out:
    *    pxlist - the atributes list
    *    bRC_BXATTR_ok - we've got a xattr data which could be empty when xlen=0
    *    bRC_BXATTR_skip - no xattr data available, no fatal error, skip rest of the runtime
    *    bRC_BXATTR_error - error getting an attribute
    *    bRC_BXATTR_fatal - required buffers are unallocated
    */
   virtual bRC_BXATTR os_get_xattr_value (JCR *jcr, char * name, char ** pvalue, uint32_t * plen){return bRC_BXATTR_fatal;};

   /**
    * Low level OS specific runtime to set extended attribute on file
    *
    * in:
    *    jcr - Job Control Record
    *    xattr - the struct with attribute/value to set
    *
    * out:
    *    bRC_BXATTR_ok - setting the attribute was ok
    *    bRC_BXATTR_error - error during extattribute set
    *    bRC_BXATTR_fatal - required buffers are unallocated
    */
   virtual bRC_BXATTR os_set_xattr (JCR *jcr, BXATTR_xattr *xattr){return bRC_BXATTR_fatal;};

   void inc_xattr_errors(){ xattr_nr_errors++;};
   bRC_BXATTR check_dev (JCR *jcr);
   void check_dev (JCR *jcr, uint32_t dev);

public:
   BXATTR ();
   virtual ~BXATTR();

   /* enable/disable functionality */
   void enable_xattr();
   void disable_xattr();

   /*
    * public methods used outside the class or derivatives
    */
   bRC_BXATTR backup_xattr (JCR *jcr, FF_PKT *ff_pkt);
   bRC_BXATTR restore_xattr (JCR *jcr, int stream, char *content, uint32_t content_length);

   /* utility functions */
   inline uint32_t get_xattr_nr_errors(){ return xattr_nr_errors;};
   void set_xattr_streams (const int *pxattr);
   void set_xattr_skiplists (const char **pxattr, const char **pxattr_acl);
   inline void clear_flag (uint32_t flag){ flags &= ~flag;};
   inline void set_flag (uint32_t flag){ flags |= flag;};
   POOLMEM * set_content (char *text);
   POOLMEM * set_content(char *data, int len);
   inline POOLMEM * get_content (void){ return content;};
   inline uint32_t get_content_size (void){ return sizeof_pool_memory(content);};
   inline uint32_t get_content_len (void){ return content_len;};
   bool check_xattr_skiplists (JCR *jcr, FF_PKT *ff_pkt, char * name);

   /* sending data to the storage */
   bRC_BXATTR send_xattr_stream (JCR *jcr, int stream);

   /* serialize / unserialize stream */
   bRC_BXATTR unserialize_xattr_stream(JCR *jcr, char *content, uint32_t length, alist *list);
   bRC_BXATTR serialize_xattr_stream(JCR *jcr, uint32_t len, alist *list);

   /* generic functions */
   bRC_BXATTR generic_backup_xattr (JCR *jcr, FF_PKT *ff_pkt);
   bRC_BXATTR generic_restore_xattr (JCR *jcr, int stream);
   bRC_BXATTR backup_plugin_xattr (JCR *jcr, FF_PKT *ff_pkt);
   bRC_BXATTR restore_plugin_xattr (JCR *jcr);
};

void *new_bxattr();

#endif /* HAVE_XATTR */

#endif /* __BXATTR_H_ */
