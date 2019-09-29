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
 * Bacula Adminstration Tool (bat) 
 *
 *     Kern Sibbald, March 2002
 */

#ifndef _BAT_CONF_H_
#define _BAT_CONF_H_

/*
 * Resource codes -- they must be sequential for indexing
 */

enum {
   R_DIRECTOR = 1001,
   R_CONSOLE,
   R_CONSOLE_FONT,
   R_FIRST = R_DIRECTOR,
   R_LAST = R_CONSOLE_FONT            /* Keep this updated */
};

/*
 * Some resource attributes
 */
enum {
   R_NAME = 1020,
   R_ADDRESS,
   R_PASSWORD,
   R_TYPE,
   R_BACKUP
};


/* Definition of the contents of each Resource */
class DIRRES {
public:
   RES   hdr;
   uint32_t DIRport;                  /* UA server port */
   char *address;                     /* UA server address */
   char *password;                    /* UA server password */
   bool tls_authenticate;             /* Authenticate with tls */
   bool tls_enable;                   /* Enable TLS */
   bool tls_require;                  /* Require TLS */
   char *tls_ca_certfile;             /* TLS CA Certificate File */
   char *tls_ca_certdir;              /* TLS CA Certificate Directory */
   char *tls_certfile;                /* TLS Client Certificate File */
   char *tls_keyfile;                 /* TLS Client Key File */
   utime_t heartbeat_interval;        /* Dir heartbeat interval */

   TLS_CONTEXT *tls_ctx;              /* Shared TLS Context */

   /* Methods */
   char *name() const;
};

inline char *DIRRES::name() const { return hdr.name; }


struct CONFONTRES {
   RES   hdr;
   char *fontface;                    /* Console Font specification */
};

class CONRES {
public:
   RES   hdr;
   char *password;                    /* UA server password */
   bool comm_compression;             /* Enable comm line compression */
   bool tls_authenticate;             /* Authenticate with tls */
   bool tls_enable;                   /* Enable TLS on all connections */
   bool tls_require;                  /* Require TLS on all connections */
   char *tls_ca_certfile;             /* TLS CA Certificate File */
   char *tls_ca_certdir;              /* TLS CA Certificate Directory */
   char *tls_certfile;                /* TLS Client Certificate File */
   char *tls_keyfile;                 /* TLS Client Key File */
   char *director;                    /* bind to director */
   utime_t heartbeat_interval;        /* Cons heartbeat interval */

   TLS_CONTEXT *tls_ctx;              /* Shared TLS Context */

   /* Methods */
   char *name() const;
};

inline char *CONRES::name() const { return hdr.name; }


/* Define the Union of all the above
 * resource structure definitions.
 */
union u_res {
   DIRRES dir_res;
   CONRES con_res;
   CONFONTRES con_font;
   RES hdr;
};

typedef union u_res URES;

#define GetConsoleResWithName(x) ((CONRES *)GetResWithName(R_CONSOLE, (x)))
#define GetDirResWithName(x) ((DIRRES *)GetResWithName(R_DIRECTOR, (x)))


#endif /* _BAT_CONF_H_ */
