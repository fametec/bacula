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
/*
 * BootStrap record definition -- for restoring files.
 *
 *    Kern Sibbald, June 2002
 *
 */


#ifndef __BSR_H
#define __BSR_H 1

#ifndef HAVE_REGEX_H
#include "lib/bregex.h"
#else
#include <regex.h>
#endif

/*
 * List of Volume names to be read by Storage daemon.
 *  Formed by Storage daemon from BSR
 */
struct VOL_LIST {
   VOL_LIST *next;
   char VolumeName[MAX_NAME_LENGTH];
   char MediaType[MAX_NAME_LENGTH];
   char device[MAX_NAME_LENGTH];   /* ***FIXME*** use alist here */
   int Slot;
   uint32_t start_file;
};


/*
 * !!!!!!!!!!!!!!!!!! NOTE !!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * !!!                                               !!!
 * !!!   All records must have a pointer to          !!!
 * !!!   the next item as the first item defined.    !!!
 * !!!                                               !!!
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 */

struct BSR_VOLUME {
   BSR_VOLUME *next;
   char VolumeName[MAX_NAME_LENGTH];
   char MediaType[MAX_NAME_LENGTH];
   char device[MAX_NAME_LENGTH];   /* ***FIXME*** use alist here */
   int32_t       Slot;                /* Slot */
};

struct BSR_CLIENT {
   BSR_CLIENT *next;
   char ClientName[MAX_NAME_LENGTH];
};

struct BSR_SESSID {
   BSR_SESSID *next;
   uint32_t sessid;
   uint32_t sessid2;
};

struct BSR_SESSTIME {
   BSR_SESSTIME *next;
   uint32_t sesstime;
   bool done;                         /* local done */
};

struct BSR_VOLFILE {
   BSR_VOLFILE *next;
   uint32_t sfile;                    /* start file */
   uint32_t efile;                    /* end file */
   bool done;                         /* local done */
};

struct BSR_VOLBLOCK {
   BSR_VOLBLOCK *next;
   uint32_t sblock;                   /* start block */
   uint32_t eblock;                   /* end block */
   bool done;                         /* local done */
};

struct BSR_VOLADDR {
   BSR_VOLADDR *next;
   uint64_t saddr;                   /* start address */
   uint64_t eaddr;                   /* end address */
   bool done;                        /* local done */
};

struct BSR_FINDEX {
   BSR_FINDEX *next;
   int32_t findex;                    /* start file index */
   int32_t findex2;                   /* end file index */
   bool done;                         /* local done */
};

struct BSR_JOBID {
   BSR_JOBID *next;
   uint32_t JobId;
   uint32_t JobId2;
};

struct BSR_JOBTYPE {
   BSR_JOBTYPE *next;
   uint32_t JobType;
};

struct BSR_JOBLEVEL {
   BSR_JOBLEVEL *next;
   uint32_t JobLevel;
};

struct BSR_JOB {
   BSR_JOB *next;
   char Job[MAX_NAME_LENGTH];
   bool done;                         /* local done */
};

struct BSR_STREAM {
   BSR_STREAM *next;
   int32_t stream;                    /* stream desired */
};

struct BSR {
   /* NOTE!!! next must be the first item */
   BSR          *next;                /* pointer to next one */
   BSR          *prev;                /* pointer to previous one */
   BSR          *root;                /* root bsr */
   BSR          *cur_bsr;
   bool          reposition;          /* set when any bsr is marked done */
   bool          mount_next_volume;   /* set when next volume should be mounted */
   bool          done;                /* set when everything found for this bsr */
   bool          use_fast_rejection;  /* set if fast rejection can be used */
   bool          use_positioning;     /* set if we can position the archive */
   bool          skip_file;           /* skip all records for current file */
   BSR_VOLUME   *volume;
   uint32_t      count;               /* count of files to restore this bsr */
   uint32_t      found;               /* count of restored files this bsr */
   int32_t       LastFI;              /* LastFI seen by this bsr */
   BSR_VOLFILE  *volfile;
   BSR_VOLBLOCK *volblock;
   BSR_VOLADDR  *voladdr;
   BSR_SESSTIME *sesstime;
   BSR_SESSID   *sessid;
   BSR_JOBID    *JobId;
   BSR_JOB      *job;
   BSR_CLIENT   *client;
   BSR_FINDEX   *FileIndex;
   BSR_JOBTYPE  *JobType;
   BSR_JOBLEVEL *JobLevel;
   BSR_STREAM   *stream;
   char         *fileregex;           /* set if restore is filtered on filename */
   regex_t      *fileregex_re;
   ATTR         *attr;                /* scratch space for unpacking */
};


#endif
