/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2015 Kern Sibbald

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
 *  Crypto subroutines used in backup.c
 *
 * Split from backup.c August 2014
 *
 *    Kern Sibbald, August MMXIV
 *
 */

#include "bacula.h"
#include "filed.h"
#include "ch.h"
#include "backup.h"


bool crypto_allocate_ctx(bctx_t &bctx)
{
   JCR *jcr = bctx.jcr;

   if ((bctx.ff_pkt->flags & FO_SPARSE) || (bctx.ff_pkt->flags & FO_OFFSETS)) {
      Jmsg0(jcr, M_FATAL, 0, _("Encrypting sparse or offset data not supported.\n"));
      return false;
   }
   /** Allocate the cipher context */
   if ((bctx.cipher_ctx = crypto_cipher_new(jcr->crypto.pki_session, true,
        &bctx.cipher_block_size)) == NULL) {
      /* Shouldn't happen! */
      Jmsg0(jcr, M_FATAL, 0, _("Failed to initialize encryption context.\n"));
      return false;
   }

   /**
    * Grow the crypto buffer, if necessary.
    * crypto_cipher_update() will buffer up to (cipher_block_size - 1).
    * We grow crypto_buf to the maximum number of blocks that
    * could be returned for the given read buffer size.
    * (Using the larger of either rsize or max_compress_len)
    */
   jcr->crypto.crypto_buf = check_pool_memory_size(jcr->crypto.crypto_buf,
        (MAX(bctx.rsize + (int)sizeof(uint32_t), (int32_t)bctx.max_compress_len) +
         bctx.cipher_block_size - 1) / bctx.cipher_block_size * bctx.cipher_block_size);

   bctx.wbuf = jcr->crypto.crypto_buf; /* Encrypted, possibly compressed output here. */
   return true;
}


bool crypto_setup_digests(bctx_t &bctx)
{
   JCR *jcr = bctx.jcr;
   FF_PKT *ff_pkt = bctx.ff_pkt;

   crypto_digest_t signing_algorithm = (crypto_digest_t)me->pki_digest;

   /**
    * Setup for digest handling. If this fails, the digest will be set to NULL
    * and not used. Note, the digest (file hash) can be any one of the four
    * algorithms below.
    *
    * The signing digest is a single algorithm depending on
    * whether or not we have SHA2.
    *   ****FIXME****  the signing algoritm should really be
    *   determined a different way!!!!!!  What happens if
    *   sha2 was available during backup but not restore?
    */
   if (ff_pkt->flags & FO_MD5) {
      bctx.digest = crypto_digest_new(jcr, CRYPTO_DIGEST_MD5);
      bctx.digest_stream = STREAM_MD5_DIGEST;

   } else if (ff_pkt->flags & FO_SHA1) {
      bctx.digest = crypto_digest_new(jcr, CRYPTO_DIGEST_SHA1);
      bctx.digest_stream = STREAM_SHA1_DIGEST;

   } else if (ff_pkt->flags & FO_SHA256) {
      bctx.digest = crypto_digest_new(jcr, CRYPTO_DIGEST_SHA256);
      bctx.digest_stream = STREAM_SHA256_DIGEST;

   } else if (ff_pkt->flags & FO_SHA512) {
      bctx.digest = crypto_digest_new(jcr, CRYPTO_DIGEST_SHA512);
      bctx.digest_stream = STREAM_SHA512_DIGEST;
   }

   /** Did digest initialization fail? */
   if (bctx.digest_stream != STREAM_NONE && bctx.digest == NULL) {
      Jmsg(jcr, M_WARNING, 0, _("%s digest initialization failed\n"),
         stream_to_ascii(bctx.digest_stream));
   }

   /**
    * Set up signature digest handling. If this fails, the signature digest
    * will be set to NULL and not used.
    */
   /* TODO landonf: We should really only calculate the digest once, for
    * both verification and signing.
    */
   if (jcr->crypto.pki_sign) {
      bctx.signing_digest = crypto_digest_new(jcr, signing_algorithm);

      /** Full-stop if a failure occurred initializing the signature digest */
      if (bctx.signing_digest == NULL) {
         Jmsg(jcr, M_NOTSAVED, 0, _("%s signature digest initialization failed\n"),
            stream_to_ascii(signing_algorithm));
         jcr->JobErrors++;
         return false;
      }
   }

   /** Enable encryption */
   if (jcr->crypto.pki_encrypt) {
      ff_pkt->flags |= FO_ENCRYPT;
   }
   return true;
}


bool crypto_session_start(JCR *jcr)
{
   crypto_cipher_t cipher = (crypto_cipher_t) me->pki_cipher;

   /**
    * Create encryption session data and a cached, DER-encoded session data
    * structure. We use a single session key for each backup, so we'll encode
    * the session data only once.
    */
   if (jcr->crypto.pki_encrypt) {
      uint32_t size = 0;

      /** Create per-job session encryption context */
      jcr->crypto.pki_session = crypto_session_new(cipher, jcr->crypto.pki_recipients);
      if (!jcr->crypto.pki_session) {
         Jmsg(jcr, M_FATAL, 0, _("Unsupported cipher on this system.\n"));
         return false;
      }

      /** Get the session data size */
      if (!crypto_session_encode(jcr->crypto.pki_session, (uint8_t *)0, &size)) {
         Jmsg(jcr, M_FATAL, 0, _("An error occurred while encrypting the stream.\n"));
         return false;
      }

      /** Allocate buffer */
      jcr->crypto.pki_session_encoded = get_memory(size);

      /** Encode session data */
      if (!crypto_session_encode(jcr->crypto.pki_session, (uint8_t *)jcr->crypto.pki_session_encoded, &size)) {
         Jmsg(jcr, M_FATAL, 0, _("An error occurred while encrypting the stream.\n"));
         return false;
      }

      /** ... and store the encoded size */
      jcr->crypto.pki_session_encoded_size = size;

      /** Allocate the encryption/decryption buffer */
      jcr->crypto.crypto_buf = get_memory(CRYPTO_CIPHER_MAX_BLOCK_SIZE);
   }
   return true;
}

void crypto_session_end(JCR *jcr)
{
   if (jcr->crypto.crypto_buf) {
      free_pool_memory(jcr->crypto.crypto_buf);
      jcr->crypto.crypto_buf = NULL;
   }
   if (jcr->crypto.pki_session) {
      crypto_session_free(jcr->crypto.pki_session);
   }
   if (jcr->crypto.pki_session_encoded) {
      free_pool_memory(jcr->crypto.pki_session_encoded);
      jcr->crypto.pki_session_encoded = NULL;
   }
}

bool crypto_session_send(JCR *jcr, BSOCK *sd)
{
   POOLMEM *msgsave;

   /** Send our header */
   Dmsg2(100, "Send hdr fi=%ld stream=%d\n", jcr->JobFiles, STREAM_ENCRYPTED_SESSION_DATA);
   sd->fsend("%ld %d %lld", jcr->JobFiles, STREAM_ENCRYPTED_SESSION_DATA,
      (int64_t)jcr->ff->statp.st_size);
   msgsave = sd->msg;
   sd->msg = jcr->crypto.pki_session_encoded;
   sd->msglen = jcr->crypto.pki_session_encoded_size;
   jcr->JobBytes += sd->msglen;

   Dmsg1(100, "Send data len=%d\n", sd->msglen);
   sd->send();
   sd->msg = msgsave;
   sd->signal(BNET_EOD);
   return true;
}

bool crypto_terminate_digests(bctx_t &bctx)
{
   JCR *jcr;
   BSOCK *sd;
   FF_PKT *ff_pkt;

   jcr = bctx.jcr;
   sd = bctx.sd;
   ff_pkt = bctx.ff_pkt;

   /** Terminate the signing digest and send it to the Storage daemon */
   if (bctx.signing_digest) {
      uint32_t size = 0;

      if ((bctx.sig = crypto_sign_new(jcr)) == NULL) {
         Jmsg(jcr, M_FATAL, 0, _("Failed to allocate memory for crypto signature.\n"));
         return false;
      }

      if (!crypto_sign_add_signer(bctx.sig, bctx.signing_digest, jcr->crypto.pki_keypair)) {
         Jmsg(jcr, M_FATAL, 0, _("An error occurred while adding signer the stream.\n"));
         return false;
      }

      /** Get signature size */
      if (!crypto_sign_encode(bctx.sig, NULL, &size)) {
         Jmsg(jcr, M_FATAL, 0, _("An error occurred while signing the stream.\n"));
         return false;
      }

      /** Grow the bsock buffer to fit our message if necessary */
      if (sizeof_pool_memory(sd->msg) < (int32_t)size) {
         sd->msg = realloc_pool_memory(sd->msg, size);
      }

      /** Send our header */
      sd->fsend("%ld %ld 0", jcr->JobFiles, STREAM_SIGNED_DIGEST);
      Dmsg1(300, "bfiled>stored:header %s\n", sd->msg);

      /** Encode signature data */
      if (!crypto_sign_encode(bctx.sig, (uint8_t *)sd->msg, &size)) {
         Jmsg(jcr, M_FATAL, 0, _("An error occurred while signing the stream.\n"));
         return false;
      }

      sd->msglen = size;
      sd->send();
      sd->signal(BNET_EOD);              /* end of checksum */
   }

   /** Terminate any digest and send it to Storage daemon */
   if (bctx.digest) {
      uint32_t size;

      sd->fsend("%ld %d 0", jcr->JobFiles, bctx.digest_stream);
      Dmsg1(300, "bfiled>stored:header %s\n", sd->msg);

      size = CRYPTO_DIGEST_MAX_SIZE;

      /** Grow the bsock buffer to fit our message if necessary */
      if (sizeof_pool_memory(sd->msg) < (int32_t)size) {
         sd->msg = realloc_pool_memory(sd->msg, size);
      }

      if (!crypto_digest_finalize(bctx.digest, (uint8_t *)sd->msg, &size)) {
         Jmsg(jcr, M_FATAL, 0, _("An error occurred finalizing signing the stream.\n"));
         return false;
      }

      /* Keep the checksum if this file is a hardlink */
      if (ff_pkt->linked) {
         ff_pkt_set_link_digest(ff_pkt, bctx.digest_stream, sd->msg, size);
      }

      sd->msglen = size;
      sd->send();
      sd->signal(BNET_EOD);              /* end of checksum */
   }

   /* Check if original file has a digest, and send it */
   if (ff_pkt->type == FT_LNKSAVED && ff_pkt->digest) {
      Dmsg2(300, "Link %s digest %d\n", ff_pkt->fname, ff_pkt->digest_len);
      sd->fsend("%ld %d 0", jcr->JobFiles, ff_pkt->digest_stream);

      sd->msg = check_pool_memory_size(sd->msg, ff_pkt->digest_len);
      memcpy(sd->msg, ff_pkt->digest, ff_pkt->digest_len);
      sd->msglen = ff_pkt->digest_len;
      sd->send();

      sd->signal(BNET_EOD);              /* end of hardlink record */
   }

   return true;
}

void crypto_free(bctx_t &bctx)
{
   if (bctx.digest) {
      crypto_digest_free(bctx.digest);
      bctx.digest = NULL;
   }
   if (bctx.signing_digest) {
      crypto_digest_free(bctx.signing_digest);
      bctx.signing_digest = NULL;
   }
   if (bctx.sig) {
      crypto_sign_free(bctx.sig);
      bctx.sig = NULL;
   }
}
