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
 *   attr.h Definition of attributes packet for unpacking from tape
 *
 *    Kern Sibbald, June MMIII
 *
 */

#ifndef __ATTR_H_
#define __ATTR_H_ 1


struct ATTR {
   int32_t stream;                    /* attribute stream id */
   int32_t data_stream;               /* id of data stream to follow */
   int32_t type;                      /* file type FT */
   int32_t file_index;                /* file index */
   int32_t LinkFI;                    /* file index to data if hard link */
   int32_t delta_seq;                 /* delta sequence numbr */
   uid_t uid;                         /* userid */
   struct stat statp;                 /* decoded stat packet */
   POOLMEM *attrEx;                   /* extended attributes if any */
   POOLMEM *ofname;                   /* output filename */
   POOLMEM *olname;                   /* output link name */
   /*
    * Note the following three variables point into the
    *  current BSOCK record, so they are invalid after
    *  the next socket read!
    */
   char *attr;                        /* attributes position */
   char *fname;                       /* filename */
   char *lname;                       /* link name if any */
   JCR *jcr;                          /* jcr pointer */
};

#endif /* __ATTR_H_ */
