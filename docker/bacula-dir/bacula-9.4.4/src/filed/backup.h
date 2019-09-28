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

#ifndef __BACKUP_H
#define __BACKUP_H

#include "ch.h"

/*
 * Define a backup context
 */
struct bctx_t {
   /* Global variables */
   JCR *jcr;
   FF_PKT *ff_pkt;                    /* find file packet */
   int data_stream;
   BSOCK *sd;
   uint64_t fileAddr;
   char *rbuf, *wbuf;
   int32_t rsize;
   POOLMEM *msgsave;

   /* Crypto variables */
   DIGEST *digest;
   DIGEST *signing_digest;
   int digest_stream;
   SIGNATURE *sig;
   CIPHER_CONTEXT *cipher_ctx;
   const uint8_t *cipher_input;
   uint32_t cipher_input_len;
   uint32_t cipher_block_size;
   uint32_t encrypted_len;

   /* Compression variables */
   /* These are the same as used by libz, but I find it very
    *  uncomfortable to define variables like this rather than
    *  specifying a number of bits. Defining them here allows us
    *  to have code that compiles with and without libz and lzo.
    *
    *  uLong == unsigned long int
    *  Bytef == unsigned char
    */
   unsigned long int max_compress_len;
   unsigned long int compress_len;
   unsigned char *cbuf;
   unsigned char *cbuf2;

#ifdef HAVE_LZO
   comp_stream_header ch;
#endif

};

bool crypto_setup_digests(bctx_t &bctx);
bool crypto_terminate_digests(bctx_t &bctx);
bool crypto_session_start(JCR *jcr);
void crypto_session_end(JCR *jcr);
bool crypto_session_send(JCR *jcr, BSOCK *sd);
bool crypto_allocate_ctx(bctx_t &bctx);
void crypto_free(bctx_t &bctx);

bool encode_and_send_attributes(bctx_t &bctx);

bool process_and_send_data(bctx_t &bctx);

#ifdef HAVE_WIN32
DWORD WINAPI read_efs_data_cb(PBYTE pbData, PVOID pvCallbackContext, ULONG ulLength);
#endif

#endif
