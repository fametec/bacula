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
 * Tray Monitor specific configuration and defines
 *
 *   Adapted from dird_conf.c
 *
 *     Nicolas Boichat, August MMIV
 *
 */

#ifndef TRAY_CONF
#define TRAY_CONF

#include "common.h"

/*
 * Resource codes -- they must be sequential for indexing
 */
enum rescode {
   R_MONITOR = 1001,
   R_DIRECTOR,
   R_CLIENT,
   R_STORAGE,
   R_CONSOLE_FONT,
   R_FIRST = R_MONITOR,
   R_LAST  = R_CONSOLE_FONT                /* keep this updated */
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

struct s_running_job
{
   int32_t Errors;                    /* FD/SD errors */
   int32_t JobType;
   int32_t JobStatus;
   int32_t JobLevel;
   uint32_t JobId;
   uint32_t VolSessionId;
   uint32_t VolSessionTime;
   uint32_t JobFiles;
   uint64_t JobBytes;
   uint64_t ReadBytes;
   utime_t start_time;
   int64_t bytespersec;
   int  SDtls;
   char Client[MAX_NAME_LENGTH];
   char FileSet[MAX_NAME_LENGTH];
   char Storage[MAX_NAME_LENGTH];
   char RStorage[MAX_NAME_LENGTH];
   char sched_time;
   char Job[MAX_NAME_LENGTH];
   char CurrentFile[4096];
};

/* forward definition */
class worker;

/* Director/Client/Storage */
struct RESMON {
   RES      hdr;                   /* Keep First */
   uint32_t type;                  /* Keep 2nd R_CLIENT, R_DIRECTOR, R_STORAGE */

   uint32_t port;                  /* UA server port */
   char    *address;               /* UA server address */
   utime_t  connect_timeout;       /* timeout for connect in seconds */
   char    *password;
   
   bool  use_remote;                /* Use Client Initiated backup feature */
   bool  use_monitor;               /* update the status icon with this resource */
   bool  use_setip;                 /* Send setip command before a job */

   bool  tls_enable;               /* Enable TLS on all connections */
   char *tls_ca_certfile;          /* TLS CA Certificate File */
   char *tls_ca_certdir;           /* TLS CA Certificate Directory */
   char *tls_certfile;             /* TLS Client Certificate File */
   char *tls_keyfile;              /* TLS Client Key File */

   /* ------------------------------------------------------------ */
   TLS_CONTEXT *tls_ctx;           /* Shared TLS Context */
   worker *wrk;                    /* worker that will handle async op */

   QMutex *mutex;
   BSOCK *bs;
   char name[MAX_NAME_LENGTH];
   char version[MAX_NAME_LENGTH];
   char plugins[MAX_NAME_LENGTH];
   char started[32];            /* ISO date */
   char reloaded[32];           /* ISO date */
   int  bwlimit;
   alist *running_jobs;
   dlist *terminated_jobs;
   btime_t last_update;
   bool new_resource;
   bool proxy_sent;

   /* List of resources available */
   alist *jobs;
   alist *clients;
   alist *filesets;
   alist *pools;
   alist *storages;
   alist *catalogs;

   /* Default value */
   struct {
      char *job;
      char *client;
      char *pool;
      char *storage;
      char *level;
      char *type;
      char *fileset;
      char *catalog;
      int   priority;
      char *where;
      int   replace;
   } defaults;

   /* Information about the job */
   struct {
      uint64_t JobBytes;
      uint32_t JobFiles;
      int CorrJobBytes;
      int CorrJobFiles;
      int CorrNbJob;
      char JobLevel;
   } infos;
};

Q_DECLARE_METATYPE(RESMON*)

/*
 *   Tray Monitor Resource
 *
 */
struct MONITOR {
   RES   hdr;                         /* Keep first */
   int32_t type;                      /* Keep second */

   bool comm_compression;             /* Enable comm line compression */
   bool require_ssl;                  /* Require SSL for all connections */
   bool display_advanced_options;     /* Display advanced options (run options for example) */
   MSGS *messages;                    /* Daemon message handler */
   char *password;                    /* UA server password */
   char *command_dir;                 /* Where to find Commands */
   utime_t RefreshInterval;           /* Status refresh interval */
};


/* Define the Union of all the above
 * resource structure definitions.
 */
union URES {
   MONITOR    res_monitor;
   RESMON     res_main;
   RES        hdr;
};

void error_handler(const char *file, int line, LEX *lc, const char *msg, ...);

#endif
