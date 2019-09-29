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
 * Bacula Core Sock Class definition
 *
 * Kern Sibbald, May MM
 *
 * Major refactoring of BSOCK code written by:
 *
 * Radosław Korzeniewski, MMXVIII
 * radoslaw@korzeniewski.net, radekk@inteos.pl
 * Inteos Sp. z o.o. http://www.inteos.pl/
 *
 * This is a common class for socket network communication derived from
 * BSOCK class. It acts as a base class for non-Bacula network communication
 * and as a base class for standard BSOCK implementation. Basically the BSOCK
 * class did not changed its functionality for any Bacula specific part.
 * Now you can use a BSOCKCLASS for other network communication.
 */

#include "bacula.h"
#include "jcr.h"
#include <netdb.h>
#include <netinet/tcp.h>

#define BSOCKCORE_DEBUG_LVL    900

#if !defined(ENODATA)              /* not defined on BSD systems */
#define ENODATA  EPIPE
#endif

#if !defined(SOL_TCP)              /* Not defined on some systems */
#define SOL_TCP  IPPROTO_TCP
#endif

#ifdef HAVE_WIN32
#include <mswsock.h>
static void win_close_wait(int fd);
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#endif

/*
 * make a nice dump of a message
 */
void dump_bsock_msg(int sock, uint32_t msgno, const char *what, uint32_t rc, int32_t pktsize, uint32_t flags,
                     POOLMEM *msg, int32_t msglen)
{
   char buf[54];
   bool is_ascii;
   int dbglvl = DT_ASX;

   if (msglen<0) {
      Dmsg4(dbglvl, "%s %d:%d SIGNAL=%s\n", what, sock, msgno, bnet_sig_to_ascii(msglen));
      // data
      smartdump(msg, msglen, buf, sizeof(buf)-9, &is_ascii);
      if (is_ascii) {
         Dmsg5(dbglvl, "%s %d:%d len=%d \"%s\"\n", what, sock, msgno, msglen, buf);
      } else {
         Dmsg5(dbglvl, "%s %d:%d len=%d %s\n", what, sock, msgno, msglen, buf);
      }
   }
}

BSOCKCallback::BSOCKCallback()
{
}

BSOCKCallback::~BSOCKCallback()
{
}

/*
 * Default constructor does class initialization.
 */
BSOCKCORE::BSOCKCORE() :
   msg(NULL),
   errmsg(NULL),
   res(NULL),
   tls(NULL),
   src_addr(NULL),
   read_seqno(0),
   in_msg_no(0),
   out_msg_no(0),
   pout_msg_no(NULL),
   msglen(0),
   timer_start(0),
   timeout(0),
   m_fd(-1),
   b_errno(0),
   m_blocking(0),
   errors(0),
   m_suppress_error_msgs(false),
   send_hook_cb(NULL),
   m_next(NULL),
   m_jcr(NULL),
   pm_rmutex(NULL),
   pm_wmutex(NULL),
   m_who(NULL),
   m_host(NULL),
   m_port(0),
   m_tid(NULL),
   m_flags(0),
   m_timed_out(false),
   m_terminated(false),
   m_closed(false),
   m_duped(false),
   m_use_locking(false),
   m_bwlimit(0),
   m_nb_bytes(0),
   m_last_tick(0)
{
   pthread_mutex_init(&m_rmutex, NULL);
   pthread_mutex_init(&m_wmutex, NULL);
   pthread_mutex_init(&m_mmutex, NULL);
   bmemzero(&peer_addr, sizeof(peer_addr));
   bmemzero(&client_addr, sizeof(client_addr));
   init();
};

/*
 * Default destructor releases resources.
 */
BSOCKCORE::~BSOCKCORE()
{
   Dmsg0(BSOCKCORE_DEBUG_LVL, "BSOCKCORE::~BSOCKCORE()\n");
   _destroy();
};

/*
 * Initialization method.
 */
void BSOCKCORE::init()
{
   m_master = this;
   set_closed();
   set_terminated();
   m_blocking = 1;
   msg = get_pool_memory(PM_BSOCK);
   errmsg = get_pool_memory(PM_MESSAGE);
   timeout = BSOCKCORE_TIMEOUT;
   pout_msg_no = &out_msg_no;
}

void BSOCKCORE::free_tls()
{
   free_tls_connection(this->tls);
   this->tls = NULL;
}

/*
 * Try to connect to host for max_retry_time at retry_time intervals.
 *   Note, you must have called the constructor prior to calling
 *   this routine.
 */
bool BSOCKCORE::connect(JCR * jcr, int retry_interval, utime_t max_retry_time,
                    utime_t heart_beat,
                    const char *name, char *host, char *service, int port,
                    int verbose)
{
   bool ok = false;
   int i;
   int fatal = 0;
   time_t begin_time = time(NULL);
   time_t now;
   btimer_t *tid = NULL;

   /* Try to trap out of OS call when time expires */
   if (max_retry_time) {
      tid = start_thread_timer(jcr, pthread_self(), (uint32_t)max_retry_time);
   }

   for (i = 0; !open(jcr, name, host, service, port, heart_beat, &fatal);
        i -= retry_interval) {
      berrno be;
      if (fatal || (jcr && job_canceled(jcr))) {
         goto bail_out;
      }
      Dmsg4(50, "Unable to connect to %s on %s:%d. ERR=%s\n",
            name, host, port, be.bstrerror());
      if (i < 0) {
         i = 60 * 5;               /* complain again in 5 minutes */
         if (verbose)
            Qmsg4(jcr, M_WARNING, 0, _(
               "Could not connect to %s on %s:%d. ERR=%s\n"
               "Retrying ...\n"), name, host, port, be.bstrerror());
      }
      bmicrosleep(retry_interval, 0);
      now = time(NULL);
      if (begin_time + max_retry_time <= now) {
         Qmsg4(jcr, M_FATAL, 0, _("Unable to connect to %s on %s:%d. ERR=%s\n"),
               name, host, port, be.bstrerror());
         goto bail_out;
      }
   }
   ok = true;

bail_out:
   if (tid) {
      stop_thread_timer(tid);
   }
   return ok;
}

/*
 * Finish initialization of the packet structure.
 */
void BSOCKCORE::fin_init(JCR * jcr, int sockfd, const char *who, const char *host, int port,
               struct sockaddr *lclient_addr)
{
   Dmsg3(100, "who=%s host=%s port=%d\n", who, host, port);
   m_fd = sockfd;
   if (m_who) {
      free(m_who);
   }
   if (m_host) {
      free(m_host);
   }
   set_who(bstrdup(who));
   set_host(bstrdup(host));
   set_port(port);
   memcpy(&client_addr, lclient_addr, sizeof(client_addr));
   set_jcr(jcr);
}

/*
 * Copy the address from the configuration dlist that gets passed in
 */
void BSOCKCORE::set_source_address(dlist *src_addr_list)
{
   IPADDR *addr = NULL;

   // delete the object we already have, if it's allocated
   if (src_addr) {
     /* TODO: Why free() instead of delete as src_addr is a IPADDR class */
     free( src_addr);
     src_addr = NULL;
   }

   if (src_addr_list) {
     addr = (IPADDR*) src_addr_list->first();
     src_addr = New( IPADDR(*addr));
   }
}

/*
 * Open a TCP connection to the server
 *    Returns true when connection was successful or false otherwise.
 */
bool BSOCKCORE::open(JCR *jcr, const char *name, char *host, char *service,
               int port, utime_t heart_beat, int *fatal)
{
   int sockfd = -1;
   dlist *addr_list;
   IPADDR *ipaddr;
   bool connected = false;
   int turnon = 1;
   const char *errstr;
   int save_errno = 0;

   /*
    * Fill in the structure serv_addr with the address of
    * the server that we want to connect with.
    */
   if ((addr_list = bnet_host2ipaddrs(host, 0, &errstr)) == NULL) {
      /* Note errstr is not malloc'ed */
      Qmsg2(jcr, M_ERROR, 0, _("gethostbyname() for host \"%s\" failed: ERR=%s\n"),
            host, errstr);
      Dmsg2(100, "bnet_host2ipaddrs() for host %s failed: ERR=%s\n",
            host, errstr);
      *fatal = 1;
      return false;
   }

   remove_duplicate_addresses(addr_list);
   foreach_dlist(ipaddr, addr_list) {
      ipaddr->set_port_net(htons(port));
      char allbuf[256 * 10];
      char curbuf[256];
      Dmsg2(100, "Current %sAll %s\n",
                   ipaddr->build_address_str(curbuf, sizeof(curbuf)),
                   build_addresses_str(addr_list, allbuf, sizeof(allbuf)));
      /* Open a TCP socket */
      if ((sockfd = socket(ipaddr->get_family(), SOCK_STREAM|SOCK_CLOEXEC, 0)) < 0) {
         berrno be;
         save_errno = errno;
         switch (errno) {
#ifdef EAFNOSUPPORT
         case EAFNOSUPPORT:
            /*
             * The name lookup of the host returned an address in a protocol family
             * we don't support. Suppress the error and try the next address.
             */
            break;
#endif
#ifdef EPROTONOSUPPORT
         /* See above comments */
         case EPROTONOSUPPORT:
            break;
#endif
#ifdef EPROTOTYPE
         /* See above comments */
         case EPROTOTYPE:
            break;
#endif
         default:
            *fatal = 1;
            Qmsg3(jcr, M_ERROR, 0,  _("Socket open error. proto=%d port=%d. ERR=%s\n"),
               ipaddr->get_family(), ipaddr->get_port_host_order(), be.bstrerror());
            Pmsg3(300, _("Socket open error. proto=%d port=%d. ERR=%s\n"),
               ipaddr->get_family(), ipaddr->get_port_host_order(), be.bstrerror());
            break;
         }
         continue;
      }

      /* Bind to the source address if it is set */
      if (src_addr) {
         if (bind(sockfd, src_addr->get_sockaddr(), src_addr->get_sockaddr_len()) < 0) {
            berrno be;
            save_errno = errno;
            *fatal = 1;
            Qmsg2(jcr, M_ERROR, 0, _("Source address bind error. proto=%d. ERR=%s\n"),
                  src_addr->get_family(), be.bstrerror() );
            Pmsg2(000, _("Source address bind error. proto=%d. ERR=%s\n"),
                  src_addr->get_family(), be.bstrerror() );
            if (sockfd >= 0) socketClose(sockfd);
            continue;
         }
      }

      /*
       * Keep socket from timing out from inactivity
       */
      if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (sockopt_val_t)&turnon, sizeof(turnon)) < 0) {
         berrno be;
         Qmsg1(jcr, M_WARNING, 0, _("Cannot set SO_KEEPALIVE on socket: %s\n"),
               be.bstrerror());
      }
#if defined(TCP_KEEPIDLE)
      if (heart_beat) {
         int opt = heart_beat;
         if (setsockopt(sockfd, SOL_TCP, TCP_KEEPIDLE, (sockopt_val_t)&opt, sizeof(opt)) < 0) {
            berrno be;
            Qmsg1(jcr, M_WARNING, 0, _("Cannot set TCP_KEEPIDLE on socket: %s\n"),
                  be.bstrerror());
         }
      }
#endif

      /* connect to server */
      if (::connect(sockfd, ipaddr->get_sockaddr(), ipaddr->get_sockaddr_len()) < 0) {
         save_errno = errno;
         if (sockfd >= 0) socketClose(sockfd);
         continue;
      }
      *fatal = 0;
      connected = true;
      break;
   }

   if (!connected) {
      berrno be;
      free_addresses(addr_list);
      errno = save_errno | b_errno_win32;
      Dmsg4(50, "Could not connect to server %s %s:%d. ERR=%s\n",
            name, host, port, be.bstrerror());
      return false;
   }
   /*
    * Keep socket from timing out from inactivity
    *   Do this a second time out of paranoia
    */
   if (setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (sockopt_val_t)&turnon, sizeof(turnon)) < 0) {
      berrno be;
      Qmsg1(jcr, M_WARNING, 0, _("Cannot set SO_KEEPALIVE on socket: %s\n"),
            be.bstrerror());
   }
   fin_init(jcr, sockfd, name, host, port, ipaddr->get_sockaddr());
   free_addresses(addr_list);

   /* Clean the packet a bit */
   m_closed = false;
   m_duped = false;
   // Moved to BSOCK m_spool = false;
   m_use_locking = false;
   m_timed_out = false;
   m_terminated = false;
   m_suppress_error_msgs = false;
   errors = 0;
   m_blocking = 0;

   Dmsg3(50, "OK connected to server  %s %s:%d.\n",
         name, host, port);

   return true;
}

/*
 * Force read/write to use locking
 */
bool BSOCKCORE::set_locking()
{
   int stat;
   if (m_use_locking) {
      return true;                      /* already set */
   }
   pm_rmutex = &m_rmutex;
   pm_wmutex = &m_wmutex;
   if ((stat = pthread_mutex_init(pm_rmutex, NULL)) != 0) {
      berrno be;
      Qmsg(m_jcr, M_FATAL, 0, _("Could not init bsockcore read mutex. ERR=%s\n"),
         be.bstrerror(stat));
      return false;
   }
   if ((stat = pthread_mutex_init(pm_wmutex, NULL)) != 0) {
      berrno be;
      Qmsg(m_jcr, M_FATAL, 0, _("Could not init bsockcore write mutex. ERR=%s\n"),
         be.bstrerror(stat));
      return false;
   }
   if ((stat = pthread_mutex_init(&m_mmutex, NULL)) != 0) {
      berrno be;
      Qmsg(m_jcr, M_FATAL, 0, _("Could not init bsockcore attribute mutex. ERR=%s\n"),
         be.bstrerror(stat));
      return false;
   }
   m_use_locking = true;
   return true;
}

void BSOCKCORE::clear_locking()
{
   if (!m_use_locking || m_duped) {
      return;
   }
   m_use_locking = false;
   pthread_mutex_destroy(pm_rmutex);
   pthread_mutex_destroy(pm_wmutex);
   pthread_mutex_destroy(&m_mmutex);
   pm_rmutex = NULL;
   pm_wmutex = NULL;
   return;
}

/*
 * Send a message over the network. Everything is sent in one write request.
 *
 * Returns: false on failure
 *          true  on success
 */
bool BSOCKCORE::send()
{
   int32_t rc;
   bool ok = true;
   bool locked = false;

   if (is_closed()) {
      if (!m_suppress_error_msgs) {
         Qmsg0(m_jcr, M_ERROR, 0,  _("Socket is closed\n"));
      }
      return false;
   }
   if (errors) {
      if (!m_suppress_error_msgs) {
         Qmsg4(m_jcr, M_ERROR, 0,  _("Socket has errors=%d on call to %s:%s:%d\n"),
             errors, m_who, m_host, m_port);
      }
      return false;
   }
   if (is_terminated()) {
      if (!m_suppress_error_msgs) {
         Qmsg4(m_jcr, M_ERROR, 0,  _("BSOCKCORE send while terminated=%d on call to %s:%s:%d\n"),
             is_terminated(), m_who, m_host, m_port);
      }
      return false;
   }

   if (msglen > 4000000) {
      if (!m_suppress_error_msgs) {
         Qmsg4(m_jcr, M_ERROR, 0,
            _("Socket has insane msglen=%d on call to %s:%s:%d\n"),
             msglen, m_who, m_host, m_port);
      }
      return false;
   }

   if (send_hook_cb) {
      if (!send_hook_cb->bsock_send_cb()) {
         Dmsg3(1, "Flowcontrol failure on %s:%s:%d\n", m_who, m_host, m_port);
         Qmsg3(m_jcr, M_ERROR, 0, _("Flowcontrol failure on %s:%s:%d\n"), m_who, m_host, m_port);
         return false;
      }
   }
   if (m_use_locking) {
      pP(pm_wmutex);
      locked = true;
   }

   (*pout_msg_no)++;        /* increment message number */

   /* send data packet */
   timer_start = watchdog_time;  /* start timer */
   clear_timed_out();
   /* Full I/O done in one write */
   rc = write_nbytes(msg, msglen);
   if (chk_dbglvl(DT_NETWORK|1900)) dump_bsock_msg(m_fd, *pout_msg_no, "SEND", rc, msglen, m_flags, msg, msglen);
   timer_start = 0;         /* clear timer */
   if (rc != msglen) {
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
                  msglen, m_who,
                  m_host, m_port, this->bstrerror());
         }
      } else {
         Qmsg5(m_jcr, M_ERROR, 0,
               _("Wrote %d bytes to %s:%s:%d, but only %d accepted.\n"),
               msglen, m_who, m_host, m_port, rc);
      }
      ok = false;
   }
//   Dmsg4(000, "cmpr=%d ext=%d cmd=%d m_flags=0x%x\n", msglen&BNET_COMPRESSED?1:0,
//      msglen&BNET_HDR_EXTEND?1:0, msglen&BNET_CMD_BIT?1:0, m_flags);
   if (locked) pV(pm_wmutex);
   return ok;
}

/*
 * Format and send a message
 *  Returns: false on error
 *           true  on success
 */
bool BSOCKCORE::fsend(const char *fmt, ...)
{
   va_list arg_ptr;
   int maxlen;

   if (is_null(this)) {
      return false;                /* do not seg fault */
   }
   if (errors || is_terminated() || is_closed()) {
      return false;
   }
   /* This probably won't work, but we vsnprintf, then if we
    * get a negative length or a length greater than our buffer
    * (depending on which library is used), the printf was truncated, so
    * get a bigger buffer and try again.
    */
   for (;;) {
      maxlen = sizeof_pool_memory(msg) - 1;
      va_start(arg_ptr, fmt);
      msglen = bvsnprintf(msg, maxlen, fmt, arg_ptr);
      va_end(arg_ptr);
      if (msglen >= 0 && msglen < (maxlen - 5)) {
         break;
      }
      msg = realloc_pool_memory(msg, maxlen + maxlen / 2);
   }
   return send();
}

/*
 * Receive a data from the other end.
 * The number of expected bytes in len.
 * Returns number of bytes read (may return zero), the msglen is set accordingly.
 * Returns -1 on error so msglen will be zero.
 */
int32_t BSOCKCORE::recvn(int len)
{
   /* The method has to be redesigned from scratch */
   int32_t nbytes;
   bool locked = false;

   msglen = nbytes = 0;
   msg[msglen] = 0;
   if (errors || is_terminated() || is_closed()) {
      /* error, cannot receive */
      return -1;
   }

   if (len > 0) {
      /* do read only when len > 0 */
      if (m_use_locking) {
         pP(pm_rmutex);
         locked = true;
      }
      read_seqno++;                 /* bump sequence number */
      timer_start = watchdog_time;  /* set start wait time */
      clear_timed_out();
      /* Make sure the buffer is big enough + one byte for EOS */
      if (len >= (int32_t) sizeof_pool_memory(msg)) {
         msg = realloc_pool_memory(msg, len + 100);
      }
      timer_start = watchdog_time;  /* set start wait time */
      clear_timed_out();
      if ((nbytes = read_nbytes(msg, len)) <= 0) {
         timer_start = 0;      /* clear timer */
         /* probably pipe broken because client died */
         if (errno == 0) {
            b_errno = ENODATA;
         } else {
            b_errno = errno;
         }
         nbytes = -1;
         errors++;
         msglen = 0;        /* assume hard EOF received */
         Qmsg4(m_jcr, M_ERROR, 0, _("Read error from %s:%s:%d: ERR=%s\n"),
               m_who, m_host, m_port, this->bstrerror());
         goto bailout;
      }
      timer_start = 0;         /* clear timer */
      in_msg_no++;
      msglen = nbytes;
      /*
       * always add a zero by to properly terminate any
       * string that was send to us. Note, we ensured above that the
       * buffer is at least one byte longer than the message length.
       */
      msg[nbytes] = 0; /* terminate in case it is a string */
      /*
       * The following uses *lots* of resources so turn it on only for
       * serious debugging.
       */
      Dsm_check(300);
   }

bailout:
   if ((chk_dbglvl(DT_NETWORK|1900))) dump_bsock_msg(m_fd, read_seqno, "GRECV", nbytes, len, m_flags, msg, msglen);

   if (locked) pV(pm_rmutex);
   return nbytes;                  /* return actual length of message or -1 */
}

/*
 * Return the string for the error that occurred
 * on the socket. Only the first error is retained.
 */
const char *BSOCKCORE::bstrerror()
{
   berrno be;
   if (errmsg == NULL) {
      errmsg = get_pool_memory(PM_MESSAGE);
   }
   if (b_errno == 0) {
      pm_strcpy(errmsg, "I/O Error");
   } else {
      pm_strcpy(errmsg, be.bstrerror(b_errno));
   }
   return errmsg;
}

int BSOCKCORE::get_peer(char *buf, socklen_t buflen)
{
#if !defined(HAVE_WIN32)
    if (peer_addr.sin_family == 0) {
        socklen_t salen = sizeof(peer_addr);
        int rval = (getpeername)(m_fd, (struct sockaddr *)&peer_addr, &salen);
        if (rval < 0) return rval;
    }
    if (!inet_ntop(peer_addr.sin_family, &peer_addr.sin_addr, buf, buflen))
        return -1;

    return 0;
#else
    return -1;
#endif
}

/*
 * Set the network buffer size, suggested size is in size.
 *  Actual size obtained is returned in bs->msglen
 *
 *  Returns: false on failure
 *           true  on success
 */
bool BSOCKCORE::set_buffer_size(uint32_t size, int rw)
{
   uint32_t dbuf_size, start_size;

#if defined(IP_TOS) && defined(IPTOS_THROUGHPUT)
   int opt;
   opt = IPTOS_THROUGHPUT;
   setsockopt(m_fd, IPPROTO_IP, IP_TOS, (sockopt_val_t)&opt, sizeof(opt));
#endif

   if (size != 0) {
      dbuf_size = size;
   } else {
      dbuf_size = DEFAULT_NETWORK_BUFFER_SIZE;
   }
   start_size = dbuf_size;
   /* The extra 512 can hold data such as Sparse/Offset pointer */
   if ((msg = realloc_pool_memory(msg, dbuf_size + 512)) == NULL) {
      Qmsg0(get_jcr(), M_FATAL, 0, _("Could not malloc BSOCKCORE data buffer\n"));
      return false;
   }

   /*
    * If user has not set the size, use the OS default -- i.e. do not
    *   try to set it.  This allows sys admins to set the size they
    *   want in the OS, and Bacula will comply. See bug #1493
    */
   if (size == 0) {
      msglen = dbuf_size;
      return true;
   }

   if (rw & BNET_SETBUF_READ) {
      while ((dbuf_size > TAPE_BSIZE) && (setsockopt(m_fd, SOL_SOCKET,
              SO_RCVBUF, (sockopt_val_t) & dbuf_size, sizeof(dbuf_size)) < 0)) {
         berrno be;
         Qmsg1(get_jcr(), M_ERROR, 0, _("sockopt error: %s\n"), be.bstrerror());
         dbuf_size -= TAPE_BSIZE;
      }
      Dmsg1(200, "set network buffer size=%d\n", dbuf_size);
      if (dbuf_size != start_size) {
         Qmsg1(get_jcr(), M_WARNING, 0,
               _("Warning network buffer = %d bytes not max size.\n"), dbuf_size);
      }
   }
   if (size != 0) {
      dbuf_size = size;
   } else {
      dbuf_size = DEFAULT_NETWORK_BUFFER_SIZE;
   }
   start_size = dbuf_size;
   if (rw & BNET_SETBUF_WRITE) {
      while ((dbuf_size > TAPE_BSIZE) && (setsockopt(m_fd, SOL_SOCKET,
              SO_SNDBUF, (sockopt_val_t) & dbuf_size, sizeof(dbuf_size)) < 0)) {
         berrno be;
         Qmsg1(get_jcr(), M_ERROR, 0, _("sockopt error: %s\n"), be.bstrerror());
         dbuf_size -= TAPE_BSIZE;
      }
      Dmsg1(900, "set network buffer size=%d\n", dbuf_size);
      if (dbuf_size != start_size) {
         Qmsg1(get_jcr(), M_WARNING, 0,
               _("Warning network buffer = %d bytes not max size.\n"), dbuf_size);
      }
   }

   msglen = dbuf_size;
   return true;
}

/*
 * Set socket non-blocking
 * Returns previous socket flag
 */
int BSOCKCORE::set_nonblocking()
{
   int oflags;

   /* Get current flags */
   if ((oflags = fcntl(m_fd, F_GETFL, 0)) < 0) {
      berrno be;
      Qmsg1(get_jcr(), M_ABORT, 0, _("fcntl F_GETFL error. ERR=%s\n"), be.bstrerror());
   }

   /* Set O_NONBLOCK flag */
   if ((fcntl(m_fd, F_SETFL, oflags|O_NONBLOCK)) < 0) {
      berrno be;
      Qmsg1(get_jcr(), M_ABORT, 0, _("fcntl F_SETFL error. ERR=%s\n"), be.bstrerror());
   }

   m_blocking = 0;
   return oflags;
}

/*
 * Set socket blocking
 * Returns previous socket flags
 */
int BSOCKCORE::set_blocking()
{
   int oflags;
   /* Get current flags */
   if ((oflags = fcntl(m_fd, F_GETFL, 0)) < 0) {
      berrno be;
      Qmsg1(get_jcr(), M_ABORT, 0, _("fcntl F_GETFL error. ERR=%s\n"), be.bstrerror());
   }

   /* Set O_NONBLOCK flag */
   if ((fcntl(m_fd, F_SETFL, oflags & ~O_NONBLOCK)) < 0) {
      berrno be;
      Qmsg1(get_jcr(), M_ABORT, 0, _("fcntl F_SETFL error. ERR=%s\n"), be.bstrerror());
   }

   m_blocking = 1;
   return oflags;
}

void BSOCKCORE::set_killable(bool killable)
{
   if (m_jcr) {
      m_jcr->set_killable(killable);
   }
}

/*
 * Restores socket flags
 */
void BSOCKCORE::restore_blocking (int flags)
{
   if ((fcntl(m_fd, F_SETFL, flags)) < 0) {
      berrno be;
      Qmsg1(get_jcr(), M_ABORT, 0, _("fcntl F_SETFL error. ERR=%s\n"), be.bstrerror());
   }

   m_blocking = (flags & O_NONBLOCK) ? true : false;
}

/*
 * Wait for a specified time for data to appear on
 * the BSOCKCORE connection.
 *
 *   Returns: 1 if data available
 *            0 if timeout
 *           -1 if error
 */
int BSOCKCORE::wait_data(int sec, int msec)
{
   for (;;) {
      switch (fd_wait_data(m_fd, WAIT_READ, sec, msec)) {
      case 0:                      /* timeout */
         b_errno = 0;
         return 0;
      case -1:
         b_errno = errno;
         if (errno == EINTR) {
            continue;
         }
         return -1;                /* error return */
      default:
         b_errno = 0;
#ifdef HAVE_TLS
         if (this->tls && !tls_bsock_probe(this)) {
            continue; /* false alarm, maybe a session key negotiation in progress on the socket */
         }
#endif
         return 1;
      }
   }
}

/*
 * As above, but returns on interrupt
 */
int BSOCKCORE::wait_data_intr(int sec, int msec)
{
   switch (fd_wait_data(m_fd, WAIT_READ, sec, msec)) {
   case 0:                      /* timeout */
      b_errno = 0;
      return 0;
   case -1:
      b_errno = errno;
      return -1;                /* error return */
   default:
      b_errno = 0;
#ifdef HAVE_TLS
      if (this->tls && !tls_bsock_probe(this)) {
         /* maybe a session key negotiation waked up the socket */
         return 0;
      }
#endif
      break;
   }
   return 1;
}

/*
 *  This routine closes the current BSOCKCORE.
 *   It does not delete the socket packet
 *   resources, which are released in bsock->destroy().
 */
#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif

/*
 * The JCR is canceled, set terminate for chained BSOCKCOREs starting from master
 */
void BSOCKCORE::cancel()
{
   master_lock();
   for (BSOCKCORE *next = m_master; next != NULL; next = next->m_next) {
      if (!next->m_closed) {
         next->m_terminated = true;
         next->m_timed_out = true;
      }
   }
   master_unlock();
}

/*
 * Note, this routine closes the socket, but leaves the
 *   bsockcore memory in place.
 *   every thread is responsible of closing and destroying its own duped or not
 *   duped BSOCKCORE
 */
void BSOCKCORE::close()
{
   BSOCKCORE *bsock = this;

   Dmsg0(BSOCKCORE_DEBUG_LVL, "BSOCKCORE::close()\n");
   if (bsock->is_closed()) {
      return;
   }
   if (!m_duped) {
      clear_locking();
   }
   bsock->set_closed();
   bsock->set_terminated();
   if (!bsock->m_duped) {
      /* Shutdown tls cleanly. */
      if (bsock->tls) {
         tls_bsock_shutdown(bsock);
         free_tls_connection(bsock->tls);
         bsock->tls = NULL;
      }

#ifdef HAVE_WIN32
      if (!bsock->is_timed_out()) {
         win_close_wait(bsock->m_fd);  /* Ensure that data is not discarded */
      }
#else
      if (bsock->is_timed_out()) {
         shutdown(bsock->m_fd, SHUT_RDWR);   /* discard any pending I/O */
      }
#endif
      /* On Windows this discards data if we did not do a close_wait() */
      socketClose(bsock->m_fd);      /* normal close */
   }
   return;
}

/*
 * Destroy the socket (i.e. release all resources)
 */
void BSOCKCORE::_destroy()
{
   Dmsg0(BSOCKCORE_DEBUG_LVL, "BSOCKCORE::_destroy()\n");
   this->close();                  /* Ensure that socket is closed */
   if (msg) {
      free_pool_memory(msg);
      msg = NULL;
   } else {
      ASSERT2(1 == 0, "Two calls to destroy socket");  /* double destroy */
   }
   if (errmsg) {
      free_pool_memory(errmsg);
      errmsg = NULL;
   }
   if (m_who) {
      free(m_who);
      m_who = NULL;
   }
   if (m_host) {
      free(m_host);
      m_host = NULL;
   }
   if (src_addr) {
      free(src_addr);
      src_addr = NULL;
   }
}

/*
 * Destroy the socket (i.e. release all resources)
 * including duped sockets.
 * should not be called from duped BSOCKCORE
 */
void BSOCKCORE::destroy()
{
   Dmsg0(BSOCKCORE_DEBUG_LVL, "BSOCKCORE::destroy()\n");
   ASSERTD(reinterpret_cast<uintptr_t>(m_next) != 0xaaaaaaaaaaaaaaaa, "BSOCKCORE::destroy() already called\n")
   ASSERTD(this == m_master, "BSOCKCORE::destroy() called by a non master BSOCKCORE\n")
   ASSERTD(!m_duped, "BSOCKCORE::destroy() called by a duped BSOCKCORE\n")
   /* I'm the master I must destroy() all the duped BSOCKCOREs */
   master_lock();
   BSOCKCORE *ahead;
   for (BSOCKCORE *next = m_next; next != NULL; next = ahead) {
      ahead = next->m_next;
      Dmsg1(BSOCKCORE_DEBUG_LVL, "BSOCKCORE::destroy():delete(%p)\n", next);
      delete(next);
   }
   master_unlock();
   Dmsg0(BSOCKCORE_DEBUG_LVL, "BSOCKCORE::destroy():delete(this)\n");
   delete(this);
}

/* Try to limit the bandwidth of a network connection
 */
void BSOCKCORE::control_bwlimit(int bytes)
{
   btime_t now, temp;
   if (bytes == 0) {
      return;
   }

   now = get_current_btime();          /* microseconds */
   temp = now - m_last_tick;           /* microseconds */

   m_nb_bytes += bytes;

   if (temp < 0 || temp > 10000000) { /* Take care of clock problems (>10s) or back in time */
      m_nb_bytes = bytes;
      m_last_tick = now;
      return;
   }

   /* Less than 0.1ms since the last call, see the next time */
   if (temp < 100) {
      return;
   }

   /* Remove what was authorised to be written in temp us */
   m_nb_bytes -= (int64_t)(temp * ((double)m_bwlimit / 1000000.0));

   if (m_nb_bytes < 0) {
      m_nb_bytes = 0;
   }

   /* What exceed should be converted in sleep time */
   int64_t usec_sleep = (int64_t)(m_nb_bytes /((double)m_bwlimit / 1000000.0));
   if (usec_sleep > 100) {
      bmicrosleep(usec_sleep/1000000, usec_sleep%1000000); /* TODO: Check that bmicrosleep slept enough or sleep again */
      m_last_tick = get_current_btime();
      m_nb_bytes = 0;
   } else {
      m_last_tick = now;
   }
}

/*
 * Write nbytes to the network.
 * It may require several writes.
 */

int32_t BSOCKCORE::write_nbytes(char *ptr, int32_t nbytes)
{
   int32_t nleft, nwritten;

#ifdef HAVE_TLS
   if (tls) {
      /* TLS enabled */
      return (tls_bsock_writen((BSOCK*)this, ptr, nbytes));
   }
#endif /* HAVE_TLS */

   nleft = nbytes;
   while (nleft > 0) {
      do {
         errno = 0;
         nwritten = socketWrite(m_fd, ptr, nleft);
         if (is_timed_out() || is_terminated()) {
            return -1;
         }

#ifdef HAVE_WIN32
         /*
          * We simulate errno on Windows for a socket
          *  error in order to handle errors correctly.
          */
         if (nwritten == SOCKET_ERROR) {
            DWORD err = WSAGetLastError();
            nwritten = -1;
            if (err == WSAEINTR) {
               errno = EINTR;
            } else if (err == WSAEWOULDBLOCK) {
               errno = EAGAIN;
            } else {
               errno = EIO;        /* some other error */
            }
         }
#endif

      } while (nwritten == -1 && errno == EINTR);
      /*
       * If connection is non-blocking, we will get EAGAIN, so
       * use select()/poll to keep from consuming all the CPU
       * and try again.
       */
      if (nwritten == -1 && errno == EAGAIN) {
         fd_wait_data(m_fd, WAIT_WRITE, 1, 0);
         continue;
      }
      if (nwritten <= 0) {
         return -1;                /* error */
      }
      nleft -= nwritten;
      ptr += nwritten;
      if (use_bwlimit()) {
         control_bwlimit(nwritten);
      }
   }
   return nbytes - nleft;
}

/*
 * Read a nbytes from the network.
 * It is possible that the total bytes require in several
 * read requests
 */

int32_t BSOCKCORE::read_nbytes(char *ptr, int32_t nbytes)
{
   int32_t nleft, nread;

#ifdef HAVE_TLS
   if (tls) {
      /* TLS enabled */
      return (tls_bsock_readn((BSOCK*)this, ptr, nbytes));
   }
#endif /* HAVE_TLS */

   nleft = nbytes;
   while (nleft > 0) {
      errno = 0;
      nread = socketRead(m_fd, ptr, nleft);
      if (is_timed_out() || is_terminated()) {
         return -1;
      }

#ifdef HAVE_WIN32
      /*
       * We simulate errno on Windows for a socket
       *  error in order to handle errors correctly.
       */
      if (nread == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        nread = -1;
        if (err == WSAEINTR) {
           errno = EINTR;
        } else if (err == WSAEWOULDBLOCK) {
           errno = EAGAIN;
        } else {
           errno = EIO;            /* some other error */
        }
     }
#endif

      if (nread == -1) {
         if (errno == EINTR) {
            continue;
         }
         if (errno == EAGAIN) {
            bmicrosleep(0, 20000);  /* try again in 20ms */
            continue;
         }
      }
      if (nread <= 0) {
         return -1;                /* error, or EOF */
      }
      nleft -= nread;
      ptr += nread;
      if (use_bwlimit()) {
         control_bwlimit(nread);
      }
   }
   return nbytes - nleft;          /* return >= 0 */
}

#ifdef HAVE_WIN32
/*
 * closesocket is supposed to do a graceful disconnect under Window
 *   but it doesn't. Comments on http://msdn.microsoft.com/en-us/li
 *   confirm this behaviour. DisconnectEx is required instead, but
 *   that function needs to be retrieved via WS IOCTL
 */
static void
win_close_wait(int fd)
{
   int ret;
   GUID disconnectex_guid = WSAID_DISCONNECTEX;
   DWORD bytes_returned;
   LPFN_DISCONNECTEX DisconnectEx;
   ret = WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &disconnectex_guid, sizeof(disconnectex_guid), &DisconnectEx, sizeof(DisconnectEx), &bytes_returned, NULL, NULL);
   Dmsg1(100, "WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER, WSAID_DISCONNECTEX) ret = %d\n", ret);
   if (!ret) {
      DisconnectEx(fd, NULL, 0, 0);
   }
}
#endif

#ifndef TEST_PROGRAM
#define TEST_PROGRAM_A
#endif

void BSOCKCORE::dump()
{
#ifdef TEST_PROGRAM
   char ed1[50];
   Pmsg1(-1, "BSOCKCORE::dump(): %p\n", this);
   Pmsg1(-1, "\tmsg: %p\n", msg);
   Pmsg1(-1, "\terrmsg: %p\n", errmsg);
   Pmsg1(-1, "\tres: %p\n", res);
   Pmsg1(-1, "\ttls: %p\n", tls);
   Pmsg1(-1, "\tsrc_addr: %p\n", src_addr);
   Pmsg1(-1, "\tread_seqno: %s\n", edit_uint64(read_seqno, ed1));
   Pmsg1(-1, "\tin_msg_no: %s\n", edit_uint64(in_msg_no, ed1));
   Pmsg1(-1, "\tout_msg_no: %s\n", edit_uint64(out_msg_no, ed1));
   Pmsg1(-1, "\tpout_msg_no: %p\n", pout_msg_no);
   Pmsg1(-1, "\tmsglen: %s\n", edit_int64(msglen, ed1));
   Pmsg1(-1, "\ttimer_start: %ld\n", timer_start);
   Pmsg1(-1, "\ttimeout: %ld\n", timeout);
   Pmsg1(-1, "\tm_fd: %d\n", m_fd);
   Pmsg1(-1, "\tb_errno: %d\n", b_errno);
   Pmsg1(-1, "\tm_blocking: %d\n", m_blocking);
   Pmsg1(-1, "\terrors: %d\n", errors);
   Pmsg1(-1, "\tm_suppress_error_msgs: %s\n", m_suppress_error_msgs?"true":"false");
//   Pmsg1(0, "\tclient_addr:{ } struct sockaddr client_addr;       /* client's IP address */
//   Pmsg1(0, "\tstruct sockaddr_in peer_addr;      /* peer's IP address */
   Pmsg1(-1, "\tsend_hook_cb: %p\n", send_hook_cb);
   Pmsg1(-1, "\tm_master: %p\n", m_master);
   Pmsg1(-1, "\tm_next: %p\n", m_next);
   Pmsg1(-1, "\tm_jcr: %p\n", m_jcr);
//   pthread_mutex_t m_rmutex;          /* for read locking if use_locking set */
//   pthread_mutex_t m_wmutex;          /* for write locking if use_locking set */
//   mutable pthread_mutex_t m_mmutex;  /* when accessing the master/next chain */
//   pthread_mutex_t *pm_rmutex;        /* Pointer to the read mutex */
//   pthread_mutex_t *pm_wmutex;        /* Pointer to the write mutex */
   Pmsg1(-1, "\tm_who: %p\n", m_who);
   Pmsg1(-1, "\tm_host: %p\n", m_host);
   Pmsg1(-1, "\tm_port: %d\n", m_port);
   Pmsg1(-1, "\tm_tid: %p\n", m_tid);
   Pmsg1(-1, "\tm_flags: %s\n", edit_uint64(m_flags, ed1));
   Pmsg1(-1, "\tm_timed_out: %s\n", m_timed_out?"true":"false");
   Pmsg1(-1, "\tm_terminated: %s\n", m_terminated?"true":"false");
   Pmsg1(-1, "\tm_closed: %s\n", m_closed?"true":"false");
   Pmsg1(-1, "\tm_duped: %s\n", m_duped?"true":"false");
   Pmsg1(-1, "\tm_use_locking: %s\n", m_use_locking?"true":"false");
   Pmsg1(-1, "\tm_bwlimit: %s\n", edit_int64(m_bwlimit, ed1));
   Pmsg1(-1, "\tm_nb_bytes: %s\n", edit_int64(m_nb_bytes, ed1));
   Pmsg1(-1, "\tm_last_tick: %s\n", edit_int64(m_last_tick, ed1));
#endif
};


#ifdef TEST_PROGRAM
#include "unittests.h"

void free_my_jcr(JCR *jcr){
   /* TODO: handle full JCR free */
   free_jcr(jcr);
};

#define  ofnamefmt      "/tmp/bsockcore.%d.test"
const char *data =      "This is a BSOCKCORE communication test: 1234567\n";
const char *hexdata =   "< 00000000 54 68 69 73 20 69 73 20 61 20 42 53 4f 43 4b 43 # This is a BSOCKC\n" \
                        "< 00000010 4f 52 45 20 63 6f 6d 6d 75 6e 69 63 61 74 69 6f # ORE communicatio\n" \
                        "< 00000020 6e 20 74 65 73 74 3a 20 31 32 33 34 35 36 37 0a # n test: 1234567.\n";

int main()
{
   Unittests bsockcore_test("bsockcore_test", true);
   BSOCKCORE *bs;
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
   bs = New(BSOCKCORE);
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
   ok(btest, "BSOCKCORE connection test");
   if (btest){
      /* we are connected, so send some data */
      bs->fsend("%s", data);
      bmicrosleep(2, 0);      // wait until data received by netcat
      bs->close();
      ok(bs->is_closed(), "Close bsockcore");
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
