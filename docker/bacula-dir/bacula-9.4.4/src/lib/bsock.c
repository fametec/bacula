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
 * Network Utility Routines
 *
 * The new code inherit common functions from BSOCKCORE class
 * and implement BSOCK/Bacula specific protocols and data flow.
 *
 *  Written by Kern Sibbald
 *
 * Major refactoring of BSOCK code written by:
 *
 * Radosław Korzeniewski, MMXVIII
 * radoslaw@korzeniewski.net, radekk@inteos.pl
 * Inteos Sp. z o.o. http://www.inteos.pl/
 *
 */

#include "bacula.h"
#include "jcr.h"
#include "lz4.h"
#include <netdb.h>
#include <netinet/tcp.h>

#define BSOCK_DEBUG_LVL    900

#if !defined(ENODATA)              /* not defined on BSD systems */
#define ENODATA  EPIPE
#endif

/* Commands sent to Director */
static char hello[]    = "Hello %s calling\n";

/* Response from Director */
static char OKhello[]   = "1000 OK:";


/*
 * BSOCK default constructor - initializes object.
 */
BSOCK::BSOCK()
{
   init();
};

/*
 * BSOCK special constructor initializes object and sets proper socked descriptor.
 */
BSOCK::BSOCK(int sockfd):
      BSOCKCORE(),
      m_spool_fd(NULL),
      cmsg(NULL),
      m_data_end(0),
      m_last_data_end(0),
      m_FileIndex(0),
      m_lastFileIndex(0),
      m_spool(false),
      m_compress(false),
      m_CommBytes(0),
      m_CommCompressedBytes(0)
{
   init();
   m_terminated = false;
   m_closed = false;
   m_fd = sockfd;
};

/*
 * BSOCK default destructor.
 */
BSOCK::~BSOCK()
{
   Dmsg0(BSOCK_DEBUG_LVL, "BSOCK::~BSOCK()\n");
   _destroy();
};

/*
 * BSOCK initialization method handles bsock specific variables.
 */
void BSOCK::init()
{
   /* the BSOCKCORE::init() is executed in base class constructor */
   timeout = BSOCK_TIMEOUT;
   m_spool_fd = NULL;
   cmsg = get_pool_memory(PM_BSOCK);
}

/*
 * BSOCK private destroy method releases bsock specific variables.
 */
void BSOCK::_destroy()
{
   Dmsg0(BSOCK_DEBUG_LVL, "BSOCK::_destroy()\n");
   if (cmsg) {
      free_pool_memory(cmsg);
      cmsg = NULL;
   }
};

/*
 * Authenticate Director
 */
bool BSOCK::authenticate_director(const char *name, const char *password,
               TLS_CONTEXT *tls_ctx, char *errmsg, int errmsg_len)
{
   int tls_local_need = BNET_TLS_NONE;
   int tls_remote_need = BNET_TLS_NONE;
   int compatible = true;
   char bashed_name[MAX_NAME_LENGTH];
   BSOCK *dir = this;        /* for readability */

   *errmsg = 0;
   /*
    * Send my name to the Director then do authentication
    */

   /* Timeout Hello after 15 secs */
   dir->start_timer(15);
   dir->fsend(hello, bashed_name);

   if (get_tls_enable(tls_ctx)) {
      tls_local_need = get_tls_enable(tls_ctx) ? BNET_TLS_REQUIRED : BNET_TLS_OK;
   }

   /* respond to Dir challenge */
   if (!cram_md5_respond(dir, password, &tls_remote_need, &compatible) ||
       /* Now challenge dir */
       !cram_md5_challenge(dir, password, tls_local_need, compatible)) {
      bsnprintf(errmsg, errmsg_len, _("Director authorization error at \"%s:%d\"\n"),
         dir->host(), dir->port());
      goto bail_out;
   }

   /* Verify that the remote host is willing to meet our TLS requirements */
   if (tls_remote_need < tls_local_need && tls_local_need != BNET_TLS_OK && tls_remote_need != BNET_TLS_OK) {
      bsnprintf(errmsg, errmsg_len, _("Authorization error:"
             " Remote server at \"%s:%d\" did not advertise required TLS support.\n"),
             dir->host(), dir->port());
      goto bail_out;
   }

   /* Verify that we are willing to meet the remote host's requirements */
   if (tls_remote_need > tls_local_need && tls_local_need != BNET_TLS_OK && tls_remote_need != BNET_TLS_OK) {
      bsnprintf(errmsg, errmsg_len, _("Authorization error with Director at \"%s:%d\":"
                     " Remote server requires TLS.\n"),
                     dir->host(), dir->port());

      goto bail_out;
   }

   /* Is TLS Enabled? */
   if (have_tls) {
      if (tls_local_need >= BNET_TLS_OK && tls_remote_need >= BNET_TLS_OK) {
         /* Engage TLS! Full Speed Ahead! */
         if (!bnet_tls_client(tls_ctx, dir, NULL)) {
            bsnprintf(errmsg, errmsg_len, _("TLS negotiation failed with Director at \"%s:%d\"\n"),
               dir->host(), dir->port());
            goto bail_out;
         }
      }
   }

   Dmsg1(6, ">dird: %s", dir->msg);
   if (dir->recv() <= 0) {
      dir->stop_timer();
      bsnprintf(errmsg, errmsg_len, _("Bad errmsg to Hello command: ERR=%s\n"
                      "The Director at \"%s:%d\" may not be running.\n"),
                    dir->bstrerror(), dir->host(), dir->port());
      return false;
   }

   dir->stop_timer();
   Dmsg1(10, "<dird: %s", dir->msg);
   if (strncmp(dir->msg, OKhello, sizeof(OKhello)-1) != 0) {
      bsnprintf(errmsg, errmsg_len, _("Director at \"%s:%d\" rejected Hello command\n"),
         dir->host(), dir->port());
      return false;
   } else {
      bsnprintf(errmsg, errmsg_len, "%s", dir->msg);
   }
   return true;

bail_out:
   dir->stop_timer();
   bsnprintf(errmsg, errmsg_len, _("Authorization error with Director at \"%s:%d\"\n"
             "Most likely the passwords do not agree.\n"
             "If you are using TLS, there may have been a certificate validation error during the TLS handshake.\n"
             "For help, please see: " MANUAL_AUTH_URL "\n"),
             dir->host(), dir->port());
   return false;
}

/*
 * Send a message over the network. Everything is sent in one
 *   write request, but depending on the mode you are using
 *   there will be either two or three read requests done.
 * Read 1: 32 bits, gets essentially the packet length, but may
 *         have the upper bits set to indicate compression or
 *         an extended header packet.
 * Read 2: 32 bits, this read is done only of BNET_HDR_EXTEND is set.
 *         In this case the top 16 bits of this 32 bit word are reserved
 *         for flags and the lower 16 bits for data. This word will be
 *         stored in the field "flags" in the BSOCKCORE packet.
 * Read 2 or 3: depending on if Read 2 is done. This is the data.
 *
 * For normal comm line compression, the whole data packet is compressed
 *   but not the msglen (first read).
 * To do data compression rather than full comm line compression, prior to
 *   call send(flags) where the lower 32 bits is the offset to the data to
 *   be compressed.  The top 32 bits are reserved for flags that can be
 *   set. The are:
 *     BNET_IS_CMD   We are sending a command
 *     BNET_OFFSET   An offset is specified (this implies data compression)
 *     BNET_NOCOMPRESS Inhibit normal comm line compression
 *     BNET_DATACOMPRESSED The data using the specified offset was
 *                   compressed, and normal comm line compression will
 *                   not be done.
 *   If any of the above bits are set, then BNET_HDR_EXTEND will be set
 *   in the top bits of msglen, and the full set of flags + the offset
 *   will be passed as a 32 bit word just after the msglen, and then
 *   followed by any data that is either compressed or not.
 *
 *   Note, neither comm line nor data compression is not
 *   guaranteed since it may result in more data, in which case, the
 *   record is sent uncompressed and there will be no offset.
 *   On the receive side, if BNET_OFFSET is set, then the data is compressed.
 *
 * Returns: false on failure
 *          true  on success
 */
#define display_errmsg() (!m_suppress_error_msgs && m_jcr && m_jcr->JobId != 0)

bool BSOCK::send(int aflags)
{
   int32_t rc;
   int32_t pktsiz;
   int32_t *hdrptr;
   int offset;
   int hdrsiz;
   bool ok = true;
   int32_t save_msglen;
   POOLMEM *save_msg;
   bool compressed;
   bool locked = false;

   if (is_closed()) {
      if (display_errmsg()) {
         Qmsg0(m_jcr, M_ERROR, 0,  _("Socket is closed\n"));
      }
      return false;
   }
   if (errors) {
      if (display_errmsg()) {
         Qmsg4(m_jcr, M_ERROR, 0,  _("Socket has errors=%d on call to %s:%s:%d\n"),
               errors, m_who, m_host, m_port);
      }
      return false;
   }
   if (is_terminated()) {
      if (display_errmsg()) {
         Qmsg4(m_jcr, M_ERROR, 0,  _("Bsock send while terminated=%d on call to %s:%s:%d\n"),
               is_terminated(), m_who, m_host, m_port);
      }
      return false;
   }

   if (msglen > 4000000) {
      if (!m_suppress_error_msgs) {
         Qmsg4(m_jcr, M_ERROR, 0,
            _("Write socket has insane msglen=%d on call to %s:%s:%d\n"),
             msglen, m_who, m_host, m_port);
      }
      return false;
   }

   if (send_hook_cb) {
      if (!send_hook_cb->bsock_send_cb()) {
         Dmsg3(1, "Flowcontrol failure on %s:%s:%d\n", m_who, m_host, m_port);
         Qmsg3(m_jcr, M_ERROR, 0,
            _("Flowcontrol failure on %s:%s:%d\n"),
                  m_who, m_host, m_port);
         return false;
      }
   }
   if (m_use_locking) {
      pP(pm_wmutex);
      locked = true;
   }
   save_msglen = msglen;
   save_msg = msg;
   m_flags = aflags;

   offset = aflags & 0xFF;              /* offset is 16 bits */
   if (offset) {
      m_flags |= BNET_OFFSET;
   }
   if (m_flags & BNET_DATACOMPRESSED) {   /* Check if already compressed */
      compressed = true;
   } else if (m_flags & BNET_NOCOMPRESS) {
      compressed = false;
   } else {
      compressed = comm_compress();       /* do requested compression */
   }
   if (offset && compressed) {
      m_flags |= BNET_DATACOMPRESSED;
   }
   if (!compressed) {
      m_flags &= ~BNET_COMPRESSED;
   }

   /* Compute total packet length */
   if (msglen <= 0) {
      hdrsiz = sizeof(pktsiz);
      pktsiz = hdrsiz;                     /* signal, no data */
   } else if (m_flags) {
      hdrsiz = 2 * sizeof(pktsiz);         /* have 64 bit header */
      pktsiz = msglen + hdrsiz;
   } else {
      hdrsiz = sizeof(pktsiz);             /* have 32 bit header */
      pktsiz = msglen + hdrsiz;
   }

   /* Set special bits */
   if (m_flags & BNET_OFFSET) {            /* if data compression on */
      compressed = false;                  /*   no comm compression */
   }
   if (compressed) {
      msglen |= BNET_COMPRESSED;           /* comm line compression */
   }

   if (m_flags) {
      msglen |= BNET_HDR_EXTEND;           /* extended header */
   }

   /*
    * Store packet length at head of message -- note, we
    *  have reserved an int32_t just before msg, so we can
    *  store there
    */
   hdrptr = (int32_t *)(msg - hdrsiz);
   *hdrptr = htonl(msglen);             /* store signal/length */
   if (m_flags) {
      *(hdrptr+1) = htonl(m_flags);     /* store flags */
   }

   (*pout_msg_no)++;        /* increment message number */

   /* send data packet */
   timer_start = watchdog_time;  /* start timer */
   clear_timed_out();
   /* Full I/O done in one write */
   rc = write_nbytes((char *)hdrptr, pktsiz);
   if (chk_dbglvl(DT_NETWORK|1900)) dump_bsock_msg(m_fd, *pout_msg_no, "SEND", rc, msglen, m_flags, save_msg, save_msglen);
   timer_start = 0;         /* clear timer */
   if (rc != pktsiz) {
      errors++;
      if (errno == 0) {
         b_errno = EIO;
      } else {
         b_errno = errno;
      }
      if (rc < 0) {
         if (!m_suppress_error_msgs) {
            Qmsg5(m_jcr, M_ERROR, 0,
                  _("Write error sending %d bytes to %s:%s:%d: ERR=%s\n"),
                  pktsiz, m_who,
                  m_host, m_port, this->bstrerror());
         }
      } else {
         Qmsg5(m_jcr, M_ERROR, 0,
               _("Wrote %d bytes to %s:%s:%d, but only %d accepted.\n"),
               pktsiz, m_who, m_host, m_port, rc);
      }
      ok = false;
   }
//   Dmsg4(000, "cmpr=%d ext=%d cmd=%d m_flags=0x%x\n", msglen&BNET_COMPRESSED?1:0,
//      msglen&BNET_HDR_EXTEND?1:0, msglen&BNET_CMD_BIT?1:0, m_flags);
   msglen = save_msglen;
   msg = save_msg;
   if (locked) pV(pm_wmutex);
   return ok;
}

/*
 * Receive a message from the other end. Each message consists of
 * two packets. The first is a header that contains the size
 * of the data that follows in the second packet.
 * Returns number of bytes read (may return zero)
 * Returns -1 on signal (BNET_SIGNAL)
 * Returns -2 on hard end of file (BNET_HARDEOF)
 * Returns -3 on error  (BNET_ERROR)
 * Returns -4 on COMMAND (BNET_COMMAND)
 *  Unfortunately, it is a bit complicated because we have these
 *    four return types:
 *    1. Normal data
 *    2. Signal including end of data stream
 *    3. Hard end of file
 *    4. Error
 *  Using bsock->is_stop() and bsock->is_error() you can figure this all out.
 */
int32_t BSOCK::recv()
{
   int32_t nbytes;
   int32_t pktsiz;
   int32_t o_pktsiz = 0;
   bool compressed = false;
   bool command = false;
   bool locked = false;

   cmsg[0] = msg[0] = 0;
   msglen = 0;
   m_flags = 0;
   if (errors || is_terminated() || is_closed()) {
      return BNET_HARDEOF;
   }
   if (m_use_locking) {
      pP(pm_rmutex);
      locked = true;
   }

   read_seqno++;            /* bump sequence number */
   timer_start = watchdog_time;  /* set start wait time */
   clear_timed_out();
   /* get data size -- in int32_t */
   if ((nbytes = read_nbytes((char *)&pktsiz, sizeof(int32_t))) <= 0) {
      timer_start = 0;      /* clear timer */
      /* probably pipe broken because client died */
      if (errno == 0) {
         b_errno = ENODATA;
      } else {
         b_errno = errno;
      }
      errors++;
      nbytes = BNET_HARDEOF;        /* assume hard EOF received */
      goto get_out;
   }
   timer_start = 0;         /* clear timer */
   if (nbytes != sizeof(int32_t)) {
      errors++;
      b_errno = EIO;
      Qmsg5(m_jcr, M_ERROR, 0, _("Read expected %d got %d from %s:%s:%d\n"),
            sizeof(int32_t), nbytes, m_who, m_host, m_port);
      nbytes = BNET_ERROR;
      goto get_out;
   }

   pktsiz = ntohl(pktsiz);         /* decode no. of bytes that follow */
   o_pktsiz = pktsiz;
   /* If extension, read it */
   if (pktsiz > 0 && (pktsiz & BNET_HDR_EXTEND)) {
      timer_start = watchdog_time;  /* set start wait time */
      clear_timed_out();
      if ((nbytes = read_nbytes((char *)&m_flags, sizeof(int32_t))) <= 0) {
         timer_start = 0;      /* clear timer */
         /* probably pipe broken because client died */
         if (errno == 0) {
            b_errno = ENODATA;
         } else {
            b_errno = errno;
         }
         errors++;
         nbytes = BNET_HARDEOF;        /* assume hard EOF received */
         goto get_out;
      }
      timer_start = 0;         /* clear timer */
      if (nbytes != sizeof(int32_t)) {
         errors++;
         b_errno = EIO;
         Qmsg5(m_jcr, M_ERROR, 0, _("Read expected %d got %d from %s:%s:%d\n"),
               sizeof(int32_t), nbytes, m_who, m_host, m_port);
         nbytes = BNET_ERROR;
         goto get_out;
      }
      pktsiz &= ~BNET_HDR_EXTEND;
      m_flags = ntohl(m_flags);
   }

   if (pktsiz > 0 && (pktsiz & BNET_COMPRESSED)) {
      compressed = true;
      pktsiz &= ~BNET_COMPRESSED;
   }

   if (m_flags & BNET_IS_CMD) {
       command = true;
   }
   if (m_flags & BNET_OFFSET) {
      compressed = true;
   }

   if (pktsiz == 0) {              /* No data transferred */
      timer_start = 0;             /* clear timer */
      in_msg_no++;
      msglen = 0;
      nbytes = 0;                  /* zero bytes read */
      goto get_out;
   }

   /* If signal or packet size too big */
   if (pktsiz < 0 || pktsiz > 1000000) {
      if (pktsiz > 0) {            /* if packet too big */
         if (m_jcr) {
            Qmsg4(m_jcr, M_FATAL, 0,
               _("Packet size=%d too big from \"%s:%s:%d\". Maximum permitted 1000000. Terminating connection.\n"),
               pktsiz, m_who, m_host, m_port);
         }
         pktsiz = BNET_TERMINATE;  /* hang up */
      }
      if (pktsiz == BNET_TERMINATE) {
         set_terminated();
      }
      timer_start = 0;                /* clear timer */
      b_errno = ENODATA;
      msglen = pktsiz;                /* signal code */
      nbytes =  BNET_SIGNAL;          /* signal */
      goto get_out;
   }

   /* Make sure the buffer is big enough + one byte for EOS */
   if (pktsiz >= (int32_t) sizeof_pool_memory(msg)) {
      msg = realloc_pool_memory(msg, pktsiz + 100);
   }

   timer_start = watchdog_time;  /* set start wait time */
   clear_timed_out();
   /* now read the actual data */
   if ((nbytes = read_nbytes(msg, pktsiz)) <= 0) {
      timer_start = 0;      /* clear timer */
      if (errno == 0) {
         b_errno = ENODATA;
      } else {
         b_errno = errno;
      }
      errors++;
      Qmsg4(m_jcr, M_ERROR, 0, _("Read error from %s:%s:%d: ERR=%s\n"),
            m_who, m_host, m_port, this->bstrerror());
      nbytes = BNET_ERROR;
      goto get_out;
   }
   timer_start = 0;         /* clear timer */
   in_msg_no++;
   msglen = nbytes;
   if (nbytes != pktsiz) {
      b_errno = EIO;
      errors++;
      Qmsg5(m_jcr, M_ERROR, 0, _("Read expected %d got %d from %s:%s:%d\n"),
            pktsiz, nbytes, m_who, m_host, m_port);
      nbytes = BNET_ERROR;
      goto get_out;
   }
   /* If compressed uncompress it */
   if (compressed) {
      int offset = 0;
      int psize = nbytes * 4;
      if (psize >= ((int32_t)sizeof_pool_memory(cmsg))) {
         cmsg = realloc_pool_memory(cmsg, psize);
      }
      psize = sizeof_pool_memory(cmsg);
      if (m_flags & BNET_OFFSET) {
         offset = m_flags & 0xFF;
         msg += offset;
         msglen -= offset;
      }
      /* Grow buffer to max approx 4MB */
      for (int i=0; i < 7; i++) {
         nbytes = LZ4_decompress_safe(msg, cmsg, msglen, psize);
         if (nbytes >=  0) {
            break;
         }
         if (psize < 65536) {
            psize = 65536;
         } else {
            psize = psize * 2;
         }
         if (psize >= ((int32_t)sizeof_pool_memory(cmsg))) {
            cmsg = realloc_pool_memory(cmsg, psize + 100);
         }
      }
      if (m_flags & BNET_OFFSET) {
         msg -= offset;
         msglen += offset;
      }
      if (nbytes < 0) {
         Jmsg1(m_jcr, M_ERROR, 0, "Decompress error!!!! ERR=%d\n", nbytes);
         Pmsg3(000, "Decompress error!! pktsiz=%d cmsgsiz=%d nbytes=%d\n", pktsiz,
           psize, nbytes);
         b_errno = EIO;
         errors++;
          Qmsg5(m_jcr, M_ERROR, 0, _("Read expected %d got %d from %s:%s:%d\n"),
               pktsiz, nbytes, m_who, m_host, m_port);
         nbytes = BNET_ERROR;
         goto get_out;
      }
      msglen = nbytes;
      /* Make sure the buffer is big enough + one byte for EOS */
      if (msglen >= (int32_t)sizeof_pool_memory(msg)) {
         msg = realloc_pool_memory(msg, msglen + 100);
      }
      /* If this is a data decompress, leave msg compressed */
      if (!(m_flags & BNET_OFFSET)) {
         memcpy(msg, cmsg, msglen);
      }
   }

   /* always add a zero by to properly terminate any
    * string that was send to us. Note, we ensured above that the
    * buffer is at least one byte longer than the message length.
    */
   msg[nbytes] = 0; /* terminate in case it is a string */
   /*
    * The following uses *lots* of resources so turn it on only for
    * serious debugging.
    */
   Dsm_check(300);

get_out:
   if ((chk_dbglvl(DT_NETWORK|1900))) dump_bsock_msg(m_fd, read_seqno, "RECV", nbytes, o_pktsiz, m_flags, msg, msglen);
   if (nbytes != BNET_ERROR && command) {
      nbytes = BNET_COMMAND;
   }

   if (locked) pV(pm_rmutex);
   return nbytes;                  /* return actual length of message */
}

/*
 * Send a signal
 */
bool BSOCK::signal(int signal)
{
   msglen = signal;
   if (signal == BNET_TERMINATE) {
      m_suppress_error_msgs = true;
   }
   return send();
}

/*
 * Despool spooled attributes
 */
bool BSOCK::despool(void update_attr_spool_size(ssize_t size), ssize_t tsize)
{
   int32_t pktsiz;
   size_t nbytes;
   ssize_t last = 0, size = 0;
   int count = 0;
   JCR *jcr = get_jcr();

   rewind(m_spool_fd);

#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_WILLNEED)
   posix_fadvise(fileno(m_spool_fd), 0, 0, POSIX_FADV_WILLNEED);
#endif

   while (fread((char *)&pktsiz, 1, sizeof(int32_t), m_spool_fd) ==
          sizeof(int32_t)) {
      size += sizeof(int32_t);
      msglen = ntohl(pktsiz);
      if (msglen > 0) {
         if (msglen > (int32_t)sizeof_pool_memory(msg)) {
            msg = realloc_pool_memory(msg, msglen + 1);
         }
         nbytes = fread(msg, 1, msglen, m_spool_fd);
         if (nbytes != (size_t)msglen) {
            berrno be;
            Dmsg2(400, "nbytes=%d msglen=%d\n", nbytes, msglen);
            Qmsg2(get_jcr(), M_FATAL, 0, _("fread attr spool error. Wanted=%d got=%d bytes.\n"),
                  msglen, nbytes);
            update_attr_spool_size(tsize - last);
            return false;
         }
         size += nbytes;
         if ((++count & 0x3F) == 0) {
            update_attr_spool_size(size - last);
            last = size;
         }
      }
      send();
      if (jcr && job_canceled(jcr)) {
         return false;
      }
   }
   update_attr_spool_size(tsize - last);
   if (ferror(m_spool_fd)) {
      Qmsg(jcr, M_FATAL, 0, _("fread attr spool I/O error.\n"));
      return false;
   }
   return true;
}

/*
 * Open a TCP connection to the server
 * Returns NULL
 * Returns BSOCKCORE * pointer on success
 */
bool BSOCK::open(JCR *jcr, const char *name, char *host, char *service,
               int port, utime_t heart_beat, int *fatal)
{
   bool status = BSOCKCORE::open(jcr, name, host, service, port, heart_beat, fatal);
   m_spool = false;
   return status;
};

/*
 * Do comm line compression (LZ4) of a bsock message.
 * Returns:  true if the compression was done
 *           false if no compression was done
 * The "offset" defines where to start compressing the message.  This
 *   allows passing "header" information uncompressed and the actual
 *   data part compressed.
 *
 * Note, we don't compress lines less than 20 characters because we
 *  want to save at least 10 characters otherwise compression doesn't
 *  help enough to warrant doing the decompression.
 */
bool BSOCK::comm_compress()
{
   bool compress = false;
   bool compressed = false;
   int offset = m_flags & 0xFF;

   /*
    * Enable compress if allowed and not spooling and the
    *  message is long enough (>20) to get some reasonable savings.
    */
   if (msglen > 20) {
      compress = can_compress() && !is_spooling();
   }
   m_CommBytes += msglen;                    /* uncompressed bytes */
   Dmsg4(DT_NETWORK|200, "can_compress=%d compress=%d CommBytes=%lld CommCompresedBytes=%lld\n",
         can_compress(), compress, m_CommBytes, m_CommCompressedBytes);
   if (compress) {
      int clen;
      int need_size;

      ASSERT2(offset <= msglen, "Comm offset bigger than message\n");
      ASSERT2(offset < 255, "Offset greater than 254\n");
      need_size = LZ4_compressBound(msglen);
      if (need_size >= ((int32_t)sizeof_pool_memory(cmsg))) {
         cmsg = realloc_pool_memory(cmsg, need_size + 100);
      }
      msglen -= offset;
      msg += offset;
      cmsg += offset;
      clen = LZ4_compress_default(msg, cmsg, msglen, msglen);
      //Dmsg2(000, "clen=%d msglen=%d\n", clen, msglen);
      /* Compression should save at least 10 characters */
      if (clen > 0 && clen + 10 <= msglen) {

#ifdef xxx_debug
         /* Debug code -- decompress and compare */
         int blen, rlen, olen;
         olen = msglen;
         POOLMEM *rmsg = get_pool_memory(PM_BSOCK);
         blen = sizeof_pool_memory(msg) * 2;
         if (blen >= sizeof_pool_memory(rmsg)) {
            rmsg = realloc_pool_memory(rmsg, blen);
         }
         rlen = LZ4_decompress_safe(cmsg, rmsg, clen, blen);
         //Dmsg4(000, "blen=%d clen=%d olen=%d rlen=%d\n", blen, clen, olen, rlen);
         ASSERT(olen == rlen);
         ASSERT(memcmp(msg, rmsg, olen) == 0);
         free_pool_memory(rmsg);
         /* end Debug code */
#endif

         msg = cmsg;
         msglen = clen;
         compressed = true;
      }
      msglen += offset;
      msg -= offset;
      cmsg -= offset;
   }
   m_CommCompressedBytes += msglen;
   return compressed;
}

/*
 * Note, this routine closes the socket, but leaves the
 *   bsock memory in place.
 *   every thread is responsible of closing and destroying its own duped or not
 *   duped BSOCKCORE
 */
void BSOCK::close()
{
   Dmsg0(BSOCK_DEBUG_LVL, "BSOCK::close()\n");
   BSOCKCORE::close();
   return;
}

/*
 * Write nbytes to the network.
 * It may require several writes.
 */

int32_t BSOCK::write_nbytes(char *ptr, int32_t nbytes)
{
   int32_t nwritten;

   if (is_spooling()) {
      nwritten = fwrite(ptr, 1, nbytes, m_spool_fd);
      if (nwritten != nbytes) {
         berrno be;
         b_errno = errno;
         Qmsg3(jcr(), M_FATAL, 0, _("Attr spool write error. wrote=%d wanted=%d bytes. ERR=%s\n"),
               nbytes, nwritten, be.bstrerror());
         Dmsg2(400, "nwritten=%d nbytes=%d.\n", nwritten, nbytes);
         errno = b_errno;
         return -1;
      }
      return nbytes;
   }

   /* reuse base code */
   return BSOCKCORE::write_nbytes(ptr, nbytes);
}

/*
 * This is a non-class BSOCK "constructor"  because we want to
 *   call the Bacula smartalloc routines instead of new.
 */
BSOCK *new_bsock()
{
   BSOCK *bsock = New(BSOCK);
   return bsock;
}


/* Initialize internal socket structure.
 *  This probably should be done in bsock.c
 */
BSOCK *init_bsock(JCR *jcr, int sockfd, const char *who,
                   const char *host, int port, struct sockaddr *client_addr)
{
   Dmsg4(100, "socket=%d who=%s host=%s port=%d\n", sockfd, who, host, port);
   BSOCK *bsock = New(BSOCK(sockfd));
   bsock->m_master = bsock; /* don't use set_master() here */
   bsock->set_who(bstrdup(who));
   bsock->set_host(bstrdup(host));
   bsock->set_port(port);
   bmemzero(&bsock->peer_addr, sizeof(bsock->peer_addr));
   memcpy(&bsock->client_addr, client_addr, sizeof(bsock->client_addr));
   bsock->set_jcr(jcr);
   return bsock;
}

BSOCK *dup_bsock(BSOCK *osock)
{
   POOLMEM *cmsg;
   POOLMEM *msg;
   POOLMEM *errmsg;

   osock->set_locking();
   BSOCK *bsock = New(BSOCK);
   // save already allocated variables
   msg = bsock->msg;
   cmsg = bsock->cmsg;
   errmsg = bsock->errmsg;
   // with this we make virtually the same job as with memcpy()
   *bsock = *osock;
   // restore saved variables
   bsock->msg = msg;
   bsock->cmsg = cmsg;
   bsock->errmsg = errmsg;
   if (osock->who()) {
      bsock->set_who(bstrdup(osock->who()));
   }
   if (osock->host()) {
      bsock->set_host(bstrdup(osock->host()));
   }
   if (osock->src_addr) {
      bsock->src_addr = New( IPADDR( *(osock->src_addr)) );
   }
   bsock->set_duped();
   bsock->set_master(osock);
   return bsock;
}

#ifndef TEST_PROGRAM
#define TEST_PROGRAM_A
#endif

void BSOCK::dump()
{
#ifdef TEST_PROGRAM
   char ed1[50];
   BSOCKCORE::dump();
   Pmsg1(-1, "BSOCK::dump(): %p\n", this);
   Pmsg1(-1, "\tm_spool_fd: %p\n", m_spool_fd);
   Pmsg1(-1, "\tcmsg: %p\n", cmsg);
   Pmsg1(-1, "\tm_data_end: %s\n", edit_int64(m_data_end, ed1));
   Pmsg1(-1, "\tm_last_data_end: %s\n", edit_int64(m_last_data_end, ed1));
   Pmsg1(-1, "\tm_FileIndex: %s\n", edit_int64(m_FileIndex, ed1));
   Pmsg1(-1, "\tm_lastFileIndex: %s\n", edit_int64(m_lastFileIndex, ed1));
   Pmsg1(-1, "\tm_spool: %s\n", m_spool?"true":"false");
   Pmsg1(-1, "\tm_compress: %s\n", m_compress?"true":"false");
   Pmsg1(-1, "\tm_CommBytes: %s\n", edit_uint64(m_CommBytes, ed1));
   Pmsg1(-1, "\tm_CommCompressedBytes: %s\n", edit_uint64(m_CommCompressedBytes, ed1));
#endif
};

#ifdef TEST_PROGRAM
#include "unittests.h"

void free_my_jcr(JCR *jcr){
   /* TODO: handle full JCR free */
   free_jcr(jcr);
};

#define  ofnamefmt      "/tmp/bsock.%d.test"
const char *data =      "This is a BSOCK communication test: 1234567\n";
const char *hexdata =   "< 00000000 00 00 00 2c 54 68 69 73 20 69 73 20 61 20 42 53 # ...,This is a BS\n" \
                        "< 00000010 4f 43 4b 20 63 6f 6d 6d 75 6e 69 63 61 74 69 6f # OCK communicatio\n" \
                        "< 00000020 6e 20 74 65 73 74 3a 20 31 32 33 34 35 36 37 0a # n test: 1234567.\n";

int main()
{
   Unittests bsock_test("bsock_test", true);
   BSOCK *bs;
   BSOCK *bsdup;
   pid_t pid;
   int rc;
   char *host = (char*)"localhost";
   char *name = (char*)"Test";
   JCR *jcr;
   bool btest;
   char buf[256];       // extend this buffer when hexdata becomes longer
   int fd;

   Pmsg0(0, "Initialize tests ...\n");

   jcr = new_jcr(sizeof(JCR), NULL);
   bs = New(BSOCK);
   bs->set_jcr(jcr);
   ok(bs != NULL && bs->jcr() == jcr,
         "Default initialization");

   Pmsg0(0, "Preparing fork\n");
   pid = fork();
   if (0 == pid){
      Pmsg0(0, "Prepare to execute netcat\n");
      pid_t mypid = getpid();
      char ofname[30];
      snprintf(ofname, sizeof(ofname), ofnamefmt, mypid);
      rc = execl("/bin/netcat", "netcat", "-v", "-p", "20000", "-l", "-o", ofname, NULL);
      Pmsg1(0, "Error executing netcat: %s\n", strerror(rc));
      exit(1);
   }
   Pmsg1(0, "After fork: %d\n", pid);
   bmicrosleep(2, 0);      // we wait a bit to netcat to start
   btest = bs->connect(jcr, 1, 10, 0, name, host, NULL, 20000, 0);
   ok(btest, "BSOCK connection test");
   if (btest) {
      /* connected */
      bsdup = dup_bsock(bs);
      ok(bsdup->is_duped() && bsdup->jcr() == jcr,
            "Check duped BSOCK");
      delete(bsdup);
      /* we are connected, so send some data */
      bs->fsend("%s", data);
      bmicrosleep(2, 0);      // wait until data received by netcat
      bs->close();
      ok(bs->is_closed(), "Close bsock");
      /* now check what netcat received */
      char ofname[30];
      snprintf(ofname, sizeof(ofname), ofnamefmt, pid);
      fd = open(ofname, O_RDONLY);
      btest = false;
      if (fd > 0){
         btest = true;
         read(fd, buf, strlen(hexdata));
         close(fd);
         unlink(ofname);
      }
      ok(btest, "Output file available");
      ok(strcmp(buf, hexdata) == 0, "Communication data");
   }
   kill(pid, SIGTERM);
   delete(bs);
   free_my_jcr(jcr);
   term_last_jobs_list();

   return report();
};
#endif /* TEST_PROGRAM */
