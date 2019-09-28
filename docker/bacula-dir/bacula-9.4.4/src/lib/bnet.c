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
 *  by Kern Sibbald
 *
 * Adapted and enhanced for Bacula, originally written
 * for inclusion in the Apcupsd package
 *
 */


#include "bacula.h"
#include "jcr.h"
#include <netdb.h>

#ifndef   INADDR_NONE
#define   INADDR_NONE    -1
#endif

#ifdef HAVE_WIN32
#undef inet_pton
#define inet_pton binet_pton
#define socketRead(fd, buf, len)  recv(fd, buf, len, 0)
#define socketWrite(fd, buf, len) send(fd, buf, len, 0)
#define socketClose(fd)           closesocket(fd)
#else
#define socketRead(fd, buf, len)  read(fd, buf, len)
#define socketWrite(fd, buf, len) write(fd, buf, len)
#define socketClose(fd)           close(fd)
#endif

#ifndef HAVE_GETADDRINFO
static pthread_mutex_t ip_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/*
 * Establish a TLS connection -- server side
 *  Returns: true  on success
 *           false on failure
 */
#ifdef HAVE_TLS
bool bnet_tls_server(TLS_CONTEXT *ctx, BSOCK * bsock, alist *verify_list)
{
   TLS_CONNECTION *tls;
   JCR *jcr = bsock->jcr();

   tls = new_tls_connection(ctx, bsock->m_fd);
   if (!tls) {
      Qmsg0(bsock->jcr(), M_FATAL, 0, _("TLS connection initialization failed.\n"));
      return false;
   }

   bsock->tls = tls;

   /* Initiate TLS Negotiation */
   if (!tls_bsock_accept(bsock)) {
      Qmsg0(bsock->jcr(), M_FATAL, 0, _("TLS Negotiation failed.\n"));
      goto err;
   }

   if (verify_list) {
      if (!tls_postconnect_verify_cn(jcr, tls, verify_list)) {
         Qmsg1(bsock->jcr(), M_FATAL, 0, _("TLS certificate verification failed."
                                         " Peer certificate did not match a required commonName\n"),
                                         bsock->host());
         goto err;
      }
   }
   Dmsg0(50, "TLS server negotiation established.\n");
   return true;

err:
   free_tls_connection(tls);
   bsock->tls = NULL;
   return false;
}

/*
 * Establish a TLS connection -- client side
 * Returns: true  on success
 *          false on failure
 */
bool bnet_tls_client(TLS_CONTEXT *ctx, BSOCK *bsock, alist *verify_list)
{
   TLS_CONNECTION *tls;
   JCR *jcr = bsock->jcr();

   tls  = new_tls_connection(ctx, bsock->m_fd);
   if (!tls) {
      Qmsg0(bsock->jcr(), M_FATAL, 0, _("TLS connection initialization failed.\n"));
      return false;
   }

   bsock->tls = tls;

   /* Initiate TLS Negotiation */
   if (!tls_bsock_connect(bsock)) {
      goto err;
   }

   /* If there's an Allowed CN verify list, use that to validate the remote
    * certificate's CN. Otherwise, we use standard host/CN matching. */
   if (verify_list) {
      if (!tls_postconnect_verify_cn(jcr, tls, verify_list)) {
         Qmsg1(bsock->jcr(), M_FATAL, 0, _("TLS certificate verification failed."
                                         " Peer certificate did not match a required commonName\n"),
                                         bsock->host());
         goto err;
      }
   } else if (!tls_postconnect_verify_host(jcr, tls, bsock->host())) {
      /* If host is 127.0.0.1, try localhost */
      if (strcmp(bsock->host(), "127.0.0.1") != 0 ||
             !tls_postconnect_verify_host(jcr, tls, "localhost")) {
         Qmsg1(bsock->jcr(), M_FATAL, 0, _("TLS host certificate verification failed. Host name \"%s\" did not match presented certificate\n"),
               bsock->host());
         goto err;
      }
   }
   Dmsg0(50, "TLS client negotiation established.\n");
   return true;

err:
   free_tls_connection(tls);
   bsock->tls = NULL;
   return false;
}
#else

bool bnet_tls_server(TLS_CONTEXT *ctx, BSOCK * bsock, alist *verify_list)
{
   Jmsg(bsock->jcr(), M_ABORT, 0, _("TLS enabled but not configured.\n"));
   return false;
}

bool bnet_tls_client(TLS_CONTEXT *ctx, BSOCK * bsock, alist *verify_list)
{
   Jmsg(bsock->jcr(), M_ABORT, 0, _("TLS enable but not configured.\n"));
   return false;
}

#endif /* HAVE_TLS */

#ifndef NETDB_INTERNAL
#define NETDB_INTERNAL  -1         /* See errno. */
#endif
#ifndef NETDB_SUCCESS
#define NETDB_SUCCESS   0          /* No problem. */
#endif
#ifndef HOST_NOT_FOUND
#define HOST_NOT_FOUND  1          /* Authoritative Answer Host not found. */
#endif
#ifndef TRY_AGAIN
#define TRY_AGAIN       2          /* Non-Authoritative Host not found, or SERVERFAIL. */
#endif
#ifndef NO_RECOVERY
#define NO_RECOVERY     3          /* Non recoverable errors, FORMERR, REFUSED, NOTIMP. */
#endif
#ifndef NO_DATA
#define NO_DATA         4          /* Valid name, no data record of requested type. */
#endif

#if defined(HAVE_GETADDRINFO)
/*
 * getaddrinfo.c - Simple example of using getaddrinfo(3) function.
 *
 * Michal Ludvig <michal@logix.cz> (c) 2002, 2003
 * http://www.logix.cz/michal/devel/
 *
 * License: public domain.
 */
const char *resolv_host(int family, const char *host, dlist *addr_list)
{
   IPADDR *ipaddr;
   struct addrinfo hints, *res, *rp;
   int errcode;
   //char addrstr[100];
   void *ptr;

   memset (&hints, 0, sizeof(hints));
   hints.ai_family = family;
   hints.ai_socktype = SOCK_STREAM;
   //hints.ai_flags |= AI_CANONNAME;

   errcode = getaddrinfo (host, NULL, &hints, &res);
   if (errcode != 0) return gai_strerror(errcode);

   for (rp=res; res; res=res->ai_next) {
      //inet_ntop (res->ai_family, res->ai_addr->sa_data, addrstr, 100);
      switch (res->ai_family) {
      case AF_INET:
         ipaddr = New(IPADDR(rp->ai_addr->sa_family));
         ipaddr->set_type(IPADDR::R_MULTIPLE);
         ptr = &((struct sockaddr_in *) res->ai_addr)->sin_addr;
         ipaddr->set_addr4((in_addr *)ptr);
         break;
#if defined(HAVE_IPV6)
      case AF_INET6:
         ipaddr = New(IPADDR(rp->ai_addr->sa_family));
         ipaddr->set_type(IPADDR::R_MULTIPLE);
         ptr = &((struct sockaddr_in6 *) res->ai_addr)->sin6_addr;
         ipaddr->set_addr6((in6_addr *)ptr);
         break;
#endif
      default:
         continue;
      }
      //inet_ntop (res->ai_family, ptr, addrstr, 100);
      //Pmsg3(000, "IPv%d address: %s (%s)\n", res->ai_family == PF_INET6 ? 6 : 4,
      //         addrstr, res->ai_canonname);
      addr_list->append(ipaddr);
   }
   freeaddrinfo(rp);
   return NULL;
}

#else

/*
 * Get human readable error for gethostbyname()
 */
static const char *gethost_strerror()
{
   const char *msg;
   berrno be;
   switch (h_errno) {
   case NETDB_INTERNAL:
      msg = be.bstrerror();
      break;
   case NETDB_SUCCESS:
      msg = _("No problem.");
      break;
   case HOST_NOT_FOUND:
      msg = _("Authoritative answer for host not found.");
      break;
   case TRY_AGAIN:
      msg = _("Non-authoritative for host not found, or ServerFail.");
      break;
   case NO_RECOVERY:
      msg = _("Non-recoverable errors, FORMERR, REFUSED, or NOTIMP.");
      break;
   case NO_DATA:
      msg = _("Valid name, no data record of resquested type.");
      break;
   default:
      msg = _("Unknown error.");
   }
   return msg;
}

/*
 * Note: this is the old way of resolving a host
 *  that does not use the new getaddrinfo() above.
 */
static const char *resolv_host(int family, const char *host, dlist * addr_list)
{
   struct hostent *hp;
   const char *errmsg;

   P(ip_mutex);                       /* gethostbyname() is not thread safe */
#ifdef HAVE_GETHOSTBYNAME2
   if ((hp = gethostbyname2(host, family)) == NULL) {
#else
   if ((hp = gethostbyname(host)) == NULL) {
#endif
      /* may be the strerror give not the right result -:( */
      errmsg = gethost_strerror();
      V(ip_mutex);
      return errmsg;
   } else {
      char **p;
      for (p = hp->h_addr_list; *p != 0; p++) {
         IPADDR *addr =  New(IPADDR(hp->h_addrtype));
         addr->set_type(IPADDR::R_MULTIPLE);
         if (addr->get_family() == AF_INET) {
             addr->set_addr4((struct in_addr*)*p);
         }
#ifdef HAVE_IPV6
         else {
             addr->set_addr6((struct in6_addr*)*p);
         }
#endif
         addr_list->append(addr);
      }
      V(ip_mutex);
   }
   return NULL;
}
#endif

static IPADDR *add_any(int family)
{
   IPADDR *addr = New(IPADDR(family));
   addr->set_type(IPADDR::R_MULTIPLE);
   addr->set_addr_any();
   return addr;
}

/*
 * i host = 0 means INADDR_ANY only for IPv4
 */
dlist *bnet_host2ipaddrs(const char *host, int family, const char **errstr)
{
   struct in_addr inaddr;
   IPADDR *addr = 0;
   const char *errmsg;
#ifdef HAVE_IPV6
   struct in6_addr inaddr6;
#endif

   dlist *addr_list = New(dlist(addr, &addr->link));
   if (!host || host[0] == '\0') {
      if (family != 0) {
         addr_list->append(add_any(family));
      } else {
         addr_list->append(add_any(AF_INET));
#ifdef HAVE_IPV6
         addr_list->append(add_any(AF_INET6));
#endif
      }
   } else if (inet_aton(host, &inaddr)) { /* MA Bug 4 */
      addr = New(IPADDR(AF_INET));
      addr->set_type(IPADDR::R_MULTIPLE);
      addr->set_addr4(&inaddr);
      addr_list->append(addr);
#ifdef HAVE_IPV6
   } else if (inet_pton(AF_INET6, host, &inaddr6) == 1) {
      addr = New(IPADDR(AF_INET6));
      addr->set_type(IPADDR::R_MULTIPLE);
      addr->set_addr6(&inaddr6);
      addr_list->append(addr);
#endif
   } else {
      if (family != 0) {
         errmsg = resolv_host(family, host, addr_list);
         if (errmsg) {
            *errstr = errmsg;
            free_addresses(addr_list);
            return 0;
         }
      } else {
#ifdef HAVE_IPV6
         /* We try to resolv host for ipv6 and ipv4, the connection procedure
          * will try to reach the host for each protocols. We report only "Host
          * not found" ipv4 message (no need to have ipv6 and ipv4 messages).
          */
         resolv_host(AF_INET6, host, addr_list);
#endif
         errmsg = resolv_host(AF_INET, host, addr_list);

         if (addr_list->size() == 0) {
            *errstr = errmsg;
            free_addresses(addr_list);
            return 0;
         }
      }
   }
   return addr_list;
}

/*
 * Convert a network "signal" code into
 * human readable ASCII.
 */
const char *bnet_sig_to_ascii(int32_t msglen)
{
   static char buf[30];
   switch (msglen) {
   case BNET_EOD:
      return "BNET_EOD";            /* End of data stream, new data may follow */
   case BNET_EOD_POLL:
      return "BNET_EOD_POLL";       /* End of data and poll all in one */
   case BNET_STATUS:
      return "BNET_STATUS";         /* Send full status */
   case BNET_TERMINATE:
      return "BNET_TERMINATE";      /* Conversation terminated, doing close() */
   case BNET_POLL:
      return "BNET_POLL";           /* Poll request, I'm hanging on a read */
   case BNET_HEARTBEAT:
      return "BNET_HEARTBEAT";      /* Heartbeat Response requested */
   case BNET_HB_RESPONSE:
      return "BNET_HB_RESPONSE";    /* Only response permited to HB */
   case BNET_BTIME:
      return "BNET_BTIME";          /* Send UTC btime */
   case BNET_BREAK:
      return "BNET_BREAK";          /* Stop current command -- ctl-c */
   case BNET_START_SELECT:
      return "BNET_START_SELECT";   /* Start of a selection list */
   case BNET_END_SELECT:
      return "BNET_END_SELECT";     /* End of a select list */
   case BNET_INVALID_CMD:
      return "BNET_INVALID_CMD";    /* Invalid command sent */
   case BNET_CMD_FAILED:
      return "BNET_CMD_FAILED";     /* Command failed */
   case BNET_CMD_OK:
      return "BNET_CMD_OK";         /* Command succeeded */
   case BNET_CMD_BEGIN:
      return "BNET_CMD_BEGIN";      /* Start command execution */
   case BNET_MSGS_PENDING:
      return "BNET_MSGS_PENDING";   /* Messages pending */
   case BNET_MAIN_PROMPT:
      return "BNET_MAIN_PROMPT";    /* Server ready and waiting */
   case BNET_SELECT_INPUT:
      return "BNET_SELECT_INPUT";   /* Return selection input */
   case BNET_WARNING_MSG:
      return "BNET_WARNING_MSG";    /* Warning message */
   case BNET_ERROR_MSG:
      return "BNET_ERROR_MSG";      /* Error message -- command failed */
   case BNET_INFO_MSG:
      return "BNET_INFO_MSG";       /* Info message -- status line */
   case BNET_RUN_CMD:
      return "BNET_RUN_CMD";        /* Run command follows */
   case BNET_YESNO:
      return "BNET_YESNO";          /* Request yes no response */
   case BNET_START_RTREE:
      return "BNET_START_RTREE";    /* Start restore tree mode */
   case BNET_END_RTREE:
      return "BNET_END_RTREE";      /* End restore tree mode */
   case BNET_SUB_PROMPT:
      return "BNET_SUB_PROMPT";     /* Indicate we are at a subprompt */
   case BNET_TEXT_INPUT:
      return "BNET_TEXT_INPUT";     /* Get text input from user */
   case BNET_EXT_TERMINATE:
       return "BNET_EXT_TERMINATE"; /* A Terminate condition has been met and
                                       already reported somewhere else */
   case BNET_FDCALLED:
      return "BNET_FDCALLED";       /* The FD should keep the connection for a new job */
   default:
      bsnprintf(buf, sizeof(buf), _("Unknown sig %d"), (int)msglen);
      return buf;
   }
}

int set_socket_errno(int sockstat)
{
#ifdef HAVE_WIN32
   /*
    * For Windows, we must simulate Unix errno on a socket
    *  error in order to handle errors correctly.
    */
   if (sockstat == SOCKET_ERROR) {
      berrno be;
      DWORD err = WSAGetLastError();
      if (err == WSAEINTR) {
         errno = EINTR;
         return sockstat;
      } else if (err == WSAEWOULDBLOCK) {
         errno = EAGAIN;
         return sockstat;
      } else {
         errno = b_errno_win32 | b_errno_WSA;
      }
      Dmsg2(20, "Socket error: err=%d %s\n", err, be.bstrerror(err));
   }
#else
   if (sockstat == SOCKET_ERROR) {
      /* Handle errrors from prior connections as EAGAIN */
      switch (errno) {
         case ENETDOWN:
         case EPROTO:
         case ENOPROTOOPT:
         case EHOSTDOWN:
#ifdef ENONET
         case ENONET:
#endif
         case EHOSTUNREACH:
         case EOPNOTSUPP:
         case ENETUNREACH:
            errno = EAGAIN;
            break;
         default:
            break;
      }
   }
#endif
   return sockstat;
}
