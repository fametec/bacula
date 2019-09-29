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
 *  Bacula File Daemon  verify-vol.c Verify files on a Volume
 *    versus attributes in Catalog
 *
 *    Kern Sibbald, July MMII
 *
 *  Data verification added by Eric Bollengier
 */

#include "bacula.h"
#include "filed.h"
#include "findlib/win32filter.h"

#if   defined(HAVE_LIBZ)
const bool have_libz = true;
#else
const bool have_libz = false;
#endif

#ifdef HAVE_LZO
const bool have_lzo = true;
#else
const bool have_lzo = false;
#endif

/* Context used during Verify Data job. We use it in the
 * verify loop to compute checksums and check attributes.
 */
class v_ctx {
public:
   JCR *jcr;
   int32_t stream;              /* stream less new bits */
   int32_t prev_stream;         /* previous stream */
   int32_t full_stream;         /* full stream including new bits */
   int32_t type;                /* file type FT_ */
   int64_t size;                /* current file size */
   ATTR *attr;                  /* Pointer to attributes */

   bool check_size;             /* Check or not the size attribute */
   bool check_chksum;           /* Check the checksum */

   crypto_digest_t digesttype;
   Win32Filter win32filter;
   char digest[BASE64_SIZE(CRYPTO_DIGEST_MAX_SIZE)]; /* current digest */

   v_ctx(JCR *ajcr) :
      jcr(ajcr), stream(0), prev_stream(0), full_stream(0), type(0), size(-1),
      attr(new_attr(jcr)), check_size(false), check_chksum(false),
      digesttype(CRYPTO_DIGEST_NONE), win32filter()
   {
      *digest = 0;
      scan_fileset();
   };
   ~v_ctx() {
      free_attr(attr);
   };
   /* Call this function when we change the file
    * We check the st_size and we compute the digest
    */
   bool close_previous_stream();

   /* Call when we have a sparse record */
   void skip_sparse_header(char **data, uint32_t *length);

   /* Scan the fileset to know if we want to check checksums or st_size */
   void scan_fileset();

   /* Check the catalog to locate the file */
   void check_accurate();

   /* In cleanup, we reset the current file size to -1 */
   void reset_size() {
      size = -1;
   };

   /* Used for sparse files */
   void set_size(int64_t val) {
      size = MAX(size, val);
   };

   void update_size(int64_t val) {
      if (size == -1) {
         size = 0;
      }
      size += val;
   };

   void update_checksum(char *wbuf, int32_t wsize) {
      if (wsize > 0 && check_chksum) {
         if (!jcr->crypto.digest) {
            jcr->crypto.digest = crypto_digest_new(jcr, digesttype);
         }
         crypto_digest_update(jcr->crypto.digest, (uint8_t *)wbuf, wsize);
      }
   };
};

/* Data received from Storage Daemon */
static char rec_header[] = "rechdr %ld %ld %ld %ld %ld";

/* Forward referenced functions */

/* We don't know in advance which digest mode is needed, we do not
 * want to store files on disk either to check afterward. So, we read
 * the fileset definition and we try to guess the digest that will be
 * used. If the FileSet uses multiple digests, it will not work.
 */
void v_ctx::scan_fileset()
{
   findFILESET *fileset;

   check_size = check_chksum = false;
   digesttype = CRYPTO_DIGEST_NONE;

   if (!jcr->ff || !jcr->ff->fileset) {
      return;
   }

   fileset = jcr->ff->fileset;

   for (int i=0; i<fileset->include_list.size(); i++) {
      findINCEXE *incexe = (findINCEXE *)fileset->include_list.get(i);

      for (int j=0; j<incexe->opts_list.size(); j++) {
         findFOPTS *fo = (findFOPTS *)incexe->opts_list.get(j);
         check_size = (strchr(fo->VerifyOpts, 's') != NULL);
         if ((strchr(fo->VerifyOpts, '1') != NULL) ||
             (strchr(fo->VerifyOpts, '5') != NULL))
         {
            check_chksum = true;
         } 

         if (fo->flags & FO_MD5) {
            digesttype = CRYPTO_DIGEST_MD5;
            return;
         }
         if (fo->flags & FO_SHA1) {
            digesttype = CRYPTO_DIGEST_SHA1;
            return;
         }
         if (fo->flags & FO_SHA256) {
            digesttype = CRYPTO_DIGEST_SHA256;
            return;
         }
         if (fo->flags & FO_SHA512) {
            digesttype = CRYPTO_DIGEST_SHA512;
            return;
         }
      }
   }
   digesttype = CRYPTO_DIGEST_NONE;
   if (check_chksum) {
      Jmsg(jcr, M_WARNING, 0, _("Checksum verification required in Verify FileSet option, but no Signature found in the FileSet\n"));
      check_chksum = false;
   }
}

/* Compute the file size for sparse records and adjust the data */
void v_ctx::skip_sparse_header(char **data, uint32_t *length)
{
   unser_declare;
   uint64_t faddr;
   unser_begin(*data, OFFSET_FADDR_SIZE);
   unser_uint64(faddr);

   /* For sparse, we assume that the file is at least big as faddr */
   set_size(faddr);
   
   *data += OFFSET_FADDR_SIZE;
   *length -= OFFSET_FADDR_SIZE;
}

void v_ctx::check_accurate()
{
   attr->fname = jcr->last_fname; /* struct stat is still valid, but not the fname */
   if (accurate_check_file(jcr, attr, digest)) {
      jcr->setJobStatus(JS_Differences);
   }
}

/*
 * If extracting, close any previous stream
 */
bool v_ctx::close_previous_stream()
{
   bool rtn = true;
   uint8_t buf[CRYPTO_DIGEST_MAX_SIZE];
   uint32_t len = CRYPTO_DIGEST_MAX_SIZE;
   char ed1[50], ed2[50];

   /* Reset the win32 filter that strips header stream out of the file */
   win32filter.init();

   /* Check the size if possible */
   if (check_size && size >= 0) {
      if (attr->type == FT_REG && size != (int64_t)attr->statp.st_size) {
         Dmsg1(50, "Size comparison failed for %s\n", jcr->last_fname);
         Jmsg(jcr, M_INFO, 0,
              _("   st_size  differs on \"%s\". Vol: %s File: %s\n"),
              jcr->last_fname,
              edit_int64(size, ed1),
              edit_int64((int64_t)attr->statp.st_size, ed2));
         jcr->setJobStatus(JS_Differences);
      }
      reset_size();
   }

   /* Compute the digest and store it */
   *digest = 0;
   if (jcr->crypto.digest) {
      if (!crypto_digest_finalize(jcr->crypto.digest, buf, &len)) {
         Dmsg1(50, "Unable to finalize digest for %s\n", jcr->last_fname);
         rtn = false;

      } else {
         bin_to_base64(digest, sizeof(digest), (char *)buf, len, true);
      }
      crypto_digest_free(jcr->crypto.digest);
      jcr->crypto.digest = NULL;
   }
   return rtn;
}

/*
 * Verify attributes or data of the requested files on the Volume
 *
 */
void do_verify_volume(JCR *jcr)
{
   BSOCK *sd, *dir;
   uint32_t size;
   uint32_t VolSessionId, VolSessionTime, file_index;
   char digest[BASE64_SIZE(CRYPTO_DIGEST_MAX_SIZE)];
   int stat;
   int bget_ret = 0;
   char *wbuf;                        /* write buffer */
   uint32_t wsize;                    /* write size */
   uint32_t rsize;                    /* read size */
   bool msg_encrypt = false, do_check_accurate=false;
   v_ctx vctx(jcr);
   ATTR *attr = vctx.attr;

   sd = jcr->store_bsock;
   if (!sd) {
      Jmsg(jcr, M_FATAL, 0, _("Storage command not issued before Verify.\n"));
      jcr->setJobStatus(JS_FatalError);
      return;
   }
   dir = jcr->dir_bsock;
   jcr->setJobStatus(JS_Running);

   LockRes();
   CLIENT *client = (CLIENT *)GetNextRes(R_CLIENT, NULL);
   UnlockRes();
   uint32_t buf_size;
   if (client) {
      buf_size = client->max_network_buffer_size;
   } else {
      buf_size = 0;                   /* use default */
   }
   if (!sd->set_buffer_size(buf_size, BNET_SETBUF_WRITE)) {
      jcr->setJobStatus(JS_FatalError);
      return;
   }
   jcr->buf_size = sd->msglen;

   /* use the same buffer size to decompress both gzip and lzo */
   if (have_libz || have_lzo) {
      uint32_t compress_buf_size = jcr->buf_size + 12 + ((jcr->buf_size+999) / 1000) + 100;
      jcr->compress_buf = get_memory(compress_buf_size);
      jcr->compress_buf_size = compress_buf_size;
   }

   GetMsg *fdmsg;
   fdmsg = New(GetMsg(jcr, sd, rec_header, GETMSG_MAX_MSG_SIZE));

   fdmsg->start_read_sock();
   bmessage *bmsg = fdmsg->new_msg(); /* get a message, to exchange with fdmsg */

   /*
    * Get a record from the Storage daemon
    */
   while ((bget_ret = fdmsg->bget_msg(&bmsg)) >= 0 && !job_canceled(jcr)) {
      /* Remember previous stream type */
      vctx.prev_stream = vctx.stream;

      /*
       * First we expect a Stream Record Header
       */
      if (sscanf(bmsg->rbuf, rec_header, &VolSessionId, &VolSessionTime, &file_index,
          &vctx.full_stream, &size) != 5) {
         Jmsg1(jcr, M_FATAL, 0, _("Record header scan error: %s\n"), bmsg->rbuf);
         goto bail_out;
      }
      vctx.stream = vctx.full_stream & STREAMMASK_TYPE;
      Dmsg4(30, "Got hdr: FilInx=%d FullStream=%d Stream=%d size=%d.\n",
            file_index, vctx.full_stream, vctx.stream, size);

      /*
       * Now we expect the Stream Data
       */
      if ((bget_ret = fdmsg->bget_msg(&bmsg)) < 0) {
         if (bget_ret != BNET_EXT_TERMINATE) {
            Jmsg1(jcr, M_FATAL, 0, _("Data record error. ERR=%s\n"), sd->bstrerror());
         } else {
            /* The error has been handled somewhere else, just quit */
         }
         goto bail_out;
      }
      if (size != ((uint32_t)bmsg->origlen)) {
         Jmsg2(jcr, M_FATAL, 0, _("Actual data size %d not same as header %d\n"), bmsg->origlen, size);
         goto bail_out;
      }
      Dmsg2(30, "Got stream data %s, len=%d\n", stream_to_ascii(vctx.stream), bmsg->rbuflen);

      /* File Attributes stream */
      switch (vctx.stream) {
      case STREAM_UNIX_ATTRIBUTES:
      case STREAM_UNIX_ATTRIBUTES_EX:
         Dmsg0(400, "Stream=Unix Attributes.\n");
         if (!vctx.close_previous_stream()) {
            goto bail_out;
         }
         if (do_check_accurate) {
            vctx.check_accurate();
         }
         /* Next loop, we want to check the file (or we do it with the md5) */
         do_check_accurate = true;

         /*
          * Unpack attributes and do sanity check them
          */
         if (!unpack_attributes_record(jcr, vctx.stream,
                                       bmsg->rbuf, bmsg->rbuflen, attr)) {
            goto bail_out;
         }

         attr->data_stream = decode_stat(attr->attr, &attr->statp,
                                         sizeof(attr->statp), &attr->LinkFI);

         jcr->lock();
         jcr->JobFiles++;
         jcr->num_files_examined++;
         pm_strcpy(jcr->last_fname, attr->fname); /* last file examined */
         jcr->unlock();

         if (jcr->getJobLevel() == L_VERIFY_VOLUME_TO_CATALOG) {
            /*
             * Send file attributes to Director
             *   File_index
             *   Stream
             *   Verify Options
             *   Filename (full path)
             *   Encoded attributes
             *   Link name (if type==FT_LNK)
             * For a directory, link is the same as fname, but with trailing
             * slash. For a linked file, link is the link.
             */
            /* Send file attributes to Director */
            Dmsg2(200, "send ATTR inx=%d fname=%s\n", jcr->JobFiles, attr->fname);
            if (attr->type == FT_LNK || attr->type == FT_LNKSAVED) {
               stat = dir->fsend("%d %d %s %s%c%s%c%s%c", jcr->JobFiles,
                                 STREAM_UNIX_ATTRIBUTES, "pinsug5", attr->fname,
                                 0, attr->attr, 0, attr->lname, 0);
               /* for a deleted record, we set fileindex=0 */
            } else if (attr->type == FT_DELETED)  {
               stat = dir->fsend("%d %d %s %s%c%s%c%c", 0,
                                 STREAM_UNIX_ATTRIBUTES, "pinsug5", attr->fname,
                                 0, attr->attr, 0, 0);
            } else {
               stat = dir->fsend("%d %d %s %s%c%s%c%c", jcr->JobFiles,
                                 STREAM_UNIX_ATTRIBUTES, "pinsug5", attr->fname,
                                 0, attr->attr, 0, 0);
            }
            Dmsg2(200, "bfiled>bdird: attribs len=%d: msg=%s\n", dir->msglen, dir->msg);
            if (!stat) {
               Jmsg(jcr, M_FATAL, 0, _("Network error in send to Director: ERR=%s\n"), dir->bstrerror());
               goto bail_out;
            }
         }
         break;

         /*
          * Restore stream object is counted, but not restored here
          */
      case STREAM_RESTORE_OBJECT:
         jcr->lock();
         jcr->JobFiles++;
         jcr->num_files_examined++;
         jcr->unlock();
         break;

      default:
         break;
      }

      const char *digest_code = NULL;

      switch(vctx.stream) {
      case STREAM_MD5_DIGEST:
         bin_to_base64(digest, sizeof(digest), (char *)bmsg->rbuf, CRYPTO_DIGEST_MD5_SIZE, true);
         digest_code = "MD5";
         break;

      case STREAM_SHA1_DIGEST:
         bin_to_base64(digest, sizeof(digest), (char *)bmsg->rbuf, CRYPTO_DIGEST_SHA1_SIZE, true);
         digest_code = "SHA1";
         break;

      case STREAM_SHA256_DIGEST:
         bin_to_base64(digest, sizeof(digest), (char *)bmsg->rbuf, CRYPTO_DIGEST_SHA256_SIZE, true);
         digest_code = "SHA256";
         break;

      case STREAM_SHA512_DIGEST:
         bin_to_base64(digest, sizeof(digest), (char *)bmsg->rbuf, CRYPTO_DIGEST_SHA512_SIZE, true);
         digest_code = "SHA512";
         break;

      default:
         *digest = 0;
         break;
      }

      if (digest_code && jcr->getJobLevel() == L_VERIFY_VOLUME_TO_CATALOG) {
         dir->fsend("%d %d %s *%s-%d*", jcr->JobFiles, vctx.stream,
                    digest, digest_code, jcr->JobFiles);

      } else if (jcr->getJobLevel() == L_VERIFY_DATA) {
         /* Compare digest */
         if (vctx.check_chksum && *digest) {
            /* probably an empty file, we can create an empty crypto session */
            if (!jcr->crypto.digest) {
               jcr->crypto.digest = crypto_digest_new(jcr, vctx.digesttype);
            }
            vctx.close_previous_stream();
            if (strncmp(digest, vctx.digest,
                        MIN(sizeof(digest), sizeof(vctx.digest))) != 0)
            {
               Jmsg(jcr, M_INFO, 0,
                    _("   %s differs on \"%s\". File=%s Vol=%s\n"),
                    stream_to_ascii(vctx.stream), jcr->last_fname,
                    vctx.digest, digest);
               jcr->setJobStatus(JS_Differences);
               Dmsg3(50, "Signature verification failed for %s %s != %s\n",
                     jcr->last_fname, digest, vctx.digest);
            }
            if (do_check_accurate) {
               vctx.check_accurate();
               do_check_accurate = false; /* Don't do it in the next loop */
            }
         }

         /* Compute size and checksum for level=Data */
         switch (vctx.stream) {
         case STREAM_ENCRYPTED_FILE_DATA:
         case STREAM_ENCRYPTED_WIN32_DATA:
         case STREAM_ENCRYPTED_FILE_GZIP_DATA:
         case STREAM_ENCRYPTED_WIN32_GZIP_DATA:
         case STREAM_ENCRYPTED_FILE_COMPRESSED_DATA:
         case STREAM_ENCRYPTED_WIN32_COMPRESSED_DATA:
            if (!msg_encrypt) {
               Jmsg(jcr, M_WARNING, 0,
                  _("Verification of encrypted file data is not supported.\n"));
               msg_encrypt = true;
            }
            break;

         case STREAM_PLUGIN_DATA:
         case STREAM_FILE_DATA:
         case STREAM_SPARSE_DATA:
         case STREAM_WIN32_DATA:
         case STREAM_GZIP_DATA:
         case STREAM_SPARSE_GZIP_DATA:
         case STREAM_WIN32_GZIP_DATA:
         case STREAM_COMPRESSED_DATA:
         case STREAM_SPARSE_COMPRESSED_DATA:
         case STREAM_WIN32_COMPRESSED_DATA:
            if (!(attr->type ==  FT_RAW || attr->type == FT_FIFO || attr->type == FT_REG || attr->type == FT_REGE)) {
               break;
            }

            wbuf = bmsg->rbuf;
            rsize = bmsg->rbuflen;
            jcr->ReadBytes += rsize;
            wsize = rsize;

            if (vctx.stream == STREAM_SPARSE_DATA
                || vctx.stream == STREAM_SPARSE_COMPRESSED_DATA
                || vctx.stream == STREAM_SPARSE_GZIP_DATA) {
               vctx.skip_sparse_header(&wbuf, &wsize);
            }

            if (vctx.stream == STREAM_GZIP_DATA
                || vctx.stream == STREAM_SPARSE_GZIP_DATA
                || vctx.stream == STREAM_WIN32_GZIP_DATA
                || vctx.stream == STREAM_ENCRYPTED_FILE_GZIP_DATA
                || vctx.stream == STREAM_COMPRESSED_DATA
                || vctx.stream == STREAM_SPARSE_COMPRESSED_DATA
                || vctx.stream == STREAM_WIN32_COMPRESSED_DATA
                || vctx.stream == STREAM_ENCRYPTED_FILE_COMPRESSED_DATA
                || vctx.stream == STREAM_ENCRYPTED_WIN32_COMPRESSED_DATA
                || vctx.stream == STREAM_ENCRYPTED_WIN32_GZIP_DATA) {

               if (!decompress_data(jcr, vctx.stream, &wbuf, &wsize)) {
                  dequeue_messages(jcr);
                  goto bail_out;
               }
            }

            vctx.update_checksum(wbuf, wsize);

            if (vctx.stream == STREAM_WIN32_GZIP_DATA
                || vctx.stream == STREAM_WIN32_DATA
                || vctx.stream == STREAM_WIN32_COMPRESSED_DATA
                || vctx.stream == STREAM_ENCRYPTED_WIN32_COMPRESSED_DATA
                || vctx.stream == STREAM_ENCRYPTED_WIN32_GZIP_DATA) {

               int64_t wbuf_len = wsize;
               int64_t wsize64 = 0;
               if (vctx.win32filter.have_data(&wbuf, &wbuf_len, &wsize64)) {
                  wsize = wsize64;
               }
            }
            jcr->JobBytes += wsize;
            vctx.update_size(wsize);
            break;

            /* TODO: Handle data to compute checksums */
            /* Ignore everything else */
         default:
            break;
         }
      } /* end switch */
   } /* end while bnet_get */
   if (bget_ret == BNET_EXT_TERMINATE) {
      goto bail_out;
   }
   if (!vctx.close_previous_stream()) {
      goto bail_out;
   }
   /* Check the last file */
   if (do_check_accurate) {
      vctx.check_accurate();
   }
   if (!accurate_finish(jcr)) {
      goto bail_out;
   }
   jcr->setJobStatus(JS_Terminated);
   goto ok_out;

bail_out:
   jcr->setJobStatus(JS_ErrorTerminated);

ok_out:
   fdmsg->wait_read_sock(jcr->is_job_canceled());
   delete bmsg;
   free_GetMsg(fdmsg);
   if (jcr->compress_buf) {
      free_pool_memory(jcr->compress_buf);
      jcr->compress_buf = NULL;
   }
   /* TODO: We probably want to mark the job as failed if we have errors */
   Dmsg2(50, "End Verify-Vol. Files=%d Bytes=%" lld "\n", jcr->JobFiles,
      jcr->JobBytes);
}
