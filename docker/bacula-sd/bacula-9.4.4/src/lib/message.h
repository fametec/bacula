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
 * Define Message Types for Bacula
 *    Kern Sibbald, 2000
 *
 */


#include "bits.h"

#undef  M_DEBUG
#undef  M_ABORT
#undef  M_FATAL
#undef  M_ERROR
#undef  M_WARNING
#undef  M_INFO
#undef  M_MOUNT
#undef  M_ERROR_TERM
#undef  M_TERM
#undef  M_RESTORED
#undef  M_SECURITY
#undef  M_ALERT
#undef  M_VOLMGMT

/*
 * Most of these message levels are more or less obvious.
 * They have evolved somewhat during the development of Bacula,
 * and here are some of the details of where I am trying to
 * head (in the process of changing the code) as of 15 June 2002.
 *
 *  M_ABORT       Bacula immediately aborts and tries to produce a traceback
 *                  This is for really serious errors like segmentation fault.
 *  M_ERROR_TERM  Bacula immediately terminates but no dump. This is for
 *                  "obvious" serious errors like daemon already running or
 *                   cannot open critical file, ... where a dump is not wanted.
 *  M_TERM        Bacula daemon shutting down because of request (SIGTERM).
 *
 * The remaining apply to Jobs rather than the daemon.
 *
 *  M_FATAL       Bacula detected a fatal Job error. The Job will be killed,
 *                  but Bacula continues running.
 *  M_ERROR       Bacula detected a Job error. The Job will continue running
 *                  but the termination status will be error.
 *  M_WARNING     Job warning message.
 *  M_INFO        Job information message.
 *
 *  M_RESTORED    An ls -l of each restored file.
 *
 *  M_SECURITY    For security alerts. This is equivalent to FATAL, but
 *                  generally occurs in a daemon with no JCR.
 *
 *  M_ALERT       For Tape Alert messages.
 *
 *  M_VOLMGMT     Volume Management message
 *
 * M_DEBUG and M_SAVED are excluded from M_ALL by default
 */

enum {
   /* Keep M_ABORT=1 for dlist.h */
   M_ABORT = 1,                       /* MUST abort immediately */
   M_DEBUG,                           /* debug message */
   M_FATAL,                           /* Fatal error, stopping job */
   M_ERROR,                           /* Error, but recoverable */
   M_WARNING,                         /* Warning message */
   M_INFO,                            /* Informational message */
   M_SAVED,                           /* Info on saved file */
   M_NOTSAVED,                        /* Info on notsaved file */
   M_SKIPPED,                         /* File skipped during backup by option setting */
   M_MOUNT,                           /* Mount requests */
   M_ERROR_TERM,                      /* Error termination request (no dump) */
   M_TERM,                            /* Terminating daemon normally */
   M_RESTORED,                        /* ls -l of restored files */
   M_SECURITY,                        /* security violation */
   M_ALERT,                           /* tape alert messages */
   M_VOLMGMT                          /* Volume management messages */
};

#define M_MAX      M_VOLMGMT          /* keep this updated ! */

/* Define message destination structure */
/* *** FIXME **** where should be extended to handle multiple values */
typedef struct s_dest {
   struct s_dest *next;
   int dest_code;                     /* destination (one of the MD_ codes) */
   int max_len;                       /* max mail line length */
   FILE *fd;                          /* file descriptor */
   char msg_types[nbytes_for_bits(M_MAX+1)]; /* message type mask */
   char *where;                       /* filename/program name */
   char *mail_cmd;                    /* mail command */
   POOLMEM *mail_filename;            /* unique mail filename */
} DEST;

/* Message Destination values for dest field of DEST */
enum {
   MD_SYSLOG = 1,                     /* send msg to syslog */
   MD_MAIL,                           /* email group of messages */
   MD_FILE,                           /* write messages to a file */
   MD_APPEND,                         /* append messages to a file */
   MD_STDOUT,                         /* print messages */
   MD_STDERR,                         /* print messages to stderr */
   MD_DIRECTOR,                       /* send message to the Director */
   MD_OPERATOR,                       /* email a single message to the operator */
   MD_CONSOLE,                        /* send msg to UserAgent or console */
   MD_MAIL_ON_ERROR,                  /* email messages if job errors */
   MD_MAIL_ON_SUCCESS,                /* email messages if job succeeds */
   MD_CATALOG                         /* sent to catalog Log table */
};

/* Queued message item */
struct MQUEUE_ITEM {
   dlink link;
   int type;                          /* message M_ type */
   int repeat;                        /* count of identical messages */
   utime_t mtime;                     /* time stamp message queued */
   char msg[1];                       /* message text */
};

/* Debug options */
#define DEBUG_CLEAR_FLAGS           /* 0    clear debug_flags */
#define DEBUG_NO_WIN32_WRITE_ERROR  /* i    continue even after win32 errors */
#define DEBUG_WIN32DECOMP           /* d */
#define DEBUG_DBG_TIMESTAMP         /* t    turn on timestamp in trace file */
#define DEBUG_DBG_NO_TIMESTAMP      /* T    turn off timestamp in trace file */
#define DEBUG_TRUNCATE_TRACE        /* c    clear trace file if opened */

/* Bits (1, 2, 4, ...) for debug_flags used by set_debug_flags() */
#define DEBUG_MUTEX_EVENT           (1 << 0)    /* l */
#define DEBUG_PRINT_EVENT           (1 << 1)    /* p */

/* Tags that can be used with the setdebug command
 * We can extend this list to use 64bit
 * When adding new ones, keep existing one
 * Corresponding strings are defined in messages.c (debugtags)
 */
#define    DT_LOCK       (1<<30)                /* lock    */
#define    DT_NETWORK    (1<<29)                /* network */
#define    DT_PLUGIN     (1<<28)                /* plugin  */
#define    DT_VOLUME     (1<<27)                /* volume  */
#define    DT_SQL        (1<<26)                /* sql     */
#define    DT_BVFS       (1<<25)                /* bvfs    */
#define    DT_MEMORY     (1<<24)                /* memory  */
#define    DT_SCHEDULER  (1<<23)                /* scheduler */
#define    DT_PROTOCOL   (1<<22)                /* protocol */
#define    DT_xxxxx      (1<<21)                /* reserved BEE */
#define    DT_xxx        (1<<20)                /* reserved BEE */
#define    DT_SNAPSHOT   (1<<19)                /* Snapshot */
#define    DT_RECORD     (1<<18)                /* Record/block */
#define    DT_ASX        (1<<16)                /* used by Alain for personal debugging */
#define    DT_ALL        (0x7FFF0000)           /* all (up to debug_level 65635, 15 flags available) */

const char *debug_get_tag(uint32_t pos, const char **desc);
bool debug_find_tag(const char *tagname, bool add, int64_t *current_level);
bool debug_parse_tags(const char *options, int64_t *current_level);


void d_msg(const char *file, int line, int64_t level, const char *fmt,...) CHECK_FORMAT(printf, 4, 5);
void e_msg(const char *file, int line, int type, int level, const char *fmt,...) CHECK_FORMAT(printf, 5, 6);;
void Jmsg(JCR *jcr, int type, utime_t mtime, const char *fmt,...) CHECK_FORMAT(printf, 4, 5);
void Qmsg(JCR *jcr, int type, utime_t mtime, const char *fmt,...) CHECK_FORMAT(printf, 4, 5);
bool get_trace(void);
void set_debug_flags(char *options);
const char *get_basename(const char *pathname);
bool is_message_type_set(JCR *jcr, int type);
void set_trace_for_tools(FILE *new_trace_fd); // called by Bacula's tools only

class BDB;                                              /* define forward reference */
typedef bool (*sql_query_call)(JCR *jcr, const char *cmd);
typedef bool (*sql_escape_call)(JCR *jcr, BDB *db, char *snew, char *sold, int len);

extern DLL_IMP_EXP sql_query_call  p_sql_query;
extern DLL_IMP_EXP sql_escape_call p_sql_escape;

extern DLL_IMP_EXP int64_t       debug_level;
extern DLL_IMP_EXP int64_t       debug_level_tags;
extern DLL_IMP_EXP int32_t       debug_flags;
extern DLL_IMP_EXP bool          dbg_timestamp;          /* print timestamp in debug output */
extern DLL_IMP_EXP bool          prt_kaboom;             /* Print kaboom output */
extern DLL_IMP_EXP int           verbose;
extern DLL_IMP_EXP char          my_name[];
extern DLL_IMP_EXP const char *  working_directory;
extern DLL_IMP_EXP utime_t       daemon_start_time;

extern DLL_IMP_EXP bool          console_msg_pending;
extern DLL_IMP_EXP FILE *        con_fd;                 /* Console file descriptor */
extern DLL_IMP_EXP brwlock_t     con_lock;               /* Console lock structure */
