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

#ifndef __RESTORE_H
#define __RESTORE_H

struct RESTORE_DATA_STREAM {
   int32_t stream;                     /* stream less new bits */
   char *content;                      /* stream data */
   uint32_t content_length;            /* stream length */
};

struct RESTORE_CIPHER_CTX {
   CIPHER_CONTEXT *cipher;
   uint32_t block_size;

   POOLMEM *buf;                       /* Pointer to descryption buffer */
   int32_t buf_len;                    /* Count of bytes currently in buf */
   int32_t packet_len;                 /* Total bytes in packet */
};

struct r_ctx {
   JCR *jcr;
   int32_t stream;                     /* stream less new bits */
   int32_t prev_stream;                /* previous stream */
   int32_t full_stream;                /* full stream including new bits */
   int32_t comp_stream;                /* last compressed stream found. needed only to restore encrypted compressed backup */
   BFILE bfd;                          /* File content */
   uint64_t fileAddr;                  /* file write address */
   uint32_t size;                      /* Size of file */
   int flags;                          /* Options for extract_data() */
   BFILE forkbfd;                      /* Alternative data stream */
   uint64_t fork_addr;                 /* Write address for alternative stream */
   int64_t fork_size;                  /* Size of alternate stream */
   int fork_flags;                     /* Options for extract_data() */
   int32_t type;                       /* file type FT_ */
   ATTR *attr;                         /* Pointer to attributes */
   bool extract;                       /* set when extracting */
   alist *delayed_streams;             /* streams that should be restored as last */
   worker *efs;                        /* Windows EFS worker thread */
   int32_t count;                      /* Debug count */

   SIGNATURE *sig;                     /* Cryptographic signature (if any) for file */
   CRYPTO_SESSION *cs;                 /* Cryptographic session data (if any) for file */
   RESTORE_CIPHER_CTX cipher_ctx;      /* Cryptographic restore context (if any) for file */
   RESTORE_CIPHER_CTX fork_cipher_ctx; /* Cryptographic restore context (if any) for alternative stream */
};

#endif

#ifdef TEST_WORKER
bool test_write_efs_data(r_ctx &rctx, char *data, const int32_t length);
#endif

#ifdef HAVE_WIN32
bool win_write_efs_data(r_ctx &rctx, char *data, const int32_t length);
#endif
