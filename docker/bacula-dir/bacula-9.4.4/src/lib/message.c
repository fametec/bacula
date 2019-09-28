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
 * Bacula message handling routines
 *
 * NOTE: don't use any Jmsg or Qmsg calls within this file,
 *   except in q_msg or j_msg (setup routines),
 *   otherwise you may get into recursive calls if there are
 *   errors, and that can lead to looping or deadlocks.
 *
 *   Kern Sibbald, April 2000
 *
 */

#include "bacula.h"
#include "jcr.h"

sql_query_call  p_sql_query = NULL;
sql_escape_call p_sql_escape = NULL;

#define FULL_LOCATION 1               /* set for file:line in Debug messages */

/*
 *  This is where we define "Globals" because all the
 *    daemons include this file.
 */
dlist *daemon_msg_queue = NULL;
pthread_mutex_t daemon_msg_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool dequeuing_daemon_msgs = false;
const char *working_directory = NULL; /* working directory path stored here */
const char *assert_msg = NULL;        /* ASSERT2 error message */
const char *version = VERSION " (" BDATE ")";
const char *dist_name = DISTNAME " " DISTVER;
char *exepath = (char *)NULL;
char *exename = (char *)NULL;
char db_engine_name[50] = {0};        /* Database engine name or type */
char con_fname[500];                  /* Console filename */
char my_name[MAX_NAME_LENGTH] = {0};  /* daemon name is stored here */
char host_name[50] = {0};             /* host machine name */
char fail_time[30] = {0};             /* Time of failure */
int verbose = 0;                      /* increase User messages */
int64_t debug_level = 0;              /* debug level */
int64_t debug_level_tags = 0;         /* debug tags */
int32_t debug_flags = 0;              /* debug flags */
bool console_msg_pending = false;
utime_t daemon_start_time = 0;        /* Daemon start time */
FILE *con_fd = NULL;                  /* Console file descriptor */
brwlock_t con_lock;                   /* Console lock structure */
bool dbg_timestamp = false;           /* print timestamp in debug output */
bool dbg_thread = false;              /* add thread_id to details */
bool prt_kaboom = false;              /* Print kaboom output */
job_code_callback_t message_job_code_callback = NULL;   /* Job code callback. Only used by director. */

/* Forward referenced functions */

/* Imported functions */
void create_jcr_key();

/* Static storage */

/* Exclude spaces but require .mail at end */
#define MAIL_REGEX "^[^ ]+\\.mail$"

/* Allow only one thread to tweak d->fd at a time */
static pthread_mutex_t fides_mutex = PTHREAD_MUTEX_INITIALIZER;
static MSGS *daemon_msgs;              /* global messages */
static void (*message_callback)(int type, char *msg) = NULL;
static FILE *trace_fd = NULL;
#if defined(HAVE_WIN32)
static bool trace = true;
#else
static bool trace = false;
#endif
static int hangup = 0;
static int blowup = 0;

/* Constants */
const char *host_os = HOST_OS;
const char *distname = DISTNAME;
const char *distver = DISTVER;

/*
 * Walk back in a string from end looking for a
 *  path separator.
 *  This routine is passed the start of the string and
 *  the end of the string, it returns either the beginning
 *  of the string or where it found a path separator.
 */
static const char *bstrrpath(const char *start, const char *end)
{
   while ( end > start ) {
      end--;
      if (IsPathSeparator(*end)) {
         break;
      }
   }
   return end;
}

/* Some message class methods */
void MSGS::lock()
{
   P(fides_mutex);
}

void MSGS::unlock()
{
   V(fides_mutex);
}

/*
 * Wait for not in use variable to be clear
 */
void MSGS::wait_not_in_use()     /* leaves fides_mutex set */
{
   lock();
   while (m_in_use || m_closing) {
      unlock();
      bmicrosleep(0, 200);         /* wait */
      lock();
   }
}

/*
 * Handle message delivery errors
 */
static void delivery_error(const char *fmt,...)
{
   va_list   arg_ptr;
   int i, len, maxlen;
   POOLMEM *pool_buf;
   char dt[MAX_TIME_LENGTH];
   int dtlen;

   pool_buf = get_pool_memory(PM_EMSG);

   bstrftime_ny(dt, sizeof(dt), time(NULL));
   dtlen = strlen(dt);
   dt[dtlen++] = ' ';
   dt[dtlen] = 0;

   i = Mmsg(pool_buf, "%s Message delivery ERROR: ", dt);

   for (;;) {
      maxlen = sizeof_pool_memory(pool_buf) - i - 1;
      va_start(arg_ptr, fmt);
      len = bvsnprintf(pool_buf+i, maxlen, fmt, arg_ptr);
      va_end(arg_ptr);
      if (len < 0 || len >= (maxlen-5)) {
         pool_buf = realloc_pool_memory(pool_buf, maxlen + i + maxlen/2);
         continue;
      }
      break;
   }

   fputs(pool_buf, stdout);  /* print this here to INSURE that it is printed */
   fflush(stdout);
   syslog(LOG_DAEMON|LOG_ERR, "%s", pool_buf);
   free_memory(pool_buf);
}

void set_debug_flags(char *options)
{
   for (char *p = options; *p ; p++) {
      switch(*p) {
      case '0':                 /* clear flags */
         debug_flags = 0;
         break;

      case 'i':                 /* used by FD */
      case 'd':                 /* used by FD */
         break;

      case 't':
         dbg_timestamp = true;
         break;

      case 'T':
         dbg_timestamp = false;
         break;

      case 'h':
         dbg_thread = true;
         break;

      case 'H':
         dbg_thread = false;
         break;

      case 'c':
         /* truncate the trace file */
         if (trace && trace_fd) {
            ftruncate(fileno(trace_fd), 0);
         }
         break;

      case 'l':
         /* Turn on/off add_events for P()/V() */
         debug_flags |= DEBUG_MUTEX_EVENT;
         break;

      case 'p':
         /* Display event stack during lockdump */
         debug_flags |= DEBUG_PRINT_EVENT;
         break;

      default:
         Dmsg1(000, "Unknown debug flag %c\n", *p);
      }
   }
}

void register_message_callback(void msg_callback(int type, char *msg))
{
   message_callback = msg_callback;
}


/*
 * Set daemon name. Also, find canonical execution
 *  path.  Note, exepath has spare room for tacking on
 *  the exename so that we can reconstruct the full name.
 *
 * Note, this routine can get called multiple times
 *  The second time is to put the name as found in the
 *  Resource record. On the second call, generally,
 *  argv is NULL to avoid doing the path code twice.
 */
void my_name_is(int argc, char *argv[], const char *name)
{
   char *l, *p;
   char *cpath;
   char *cargv0;
   int len;
   int path_max;
   bool respath;

   if (gethostname(host_name, sizeof(host_name)) != 0) {
      bstrncpy(host_name, "Hostname unknown", sizeof(host_name));
   }
   bstrncpy(my_name, name, sizeof(my_name));

   if (argc>0 && argv && argv[0]) {
      /* use a dynamic PATH_MAX and allocate temporary variables */
      path_max = pathconf(argv[0], _PC_PATH_MAX);
      if (path_max < 4096){
         path_max = 4096;
      }
      cpath = (char *)malloc(path_max);
      cargv0 = (char *)malloc(path_max);

      respath = false;
#ifdef HAVE_REALPATH
      /* make a canonical argv[0] */
      if (realpath(argv[0], cargv0) != NULL){
         respath = true;
      }
#endif
      if (!respath){
         /* no resolved_path available in cargv0, so populate it */
         strncpy(cargv0, argv[0], path_max);
      }
      /* strip trailing filename and save exepath */
      for (l=p=cargv0; *p; p++) {
         if (IsPathSeparator(*p)) {
            l = p;                       /* set pos of last path separator */
         }
      }
      if (IsPathSeparator(*l)) {
         l++;
      } else {
         l = cargv0;
#if defined(HAVE_WIN32)
         /* On Windows allow c: drive specification */
         if (l[1] == ':') {
            l += 2;
         }
#endif
      }
      len = strlen(l) + 1;
      if (exename) {
         free(exename);
      }
      exename = (char *)malloc(len);
      strcpy(exename, l);
      if (exepath) {
         free(exepath);
      }
      /* separate exepath from exename */
      *l = 0;
      exepath = bstrdup(cargv0);
      if (strstr(exepath, PathSeparatorUp) != NULL || strstr(exepath, PathSeparatorCur) != NULL || !IsPathSeparator(exepath[0])) {
         /* fallback to legacy code */
         if (getcwd(cpath, path_max)) {
            free(exepath);
            exepath = (char *)malloc(strlen(cpath) + 1 + len);
            strcpy(exepath, cpath);
         }
      }
      Dmsg2(500, "exepath=%s\nexename=%s\n", exepath, exename);
      free(cpath);
      free(cargv0);
   }
}

/* Set special ASSERT2 message where debugger can find it */
void
set_assert_msg(const char *file, int line, const char *msg)
{
   char buf[2000];
   bsnprintf(buf, sizeof(buf), "ASSERT at %s:%d-%u ERR=%s",
      get_basename(file), line, get_jobid_from_tsd(), msg);
   assert_msg = bstrdup(buf);
}

void set_db_engine_name(const char *name)
{
   bstrncpy(db_engine_name, name, sizeof(db_engine_name)-1);
}

/*
 * Initialize message handler for a daemon or a Job
 *   We make a copy of the MSGS resource passed, so it belows
 *   to the job or daemon and thus can be modified.
 *
 *   NULL for jcr -> initialize global messages for daemon
 *   non-NULL     -> initialize jcr using Message resource
 */
void
init_msg(JCR *jcr, MSGS *msg, job_code_callback_t job_code_callback)
{
   DEST *d, *dnew, *temp_chain = NULL;
   int i;

   if (jcr == NULL && msg == NULL) {
      init_last_jobs_list();
      /* Create a daemon key then set invalid jcr */
      /* Maybe we should give the daemon a jcr??? */
      create_jcr_key();
      set_jcr_in_tsd(INVALID_JCR);
   }

   message_job_code_callback = job_code_callback;

#if !defined(HAVE_WIN32)
   /*
    * Make sure we have fd's 0, 1, 2 open
    *  If we don't do this one of our sockets may open
    *  there and if we then use stdout, it could
    *  send total garbage to our socket.
    *
    */
   int fd;
   fd = open("/dev/null", O_RDONLY, 0644);
   if (fd > 2) {
      close(fd);
   } else {
      for(i=1; fd + i <= 2; i++) {
         dup2(fd, fd+i);
      }
   }

#endif
   /*
    * If msg is NULL, initialize global chain for STDOUT and syslog
    */
   if (msg == NULL) {
      daemon_msgs = (MSGS *)malloc(sizeof(MSGS));
      memset(daemon_msgs, 0, sizeof(MSGS));
      for (i=1; i<=M_MAX; i++) {
         add_msg_dest(daemon_msgs, MD_STDOUT, i, NULL, NULL);
      }
      Dmsg1(050, "Create daemon global message resource %p\n", daemon_msgs);
      return;
   }

   /*
    * Walk down the message resource chain duplicating it
    * for the current Job.
    */
   for (d=msg->dest_chain; d; d=d->next) {
      dnew = (DEST *)malloc(sizeof(DEST));
      memcpy(dnew, d, sizeof(DEST));
      dnew->next = temp_chain;
      dnew->fd = NULL;
      dnew->mail_filename = NULL;
      if (d->mail_cmd) {
         dnew->mail_cmd = bstrdup(d->mail_cmd);
      }
      if (d->where) {
         dnew->where = bstrdup(d->where);
      }
      temp_chain = dnew;
   }

   if (jcr) {
      jcr->jcr_msgs = (MSGS *)malloc(sizeof(MSGS));
      memset(jcr->jcr_msgs, 0, sizeof(MSGS));
      jcr->jcr_msgs->dest_chain = temp_chain;
      memcpy(jcr->jcr_msgs->send_msg, msg->send_msg, sizeof(msg->send_msg));
   } else {
      /* If we have default values, release them now */
      if (daemon_msgs) {
         free_msgs_res(daemon_msgs);
      }
      daemon_msgs = (MSGS *)malloc(sizeof(MSGS));
      memset(daemon_msgs, 0, sizeof(MSGS));
      daemon_msgs->dest_chain = temp_chain;
      memcpy(daemon_msgs->send_msg, msg->send_msg, sizeof(msg->send_msg));
   }

   Dmsg2(250, "Copy message resource %p to %p\n", msg, temp_chain);
}

/* Initialize so that the console (User Agent) can
 * receive messages -- stored in a file.
 */
void init_console_msg(const char *wd)
{
   int fd;

   bsnprintf(con_fname, sizeof(con_fname), "%s%c%s.conmsg", wd, PathSeparator, my_name);
   fd = open(con_fname, O_CREAT|O_RDWR|O_BINARY, 0600);
   if (fd == -1) {
      berrno be;
      Emsg2(M_ERROR_TERM, 0, _("Could not open console message file %s: ERR=%s\n"),
          con_fname, be.bstrerror());
   }
   if (lseek(fd, 0, SEEK_END) > 0) {
      console_msg_pending = true;
   }
   close(fd);
   con_fd = bfopen(con_fname, "a+b");
   if (!con_fd) {
      berrno be;
      Emsg2(M_ERROR, 0, _("Could not open console message file %s: ERR=%s\n"),
          con_fname, be.bstrerror());
   }
   if (rwl_init(&con_lock) != 0) {
      berrno be;
      Emsg1(M_ERROR_TERM, 0, _("Could not get con mutex: ERR=%s\n"),
         be.bstrerror());
   }
}

/*
 * Called only during parsing of the config file.
 *
 * Add a message destination. I.e. associate a message type with
 *  a destination (code).
 * Note, where in the case of dest_code FILE is a filename,
 *  but in the case of MAIL is a space separated list of
 *  email addresses, ...
 */
void add_msg_dest(MSGS *msg, int dest_code, int msg_type, char *where, char *mail_cmd)
{
   DEST *d;
   /*
    * First search the existing chain and see if we
    * can simply add this msg_type to an existing entry.
    */
   for (d=msg->dest_chain; d; d=d->next) {
      if (dest_code == d->dest_code && ((where == NULL && d->where == NULL) ||
                     bstrcmp(where, d->where))) {
         Dmsg4(850, "Add to existing d=%p msgtype=%d destcode=%d where=%s\n",
             d, msg_type, dest_code, NPRT(where));
         set_bit(msg_type, d->msg_types);
         set_bit(msg_type, msg->send_msg);  /* set msg_type bit in our local */
         return;
      }
   }
   /* Not found, create a new entry */
   d = (DEST *)malloc(sizeof(DEST));
   memset(d, 0, sizeof(DEST));
   d->next = msg->dest_chain;
   d->dest_code = dest_code;
   set_bit(msg_type, d->msg_types);      /* set type bit in structure */
   set_bit(msg_type, msg->send_msg);     /* set type bit in our local */
   if (where) {
      d->where = bstrdup(where);
   }
   if (mail_cmd) {
      d->mail_cmd = bstrdup(mail_cmd);
   }
   Dmsg5(850, "add new d=%p msgtype=%d destcode=%d where=%s mailcmd=%s\n",
          d, msg_type, dest_code, NPRT(where), NPRT(d->mail_cmd));
   msg->dest_chain = d;
}

/*
 * Called only during parsing of the config file.
 *
 * Remove a message destination
 */
void rem_msg_dest(MSGS *msg, int dest_code, int msg_type, char *where)
{
   DEST *d;

   for (d=msg->dest_chain; d; d=d->next) {
      Dmsg2(850, "Remove_msg_dest d=%p where=%s\n", d, NPRT(d->where));
      if (bit_is_set(msg_type, d->msg_types) && (dest_code == d->dest_code) &&
          ((where == NULL && d->where == NULL) ||
                     (strcmp(where, d->where) == 0))) {
         Dmsg3(850, "Found for remove d=%p msgtype=%d destcode=%d\n",
               d, msg_type, dest_code);
         clear_bit(msg_type, d->msg_types);
         Dmsg0(850, "Return rem_msg_dest\n");
         return;
      }
   }
}


/*
 * Create a unique filename for the mail command
 */
static void make_unique_mail_filename(JCR *jcr, POOLMEM *&name, DEST *d)
{
   if (jcr) {
      Mmsg(name, "%s/%s.%s.%d.mail", working_directory, my_name,
                 jcr->Job, (int)(intptr_t)d);
   } else {
      Mmsg(name, "%s/%s.%s.%d.mail", working_directory, my_name,
                 my_name, (int)(intptr_t)d);
   }
   Dmsg1(850, "mailname=%s\n", name);
}

/*
 * Open a mail pipe
 */
static BPIPE *open_mail_pipe(JCR *jcr, POOLMEM *&cmd, DEST *d)
{
   BPIPE *bpipe;

   if (d->mail_cmd) {
      cmd = edit_job_codes(jcr, cmd, d->mail_cmd, d->where, message_job_code_callback);
   } else {
      Mmsg(cmd, "/usr/lib/sendmail -F Bacula %s", d->where);
   }
   fflush(stdout);

   if ((bpipe = open_bpipe(cmd, 120, "rw"))) {
      /* If we had to use sendmail, add subject */
      if (!d->mail_cmd) {
         fprintf(bpipe->wfd, "Subject: %s\r\n\r\n", _("Bacula Message"));
      }
   } else {
      berrno be;
      delivery_error(_("open mail pipe %s failed: ERR=%s\n"),
         cmd, be.bstrerror());
   }
   return bpipe;
}

/*
 * Close the messages for this Messages resource, which means to close
 *  any open files, and dispatch any pending email messages.
 */
void close_msg(JCR *jcr)
{
   MSGS *msgs;
   DEST *d;
   BPIPE *bpipe;
   POOLMEM *cmd, *line;
   int len, stat;

   Dmsg1(580, "Close_msg jcr=%p\n", jcr);

   if (jcr == NULL) {                /* NULL -> global chain */
      msgs = daemon_msgs;
   } else {
      msgs = jcr->jcr_msgs;
      jcr->jcr_msgs = NULL;
   }
   if (msgs == NULL) {
      return;
   }

   /* Wait for item to be not in use, then mark closing */
   if (msgs->is_closing()) {
      return;
   }
   msgs->wait_not_in_use();          /* leaves fides_mutex set */
   /* Note get_closing() does not lock because we are already locked */
   if (msgs->get_closing()) {
      msgs->unlock();
      return;
   }
   msgs->set_closing();
   msgs->unlock();

   Dmsg1(850, "===Begin close msg resource at %p\n", msgs);
   cmd = get_pool_memory(PM_MESSAGE);
   for (d=msgs->dest_chain; d; ) {
      bool success;
      if (d->fd) {
         switch (d->dest_code) {
         case MD_FILE:
         case MD_APPEND:
            if (d->fd) {
               fclose(d->fd);            /* close open file descriptor */
               d->fd = NULL;
            }
            break;
         case MD_MAIL:
         case MD_MAIL_ON_ERROR:
         case MD_MAIL_ON_SUCCESS:
            Dmsg0(850, "Got MD_MAIL, MD_MAIL_ON_ERROR or MD_MAIL_ON_SUCCESS\n");
            if (!d->fd) {
               break;
            }
            success = jcr && (jcr->JobStatus == JS_Terminated || jcr->JobStatus == JS_Warnings);

            if (d->dest_code == MD_MAIL_ON_ERROR && success) {
               goto rem_temp_file;       /* no mail */
            } else if (d->dest_code == MD_MAIL_ON_SUCCESS && !success) {
               goto rem_temp_file;       /* no mail */
            }

            if (!(bpipe=open_mail_pipe(jcr, cmd, d))) {
               Pmsg0(000, _("open mail pipe failed.\n"));
               goto rem_temp_file;       /* error get out */
            }
            Dmsg0(850, "Opened mail pipe\n");
            len = d->max_len+10;
            line = get_memory(len);
            rewind(d->fd);
            while (fgets(line, len, d->fd)) {
               fputs(line, bpipe->wfd);
            }
            if (!close_wpipe(bpipe)) {       /* close write pipe sending mail */
               berrno be;
               Pmsg1(000, _("close error: ERR=%s\n"), be.bstrerror());
            }

            /*
             * Since we are closing all messages, before "recursing"
             * make sure we are not closing the daemon messages, otherwise
             * kaboom.
             */
            if (msgs != daemon_msgs) {
               /* read what mail prog returned -- should be nothing */
               while (fgets(line, len, bpipe->rfd)) {
                  delivery_error(_("Mail prog: %s"), line);
               }
            }

            stat = close_bpipe(bpipe);
            if (stat != 0 && msgs != daemon_msgs) {
               berrno be;
               be.set_errno(stat);
               Dmsg1(850, "Calling emsg. CMD=%s\n", cmd);
               delivery_error(_("Mail program terminated in error.\n"
                                 "CMD=%s\n"
                                 "ERR=%s\n"), cmd, be.bstrerror());
            }
            free_memory(line);

rem_temp_file:
            /* Remove temp mail file */
            if (d->fd) {
              fclose(d->fd);
              d->fd = NULL;
            }
            /* Exclude spaces in mail_filename */
            if (d->mail_filename) {
               safer_unlink(d->mail_filename, MAIL_REGEX);
               free_pool_memory(d->mail_filename);
               d->mail_filename = NULL;
            }
            Dmsg0(850, "end mail or mail on error\n");
            break;
         default:
            break;
         }
         d->fd = NULL;
      }
      d = d->next;                    /* point to next buffer */
   }
   free_pool_memory(cmd);
   Dmsg0(850, "Done walking message chain.\n");
   if (jcr) {
      free_msgs_res(msgs);
      msgs = NULL;
   } else {
      msgs->clear_closing();
   }
   Dmsg0(850, "===End close msg resource\n");
}

/*
 * Free memory associated with Messages resource
 */
void free_msgs_res(MSGS *msgs)
{
   DEST *d, *old;

   /* Walk down the message chain releasing allocated buffers */
   for (d=msgs->dest_chain; d; ) {
      if (d->where) {
         free(d->where);
         d->where = NULL;
      }
      if (d->mail_cmd) {
         free(d->mail_cmd);
         d->mail_cmd = NULL;
      }
      old = d;                        /* save pointer to release */
      d = d->next;                    /* point to next buffer */
      free(old);                      /* free the destination item */
   }
   msgs->dest_chain = NULL;
   free(msgs);                        /* free the head */
}


/*
 * Terminate the message handler for good.
 * Release the global destination chain.
 *
 * Also, clean up a few other items (cons, exepath). Note,
 *   these really should be done elsewhere.
 */
void term_msg()
{
   Dmsg0(850, "Enter term_msg\n");
   close_msg(NULL);                   /* close global chain */
   free_msgs_res(daemon_msgs);        /* free the resources */
   daemon_msgs = NULL;
   if (con_fd) {
      fflush(con_fd);
      fclose(con_fd);
      con_fd = NULL;
   }
   if (exepath) {
      free(exepath);
      exepath = NULL;
   }
   if (exename) {
      free(exename);
      exename = NULL;
   }
   if (trace_fd) {
      fclose(trace_fd);
      trace_fd = NULL;
      trace = false;
   }
   working_directory = NULL;
   term_last_jobs_list();
}

static bool open_dest_file(JCR *jcr, DEST *d, const char *mode)
{
   d->fd = bfopen(d->where, mode);
   if (!d->fd) {
      berrno be;
      delivery_error(_("fopen %s failed: ERR=%s\n"), d->where, be.bstrerror());
      return false;
   }
   return true;
}

/* Split the output for syslog (it converts \n to ' ' and is
 *   limited to 1024 characters per syslog message
 */
static void send_to_syslog(int mode, const char *msg)
{
   int len;
   char buf[1024];
   const char *p2;
   const char *p = msg;

   while (*p && ((p2 = strchr(p, '\n')) != NULL)) {
      len = MIN((int)sizeof(buf) - 1, p2 - p + 1); /* Add 1 to keep \n */
      strncpy(buf, p, len);
      buf[len] = 0;
      syslog(mode, "%s", buf);
      p = p2+1;                 /* skip \n */
   }
   if (*p != 0) {               /* no \n at the end ? */
      syslog(mode, "%s", p);
   }
}

/*
 * Handle sending the message to the appropriate place
 */
void dispatch_message(JCR *jcr, int type, utime_t mtime, char *msg)
{
    DEST *d;
    char dt[MAX_TIME_LENGTH];
    POOLMEM *mcmd;
    int len, dtlen;
    MSGS *msgs;
    BPIPE *bpipe;
    const char *mode;
    bool created_jcr = false;

    Dmsg2(850, "Enter dispatch_msg type=%d msg=%s", type, msg);

    /*
     * Most messages are prefixed by a date and time. If mtime is
     *  zero, then we use the current time.  If mtime is 1 (special
     *  kludge), we do not prefix the date and time. Otherwise,
     *  we assume mtime is a utime_t and use it.
     */
    if (mtime == 0) {
       mtime = time(NULL);
    }
    if (mtime == 1) {
       *dt = 0;
       dtlen = 0;
       mtime = time(NULL);      /* get time for SQL log */
    } else {
       bstrftime_ny(dt, sizeof(dt), mtime);
       dtlen = strlen(dt);
       dt[dtlen++] = ' ';
       dt[dtlen] = 0;
    }

    /* If the program registered a callback, send it there */
    if (message_callback) {
       message_callback(type, msg);
       return;
    }

    /* For serious errors make sure message is printed or logged */
    if (type == M_ABORT || type == M_ERROR_TERM) {
       fputs(dt, stdout);
       fputs(msg, stdout);
       fflush(stdout);
       if (type == M_ABORT) {
          syslog(LOG_DAEMON|LOG_ERR, "%s", msg);
       }
    }


    /* Now figure out where to send the message */
    msgs = NULL;
    if (!jcr) {
       jcr = get_jcr_from_tsd();
    }

/* Temporary fix for a deadlock in the reload command when
 * the configuration has a problem. The JCR chain is locked
 * during the reload, we cannot create a new JCR.
 */
#if 0
    if (!jcr) {
       jcr = new_jcr(sizeof(JCR), NULL);
       created_jcr = true;
    }
#endif

    if (jcr) {
       msgs = jcr->jcr_msgs;
    }
    if (msgs == NULL) {
       msgs = daemon_msgs;
    }
    /*
     * If closing this message resource, print and send to syslog,
     *   then get out.
     */
    if (msgs->is_closing()) {
       fputs(dt, stdout);
       fputs(msg, stdout);
       fflush(stdout);
       syslog(LOG_DAEMON|LOG_ERR, "%s", msg);
       return;
    }


    for (d=msgs->dest_chain; d; d=d->next) {
       if (bit_is_set(type, d->msg_types)) {
          bool ok;
          switch (d->dest_code) {
             case MD_CATALOG:
                char ed1[50];
                if (!jcr || !jcr->db) {
                   break;
                }
                if (p_sql_query && p_sql_escape) {
                   POOLMEM *cmd = get_pool_memory(PM_MESSAGE);
                   POOLMEM *esc_msg = get_pool_memory(PM_MESSAGE);

                   int len = strlen(msg) + 1;
                   esc_msg = check_pool_memory_size(esc_msg, len*2+1);
                   ok = p_sql_escape(jcr, jcr->db, esc_msg, msg, len);
                   if (ok) {
                      bstrutime(dt, sizeof(dt), mtime);
                      Mmsg(cmd, "INSERT INTO Log (JobId, Time, LogText) VALUES (%s,'%s','%s')",
                           edit_int64(jcr->JobId, ed1), dt, esc_msg);
                      ok = p_sql_query(jcr, cmd);
                   }
                   if (!ok) {
                      delivery_error(_("Message delivery error: Unable to store data in database.\n"));
                   }
                   free_pool_memory(cmd);
                   free_pool_memory(esc_msg);
                }
                break;
             case MD_CONSOLE:
                Dmsg1(850, "CONSOLE for following msg: %s", msg);
                if (!con_fd) {
                   con_fd = bfopen(con_fname, "a+b");
                   Dmsg0(850, "Console file not open.\n");
                }
                if (con_fd) {
                   Pw(con_lock);      /* get write lock on console message file */
                   errno = 0;
                   if (dtlen) {
                      (void)fwrite(dt, dtlen, 1, con_fd);
                   }
                   len = strlen(msg);
                   if (len > 0) {
                      (void)fwrite(msg, len, 1, con_fd);
                      if (msg[len-1] != '\n') {
                         (void)fwrite("\n", 2, 1, con_fd);
                      }
                   } else {
                      (void)fwrite("\n", 2, 1, con_fd);
                   }
                   fflush(con_fd);
                   console_msg_pending = true;
                   Vw(con_lock);
                }
                break;
             case MD_SYSLOG:
                Dmsg1(850, "SYSLOG for following msg: %s\n", msg);
                /*
                 * We really should do an openlog() here.
                 */
                send_to_syslog(LOG_DAEMON|LOG_ERR, msg);
                break;
             case MD_OPERATOR:
                Dmsg1(850, "OPERATOR for following msg: %s\n", msg);
                mcmd = get_pool_memory(PM_MESSAGE);
                if ((bpipe=open_mail_pipe(jcr, mcmd, d))) {
                   int stat;
                   fputs(dt, bpipe->wfd);
                   fputs(msg, bpipe->wfd);
                   /* Messages to the operator go one at a time */
                   stat = close_bpipe(bpipe);
                   if (stat != 0) {
                      berrno be;
                      be.set_errno(stat);
                      delivery_error(_("Msg delivery error: Operator mail program terminated in error.\n"
                            "CMD=%s\n"
                            "ERR=%s\n"), mcmd, be.bstrerror());
                   }
                }
                free_pool_memory(mcmd);
                break;
             case MD_MAIL:
             case MD_MAIL_ON_ERROR:
             case MD_MAIL_ON_SUCCESS:
                Dmsg1(850, "MAIL for following msg: %s", msg);
                if (msgs->is_closing()) {
                   break;
                }
                msgs->set_in_use();
                if (!d->fd) {
                   POOLMEM *name = get_pool_memory(PM_MESSAGE);
                   make_unique_mail_filename(jcr, name, d);
                   d->fd = bfopen(name, "w+b");
                   if (!d->fd) {
                      berrno be;
                      delivery_error(_("Msg delivery error: fopen %s failed: ERR=%s\n"), name,
                            be.bstrerror());
                      free_pool_memory(name);
                      msgs->clear_in_use();
                      break;
                   }
                   d->mail_filename = name;
                }
                fputs(dt, d->fd);
                len = strlen(msg) + dtlen;;
                if (len > d->max_len) {
                   d->max_len = len;      /* keep max line length */
                }
                fputs(msg, d->fd);
                msgs->clear_in_use();
                break;
             case MD_APPEND:
                Dmsg1(850, "APPEND for following msg: %s", msg);
                mode = "ab";
                goto send_to_file;
             case MD_FILE:
                Dmsg1(850, "FILE for following msg: %s", msg);
                mode = "w+b";
send_to_file:
                if (msgs->is_closing()) {
                   break;
                }
                msgs->set_in_use();
                if (!d->fd && !open_dest_file(jcr, d, mode)) {
                   msgs->clear_in_use();
                   break;
                }
                fputs(dt, d->fd);
                fputs(msg, d->fd);
                /* On error, we close and reopen to handle log rotation */
                if (ferror(d->fd)) {
                   fclose(d->fd);
                   d->fd = NULL;
                   if (open_dest_file(jcr, d, mode)) {
                      fputs(dt, d->fd);
                      fputs(msg, d->fd);
                   }
                }
                msgs->clear_in_use();
                break;
             case MD_DIRECTOR:
                Dmsg1(850, "DIRECTOR for following msg: %s", msg);
                if (jcr && jcr->dir_bsock && !jcr->dir_bsock->errors) {
                   jcr->dir_bsock->fsend("Jmsg JobId=%ld type=%d level=%lld %s",
                      jcr->JobId, type, mtime, msg);
                } else {
                   Dmsg1(800, "no jcr for following msg: %s", msg);
                }
                break;
             case MD_STDOUT:
                Dmsg1(850, "STDOUT for following msg: %s", msg);
                if (type != M_ABORT && type != M_ERROR_TERM) { /* already printed */
                   fputs(dt, stdout);
                   fputs(msg, stdout);
                   fflush(stdout);
                }
                break;
             case MD_STDERR:
                Dmsg1(850, "STDERR for following msg: %s", msg);
                fputs(dt, stderr);
                fputs(msg, stderr);
                fflush(stdout);
                break;
             default:
                break;
          }
       }
    }
    if (created_jcr) {
       free_jcr(jcr);
    }
}

/*********************************************************************
 *
 *  This subroutine returns the filename portion of a path.
 *  It is used because some compilers set __FILE__
 *  to the full path.  Try to return base + next higher path.
 */

const char *get_basename(const char *pathname)
{
   const char *basename;

   if ((basename = bstrrpath(pathname, pathname+strlen(pathname))) == pathname) {
      /* empty */
   } else if ((basename = bstrrpath(pathname, basename-1)) == pathname) {
      /* empty */
   } else {
      basename++;
   }
   return basename;
}

/*
 * print or write output to trace file
 */
static void pt_out(char *buf)
{
    /*
     * Used the "trace on" command in the console to turn on
     *  output to the trace file.  "trace off" will close the file.
     */
    if (trace) {
       if (!trace_fd) {
          char fn[200];
          bsnprintf(fn, sizeof(fn), "%s/%s.trace", working_directory ? working_directory : "./", my_name);
          trace_fd = bfopen(fn, "a+b");
       }
       if (trace_fd) {
          fputs(buf, trace_fd);
          fflush(trace_fd);
          return;
       } else {
          /* Some problem, turn off tracing */
          trace = false;
       }
    }
    /* not tracing */
    fputs(buf, stdout);
    fflush(stdout);
}

/*********************************************************************
 *
 *  This subroutine prints a debug message if the level number
 *  is less than or equal the debug_level. File and line numbers
 *  are included for more detail if desired, but not currently
 *  printed.
 *
 *  If the level is negative, the details of file and line number
 *  are not printed.
 *
 */
void
vd_msg(const char *file, int line, int64_t level, const char *fmt, va_list arg_ptr)
{
    char      buf[5000];
    int       len = 0; /* space used in buf */
    bool      details = true;
    utime_t   mtime;

    if (level < 0) {
       details = false;
       level = -level;
    }

    if (chk_dbglvl(level)) {
       if (dbg_timestamp) {
          mtime = time(NULL);
          bstrftimes(buf+len, sizeof(buf)-len, mtime);
          len = strlen(buf);
          buf[len++] = ' ';
       }

#ifdef FULL_LOCATION
       if (details) {
          if (dbg_thread) {
             len += bsnprintf(buf+len, sizeof(buf)-len, "%s[%lld]: %s:%d-%u ",
                             my_name, bthread_get_thread_id(),
                             get_basename(file), line, get_jobid_from_tsd());
          } else {
             len += bsnprintf(buf+len, sizeof(buf)-len, "%s: %s:%d-%u ",
                   my_name, get_basename(file), line, get_jobid_from_tsd());
          }
       }
#endif
       bvsnprintf(buf+len, sizeof(buf)-len, (char *)fmt, arg_ptr);

       pt_out(buf);
    }
}

void
d_msg(const char *file, int line, int64_t level, const char *fmt,...)
{
   va_list arg_ptr;
   va_start(arg_ptr, fmt);
   vd_msg(file, line, level, fmt, arg_ptr); /* without tags */
   va_end(arg_ptr);
}


/*
 * Set trace flag on/off. If argument is negative, there is no change
 */
void set_trace(int trace_flag)
{
   if (trace_flag < 0) {
      return;
   } else if (trace_flag > 0) {
      trace = true;
   } else {
      trace = false;
   }
   if (!trace && trace_fd) {
      FILE *ltrace_fd = trace_fd;
      trace_fd = NULL;
      bmicrosleep(0, 100000);         /* yield to prevent seg faults */
      fclose(ltrace_fd);
   }
}

/*
 * Can be called by Bacula's tools that use Bacula's libraries, to control where
 * to redirect Dmsg() emitted by the code inside the Bacula's library.
 * This should not be called by the main daemon, this is a Hack !
 * See in bsnapshot.c how it is used
 * In your tools be careful to no call any function that here in messages.c
 * that modify "trace" or close() or re-open() trace_fd
 */
void set_trace_for_tools(FILE *new_trace_fd)
{
   // don't call fclose(trace_fd) here
   trace = true;
   trace_fd = new_trace_fd;
}

void set_hangup(int hangup_value)
{
   if (hangup_value != -1) {
      hangup = hangup_value;
   }
}

int get_hangup(void)
{
   return hangup;
}

void set_blowup(int blowup_value)
{
   if (blowup_value != -1) {
      blowup = blowup_value;
   }
}

int get_blowup(void)
{
   return blowup;
}

bool handle_hangup_blowup(JCR *jcr, uint32_t file_count, uint64_t byte_count)
{
   if (hangup == 0 && blowup == 0) {
      /* quick check */
      return false;
   }
   /* Debug code: check if we must hangup  or blowup */
   if ((hangup > 0 && (file_count > (uint32_t)hangup)) ||
       (hangup < 0 && (byte_count/1024 > (uint32_t)-hangup)))  {
      jcr->setJobStatus(JS_Incomplete);
      if (hangup > 0) {
         Jmsg1(jcr, M_FATAL, 0, "Debug hangup requested after %d files.\n", hangup);
      } else {
         Jmsg1(jcr, M_FATAL, 0, "Debug hangup requested after %d Kbytes.\n", -hangup);
      }
      set_hangup(0);
      return true;
   }
   if ((blowup > 0 && (file_count > (uint32_t)blowup)) ||
       (blowup < 0 && (byte_count/1024 > (uint32_t)-blowup)))  {
      if (blowup > 0) {
         Jmsg1(jcr, M_ABORT, 0, "Debug blowup requested after %d files.\n", blowup);
      } else {
         Jmsg1(jcr, M_ABORT, 0, "Debug blowup requested after %d Kbytes.\n", -blowup);
      }
      /* will never reach this line */
      return true;
   }
   return false;
}

bool get_trace(void)
{
   return trace;
}

/*********************************************************************
 *
 *  This subroutine prints a message regardless of the debug level
 *
 *  If the level is negative, the details of file and line number
 *  are not printed.
 */
void
p_msg(const char *file, int line, int level, const char *fmt,...)
{
    char      buf[5000];
    int       len = 0; /* space used in buf */
    va_list   arg_ptr;

    if (dbg_timestamp) {
       utime_t mtime = time(NULL);
       bstrftimes(buf+len, sizeof(buf)-len, mtime);
       len = strlen(buf);
       buf[len++] = ' ';
    }

#ifdef FULL_LOCATION
    if (level >= 0) {
       len += bsnprintf(buf+len, sizeof(buf)-len, "%s: %s:%d-%u ",
             my_name, get_basename(file), line, get_jobid_from_tsd());
    }
#endif

    va_start(arg_ptr, fmt);
    bvsnprintf(buf+len, sizeof(buf)-len, (char *)fmt, arg_ptr);
    va_end(arg_ptr);

    pt_out(buf);
}


/*********************************************************************
 *
 *  subroutine writes a debug message to the trace file if the level number
 *  is less than or equal the debug_level. File and line numbers
 *  are included for more detail if desired, but not currently
 *  printed.
 *
 *  If the level is negative, the details of file and line number
 *  are not printed.
 */
void
t_msg(const char *file, int line, int64_t level, const char *fmt,...)
{
    char      buf[5000];
    int       len;
    va_list   arg_ptr;
    int       details = TRUE;

    level = level & ~DT_ALL;    /* level should be tag free */

    if (level < 0) {
       details = FALSE;
       level = -level;
    }

    if (level <= debug_level) {
       if (!trace_fd) {
          bsnprintf(buf, sizeof(buf), "%s/%s.trace", working_directory ? working_directory : ".", my_name);
          trace_fd = bfopen(buf, "a+b");
       }

#ifdef FULL_LOCATION
       if (details) {
          len = bsnprintf(buf, sizeof(buf), "%s: %s:%d ", my_name, get_basename(file), line);
       } else {
          len = 0;
       }
#else
       len = 0;
#endif
       va_start(arg_ptr, fmt);
       bvsnprintf(buf+len, sizeof(buf)-len, (char *)fmt, arg_ptr);
       va_end(arg_ptr);
       if (trace_fd != NULL) {
           fputs(buf, trace_fd);
           fflush(trace_fd);
       }
   }
}

/* *********************************************************
 *
 * print an error message
 *
 */
void
e_msg(const char *file, int line, int type, int level, const char *fmt,...)
{
    char     buf[5000];
    va_list   arg_ptr;
    int len;

    /*
     * Check if we have a message destination defined.
     * We always report M_ABORT and M_ERROR_TERM
     */
    if (!daemon_msgs || ((type != M_ABORT && type != M_ERROR_TERM) &&
                         !bit_is_set(type, daemon_msgs->send_msg))) {
       return;                        /* no destination */
    }
    switch (type) {
    case M_ABORT:
       len = bsnprintf(buf, sizeof(buf), _("%s: ABORTING due to ERROR in %s:%d\n"),
               my_name, get_basename(file), line);
       break;
    case M_ERROR_TERM:
       len = bsnprintf(buf, sizeof(buf), _("%s: ERROR TERMINATION at %s:%d\n"),
               my_name, get_basename(file), line);
       break;
    case M_FATAL:
       if (level == -1)            /* skip details */
          len = bsnprintf(buf, sizeof(buf), _("%s: Fatal Error because: "), my_name);
       else
          len = bsnprintf(buf, sizeof(buf), _("%s: Fatal Error at %s:%d because:\n"), my_name, get_basename(file), line);
       break;
    case M_ERROR:
       if (level == -1)            /* skip details */
          len = bsnprintf(buf, sizeof(buf), _("%s: ERROR: "), my_name);
       else
          len = bsnprintf(buf, sizeof(buf), _("%s: ERROR in %s:%d "), my_name, get_basename(file), line);
       break;
    case M_WARNING:
       len = bsnprintf(buf, sizeof(buf), _("%s: Warning: "), my_name);
       break;
    case M_SECURITY:
       len = bsnprintf(buf, sizeof(buf), _("%s: Security Alert: "), my_name);
       break;
    default:
       len = bsnprintf(buf, sizeof(buf), "%s: ", my_name);
       break;
    }

    va_start(arg_ptr, fmt);
    bvsnprintf(buf+len, sizeof(buf)-len, (char *)fmt, arg_ptr);
    va_end(arg_ptr);

    pt_out(buf);
    dispatch_message(NULL, type, 0, buf);

    if (type == M_ABORT) {
       char *p = 0;
       p[0] = 0;                      /* generate segmentation violation */
    }
    if (type == M_ERROR_TERM) {
       exit(1);
    }
}

/* Check in the msgs resource if a given type is defined */
bool is_message_type_set(JCR *jcr, int type)
{
   MSGS *msgs = NULL;
   if (jcr) {
       msgs = jcr->jcr_msgs;
   }
   if (!msgs) {
      msgs = daemon_msgs;            /* if no jcr, we use daemon handler */
   }
   if (msgs && (type != M_ABORT && type != M_ERROR_TERM) &&
       !bit_is_set(type, msgs->send_msg)) {
      return false;                 /* no destination */
   }
   return true;
}

/* *********************************************************
 *
 * Generate a Job message
 *
 */
void
Jmsg(JCR *jcr, int type, utime_t mtime, const char *fmt,...)
{
    char     rbuf[5000];
    va_list   arg_ptr;
    int len;
    MSGS *msgs;
    uint32_t JobId = 0;


    Dmsg1(850, "Enter Jmsg type=%d\n", type);

    /*
     * Special case for the console, which has a dir_bsock and JobId==0,
     *  in that case, we send the message directly back to the
     *  dir_bsock.
     *  This allow commands such as "estimate" to work.
     *  It probably should be restricted to work only in the FD.
     */
    if (jcr && jcr->JobId == 0 && jcr->dir_bsock && type != M_SECURITY) {
       BSOCK *dir = jcr->dir_bsock;
       va_start(arg_ptr, fmt);
       dir->msglen = bvsnprintf(dir->msg, sizeof_pool_memory(dir->msg),
                                fmt, arg_ptr);
       va_end(arg_ptr);
       jcr->dir_bsock->send();
       return;
    }

    /* The watchdog thread can't use Jmsg directly, we always queued it */
    if (is_watchdog()) {
       va_start(arg_ptr, fmt);
       bvsnprintf(rbuf,  sizeof(rbuf), fmt, arg_ptr);
       va_end(arg_ptr);
       Qmsg(jcr, type, mtime, "%s", rbuf);
       return;
    }

    msgs = NULL;
    if (!jcr) {
       jcr = get_jcr_from_tsd();
    }
    if (jcr) {
       if (!jcr->dequeuing_msgs) { /* Avoid recursion */
          /* Dequeue messages to keep the original order  */
          dequeue_messages(jcr);
       }
       msgs = jcr->jcr_msgs;
       JobId = jcr->JobId;
    }
    if (!msgs) {
       msgs = daemon_msgs;            /* if no jcr, we use daemon handler */
    }

    /*
     * Check if we have a message destination defined.
     * We always report M_ABORT and M_ERROR_TERM
     */
    if (msgs && (type != M_ABORT && type != M_ERROR_TERM) &&
         !bit_is_set(type, msgs->send_msg)) {
       return;                        /* no destination */
    }
    switch (type) {
    case M_ABORT:
       len = bsnprintf(rbuf, sizeof(rbuf), _("%s ABORTING due to ERROR\n"), my_name);
       break;
    case M_ERROR_TERM:
       len = bsnprintf(rbuf, sizeof(rbuf), _("%s ERROR TERMINATION\n"), my_name);
       break;
    case M_FATAL:
       len = bsnprintf(rbuf, sizeof(rbuf), _("%s JobId %u: Fatal error: "), my_name, JobId);
       if (jcr) {
          jcr->setJobStatus(JS_FatalError);
       }
       if (jcr && jcr->JobErrors == 0) {
          jcr->JobErrors = 1;
       }
       break;
    case M_ERROR:
       len = bsnprintf(rbuf, sizeof(rbuf), _("%s JobId %u: Error: "), my_name, JobId);
       if (jcr) {
          jcr->JobErrors++;
       }
       break;
    case M_WARNING:
       len = bsnprintf(rbuf, sizeof(rbuf), _("%s JobId %u: Warning: "), my_name, JobId);
       if (jcr) {
          jcr->JobWarnings++;
       }
       break;
    case M_SECURITY:
       len = bsnprintf(rbuf, sizeof(rbuf), _("%s JobId %u: Security Alert: "),
               my_name, JobId);
       break;
    default:
       len = bsnprintf(rbuf, sizeof(rbuf), "%s JobId %u: ", my_name, JobId);
       break;
    }

    va_start(arg_ptr, fmt);
    bvsnprintf(rbuf+len,  sizeof(rbuf)-len, fmt, arg_ptr);
    va_end(arg_ptr);

    dispatch_message(jcr, type, mtime, rbuf);

    if (type == M_ABORT){
       char *p = 0;
       printf("Bacula forced SEG FAULT to obtain traceback.\n");
       syslog(LOG_DAEMON|LOG_ERR, "Bacula forced SEG FAULT to obtain traceback.\n");
       p[0] = 0;                      /* generate segmentation violation */
    }
    if (type == M_ERROR_TERM) {
       exit(1);
    }
}

/*
 * If we come here, prefix the message with the file:line-number,
 *  then pass it on to the normal Jmsg routine.
 */
void j_msg(const char *file, int line, JCR *jcr, int type, utime_t mtime, const char *fmt,...)
{
   va_list   arg_ptr;
   int i, len, maxlen;
   POOLMEM *pool_buf;

   va_start(arg_ptr, fmt);
   vd_msg(file, line, 0, fmt, arg_ptr);
   va_end(arg_ptr);

   pool_buf = get_pool_memory(PM_EMSG);
   i = Mmsg(pool_buf, "%s:%d ", get_basename(file), line);

   for (;;) {
      maxlen = sizeof_pool_memory(pool_buf) - i - 1;
      va_start(arg_ptr, fmt);
      len = bvsnprintf(pool_buf+i, maxlen, fmt, arg_ptr);
      va_end(arg_ptr);
      if (len < 0 || len >= (maxlen-5)) {
         pool_buf = realloc_pool_memory(pool_buf, maxlen + i + maxlen/2);
         continue;
      }
      break;
   }

   Jmsg(jcr, type, mtime, "%s", pool_buf);
   free_memory(pool_buf);
}


/*
 * Edit a message into a Pool memory buffer, with file:lineno
 */
int m_msg(const char *file, int line, POOLMEM **pool_buf, const char *fmt, ...)
{
   va_list   arg_ptr;
   int i, len, maxlen;

   i = sprintf(*pool_buf, "%s:%d ", get_basename(file), line);

   for (;;) {
      maxlen = sizeof_pool_memory(*pool_buf) - i - 1;
      va_start(arg_ptr, fmt);
      len = bvsnprintf(*pool_buf+i, maxlen, fmt, arg_ptr);
      va_end(arg_ptr);
      if (len < 0 || len >= (maxlen-5)) {
         *pool_buf = realloc_pool_memory(*pool_buf, maxlen + i + maxlen/2);
         continue;
      }
      break;
   }
   return len;
}

int m_msg(const char *file, int line, POOLMEM *&pool_buf, const char *fmt, ...)
{
   va_list   arg_ptr;
   int i, len, maxlen;

   i = sprintf(pool_buf, "%s:%d ", get_basename(file), line);

   for (;;) {
      maxlen = sizeof_pool_memory(pool_buf) - i - 1;
      va_start(arg_ptr, fmt);
      len = bvsnprintf(pool_buf+i, maxlen, fmt, arg_ptr);
      va_end(arg_ptr);
      if (len < 0 || len >= (maxlen-5)) {
         pool_buf = realloc_pool_memory(pool_buf, maxlen + i + maxlen/2);
         continue;
      }
      break;
   }
   return len;
}


/*
 * Edit a message into a Pool Memory buffer NO file:lineno
 *  Returns: string length of what was edited.
 */
int Mmsg(POOLMEM **pool_buf, const char *fmt, ...)
{
   va_list   arg_ptr;
   int len, maxlen;

   for (;;) {
      maxlen = sizeof_pool_memory(*pool_buf) - 1;
      va_start(arg_ptr, fmt);
      len = bvsnprintf(*pool_buf, maxlen, fmt, arg_ptr);
      va_end(arg_ptr);
      if (len < 0 || len >= (maxlen-5)) {
         *pool_buf = realloc_pool_memory(*pool_buf, maxlen + maxlen/2);
         continue;
      }
      break;
   }
   return len;
}

int Mmsg(POOLMEM *&pool_buf, const char *fmt, ...)
{
   va_list   arg_ptr;
   int len, maxlen;

   for (;;) {
      maxlen = sizeof_pool_memory(pool_buf) - 1;
      va_start(arg_ptr, fmt);
      len = bvsnprintf(pool_buf, maxlen, fmt, arg_ptr);
      va_end(arg_ptr);
      if (len < 0 || len >= (maxlen-5)) {
         pool_buf = realloc_pool_memory(pool_buf, maxlen + maxlen/2);
         continue;
      }
      break;
   }
   return len;
}

int Mmsg(POOL_MEM &pool_buf, const char *fmt, ...)
{
   va_list   arg_ptr;
   int len, maxlen;

   for (;;) {
      maxlen = pool_buf.max_size() - 1;
      va_start(arg_ptr, fmt);
      len = bvsnprintf(pool_buf.c_str(), maxlen, fmt, arg_ptr);
      va_end(arg_ptr);
      if (len < 0 || len >= (maxlen-5)) {
         pool_buf.realloc_pm(maxlen + maxlen/2);
         continue;
      }
      break;
   }
   return len;
}


/*
 * We queue messages rather than print them directly. This
 *  is generally used in low level routines (msg handler, bnet)
 *  to prevent recursion (i.e. if you are in the middle of
 *  sending a message, it is a bit messy to recursively call
 *  yourself when the bnet packet is not reentrant).
 */
void Qmsg(JCR *jcr, int type, utime_t mtime, const char *fmt,...)
{
   va_list   arg_ptr;
   int len, maxlen;
   POOLMEM *pool_buf;
   MQUEUE_ITEM *item, *last_item;

   pool_buf = get_pool_memory(PM_EMSG);

   for (;;) {
      maxlen = sizeof_pool_memory(pool_buf) - 1;
      va_start(arg_ptr, fmt);
      len = bvsnprintf(pool_buf, maxlen, fmt, arg_ptr);
      va_end(arg_ptr);
      if (len < 0 || len >= (maxlen-5)) {
         pool_buf = realloc_pool_memory(pool_buf, maxlen + maxlen/2);
         continue;
      }
      break;
   }
   item = (MQUEUE_ITEM *)malloc(sizeof(MQUEUE_ITEM) + strlen(pool_buf) + 1);
   item->type = type;
   item->repeat = 0;
   item->mtime = time(NULL);
   strcpy(item->msg, pool_buf);
   if (!jcr) {
      jcr = get_jcr_from_tsd();
   }

   if (jcr && type==M_FATAL) {
      jcr->setJobStatus(JS_FatalError);
    }

   /* If no jcr or no queue or dequeuing send to syslog */
   if (!jcr || !jcr->msg_queue || jcr->dequeuing_msgs) {
      syslog(LOG_DAEMON|LOG_ERR, "%s", item->msg);
      P(daemon_msg_queue_mutex);
      if (daemon_msg_queue) {
         if (item->type == M_SECURITY) {  /* can be repeated */
            /* Keep repeat count of identical messages */
            last_item = (MQUEUE_ITEM *)daemon_msg_queue->last();
            if (last_item) {
               if (strcmp(last_item->msg, item->msg) == 0) {
                  last_item->repeat++;
                  free(item);
                  item = NULL;
               }
            }
         }
         if (item) {
            daemon_msg_queue->append(item);
         }
      }
      V(daemon_msg_queue_mutex);
   } else {
      /* Queue message for later sending */
      P(jcr->msg_queue_mutex);
      jcr->msg_queue->append(item);
      V(jcr->msg_queue_mutex);
   }
   free_memory(pool_buf);
}

/*
 * Dequeue daemon messages
 */
void dequeue_daemon_messages(JCR *jcr)
{
   MQUEUE_ITEM *item;
   JobId_t JobId;

   /* Dequeue daemon messages */
   if (daemon_msg_queue && !dequeuing_daemon_msgs) {
      P(daemon_msg_queue_mutex);
      dequeuing_daemon_msgs = true;
      jcr->dequeuing_msgs = true;
      JobId = jcr->JobId;
      jcr->JobId = 0;       /* set daemon JobId == 0 */
      if (jcr->dir_bsock) jcr->dir_bsock->suppress_error_messages(true);
      foreach_dlist(item, daemon_msg_queue) {
         if (item->type == M_FATAL || item->type == M_ERROR) {
            item->type = M_SECURITY;
         }
         if (item->repeat == 0) {
            /* repeat = 0 => seen 1 time */
            Jmsg(jcr, item->type, item->mtime, "%s", item->msg);
         } else {
            /* repeat = 1, seen 2 times */
            Jmsg(jcr, item->type, item->mtime, "Message repeated %d times: %s",
                 item->repeat+1, item->msg);
         }
      }
      if (jcr->dir_bsock) jcr->dir_bsock->suppress_error_messages(false);
      /* Remove messages just sent */
      daemon_msg_queue->destroy();
      jcr->JobId = JobId;   /* restore JobId */
      jcr->dequeuing_msgs = false;
      dequeuing_daemon_msgs = false;
      V(daemon_msg_queue_mutex);
   }
}

/*
 * Dequeue messages
 */
void dequeue_messages(JCR *jcr)
{
   MQUEUE_ITEM *item;

   /* Avoid bad calls and recursion */
   if (!jcr || jcr->dequeuing_msgs) {
      return;
   }


   /* Dequeue Job specific messages */
   if (!jcr->msg_queue || jcr->dequeuing_msgs) {
      return;
   }
   P(jcr->msg_queue_mutex);
   jcr->dequeuing_msgs = true;
   if (jcr->dir_bsock) jcr->dir_bsock->suppress_error_messages(true);
   foreach_dlist(item, jcr->msg_queue) {
      Jmsg(jcr, item->type, item->mtime, "%s", item->msg);
   }
   if (jcr->dir_bsock) jcr->dir_bsock->suppress_error_messages(false);
   /* Remove messages just sent */
   jcr->msg_queue->destroy();
   jcr->dequeuing_msgs = false;
   V(jcr->msg_queue_mutex);
}


/*
 * If we come here, prefix the message with the file:line-number,
 *  then pass it on to the normal Qmsg routine.
 */
void q_msg(const char *file, int line, JCR *jcr, int type, utime_t mtime, const char *fmt,...)
{
   va_list   arg_ptr;
   int i, len, maxlen;
   POOLMEM *pool_buf;

   pool_buf = get_pool_memory(PM_EMSG);
   i = Mmsg(pool_buf, "%s:%d ", get_basename(file), line);

   for (;;) {
      maxlen = sizeof_pool_memory(pool_buf) - i - 1;
      va_start(arg_ptr, fmt);
      len = bvsnprintf(pool_buf+i, maxlen, fmt, arg_ptr);
      va_end(arg_ptr);
      if (len < 0 || len >= (maxlen-5)) {
         pool_buf = realloc_pool_memory(pool_buf, maxlen + i + maxlen/2);
         continue;
      }
      break;
   }

   Qmsg(jcr, type, mtime, "%s", pool_buf);
   free_memory(pool_buf);
}


/* not all in alphabetical order.  New commands are added after existing commands with similar letters
   to prevent breakage of existing user scripts.  */
struct debugtags {
   const char *tag;             /* command */
   int64_t     bit;             /* bit to set */
   const char *help;            /* main purpose */
};

/* setdebug tag=all,-plugin */
static struct debugtags debug_tags[] = {
 { NT_("lock"),        DT_LOCK,     _("Debug lock information")},
 { NT_("network"),     DT_NETWORK,  _("Debug network information")},
 { NT_("plugin"),      DT_PLUGIN,   _("Debug plugin information")},
 { NT_("volume"),      DT_VOLUME,   _("Debug volume information")},
 { NT_("sql"),         DT_SQL,      _("Debug SQL queries")},
 { NT_("bvfs"),        DT_BVFS,     _("Debug BVFS queries")},
 { NT_("memory"),      DT_MEMORY,   _("Debug memory allocation")},
 { NT_("scheduler"),   DT_SCHEDULER,_("Debug scheduler information")},
 { NT_("protocol"),    DT_PROTOCOL, _("Debug protocol information")},
 { NT_("snapshot"),    DT_SNAPSHOT, _("Debug snapshots")},
 { NT_("record"),      DT_RECORD,   _("Debug records")},
 { NT_("asx"),         DT_ASX,      _("ASX personal's debugging")},
 { NT_("all"),         DT_ALL,      _("Debug all information")},
 { NULL,               0,   NULL}
};

#define MAX_TAG (sizeof(debug_tags) / sizeof(struct debugtags))

const char *debug_get_tag(uint32_t pos, const char **desc)
{
   if (pos < MAX_TAG) {
      if (desc) {
         *desc = debug_tags[pos].help;
      }
      return debug_tags[pos].tag;
   }
   return NULL;
}

/* Allow +-, */
bool debug_find_tag(const char *tagname, bool add, int64_t *current_level)
{
   Dmsg3(010, "add=%d tag=%s level=%lld\n", add, tagname, *current_level);
   if (!*tagname) {
      /* Nothing in the buffer */
      return true;
   }
   for (int i=0; debug_tags[i].tag ; i++) {
      if (strcasecmp(debug_tags[i].tag, tagname) == 0) {
         if (add) {
            *current_level |= debug_tags[i].bit;
         } else {
            *current_level &= ~(debug_tags[i].bit);
         }
         return true;
      }
   }
   return false;
}

bool debug_parse_tags(const char *options, int64_t *current_level)
{
   bool operation;              /* + => true, - false */
   char *p, *t, tag[256];
   int max = sizeof(tag) - 1;
   bool ret=true;
   int64_t level= *current_level;

   t = tag;
   *tag = 0;
   operation = true;            /* add by default */

   if (!options) {
      Dmsg0(100, "No options for tags\n");
      return false;
   }

   for (p = (char *)options; *p ; p++) {
      if (*p == ',' || *p == '+' || *p == '-' || *p == '!') {
         /* finish tag keyword */
         *t = 0;
         /* handle tag */
         ret &= debug_find_tag(tag, operation, &level);

         if (*p == ',') {
            /* reset tag */
            t = tag;
            *tag = 0;
            operation = true;

         } else {
            /* reset tag */
            t = tag;
            *tag = 0;
            operation = (*p == '+');
         }

      } else if (isalpha(*p) && (t - tag) < max) {
         *t++ = *p;

      } else {                  /* not isalpha or too long */
         Dmsg1(010, "invalid %c\n", *p);
         return false;
      }
   }

   /* At the end, finish the string and look it */
   *t = 0;
   if (t > tag) {               /* something found */
      /* handle tag */
      ret &= debug_find_tag(tag, operation, &level);
   }

   *current_level = level;
   return ret;
}

int generate_daemon_event(JCR *jcr, const char *event) { return 0; }

void setup_daemon_message_queue()
{
   static MQUEUE_ITEM *item = NULL;
   daemon_msg_queue = New(dlist(item, &item->link));
}

void free_daemon_message_queue()
{
   P(daemon_msg_queue_mutex);
   daemon_msg_queue->destroy();
   free(daemon_msg_queue);
   V(daemon_msg_queue_mutex);
}
