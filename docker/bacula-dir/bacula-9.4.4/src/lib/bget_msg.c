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
 *  Subroutines to receive network data and handle
 *   network signals for the FD and the SD.
 *
 *   Kern Sibbald, May MMI previously in src/stored/fdmsg.c
 *
 */

#include "bacula.h"
#include "jcr.h"

static char OK_msg[]   = "2000 OK\n";
static char TERM_msg[] = "2999 Terminate\n";

#define msglvl 500

/*
 * This routine does a bnet_recv(), then if a signal was
 *   sent, it handles it.  The return codes are the same as
 *   bne_recv() except the BNET_SIGNAL messages that can
 *   be handled are done so without returning.
 *
 * Returns number of bytes read (may return zero)
 * Returns -1 on signal (BNET_SIGNAL)
 * Returns -2 on hard end of file (BNET_HARDEOF)
 * Returns -3 on error  (BNET_ERROR)
 * Returns -4 on Command (BNET_COMMAND)
 */
int bget_msg(BSOCK *sock)
{
   int n;
   for ( ;; ) {
      n = sock->recv();
      if (n >= 0) {                  /* normal return */
         return n;
      }
      if (sock->is_stop()) {         /* error return */
         return n;
      }
      if (n == BNET_COMMAND) {
         return n;
      }

      /* BNET_SIGNAL (-1) return from bnet_recv() => network signal */
      switch (sock->msglen) {
      case BNET_EOD:               /* end of data */
         Dmsg0(msglvl, "Got BNET_EOD\n");
         return n;
      case BNET_EOD_POLL:
         Dmsg0(msglvl, "Got BNET_EOD_POLL\n");
         if (sock->is_terminated()) {
            sock->fsend(TERM_msg);
         } else {
            sock->fsend(OK_msg); /* send response */
         }
         return n;                 /* end of data */
      case BNET_TERMINATE:
         Dmsg0(msglvl, "Got BNET_TERMINATE\n");
         sock->set_terminated();
         return n;
      case BNET_POLL:
         Dmsg0(msglvl, "Got BNET_POLL\n");
         if (sock->is_terminated()) {
            sock->fsend(TERM_msg);
         } else {
            sock->fsend(OK_msg); /* send response */
         }
         break;
      case BNET_HEARTBEAT:
      case BNET_HB_RESPONSE:
         break;
      case BNET_STATUS:
         /* *****FIXME***** Implement BNET_STATUS */
         Dmsg0(msglvl, "Got BNET_STATUS\n");
         sock->fsend(_("Status OK\n"));
         sock->signal(BNET_EOD);
         break;
      default:
         Emsg1(M_ERROR, 0, _("bget_msg: unknown signal %d\n"), sock->msglen);
         break;
      }
   }
}

bmessage::bmessage(int bufsize)
{
   msg = get_pool_memory(PM_BSOCK);
   msg = realloc_pool_memory(msg, bufsize);
   status = bmessage::bm_busy;
   jobbytes = 0;
}

bmessage::~bmessage()
{
   free_pool_memory(msg);
}

void bmessage::swap(BSOCK *sock)
{
   POOLMEM *swap = sock->msg;
   sock->msg = msg;
   msg = swap;
}

GetMsg::GetMsg(JCR *a_jcr, BSOCK *a_bsock, const char *a_rec_header, int32_t a_bufsize):
      jcr(a_jcr),
      bsock(a_bsock),
      rec_header(a_rec_header),
      bufsize(a_bufsize),
      m_is_stop(false),
      m_is_done(false),
      m_is_error(false),
      m_use_count(1)
{
   jcr->inc_use_count();        /* We own a copy of the JCR */
   bmsg_aux = New(bmessage(bufsize));
   bmsg = bmsg_aux;
   pthread_mutex_init(&mutex, 0);
   pthread_cond_init(&cond, NULL);
};

GetMsg::~GetMsg()
{
   free_jcr(jcr);               /* Release our copy of the JCR */
   delete bmsg_aux;
   pthread_mutex_destroy(&mutex);
   pthread_cond_destroy(&cond);
};

int GetMsg::bget_msg(bmessage **pbmsg)
{
   // Get our own local copy of the socket

   if (pbmsg == NULL) {
      pbmsg = &bmsg_aux;
   }
   bmessage *bmsg = *pbmsg;
   bmsg->ret = ::bget_msg(bsock);
   bmsg->status = bmessage::bm_ready;
   bmsg->rbuflen = bmsg->msglen = bmsg->origlen = bsock->msglen;
/*   bmsg->is_header = !bmsg->is_header; ALAIN SAYS: I think this line is useless */
   /* swap msg instead of copying */
   bmsg->swap(bsock);
   bmsg->rbuf = bmsg->msg;

   msglen = bmsg->msglen;
   msg = bmsg->msg;
   m_is_stop = bsock->is_stop() || bsock->is_error();
   return bmsg->ret;
}
