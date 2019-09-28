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
/*
 * Block definitions for Bacula media data format.
 *
 *    Kern Sibbald, MM
 */


#ifndef __BLOCK_H
#define __BLOCK_H 1

#define MAX_BLOCK_SIZE  20000000        /* this is a sort of sanity check */
#define DEFAULT_BLOCK_SIZE (512 * 126)  /* 64,512 N.B. do not use 65,636 here */
#define MIN_DEDUP_BLOCK_SIZE (512 * 2)  /* Minimum block (bucket) size */

#define DEDUP_BLOCK_SIZE (512 * 128)    /* For now use a fixed dedup block size */

/* Block Header definitions. */
#define BLKHDR1_ID       "BB01"
#define BLKHDR2_ID       "BB02"
#define BLKHDR_ID_LENGTH  4
#define BLKHDR_CS_LENGTH  4             /* checksum length */
#define BLKHDR1_LENGTH   16             /* Total length */
#define BLKHDR2_LENGTH   24             /* Total length */

#define WRITE_BLKHDR_ID     BLKHDR2_ID
#define WRITE_BLKHDR_LENGTH BLKHDR2_LENGTH
#define WRITE_ADATA_BLKHDR_LENGTH (6*sizeof(int32_t)+sizeof(uint64_t))
#define BLOCK_VER               2

/* Record header definitions */
#define RECHDR1_LENGTH      20
/*
 * Record header consists of:
 *  int32_t FileIndex
 *  int32_t Stream
 *  uint32_t data_length
 *  uint64_t FileOffset (if offset_stream)
 */
#define RECHDR2_LENGTH  (3*sizeof(int32_t))
#define WRITE_RECHDR_LENGTH RECHDR2_LENGTH

/*
 * An adata record header includes:
 *  int32_t FileIndex
 *  int32_t Stream      STREAM_ADATA_RECORD_HEADER
 *  uint32_t data_length
 *  uint32_t block length (binbuf to that point in time)
 *  int32_t Stream (original stream)
 */
#define WRITE_ADATA_RECHDR_LENGTH  (5*sizeof(int32_t))

/* Tape label and version definitions */
#define BaculaId         "Bacula 1.0 immortal\n"
#define OldBaculaId      "Bacula 0.9 mortal\n"
#define BaculaTapeVersion                11
#define OldCompatibleBaculaTapeVersion1  10
#define OldCompatibleBaculaTapeVersion2   9

#define BaculaMetaDataId     "Bacula 1.0 Metadata\n"
#define BaculaAlignedDataId  "Bacula 1.0 Aligned Data\n"
#define BaculaMetaDataVersion         10000
#define BaculaAlignedDataVersion      20000

#define BaculaDedupMetaDataId "Bacula 1.0 Dedup Metadata\n"
#define BaculaDedupMetaDataVersion    30000

#define BaculaS3CloudId "Bacula 1.0 S3 Cloud Data\n"
#define BaculaS3CloudVersion          40000

/*
 * This is the Media structure for a block header
 *  Note, when written, it is serialized.
   16 bytes

   uint32_t CheckSum;
   uint32_t block_len;
   uint32_t BlockNumber;
   char     Id[BLKHDR_ID_LENGTH];

 * for BB02 block, we have
   24 bytes

   uint32_t CheckSum;
   uint32_t block_len;
   uint32_t BlockNumber;
   char     Id[BLKHDR_ID_LENGTH];
   uint32_t VolSessionId;
   uint32_t VolSessionTime;

 * for an adata block header (in ameta file), we have
   32 bytes

   uint32_t BlockNumber;
   int32_t  Stream;   STREAM_ADATA_BLOCK_HEADER
   uint32_t block_len;
   uint32_t CheckSum;
   uint32_t VolSessionId;
   uint32_t VolSessionTime;
   uint64_t BlockAddr;

 */

class DEVICE;                         /* for forward reference */

/*
 * DEV_BLOCK for reading and writing blocks.
 * This is the basic unit that is written to the device, and
 * it contains a Block Header followd by Records.  Note,
 * at times (when reading a file), this block may contain
 * multiple blocks.
 *
 *  This is the memory structure for a device block.
 */
struct DEV_BLOCK {
   DEV_BLOCK *next;                   /* pointer to next one */
   DEVICE    *dev;                    /* pointer to device */
   /* binbuf is the number of bytes remaining in the buffer.
    *   For writes, it is bytes not yet written.
    *   For reads, it is remaining bytes not yet read.
    */
   uint64_t BlockAddr;                /* Block address */
   uint32_t binbuf;                   /* bytes in buffer */
   uint32_t block_len;                /* length of current block read */
   uint32_t buf_len;                  /* max/default block length */
   uint32_t reclen;                   /* Last record length put in adata block */
   uint32_t BlockNumber;              /* sequential Bacula block number */
   uint32_t read_len;                 /* bytes read into buffer, if zero, block empty */
   uint32_t VolSessionId;             /* */
   uint32_t VolSessionTime;           /* */
   uint32_t read_errors;              /* block errors (checksum, header, ...) */
   uint32_t CheckSum;                 /* Block checksum */
   uint32_t RecNum;                   /* Number of records read from the current block */
   int      BlockVer;                 /* block version 1 or 2 */
   bool     write_failed;             /* set if write failed */
   bool     block_read;               /* set when block read */
   bool     needs_write;              /* block must be written */
   bool     adata;                    /* adata block */
   bool     no_header;                /* Set if no block header */
   bool     new_fi;                   /* New FI arrived */
   int32_t  FirstIndex;               /* first index this block */
   int32_t  LastIndex;                /* last index this block */
   int32_t  rechdr_items;             /* number of items in rechdr queue */
   char    *bufp;                     /* pointer into buffer */
   char     ser_buf[BLKHDR2_LENGTH];  /* Serial buffer for adata */
   POOLMEM *rechdr_queue;             /* record header queue */
   POOLMEM *buf;                      /* actual data buffer */
};

#define block_is_empty(block) ((block)->read_len == 0)

#endif
