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

#ifndef __BSOCKCORE_H_
#define __BSOCKCORE_H_

#define BSOCKCORE_TIMEOUT  3600 * 24 * 5;  /* default 5 days */

struct btimer_t;                      /* forward reference */
class BSOCKCORE;
btimer_t *start_bsock_timer(BSOCKCORE *bs, uint32_t wait);
void stop_bsock_timer(btimer_t *wid);
void dump_bsock_msg(int sock, uint32_t msgno, const char *what, uint32_t rc, int32_t pktsize, uint32_t flags,
                     POOLMEM *msg, int32_t msglen);

class BSOCKCallback {
public:
   BSOCKCallback();
   virtual ~BSOCKCallback();
   virtual bool bsock_send_cb() = 0;
};

class BSOCKCORE: public SMARTALLOC {
/*
 * Note, keep this public part before the private otherwise
 *  bat breaks on some systems such as RedHat.
 */
public:
   POOLMEM *msg;                      /* message pool buffer */
   POOLMEM *errmsg;                   /* edited error message */
   RES *res;                          /* Resource to which we are connected */
   TLS_CONNECTION *tls;               /* associated tls connection */
   IPADDR *src_addr;                  /* IP address to source connections from */
   uint64_t read_seqno;               /* read sequence number */
   uint32_t in_msg_no;                /* input message number */
   uint32_t out_msg_no;               /* output message number */
   uint32_t *pout_msg_no;             /* pointer to the above */
   int32_t msglen;                    /* message length */
   volatile time_t timer_start;       /* time started read/write */
   volatile time_t timeout;           /* timeout BSOCKCORE after this interval */
   int m_fd;                          /* socket file descriptor */
   int b_errno;                       /* bsockcore errno */
   int m_blocking;                    /* blocking state (0 = nonblocking, 1 = blocking) */
   volatile int errors;               /* incremented for each error on socket */
   volatile bool m_suppress_error_msgs; /* set to suppress error messages */
   /* when "installed", send_hook_cb->bsock_send_cb() is called before
    * any ::send(). */
   BSOCKCallback *send_hook_cb;
   struct sockaddr client_addr;       /* client's IP address */
   struct sockaddr_in peer_addr;      /* peer's IP address */

protected:
   /* m_master is used by "duped" BSOCKCORE to access some attributes of the "parent"
    * thread to have an up2date status (for example when the job is canceled,
    * the "parent" BSOCKCORE is "terminated", but the duped BSOCKCORE is unchanged)
    * In the future more attributes and method could use the "m_master"
    * indirection.
    * master->m_rmutex could replace pm_rmutex, idem for the (w)rite" mutex
    * "m_master->error" should be incremented instead of "error", but
    * this require a lock.
    *
    * USAGE: the parent thread MUST be sure that the child thread have quit
    * before to free the "parent" BSOCKCORE.
    */
   BSOCKCORE *m_next;                 /* next BSOCKCORE if duped (not actually used) */
   JCR *m_jcr;                        /* jcr or NULL for error msgs */
   pthread_mutex_t m_rmutex;          /* for read locking if use_locking set */
   pthread_mutex_t m_wmutex;          /* for write locking if use_locking set */
   mutable pthread_mutex_t m_mmutex;  /* when accessing the master/next chain */
   pthread_mutex_t *pm_rmutex;        /* Pointer to the read mutex */
   pthread_mutex_t *pm_wmutex;        /* Pointer to the write mutex */
   char *m_who;                       /* Name of daemon to which we are talking */
   char *m_host;                      /* Host name/IP */
   int m_port;                        /* desired port */
   btimer_t *m_tid;                   /* timer id */
   uint32_t m_flags;                  /* Special flags */
   volatile bool m_timed_out: 1;      /* timed out in read/write */
   volatile bool m_terminated: 1;     /* set when BNET_TERMINATE arrives */
   bool m_closed: 1;                  /* set when socket is closed */
   bool m_duped: 1;                   /* set if duped BSOCKCORE */
   bool m_use_locking;                /* set to use locking (out of a bitfield */
                                      /* to avoid race conditions) */
   int64_t m_bwlimit;                 /* set to limit bandwidth */
   int64_t m_nb_bytes;                /* bytes sent/recv since the last tick */
   btime_t m_last_tick;               /* last tick used by bwlimit */

   void fin_init(JCR * jcr, int sockfd, const char *who, const char *host, int port,
               struct sockaddr *lclient_addr);
   virtual bool open(JCR *jcr, const char *name, char *host, char *service,
               int port, utime_t heart_beat, int *fatal);
   void master_lock() const { if (m_use_locking) pP((&m_mmutex)); };
   void master_unlock() const { if (m_use_locking) pV((&m_mmutex)); };
   virtual void init();
   void _destroy();                   /* called by destroy() */
   virtual int32_t write_nbytes(char *ptr, int32_t nbytes);
   virtual int32_t read_nbytes(char *ptr, int32_t nbytes);

public:
   BSOCKCORE *m_master;                    /* "this" or the "parent" BSOCK if duped */
   /* methods -- in bsockcore.c */
   BSOCKCORE();
   virtual ~BSOCKCORE();
   void free_tls();
   bool connect(JCR * jcr, int retry_interval, utime_t max_retry_time,
                utime_t heart_beat, const char *name, char *host,
                char *service, int port, int verbose);
  virtual int32_t recvn(int /*len*/);
   virtual bool send();
   bool fsend(const char*, ...);
   void close();              /* close connection and destroy packet */
   void destroy();                    /* destroy socket packet */
   const char *bstrerror();           /* last error on socket */
   int get_peer(char *buf, socklen_t buflen);
   bool set_buffer_size(uint32_t size, int rw);
   int set_nonblocking();
   int set_blocking();
   void restore_blocking(int flags);
   void set_killable(bool killable);
   int wait_data(int sec, int msec=0);
   int wait_data_intr(int sec, int msec=0);
   bool set_locking();
   void clear_locking();
   void set_source_address(dlist *src_addr_list);
   void control_bwlimit(int bytes);

   /* Inline functions */
   void suppress_error_messages(bool flag) { m_suppress_error_msgs = flag; };
   void set_jcr(JCR *jcr) { m_jcr = jcr; };
   void set_who(char *who) { m_who = who; };
   void set_host(char *host) { m_host = host; };
   void set_port(int port) { m_port = port; };
   char *who() const { return m_who; };
   char *host() const { return m_host; };
   int port() const { return m_port; };
   JCR *jcr() const { return m_jcr; };
   JCR *get_jcr() const { return m_jcr; };
   bool is_duped() const { return m_duped; };
   bool is_terminated() const { return m_terminated; };
   bool is_timed_out() const { return m_timed_out; };
   bool is_closed() const { return m_closed; };
   bool is_open() const { return !m_closed; };
   bool is_stop() const { return errors || is_terminated() || is_closed(); };
   bool is_error() { errno = b_errno; return errors; };
   void set_bwlimit(int64_t maxspeed) { m_bwlimit = maxspeed; };
   bool use_bwlimit() { return m_bwlimit > 0;};
   void set_duped() { m_duped = true; };
   void set_master(BSOCKCORE *master) {
            master_lock();
            m_master = master;
            m_next = master->m_next;
            master->m_next = this;
            master_unlock();
        };
   void set_timed_out() { m_timed_out = true; };
   void clear_timed_out() { m_timed_out = false; };
   void set_terminated() { m_terminated = true; };
   void set_closed() { m_closed = true; };
   void start_timer(int sec) { m_tid = start_bsock_timer(this, sec); };
   void stop_timer() { stop_bsock_timer(m_tid); };
   void swap_msgs();
   void install_send_hook_cb(BSOCKCallback *obj) { send_hook_cb=obj; };
   void uninstall_send_hook_cb() { send_hook_cb=NULL; };
   void cancel(); /* call it when JCR is canceled */
#ifdef HAVE_WIN32
   int socketRead(int fd, void *buf, size_t len) { return ::recv(fd, (char *)buf, len, 0); };
   int socketWrite(int fd, void *buf, size_t len) { return ::send(fd, (const char*)buf, len, 0); };
   int socketClose(int fd) { return ::closesocket(fd); };
#else
   int socketRead(int fd, void *buf, size_t len) { return ::read(fd, buf, len); };
   int socketWrite(int fd, void *buf, size_t len) { return ::write(fd, buf, len); };
   int socketClose(int fd) { return ::close(fd); };
#endif
   void dump();
};

/*
 * Completely release the socket packet, and NULL the pointer
 */
#define free_bsockcore(a) do{if(a){(a)->destroy(); (a)=NULL;}} while(0)

/*
 * Does the socket exist and is it open?
 */
#define is_bsockcore_open(a) ((a) && (a)->is_open())

#endif /* __BSOCKCORE_H_ */
