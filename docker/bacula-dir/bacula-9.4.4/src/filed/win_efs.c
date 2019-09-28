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
 *  Bacula File Daemon  Windows EFS restore
 *
 *    Kern Sibbald, September MMXIV
 *
 */

#include "bacula.h"
#include "filed.h"
#include "ch.h"
#include "restore.h"
#include "backup.h"

#ifdef TEST_WORKER
/*
 * This is the test version of the worker routines, which simulates
 *  Windows EFS backup on Linux.
 *
 * This subroutine is called back from the Windows
 *  WriteEncryptedFileRaw and returns a single buffer of data or
 *  sets ulLength = 0 to indicate the end.
 */
static uint32_t test_write_efs_data_cb(char *pbData, void *arctx, uint32_t *len)
{
   r_ctx *rctx = (r_ctx *)arctx;
   worker *wrk = (worker *)rctx->efs;
   char *head, *buf;
   uint32_t data_len;
   int32_t count;

   head = (char *)wrk->dequeue();      /* dequeue buffer to write */
   Dmsg1(200, "dequeue buffer. head=%p\n", head);
   if (!head) {
      *len = 0;
      Dmsg0(200, "cb got NULL.\n");
   } else {
      data_len = *(int32_t *)head;
      Dmsg1(200, "data_len=%d\n", data_len);
      if (data_len == 0) {
         Dmsg0(200, "Length is zero.\n");
         wrk->push_free_buffer(head);
         return ERROR_BUFFER_OVERFLOW;
      }
      if (data_len > *len) {
         Dmsg2(200, "Restore data %ld bytes too long for Microsoft buffer %ld bytes.\n",
            data_len, *len);
         *len = 0;
         errno = b_errno_win32;
         wrk->push_free_buffer(head);
         return ERROR_BUFFER_OVERFLOW;
      } else {
         buf = head + sizeof(uint32_t);    /* Point to buffer */
         count = *(int32_t *)buf;
         buf += sizeof(int32_t);
         memcpy(pbData, buf, data_len);
         *len = data_len;
         Dmsg2(200, "Got count=%d len=%d\n", count, data_len);
         wrk->push_free_buffer(head);
      }
   }
   return ERROR_SUCCESS;
}

/*
 * Thread created to run the WriteEncryptedFileRaw code
 */
static void *test_efs_write_thread(void *awrk)
{
   ssize_t wstat;
   worker *wrk = (worker *)awrk;
   r_ctx *rctx;
   uint32_t len;
   uint32_t size = 100000;
   char *buf = (char *)malloc(size);    /* allocate working buffer */

   rctx = (r_ctx *)wrk->get_ctx();
   Dmsg2(200, "rctx=%p wrk=%p\n", rctx, wrk);
   wrk->set_running();

   while (!wrk->is_quit_state()) {
      if (wrk->is_wait_state()) {      /* wait if so requested */
         Dmsg0(200, "Enter wait state\n");
         wrk->wait();
         Dmsg0(200, "Leave wait state\n");
         continue;
      }
      len = size;
      if (test_write_efs_data_cb(buf, rctx, &len) != 0) {  /* get a buffer */
         berrno be;
         Qmsg2(rctx->jcr, M_FATAL, 0, _("Restore data %ld bytes too long for Microsoft buffer %ld bytes.\n"),
            len, size);
         break;
      }
      if (len == 0) {       /* done? */
         Dmsg0(200, "Got len 0 set_wait_state.\n");
         continue;          /* yes */
      }
      Dmsg2(200, "Write buf=%p len=%d\n", buf, len);
      if ((wstat=bwrite(&rctx->bfd, buf, len)) != (ssize_t)len) {
         Dmsg4(000, "bwrite of %d error %d open=%d on file=%s\n",
            len, wstat, is_bopen(&rctx->bfd), rctx->jcr->last_fname);
         continue;
      }
   }
   Dmsg0(200, "worker thread quiting\n");
   free(buf);
   return NULL;
}

/*
 * If the writer thread is not created, create it, then queue
 *  a buffer to be written by the thread.
 */
bool test_write_efs_data(r_ctx &rctx, char *data, const int32_t length)
{
   POOLMEM *buf, *head;

   if (!rctx.efs) {
      rctx.efs = New(worker(10));
      Dmsg2(200, "Start test_efs_write_thread rctx=%p work=%p\n", &rctx, rctx.efs);
      rctx.efs->start(test_efs_write_thread, &rctx);
   }
   head = (POOLMEM *)rctx.efs->pop_free_buffer();
   if (!head) {
      head = get_memory(length + 2*sizeof(int32_t)+1);
   } else {
      head = check_pool_memory_size(head, length+2*sizeof(int32_t)+1);
   }
   buf = head;
   *(int32_t *)buf = length;
   buf += sizeof(int32_t);
   *(int32_t *)buf = ++rctx.count;
   buf += sizeof(int32_t);
   memcpy(buf, data, length);
   Dmsg3(200, "Put count=%d len=%d head=%p\n", rctx.count, length, head);
   rctx.efs->queue(head);
   rctx.efs->set_run_state();
   return true;
}
#endif


#ifdef HAVE_WIN32

/* =============================================================
 *
 *   Win EFS functions for restore
 *
 * =============================================================
 */

/*
 * This subroutine is called back from the Windows
 *  WriteEncryptedFileRaw.
 */
static DWORD WINAPI write_efs_data_cb(PBYTE pbData, PVOID arctx, PULONG ulLength)
{
   r_ctx *rctx = (r_ctx *)arctx;
   worker *wrk = (worker *)rctx->efs;
   char *data;
   char *buf;
   uint32_t data_len;
   JCR *jcr = rctx->jcr;

   data = (char *)rctx->efs->dequeue();    /* dequeue buffer to write */
   Dmsg1(200, "dequeue buffer. head=%p\n", data);
   if (jcr->is_job_canceled()) {
      return ERROR_CANCELLED;
   }
   if (!data) {
      *ulLength = 0;
      Dmsg0(200, "cb got NULL.\n");
   } else {
      data_len = *(int32_t *)data;
      if (data_len > *ulLength) {
         Qmsg2(rctx->jcr, M_FATAL, 0, _("Restore data %ld bytes too long for Microsoft buffer %lld bytes.\n"),
            data_len, *ulLength);
         *ulLength = 0;
      } else {
         buf = data + sizeof(uint32_t);
         memcpy(pbData, buf, data_len);
         *ulLength = (ULONG)data_len;
         Dmsg1(200, "Got len=%d\n", data_len);
      }
      wrk->push_free_buffer(data);
   }
   return ERROR_SUCCESS;
}

/*
 * Thread created to run the WriteEncryptedFileRaw code
 */
static void *efs_write_thread(void *awrk)
{
   worker *wrk = (worker *)awrk;
   r_ctx *rctx;

   rctx = (r_ctx *)wrk->get_ctx();
   wrk->set_running();

   while (!wrk->is_quit_state() && !rctx->jcr->is_job_canceled()) {
      if (wrk->is_wait_state()) {      /* wait if so requested */
         Dmsg0(200, "Enter wait state\n");
         wrk->wait();
         Dmsg0(200, "Leave wait state\n");
         continue;
      }
      if (p_WriteEncryptedFileRaw((PFE_IMPORT_FUNC)write_efs_data_cb, rctx,
             rctx->bfd.pvContext)) {
         berrno be;
         Qmsg1(rctx->jcr, M_FATAL, 0, _("WriteEncryptedFileRaw failure: ERR=%s\n"),
            be.bstrerror(b_errno_win32));
         return NULL;
      }
      Dmsg0(200, "Got return from WriteEncryptedFileRaw\n");
   }         
   return NULL;
}

/*
 * Called here from Bacula to write a block to a Windows EFS file.
 * Since the Windows WriteEncryptedFileRaw function uses a callback
 *  subroutine to get the blocks to write, we create a writer thread,
 *  and queue the blocks (buffers) we get in this routine.  That
 *  writer thread then hangs on the WriteEncryptedRaw file, calling
 *  back to the callback subroutine which then dequeues the blocks
 *  we have queued.
 *
 * If the writer thread is not created, create it, then queue
 *  a buffer to be written by the thread.
 */
bool win_write_efs_data(r_ctx &rctx, char *data, const int32_t length)
{
   POOLMEM *buf;

   if (!rctx.efs) {
      rctx.efs = New(worker(10));
      rctx.efs->start(efs_write_thread, &rctx);
   }
   buf = (POOLMEM *)rctx.efs->pop_free_buffer();
   if (!buf) {
      buf = get_memory(length + sizeof(int32_t)+1);
   } else {
      buf = check_pool_memory_size(buf, length+sizeof(int32_t)+1);
   }
   *(int32_t *)buf = length;
   memcpy(buf+sizeof(int32_t), data, length);
   Dmsg2(200, "Put len=%d head=%p\n", length, buf);
   rctx.efs->queue(buf);
   rctx.efs->set_run_state();
   return true;
}

/*
 * The ReadEncryptedFileRaw from bacula.c calls us back here
 */
DWORD WINAPI read_efs_data_cb(PBYTE pbData, PVOID pvCallbackContext, ULONG ulLength)
{
   bctx_t *ctx = (bctx_t *)pvCallbackContext;  /* get our context */
   BSOCK *sd = ctx->jcr->store_bsock;
   ULONG ulSent = 0;

   if (ctx->jcr->is_job_canceled()) {
      return ERROR_CANCELLED;
   }
   if (ulLength == 0) {
      Dmsg0(200, "ulLen=0 => done.\n");
      return ERROR_SUCCESS;           /* all done */
   }
   while (ulLength > 0) {
      /* Get appropriate block length */
      if (ulLength <= (ULONG)ctx->rsize) {
         sd->msglen = ulLength;
      } else {
         sd->msglen = (ULONG)ctx->rsize;
      }
      Dmsg5(200, "ctx->rbuf=%p msg=%p msgbuflen=%d ulSent=%d len=%d\n", 
        ctx->rbuf, sd->msg, ctx->rsize, ulSent, sd->msglen);
      /* Copy data into Bacula buffer */
      memcpy(ctx->rbuf, pbData + ulSent, sd->msglen);
      /* Update sent count and remaining count */
      ulSent += sd->msglen;
      ulLength -= sd->msglen;
      /* Send the data off to the SD */
      if (!process_and_send_data(*ctx)) {
         return ERROR_UNEXP_NET_ERR;
      }
   }
   return ERROR_SUCCESS;
}

#endif
