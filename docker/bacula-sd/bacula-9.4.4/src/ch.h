/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2019 Kern Sibbald

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
 * Compressed stream header struct
 *
 *  Laurent Papier
 */

#ifndef __CH_H
#define __CH_H 1

/*
 * Compression algorithm signature. 4 letters as a 32bits integer
 */
#define COMPRESS_NONE  0x4e4f4e45  /* used for incompressible block */
#define COMPRESS_GZIP  0x475a4950
#define COMPRESS_LZO1X 0x4c5a4f58

/*
 * Compression header version
 */
#define COMP_HEAD_VERSION 0x1

/* Compressed data stream header */
typedef struct {
   uint32_t magic;      /* compression algo used in this compressed data stream */
   uint16_t level;      /* compression level used */
   uint16_t version;    /* for futur evolution */
   uint32_t size;       /* compressed size of the original data */
} comp_stream_header;

#endif /* __CH_H */
