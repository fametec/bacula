/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2018 Kern Sibbald

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
 * Record, and label definitions for Bacula
 *  media data format.
 *
 *   Kern Sibbald, MM
 *
 */


#ifndef __RECORD_H
#define __RECORD_H 1

/* Return codes from read_device_volume_label() */
enum {
   VOL_NOT_READ      = 1,                 /* Volume label not read */
   VOL_OK            = 2,                 /* volume name OK */
   VOL_NO_LABEL      = 3,                 /* volume not labeled */
   VOL_IO_ERROR      = 4,                 /* volume I/O error */
   VOL_NAME_ERROR    = 5,                 /* Volume name mismatch */
   VOL_CREATE_ERROR  = 6,                 /* Error creating label */
   VOL_VERSION_ERROR = 7,                 /* Bacula version error */
   VOL_LABEL_ERROR   = 8,                 /* Bad label type */
   VOL_NO_MEDIA      = 9,                 /* Hard error -- no media present */
   VOL_TYPE_ERROR    = 10                 /* Volume type (aligned/non-aligned) error */
};

enum rec_state {
   st_none,                               /* No state */
   st_header,                             /* Write header */
   st_cont_header,                        /* Write continuation header */
   st_data,                               /* Write data record */
   st_adata_blkhdr,                       /* Adata block header */
   st_adata_rechdr,                       /* Adata record header */
   st_cont_adata_rechdr,                  /* Adata continuation rechdr */
   st_adata,                              /* Write aligned data */
   st_cont_adata,                         /* Write more aligned data */
   st_adata_label                         /* Writing adata vol label */
};


/*  See block.h for RECHDR_LENGTH */

/*
 * This is the Media structure for a record header.
 *  NB: when it is written it is serialized.

   uint32_t VolSessionId;
   uint32_t VolSessionTime;

 * The above 8 bytes are only written in a BB01 block, BB02
 *  and later blocks contain these values in the block header
 *  rather than the record header.

   int32_t  FileIndex;
   int32_t  Stream;
   uint32_t data_len;

 */

/* Record state bit definitions */
#define REC_NO_HEADER        (1<<0)   /* No header read */
#define REC_PARTIAL_RECORD   (1<<1)   /* returning partial record */
#define REC_BLOCK_EMPTY      (1<<2)   /* Not enough data in block */
#define REC_NO_MATCH         (1<<3)   /* No match on continuation data */
#define REC_CONTINUATION     (1<<4)   /* Continuation record found */
#define REC_ISTAPE           (1<<5)   /* Set if device is tape */
#define REC_ADATA_EMPTY      (1<<6)   /* Not endough adata in block */
#define REC_NO_SPLIT         (1<<7)   /* Do not split this record */

#define is_partial_record(r) ((r)->state_bits & REC_PARTIAL_RECORD)
#define is_block_marked_empty(r) ((r)->state_bits & (REC_BLOCK_EMPTY|REC_ADATA_EMPTY))

/*
 * DEV_RECORD for reading and writing records.
 * It consists of a Record Header, and the Record Data
 *
 *  This is the memory structure for the record header.
 */
struct BSR;                           /* satisfy forward reference */
struct VOL_LIST;
struct DEV_RECORD {
   dlink link;                        /* link for chaining in read_record.c */
   /* File and Block are always returned during reading
    *  and writing records.
    */
   uint64_t StreamLen;                /* Expected data stream length */
   uint64_t FileOffset;               /* Offset of this record inside the file */
   uint64_t StartAddr;                /* Start address (when the record is partial) */
   uint64_t Addr;                     /* Record address */
   uint32_t VolSessionId;             /* sequential id within this session */
   uint32_t VolSessionTime;           /* session start time */
   int32_t  FileIndex;                /* sequential file number */
   int32_t  Stream;                   /* Full Stream number with high bits */
   int32_t  last_FI;                  /* previous fi for adata */
   int32_t  last_Stream;              /* previous stream for adata */
   int32_t  maskedStream;             /* Masked Stream without high bits */
   uint32_t data_len;                 /* current record length */
   uint32_t remainder;                /* remaining bytes to read/write */
   uint32_t adata_remainder;          /* remaining adata bytes to read/write */
   uint32_t remlen;                   /* temp remainder bytes */
   uint32_t data_bytes;               /* data_bytes */
   uint32_t state_bits;               /* state bits */
   uint32_t RecNum;                   /* Record number in the block */
   uint32_t BlockNumber;              /* Block number for this record (used in read_records()) */
   bool     invalid;                  /* The record may be invalid if it was merged with a previous record */
   rec_state wstate;                  /* state of write_record_to_block */
   rec_state rstate;                  /* state of read_record_from_block */
   BSR *bsr;                          /* pointer to bsr that matched */
   POOLMEM *data;                     /* Record data. This MUST be a memory pool item */
   const char *VolumeName;            /* From JCR::VolList::VolumeName, freed at the end */
   int32_t match_stat;                /* bsr match status */
   uint32_t last_VolSessionId;        /* used in sequencing FI for Vbackup */
   uint32_t last_VolSessionTime;
   int32_t  last_FileIndex;
};


/*
 * Values for LabelType that are put into the FileIndex field
 * Note, these values are negative to distinguish them
 * from user records where the FileIndex is forced positive.
 */
#define PRE_LABEL   -1                /* Vol label on unwritten tape */
#define VOL_LABEL   -2                /* Volume label first file */
#define EOM_LABEL   -3                /* Writen at end of tape */
#define SOS_LABEL   -4                /* Start of Session */
#define EOS_LABEL   -5                /* End of Session */
#define EOT_LABEL   -6                /* End of physical tape (2 eofs) */
#define SOB_LABEL   -7                /* Start of object -- file/directory */
#define EOB_LABEL   -8                /* End of object (after all streams) */

/*
 *   Volume Label Record.  This is the in-memory definition. The
 *     tape definition is defined in the serialization code itself
 *     ser_volume_label() and unser_volume_label() and is slightly different.
 */


struct Volume_Label {
  /*
   * The first items in this structure are saved
   * in the DEVICE buffer, but are not actually written
   * to the tape.
   */
  int32_t LabelType;                  /* This is written in header only */
  uint32_t LabelSize;                 /* length of serialized label */
  /*
   * The items below this line are stored on
   * the tape
   */
  char Id[32];                        /* Bacula Immortal ... */

  uint32_t VerNum;                    /* Label version number */

  /* VerNum <= 10 */
  float64_t label_date;               /* Date tape labeled */
  float64_t label_time;               /* Time tape labeled */

  /* VerNum >= 11 */
  btime_t   label_btime;              /* tdate tape labeled */
  btime_t   write_btime;              /* tdate tape written */

  /* Unused with VerNum >= 11 */
  float64_t write_date;               /* Date this label written */
  float64_t write_time;               /* Time this label written */

  char VolumeName[MAX_NAME_LENGTH];   /* Volume name */
  char PrevVolumeName[MAX_NAME_LENGTH]; /* Previous Volume Name */
  char PoolName[MAX_NAME_LENGTH];     /* Pool name */
  char PoolType[MAX_NAME_LENGTH];     /* Pool type */
  char MediaType[MAX_NAME_LENGTH];    /* Type of this media */

  char HostName[MAX_NAME_LENGTH];     /* Host name of writing computer */
  char LabelProg[50];                 /* Label program name */
  char ProgVersion[50];               /* Program version */
  char ProgDate[50];                  /* Program build date/time */

  /* Mostly for aligned volumes, BlockSize also used for dedup volumes */
  char AlignedVolumeName[MAX_NAME_LENGTH+4]; /* Aligned block volume name */
  uint64_t  FirstData;                /* Offset to first data address */
  uint32_t  FileAlignment;            /* File alignment factor */
  uint32_t  PaddingSize;              /* Block padding */
  uint32_t  BlockSize;                /* Basic block size */

  /* For Cloud */
  uint64_t  MaxPartSize;              /* Maximum Part Size */

};

#define SER_LENGTH_Volume_Label 1024   /* max serialised length of volume label */
#define SER_LENGTH_Session_Label 1024  /* max serialised length of session label */

typedef struct Volume_Label VOLUME_LABEL;

/*
 * Session Start/End Label
 *  This record is at the beginning and end of each session
 */
struct Session_Label {
  char Id[32];                        /* Bacula Immortal ... */

  uint32_t VerNum;                    /* Label version number */

  uint32_t JobId;                     /* Job id */
  uint32_t VolumeIndex;               /* Sequence no of volume for this job */

  /* VerNum >= 11 */
  btime_t   write_btime;              /* Tdate this label written */

  /* VerNum < 11 */
  float64_t write_date;               /* Date this label written */

  /* Unused VerNum >= 11 */
  float64_t write_time;               /* Time this label written */

  char PoolName[MAX_NAME_LENGTH];     /* Pool name */
  char PoolType[MAX_NAME_LENGTH];     /* Pool type */
  char JobName[MAX_NAME_LENGTH];      /* base Job name */
  char ClientName[MAX_NAME_LENGTH];
  char Job[MAX_NAME_LENGTH];          /* Unique name of this Job */
  char FileSetName[MAX_NAME_LENGTH];
  char FileSetMD5[MAX_NAME_LENGTH];
  uint32_t JobType;
  uint32_t JobLevel;
  /* The remainder are part of EOS label only */
  uint32_t JobFiles;
  uint64_t JobBytes;
  uint32_t StartBlock;
  uint32_t EndBlock;
  uint32_t StartFile;
  uint32_t EndFile;
  uint32_t JobErrors;
  uint32_t JobStatus;                 /* Job status */

};
typedef struct Session_Label SESSION_LABEL;

#define SERIAL_BUFSIZE  1024          /* volume serialisation buffer size */

#endif
