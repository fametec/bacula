/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

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
  * Originally written by Kern Sibbald for inclusion in apcupsd,
  *  but heavily modified for Bacula
  *
  */

#include "bacula.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifdef HAVE_ARPA_NAMESER_H
#include <arpa/nameser.h>
#endif
#ifdef HAVE_RESOLV_H
//#include <resolv.h>
#endif


static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

#ifdef HAVE_LIBWRAP
#include "tcpd.h"
int allow_severity = LOG_NOTICE;
int deny_severity = LOG_WARNING;
#endif

static bool quit = false;

void bnet_stop_thread_server(pthread_t tid)
{
   quit = true;
   if (!pthread_equal(tid, pthread_self())) {
      pthread_kill(tid, TIMEOUT_SIGNAL);
   }
}

 /*
     Become Threaded Network Server
     This function is able to handle multiple server ips in
     ipv4 and ipv6 style. The Addresse are give in a comma
     seperated string in bind_addr
     In the moment it is inpossible to bind different ports.
  */
void bnet_thread_server(dlist *addrs, int max_clients, 
        workq_t *client_wq, void *handle_client_request(void *bsock))
{
   int newsockfd, stat;
   socklen_t clilen;
   struct sockaddr_storage clientaddr;   /* client's address */
   int tlog;
   int turnon = 1;
#ifdef HAVE_LIBWRAP
   struct request_info request;
#endif
   IPADDR *addr;
   struct s_sockfd {
      dlink link;                     /* this MUST be the first item */
      int fd;
      int port;
   } *fd_ptr = NULL;
   char buf[128];
   dlist sockfds;

   char allbuf[256 * 10];
   remove_duplicate_addresses(addrs);
   Dmsg1(20, "Addresses %s\n", build_addresses_str(addrs, allbuf, sizeof(allbuf)));
   /*
    * Listen on each address provided.
    */
   foreach_dlist(addr, addrs) {
      /* Allocate on stack from -- no need to free */
      fd_ptr = (s_sockfd *)alloca(sizeof(s_sockfd));
      fd_ptr->port = addr->get_port_net_order();
      /*
       * Open a TCP socket
       */
      for (tlog= 60; (fd_ptr->fd=socket(addr->get_family(), SOCK_STREAM, 0)) < 0; tlog -= 10) {
         if (tlog <= 0) {
            berrno be;
            char curbuf[256];
            Emsg3(M_ABORT, 0, _("Cannot open stream socket. ERR=%s. Current %s All %s\n"),
                       be.bstrerror(),
                       addr->build_address_str(curbuf, sizeof(curbuf)),
                       build_addresses_str(addrs, allbuf, sizeof(allbuf)));
         }
         bmicrosleep(10, 0);
      }
      /*
       * Reuse old sockets
       */
      if (setsockopt(fd_ptr->fd, SOL_SOCKET, SO_REUSEADDR, (sockopt_val_t)&turnon,
           sizeof(turnon)) < 0) {
         berrno be;
         Emsg1(M_WARNING, 0, _("Cannot set SO_REUSEADDR on socket: %s\n"),
               be.bstrerror());
      }

      int tmax = 1 * (60 / 5);    /* wait 1 minute max */
      for (tlog = 0; bind(fd_ptr->fd, addr->get_sockaddr(), addr->get_sockaddr_len()) == SOCKET_ERROR; tlog -= 5) {
         berrno be;
         if (tlog <= 0) {
            tlog = 1 * 60;         /* Complain every 1 minute */
            Emsg2(M_WARNING, 0, _("Cannot bind port %d: ERR=%s: Retrying ...\n"),
                  ntohs(fd_ptr->port), be.bstrerror());
            Dmsg2(20, "Cannot bind port %d: ERR=%s: Retrying ...\n",
                  ntohs(fd_ptr->port), be.bstrerror());

         }
         bmicrosleep(5, 0);
         if (--tmax <= 0) {
            Emsg2(M_ABORT, 0, _("Cannot bind port %d: ERR=%s.\n"), ntohs(fd_ptr->port),
                  be.bstrerror());
            Pmsg2(000, "Aborting cannot bind port %d: ERR=%s.\n", ntohs(fd_ptr->port),
                  be.bstrerror());
         }
      }
      if (listen(fd_ptr->fd, 50) < 0) {      /* tell system we are ready */
         berrno be;
         Emsg2(M_ABORT, 0, _("Cannot bind port %d: ERR=%s.\n"), ntohs(fd_ptr->port),
               be.bstrerror());
      } else {
         sockfds.append(fd_ptr);
      }
   }
   if (sockfds.size() == 0) {
      Emsg0(M_ABORT, 0, _("No addr/port found to listen on.\n"));
   }
   /* Start work queue thread */
   if ((stat = workq_init(client_wq, max_clients, handle_client_request)) != 0) {
      berrno be;
      be.set_errno(stat);
      Emsg1(M_ABORT, 0, _("Could not init client queue: ERR=%s\n"), be.bstrerror());
   }
   /*
    * Wait for a connection from the client process.
    */
   for (; !quit;) {
      unsigned int maxfd = 0;
      fd_set sockset;
      FD_ZERO(&sockset);
      foreach_dlist(fd_ptr, &sockfds) {
         FD_SET((unsigned)fd_ptr->fd, &sockset);
         maxfd = maxfd > (unsigned)fd_ptr->fd ? maxfd : fd_ptr->fd;
      }
      errno = 0;
      if ((stat = select(maxfd + 1, &sockset, NULL, NULL, NULL)) < 0) {
         berrno be;                   /* capture errno */
         if (errno == EINTR) {
            continue;
         }
         Emsg1(M_FATAL, 0, _("Error in select: %s\n"), be.bstrerror());
         break;
      }

      foreach_dlist(fd_ptr, &sockfds) {
         if (FD_ISSET(fd_ptr->fd, &sockset)) {
            /* Got a connection, now accept it. */
            do {
               clilen = sizeof(clientaddr);
               newsockfd = baccept(fd_ptr->fd, (struct sockaddr *)&clientaddr, &clilen);
               newsockfd = set_socket_errno(newsockfd);
            } while (newsockfd == SOCKET_ERROR && (errno == EINTR || errno == EAGAIN));
            if (newsockfd == SOCKET_ERROR) {
               Dmsg2(20, "Accept=%d errno=%d\n", newsockfd, errno);
               continue;
            }
#ifdef HAVE_LIBWRAP
            P(mutex);              /* hosts_access is not thread safe */
            request_init(&request, RQ_DAEMON, my_name, RQ_FILE, newsockfd, 0);
            fromhost(&request);
            if (!hosts_access(&request)) {
               V(mutex);
               Qmsg2(NULL, M_SECURITY, 0,
                     _("Connection from %s:%d refused by hosts.access\n"),
                     sockaddr_to_ascii((struct sockaddr *)&clientaddr,
                                       sizeof(clientaddr), buf, sizeof(buf)),
                     sockaddr_get_port((struct sockaddr *)&clientaddr));
               close(newsockfd);
               continue;
            }
            V(mutex);
#endif

            /*
             * Receive notification when connection dies.
             */
            if (setsockopt(newsockfd, SOL_SOCKET, SO_KEEPALIVE, (sockopt_val_t)&turnon,
                 sizeof(turnon)) < 0) {
               berrno be;
               Qmsg1(NULL, M_WARNING, 0, _("Cannot set SO_KEEPALIVE on socket: %s\n"),
                     be.bstrerror());
            }

            /* see who client is. i.e. who connected to us. */
            P(mutex);
            sockaddr_to_ascii((struct sockaddr *)&clientaddr, sizeof(clientaddr), buf, sizeof(buf));
            V(mutex);
            BSOCK *bs;
            bs = init_bsock(NULL, newsockfd, "client", buf,
                    sockaddr_get_port((struct sockaddr *)&clientaddr),
                    (struct sockaddr *)&clientaddr);
            if (bs == NULL) {
               Qmsg0(NULL, M_ABORT, 0, _("Could not create client BSOCK.\n"));
            }

            /* Queue client to be served */
            if ((stat = workq_add(client_wq, (void *)bs, NULL, 0)) != 0) {
               berrno be;
               be.set_errno(stat);
               bs->destroy();
               Qmsg1(NULL, M_ABORT, 0, _("Could not add job to client queue: ERR=%s\n"),
                     be.bstrerror());
            }
         }
      }
   }

   /* Cleanup open files and pointers to them */
   while ((fd_ptr = (s_sockfd *)sockfds.first())) {
      close(fd_ptr->fd);
      sockfds.remove(fd_ptr);     /* don't free() item it is on stack */
   }

   /* Stop work queue thread */
   if ((stat = workq_destroy(client_wq)) != 0) {
      berrno be;
      be.set_errno(stat);
      Jmsg1(NULL, M_FATAL, 0, _("Could not destroy client queue: ERR=%s\n"),
            be.bstrerror());
   }
}
