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
 *   Bacula Director daemon -- this is the main program
 *
 *     Kern Sibbald, March MM
 */

#include "bacula.h"
#include "dird.h"
#ifndef HAVE_REGEX_H
#include "lib/bregex.h"
#else
#include <regex.h>
#endif
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
int breaddir(DIR *dirp, POOLMEM *&d_name);

/* Forward referenced subroutines */
void terminate_dird(int sig);
static bool check_resources();
static void cleanup_old_files();
static void resize_reload(int nb);

/* Exported subroutines */
extern "C" void reload_config(int sig);
extern void invalidate_schedules();
extern bool parse_dir_config(CONFIG *config, const char *configfile, int exit_code);

/* Imported subroutines */
JCR *wait_for_next_job(char *runjob);
void term_scheduler();
void start_UA_server(dlist *addrs);
void stop_UA_server();
void init_job_server(int max_workers);
void term_job_server();
void store_jobtype(LEX *lc, RES_ITEM *item, int index, int pass);
void store_level(LEX *lc, RES_ITEM *item, int index, int pass);
void store_replace(LEX *lc, RES_ITEM *item, int index, int pass);
void store_migtype(LEX *lc, RES_ITEM *item, int index, int pass);
void init_device_resources();


static char *runjob = NULL;
static bool foreground = false;
static bool make_pid_file = true;     /* create pid file */
static void init_reload(void);
static CONFIG *config;
static bool test_config = false;

/* Globals Exported */
DIRRES *director;                     /* Director resource */
int FDConnectTimeout;
int SDConnectTimeout;
char *configfile = NULL;
void *start_heap;
utime_t last_reload_time = 0;


/* Globals Imported */
extern dlist client_globals;
extern dlist store_globals;
extern dlist job_globals;
extern dlist sched_globals;
extern RES_ITEM job_items[];
#if defined(_MSC_VER)
extern "C" { // work around visual compiler mangling variables
   extern URES res_all;
}
#else
extern URES res_all;
#endif

typedef enum {
   CHECK_CONNECTION,  /* Check catalog connection */
   UPDATE_CATALOG,    /* Ensure that catalog is ok with conf */
   UPDATE_AND_FIX     /* Ensure that catalog is ok, and fix old jobs */
} cat_op;
static bool check_catalog(cat_op mode);

#define CONFIG_FILE "bacula-dir.conf" /* default configuration file */

static bool dir_sql_query(JCR *jcr, const char *cmd) 
{ 
   if (jcr && jcr->db && jcr->db->is_connected()) {
      return db_sql_query(jcr->db, cmd, NULL, NULL);
   } 
   return false; 
} 

static bool dir_sql_escape(JCR *jcr, BDB *mdb, char *snew, char *sold, int len) 
{ 
   if (jcr && jcr->db && jcr->db->is_connected()) { 
      db_escape_string(jcr, mdb, snew, sold, len);
      return true;
   } 
   return false; 
} 

static void usage()
{
   fprintf(stderr, _(
      PROG_COPYRIGHT
      "\n%sVersion: %s (%s)\n\n"
      "Usage: bacula-dir [-f -s] [-c config_file] [-d debug_level] [config_file]\n"
      "     -c <file>        set configuration file to file\n"
      "     -d <nn>[,<tags>] set debug level to <nn>, debug tags to <tags>\n"
      "     -dt              print timestamp in debug output\n"
      "     -T               set trace on\n"
      "     -f               run in foreground (for debugging)\n"
      "     -g               groupid\n"
      "     -m               print kaboom output (for debugging)\n"
      "     -r <job>         run <job> now\n"
      "     -P               do not create pid file\n"
      "     -s               no signals\n"
      "     -t               test - read configuration and exit\n"
      "     -u               userid\n"
      "     -v               verbose user messages\n"
      "     -?               print this message.\n"
      "\n"), 2000, "", VERSION, BDATE);

   exit(1);
}

/*
 * !!! WARNING !!! Use this function only when bacula is stopped.
 * ie, after a fatal signal and before exiting the program
 * Print information about a JCR
 */
static void dir_debug_print(JCR *jcr, FILE *fp)
{
   fprintf(fp, "\twstore=%p rstore=%p wjcr=%p client=%p reschedule_count=%d SD_msg_chan_started=%d\n",
           jcr->wstore, jcr->rstore, jcr->wjcr, jcr->client, jcr->reschedule_count, (int)jcr->SD_msg_chan_started);
}

/*********************************************************************
 *
 *         Main Bacula Director Server program
 *
 */
#if defined(HAVE_WIN32)
/* For Win32 main() is in src/win32 code ... */
#define main BaculaMain
#endif

/* DELETE ME when bugs in MA1512, MA1632 MA1639 are fixed */
extern void (*MA1512_reload_job_end_cb)(JCR *,void *);
static void reload_job_end_cb(JCR *jcr, void *ctx);

int main (int argc, char *argv[])
{
   int ch;
   JCR *jcr;
   bool no_signals = false;
   char *uid = NULL;
   char *gid = NULL;

   /* DELETE ME when bugs in MA1512, MA1632 MA1639 are fixed */
   MA1512_reload_job_end_cb = reload_job_end_cb;

   start_heap = sbrk(0);
   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");

   init_stack_dump();
   my_name_is(argc, argv, "bacula-dir");
   init_msg(NULL, NULL);              /* initialize message handler */
   init_reload();
   daemon_start_time = time(NULL);
   setup_daemon_message_queue();
   console_command = run_console_command;

   while ((ch = getopt(argc, argv, "c:d:fg:mPr:stu:v?T")) != -1) {
      switch (ch) {
      case 'c':                    /* specify config file */
         if (configfile != NULL) {
            free(configfile);
         }
         configfile = bstrdup(optarg);
         break;

      case 'd':                    /* set debug level */
         if (*optarg == 't') {
            dbg_timestamp = true;
         } else {
            char *p;
            /* We probably find a tag list -d 10,sql,bvfs */
            if ((p = strchr(optarg, ',')) != NULL) {
               *p = 0;
            }
            debug_level = atoi(optarg);
            if (debug_level <= 0) {
               debug_level = 1;
            }
            if (p) {
               debug_parse_tags(p+1, &debug_level_tags);
            }
         }
         Dmsg1(10, "Debug level = %lld\n", debug_level);
         break;

      case 'T':
         set_trace(true);
         break;

      case 'f':                    /* run in foreground */
         foreground = true;
         break;

      case 'g':                    /* set group id */
         gid = optarg;
         break;

      case 'm':                    /* print kaboom output */
         prt_kaboom = true;
         break;

      case 'P':                    /* no pid file */
         make_pid_file = false;
         break;

      case 'r':                    /* run job */
         if (runjob != NULL) {
            free(runjob);
         }
         if (optarg) {
            runjob = bstrdup(optarg);
         }
         break;

      case 's':                    /* turn off signals */
         no_signals = true;
         break;

      case 't':                    /* test config */
         test_config = true;
         break;

      case 'u':                    /* set uid */
         uid = optarg;
         break;

      case 'v':                    /* verbose */
         verbose++;
         break;

      case '?':
      default:
         usage();

      }
   }
   argc -= optind;
   argv += optind;

   if (argc) {
      if (configfile != NULL) {
         free(configfile);
      }
      configfile = bstrdup(*argv);
      argc--;
      argv++;
   }
   if (argc) {
      usage();
   }

   if (!foreground && !test_config) {
      daemon_start();
      init_stack_dump();              /* grab new pid */
   }

   if (!no_signals) {
      init_signals(terminate_dird);
   }

   if (configfile == NULL) {
      configfile = bstrdup(CONFIG_FILE);
   }

   config = New(CONFIG());
   parse_dir_config(config, configfile, M_ERROR_TERM);

   if (init_crypto() != 0) {
      Jmsg((JCR *)NULL, M_ERROR_TERM, 0, _("Cryptography library initialization failed.\n"));
   }

   if (!check_resources()) {
      Jmsg((JCR *)NULL, M_ERROR_TERM, 0, _("Please correct configuration file: %s\n"), configfile);
   }

   /* The configuration is correct */
   director = (DIRRES *)GetNextRes(R_DIRECTOR, NULL);

   if (!test_config) {
      /* Create pid must come after we are a daemon -- so we have our final pid */
      if (make_pid_file) {
         create_pid_file(director->pid_directory, "bacula-dir",
                          get_first_port_host_order(director->DIRaddrs));
      }
      read_state_file(director->working_directory, "bacula-dir",
                      get_first_port_host_order(director->DIRaddrs));
   }

   set_jcr_in_tsd(INVALID_JCR);
   set_thread_concurrency(director->MaxConcurrentJobs * 2 +
                          4 /* UA */ + 5 /* sched+watchdog+jobsvr+misc */);
   lmgr_init_thread(); /* initialize the lockmanager stack */

   load_dir_plugins(director->plugin_directory);

   drop(uid, gid, false);                    /* reduce privileges if requested */

   /* If we are in testing mode, we don't try to fix the catalog */
   cat_op mode=(test_config)?CHECK_CONNECTION:UPDATE_AND_FIX;

   if (!check_catalog(mode)) {
      Jmsg((JCR *)NULL, M_ERROR_TERM, 0, _("Please correct configuration file: %s\n"), configfile);
   }

   if (test_config) {
      terminate_dird(0);
   }

   my_name_is(0, NULL, director->name());    /* set user defined name */

   cleanup_old_files();

   /* Plug database interface for library routines */
   p_sql_query = (sql_query_call)dir_sql_query;
   p_sql_escape = (sql_escape_call)dir_sql_escape;

   FDConnectTimeout = (int)director->FDConnectTimeout;
   SDConnectTimeout = (int)director->SDConnectTimeout;

   resize_reload(director->MaxReload);

#if !defined(HAVE_WIN32)
   signal(SIGHUP, reload_config);
#endif

   init_console_msg(working_directory);

   Dmsg0(200, "Start UA server\n");
   start_UA_server(director->DIRaddrs);

   start_watchdog();                  /* start network watchdog thread */

   init_jcr_subsystem();              /* start JCR watchdogs etc. */

   init_job_server(director->MaxConcurrentJobs);

   dbg_jcr_add_hook(dir_debug_print); /* used to director variables */
   dbg_jcr_add_hook(bdb_debug_print);     /* used to debug B_DB connexion after fatal signal */

//   init_device_resources();

   Dmsg0(200, "wait for next job\n");
   /* Main loop -- call scheduler to get next job to run */
   while ( (jcr = wait_for_next_job(runjob)) ) {
      run_job(jcr);                   /* run job */
      free_jcr(jcr);                  /* release jcr */
      set_jcr_in_tsd(INVALID_JCR);
      if (runjob) {                   /* command line, run a single job? */
         break;                       /* yes, terminate */
      }
   }

   terminate_dird(0);

   return 0;
}

struct RELOAD_TABLE {
   int job_count;
   RES_HEAD **res_head;
};

static int max_reloads = 32;
static RELOAD_TABLE *reload_table=NULL;

static void resize_reload(int nb)
{
   if (nb <= max_reloads) {
      return;
   }

   reload_table = (RELOAD_TABLE*)realloc(reload_table, nb * sizeof(RELOAD_TABLE));
   for (int i=max_reloads; i < nb ; i++) {
      reload_table[i].job_count = 0;
      reload_table[i].res_head = NULL;
   }
   max_reloads = nb;
}

static void init_reload(void)
{
   reload_table = (RELOAD_TABLE*)malloc(max_reloads * sizeof(RELOAD_TABLE));
   for (int i=0; i < max_reloads; i++) {
      reload_table[i].job_count = 0;
      reload_table[i].res_head = NULL;
   }
}

/*
 * This subroutine frees a saved resource table.
 *  It was saved when a new table was created with "reload"
 */
static void free_saved_resources(int table)
{
   RES *next, *res;
   int num = r_last - r_first + 1;
   RES_HEAD **res_tab = reload_table[table].res_head;

   if (res_tab == NULL) {
      Dmsg1(100, "res_tab for table %d already released.\n", table);
      return;
   }
   Dmsg1(100, "Freeing resources for table %d\n", table);
   for (int j=0; j<num; j++) {
      if (res_tab[j]) {
         next = res_tab[j]->first;
         for ( ; next; ) {
            res = next;
            next = res->res_next;
            free_resource(res, r_first + j);
         }
         free(res_tab[j]->res_list);
         free(res_tab[j]);
         res_tab[j] = NULL;
      }
   }
   free(res_tab);
   reload_table[table].job_count = 0;
   reload_table[table].res_head = NULL;
}

/*
 * Called here at the end of every job that was
 * hooked decrementing the active job_count. When
 * it goes to zero, no one is using the associated
 * resource table, so free it.
 */
static void reload_job_end_cb(JCR *jcr, void *ctx)
{
   int reload_id = (int)((intptr_t)ctx);
   Dmsg3(100, "reload job_end JobId=%d table=%d cnt=%d\n", jcr->JobId,
      reload_id, reload_table[reload_id].job_count);
   lock_jobs();
   LockRes();
   if (--reload_table[reload_id].job_count <= 0) {
      free_saved_resources(reload_id);
   }
   UnlockRes();
   unlock_jobs();
}

static int find_free_reload_table_entry()
{
   int table = -1;
   for (int i=0; i < max_reloads; i++) {
      if (reload_table[i].res_head == NULL) {
         table = i;
         break;
      }
   }
   return table;
}

static pthread_mutex_t reload_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * If we get here, we have received a SIGHUP, which means to
 *    reread our configuration file.
 *
 * The algorithm used is as follows: we count how many jobs are
 *   running and mark the running jobs to make a callback on
 *   exiting. The old config is saved with the reload table
 *   id in a reload table. The new config file is read. Now, as
 *   each job exits, it calls back to the reload_job_end_cb(), which
 *   decrements the count of open jobs for the given reload table.
 *   When the count goes to zero, we release those resources.
 *   This allows us to have pointers into the resource table (from
 *   jobs), and once they exit and all the pointers are released, we
 *   release the old table. Note, if no new jobs are running since the
 *   last reload, then the old resources will be immediately release.
 *   A console is considered a job because it may have pointers to
 *   resources, but a SYSTEM job is not since it *should* not have any
 *   permanent pointers to jobs.
 */
extern "C"
void reload_config(int sig)
{
   static bool already_here = false;
#if !defined(HAVE_WIN32)
   sigset_t set;
#endif
   JCR *jcr;
   int njobs = 0;                     /* number of running jobs */
   int table, rtable;
   bool ok=false;
   int tries=0;

   /* Wait to do the reload */
   do {
      P(reload_mutex);
      if (already_here) {
         V(reload_mutex);
         if (tries++ > 10) {
            Qmsg(NULL, M_INFO, 0, _("Already doing a reload request, "
                                    "request ignored.\n"));
            return;
         }
         Dmsg0(10, "Already doing a reload request, waiting a bit\n");
         bmicrosleep(1, 0);
      } else {
         already_here = true;
         V(reload_mutex);
         ok = true;
      }
   } while (!ok);

#if !defined(HAVE_WIN32)
   sigemptyset(&set);
   sigaddset(&set, SIGHUP);
   sigprocmask(SIG_BLOCK, &set, NULL);
#endif

   lock_jobs();
   LockRes();

   table = find_free_reload_table_entry();
   if (table < 0) {
      Qmsg(NULL, M_ERROR, 0, _("Too many (%d) open reload requests. "
                               "Request ignored.\n"), max_reloads);
      goto bail_out;
   }

   Dmsg1(100, "Reload_config njobs=%d\n", njobs);
   /* Save current res_head */
   reload_table[table].res_head = res_head;
   Dmsg1(100, "Saved old config in table %d\n", table);

   /* Create a new res_head and parse into it */
   ok = parse_dir_config(config, configfile, M_ERROR);

   Dmsg0(100, "Reloaded config file\n");
   if (!ok || !check_resources() || !check_catalog(UPDATE_CATALOG)) {
      /*
       * We got an error, save broken point, restore old one,
       *  then release everything from broken pointer.
       */
      rtable = find_free_reload_table_entry();    /* save new, bad table */
      if (rtable < 0) {
         Qmsg(NULL, M_ERROR, 0, _("Please correct configuration file: %s\n"), configfile);
         Qmsg(NULL, M_ERROR_TERM, 0, _("Out of reload table entries. Giving up.\n"));
      } else {
         Qmsg(NULL, M_ERROR, 0, _("Please correct configuration file: %s\n"), configfile);
         Qmsg(NULL, M_ERROR, 0, _("Resetting previous configuration.\n"));
      }
      /* Save broken res_head pointer */
      reload_table[rtable].res_head = res_head;

      /* Now restore old resource pointer */
      res_head = reload_table[table].res_head;
      table = rtable;           /* release new, bad, saved table below */
   } else {
      invalidate_schedules();

      /* We know that the configuration is correct and we will keep it,
       * so we can update the global pointer to the director resource.
       */
      director = (DIRRES *)GetNextRes(R_DIRECTOR, NULL);

      /*
       * Hook all active jobs so that they release this table
       */
      foreach_jcr(jcr) {
         if (jcr->getJobType() != JT_SYSTEM) {
            reload_table[table].job_count++;
            job_end_push(jcr, reload_job_end_cb, (void *)((long int)table));
            njobs++;
         }
      }
      endeach_jcr(jcr);
      /*
       * Now walk through globals tables and plug them into the
       * new resources.
       */
      CLIENT_GLOBALS *cg;
      foreach_dlist(cg, &client_globals) {
         CLIENT *client;
         client = GetClientResWithName(cg->name);
         if (!client) {
            Qmsg(NULL, M_INFO, 0, _("Client=%s not found. Assuming it was removed!!!\n"), cg->name);
         } else {
            client->globals = cg;      /* Set globals pointer */
         }
      }
      STORE_GLOBALS *sg;
      foreach_dlist(sg, &store_globals) {
         STORE *store;
         store = GetStoreResWithName(sg->name);
         if (!store) {
            Qmsg(NULL, M_INFO, 0, _("Storage=%s not found. Assuming it was removed!!!\n"), sg->name);
         } else {
            store->globals = sg;       /* set globals pointer */
            Dmsg2(200, "Reload found numConcurrent=%ld for Store %s\n",
               sg->NumConcurrentJobs, sg->name);
         }
      }
      JOB_GLOBALS *jg;
      foreach_dlist(jg, &job_globals) {
         JOB *job;
         job = GetJobResWithName(jg->name);
         if (!job) {
            Qmsg(NULL, M_INFO, 0, _("Job=%s not found. Assuming it was removed!!!\n"), jg->name);
         } else {
            job->globals = jg;         /* Set globals pointer */
         }
      }
      SCHED_GLOBALS *schg;
      foreach_dlist(schg, &sched_globals) {
         SCHED *sched;
         sched = GetSchedResWithName(schg->name);
         if (!sched) {
            Qmsg(NULL, M_INFO, 0, _("Schedule=%s not found. Assuming it was removed!!!\n"), schg->name);
         } else {
            sched->globals = schg;     /* Set globals pointer */
         }
      }
   }

   /* Reset other globals */
   set_working_directory(director->working_directory);
   FDConnectTimeout = director->FDConnectTimeout;
   SDConnectTimeout = director->SDConnectTimeout;
   Dmsg0(10, "Director's configuration file reread.\n");

   /* Now release saved resources, if no jobs using the resources */
   if (njobs == 0) {
      free_saved_resources(table);
   }

bail_out:
   UnlockRes();
   unlock_jobs();
#if !defined(HAVE_WIN32)
   sigprocmask(SIG_UNBLOCK, &set, NULL);
   signal(SIGHUP, reload_config);
#endif
   already_here = false;
}

/* Cleanup and then exit */
void terminate_dird(int sig)
{
   static bool already_here = false;

   if (already_here) {                /* avoid recursive temination problems */
      bmicrosleep(2, 0);              /* yield */
      exit(1);
   }
   already_here = true;
   debug_level = 0;                   /* turn off debug */
   stop_watchdog();
   generate_daemon_event(NULL, "Exit");
   unload_plugins();
   if (!test_config) {
      write_state_file(director->working_directory, "bacula-dir", get_first_port_host_order(director->DIRaddrs));
      if (make_pid_file) {
         delete_pid_file(director->pid_directory, "bacula-dir", get_first_port_host_order(director->DIRaddrs));
      }
   }
   term_scheduler();
   term_job_server();
   if (runjob) {
      free(runjob);
   }
   if (configfile != NULL) {
      free(configfile);
   }
   if (chk_dbglvl(5)) {
      print_memory_pool_stats();
   }
   if (config) {
      delete config;
      config = NULL;
   }
   stop_UA_server();
   term_msg();                        /* terminate message handler */
   cleanup_crypto();

   free_daemon_message_queue();

   if (reload_table) {
      free(reload_table);
   }
   free(res_head);
   res_head = NULL;
   /*
    * Now walk through resource globals tables and release them
    */
   CLIENT_GLOBALS *cg;
   foreach_dlist(cg, &client_globals) {
      free(cg->name);
      if (cg->SetIPaddress) {
         free(cg->SetIPaddress);
      }
   }
   client_globals.destroy();

   STORE_GLOBALS *sg;
   foreach_dlist(sg, &store_globals) {
      free(sg->name);
   }
   store_globals.destroy();

   JOB_GLOBALS *jg;
   foreach_dlist(jg, &job_globals) {
      free(jg->name);
   }
   job_globals.destroy();

   close_memory_pool();               /* release free memory in pool */
   lmgr_cleanup_main();
   sm_dump(false);
   exit(sig);
}

/*
 * Make a quick check to see that we have all the
 * resources needed.
 *
 *  **** FIXME **** this routine could be a lot more
 *   intelligent and comprehensive.
 */
static bool check_resources()
{
   bool OK = true;
   JOB *job;
   bool need_tls;
   DIRRES *newDirector;

   LockRes();

   job = (JOB *)GetNextRes(R_JOB, NULL);
   newDirector = (DIRRES *)GetNextRes(R_DIRECTOR, NULL);
   if (!newDirector) {
      Jmsg(NULL, M_FATAL, 0, _("No Director resource defined in %s\n"
"Without that I don't know who I am :-(\n"), configfile);
      OK = false;
   } else {
      set_working_directory(newDirector->working_directory);
      if (!newDirector->messages) { /* If message resource not specified */
         newDirector->messages = (MSGS *)GetNextRes(R_MSGS, NULL);
         if (!newDirector->messages) {
            Jmsg(NULL, M_FATAL, 0, _("No Messages resource defined in %s\n"), configfile);
            OK = false;
         }
      }
      if (GetNextRes(R_DIRECTOR, (RES *)newDirector) != NULL) {
         Jmsg(NULL, M_FATAL, 0, _("Only one Director resource permitted in %s\n"),
            configfile);
         OK = false;
      }
      /* tls_require implies tls_enable */
      if (newDirector->tls_require) {
         if (have_tls) {
            newDirector->tls_enable = true;
         } else {
            Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
            OK = false;
         }
      }

      need_tls = newDirector->tls_enable || newDirector->tls_authenticate;

      if (!newDirector->tls_certfile && need_tls) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Certificate\" file not defined for Director \"%s\" in %s.\n"),
            newDirector->name(), configfile);
         OK = false;
      }

      if (!newDirector->tls_keyfile && need_tls) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Key\" file not defined for Director \"%s\" in %s.\n"),
            newDirector->name(), configfile);
         OK = false;
      }

      if ((!newDirector->tls_ca_certfile && !newDirector->tls_ca_certdir) &&
           need_tls && newDirector->tls_verify_peer) {
         Jmsg(NULL, M_FATAL, 0, _("Neither \"TLS CA Certificate\" or \"TLS CA"
              " Certificate Dir\" are defined for Director \"%s\" in %s."
              " At least one CA certificate store is required"
              " when using \"TLS Verify Peer\".\n"),
              newDirector->name(), configfile);
         OK = false;
      }

      /* If everything is well, attempt to initialize our per-resource TLS context */
      if (OK && (need_tls || newDirector->tls_require)) {
         /* Initialize TLS context:
          * Args: CA certfile, CA certdir, Certfile, Keyfile,
          * Keyfile PEM Callback, Keyfile CB Userdata, DHfile, Verify Peer */
         newDirector->tls_ctx = new_tls_context(newDirector->tls_ca_certfile,
            newDirector->tls_ca_certdir, newDirector->tls_certfile,
            newDirector->tls_keyfile, NULL, NULL, newDirector->tls_dhfile,
            newDirector->tls_verify_peer);

         if (!newDirector->tls_ctx) {
            Jmsg(NULL, M_FATAL, 0, _("Failed to initialize TLS context for Director \"%s\" in %s.\n"),
                 newDirector->name(), configfile);
            OK = false;
         }
      }
   }

   if (!job) {
      Jmsg(NULL, M_FATAL, 0, _("No Job records defined in %s\n"), configfile);
      OK = false;
   }
   foreach_res(job, R_JOB) {
      int i;

      if (job->jobdefs) {
         JOB *jobdefs = job->jobdefs;
         /* Handle RunScripts alists specifically */
         if (jobdefs->RunScripts) {
            RUNSCRIPT *rs, *elt;

            if (!job->RunScripts) {
               job->RunScripts = New(alist(10, not_owned_by_alist));
            }

            foreach_alist(rs, jobdefs->RunScripts) {
               elt = copy_runscript(rs);
               job->RunScripts->append(elt); /* we have to free it */
            }
         }

         /* Transfer default items from JobDefs Resource */
         for (i=0; job_items[i].name; i++) {
            char **def_svalue, **svalue;  /* string value */
            uint32_t *def_ivalue, *ivalue;     /* integer value */
            bool *def_bvalue, *bvalue;    /* bool value */
            int64_t *def_lvalue, *lvalue; /* 64 bit values */
            alist **def_avalue, **avalue; /* alist values */
            uint32_t offset;

            Dmsg4(1400, "Job \"%s\", field \"%s\" bit=%d def=%d\n",
                job->name(), job_items[i].name,
                bit_is_set(i, job->hdr.item_present),
                bit_is_set(i, job->jobdefs->hdr.item_present));

            if (!bit_is_set(i, job->hdr.item_present) &&
                 bit_is_set(i, job->jobdefs->hdr.item_present)) {
               Dmsg2(400, "Job \"%s\", field \"%s\": getting default.\n",
                 job->name(), job_items[i].name);
               offset = (char *)(job_items[i].value) - (char *)&res_all;
               /*
                * Handle strings and directory strings
                */
               if (job_items[i].handler == store_str ||
                   job_items[i].handler == store_dir) {
                  def_svalue = (char **)((char *)(job->jobdefs) + offset);
                  Dmsg5(400, "Job \"%s\", field \"%s\" def_svalue=%s item %d offset=%u\n",
                       job->name(), job_items[i].name, *def_svalue, i, offset);
                  svalue = (char **)((char *)job + offset);
                  if (*svalue) {
                     Pmsg1(000, _("Hey something is wrong. p=0x%lu\n"), *svalue);
                  }
                  *svalue = bstrdup(*def_svalue);
                  set_bit(i, job->hdr.item_present);
               /*
                * Handle resources
                */
               } else if (job_items[i].handler == store_res) {
                  def_svalue = (char **)((char *)(job->jobdefs) + offset);
                  Dmsg4(400, "Job \"%s\", field \"%s\" item %d offset=%u\n",
                       job->name(), job_items[i].name, i, offset);
                  svalue = (char **)((char *)job + offset);
                  if (*svalue) {
                     Pmsg1(000, _("Hey something is wrong. p=0x%lu\n"), *svalue);
                  }
                  *svalue = *def_svalue;
                  set_bit(i, job->hdr.item_present);
               /*
                * Handle alist resources
                */
               } else if (job_items[i].handler == store_alist_res) {
                  void *elt;

                  def_avalue = (alist **)((char *)(job->jobdefs) + offset);
                  avalue = (alist **)((char *)job + offset);

                  *avalue = New(alist(10, not_owned_by_alist));

                  foreach_alist(elt, (*def_avalue)) {
                     (*avalue)->append(elt);
                  }
                  set_bit(i, job->hdr.item_present);
               /*
                * Handle integer fields
                *    Note, our store_bit does not handle bitmaped fields
                */
               } else if (job_items[i].handler == store_bit     ||
                          job_items[i].handler == store_pint32  ||
                          job_items[i].handler == store_jobtype ||
                          job_items[i].handler == store_level   ||
                          job_items[i].handler == store_int32   ||
                          job_items[i].handler == store_size32  ||
                          job_items[i].handler == store_migtype ||
                          job_items[i].handler == store_replace) {
                  def_ivalue = (uint32_t *)((char *)(job->jobdefs) + offset);
                  Dmsg5(400, "Job \"%s\", field \"%s\" def_ivalue=%d item %d offset=%u\n",
                       job->name(), job_items[i].name, *def_ivalue, i, offset);
                  ivalue = (uint32_t *)((char *)job + offset);
                  *ivalue = *def_ivalue;
                  set_bit(i, job->hdr.item_present);
               /*
                * Handle 64 bit integer fields
                */
               } else if (job_items[i].handler == store_time   ||
                          job_items[i].handler == store_size64 ||
                          job_items[i].handler == store_speed  ||
                          job_items[i].handler == store_int64) {
                  def_lvalue = (int64_t *)((char *)(job->jobdefs) + offset);
                  Dmsg5(400, "Job \"%s\", field \"%s\" def_lvalue=%" lld " item %d offset=%u\n",
                       job->name(), job_items[i].name, *def_lvalue, i, offset);
                  lvalue = (int64_t *)((char *)job + offset);
                  *lvalue = *def_lvalue;
                  set_bit(i, job->hdr.item_present);
               /*
                * Handle bool fields
                */
               } else if (job_items[i].handler == store_bool) {
                  def_bvalue = (bool *)((char *)(job->jobdefs) + offset);
                  Dmsg5(400, "Job \"%s\", field \"%s\" def_bvalue=%d item %d offset=%u\n",
                       job->name(), job_items[i].name, *def_bvalue, i, offset);
                  bvalue = (bool *)((char *)job + offset);
                  *bvalue = *def_bvalue;
                  set_bit(i, job->hdr.item_present);
               }
            }
         }
      }
      /*
       * Ensure that all required items are present
       */
      for (i=0; job_items[i].name; i++) {
         if (job_items[i].flags & ITEM_REQUIRED) {
               if (!bit_is_set(i, job->hdr.item_present)) {
                  Jmsg(NULL, M_ERROR_TERM, 0, _("\"%s\" directive in Job \"%s\" resource is required, but not found.\n"),
                    job_items[i].name, job->name());
                  OK = false;
                }
         }
         /* If this triggers, take a look at lib/parse_conf.h */
         if (i >= MAX_RES_ITEMS) {
            Emsg0(M_ERROR_TERM, 0, _("Too many items in Job resource\n"));
         }
      }
      if (!job->storage && !job->pool->storage) {
         Jmsg(NULL, M_FATAL, 0, _("No storage specified in Job \"%s\" nor in Pool.\n"),
            job->name());
         OK = false;
      }

      /* Make sure the job doesn't use the Scratch Pool to start with */
      const char *name;
      if (!check_pool(job->JobType, job->JobLevel,
                      job->pool, job->next_pool, &name)) { 
         Jmsg(NULL, M_FATAL, 0,
              _("%s \"Scratch\" not valid in Job \"%s\".\n"),
              name, job->name());
         OK = false;
      }
   } /* End loop over Job res */


   /* Loop over Consoles */
   CONRES *cons;
   foreach_res(cons, R_CONSOLE) {
      /* tls_require implies tls_enable */
      if (cons->tls_require) {
         if (have_tls) {
            cons->tls_enable = true;
         } else {
            Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
            OK = false;
            continue;
         }
      }

      need_tls = cons->tls_enable || cons->tls_authenticate;

      if (!cons->tls_certfile && need_tls) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Certificate\" file not defined for Console \"%s\" in %s.\n"),
            cons->name(), configfile);
         OK = false;
      }

      if (!cons->tls_keyfile && need_tls) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Key\" file not defined for Console \"%s\" in %s.\n"),
            cons->name(), configfile);
         OK = false;
      }

      if ((!cons->tls_ca_certfile && !cons->tls_ca_certdir)
            && need_tls && cons->tls_verify_peer) {
         Jmsg(NULL, M_FATAL, 0, _("Neither \"TLS CA Certificate\" or \"TLS CA"
            " Certificate Dir\" are defined for Console \"%s\" in %s."
            " At least one CA certificate store is required"
            " when using \"TLS Verify Peer\".\n"),
            cons->name(), configfile);
         OK = false;
      }
      /* If everything is well, attempt to initialize our per-resource TLS context */
      if (OK && (need_tls || cons->tls_require)) {
         /* Initialize TLS context:
          * Args: CA certfile, CA certdir, Certfile, Keyfile,
          * Keyfile PEM Callback, Keyfile CB Userdata, DHfile, Verify Peer */
         cons->tls_ctx = new_tls_context(cons->tls_ca_certfile,
            cons->tls_ca_certdir, cons->tls_certfile,
            cons->tls_keyfile, NULL, NULL, cons->tls_dhfile, cons->tls_verify_peer);

         if (!cons->tls_ctx) {
            Jmsg(NULL, M_FATAL, 0, _("Failed to initialize TLS context for File daemon \"%s\" in %s.\n"),
               cons->name(), configfile);
            OK = false;
         }
      }

   }

   /* Loop over Clients */
   CLIENT *client;
   foreach_res(client, R_CLIENT) {
      /* tls_require implies tls_enable */
      if (client->tls_require) {
         if (have_tls) {
            client->tls_enable = true;
         } else {
            Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
            OK = false;
            continue;
         }
      }
      need_tls = client->tls_enable || client->tls_authenticate;
      if ((!client->tls_ca_certfile && !client->tls_ca_certdir) && need_tls) {
         Jmsg(NULL, M_FATAL, 0, _("Neither \"TLS CA Certificate\""
            " or \"TLS CA Certificate Dir\" are defined for File daemon \"%s\" in %s.\n"),
            client->name(), configfile);
         OK = false;
      }

      /* If everything is well, attempt to initialize our per-resource TLS context */
      if (OK && (need_tls || client->tls_require)) {
         /* Initialize TLS context:
          * Args: CA certfile, CA certdir, Certfile, Keyfile,
          * Keyfile PEM Callback, Keyfile CB Userdata, DHfile, Verify Peer */
         client->tls_ctx = new_tls_context(client->tls_ca_certfile,
            client->tls_ca_certdir, client->tls_certfile,
            client->tls_keyfile, NULL, NULL, NULL,
            true);

         if (!client->tls_ctx) {
            Jmsg(NULL, M_FATAL, 0, _("Failed to initialize TLS context for File daemon \"%s\" in %s.\n"),
               client->name(), configfile);
            OK = false;
         }
      }
   }

   /* Loop over all pools, check PoolType */
   POOL *pool;
   foreach_res(pool, R_POOL) {
      if (!pool->pool_type) {
         /* This case is checked by the parse engine, we should not */
         Jmsg(NULL, M_FATAL, 0, _("PoolType required in Pool resource \"%s\".\n"), pool->hdr.name);
         OK = false;
         continue;
      }
      if ((strcasecmp(pool->pool_type, NT_("backup"))  != 0) &&
          (strcasecmp(pool->pool_type, NT_("copy"))    != 0) &&
          (strcasecmp(pool->pool_type, NT_("cloned"))  != 0) &&
          (strcasecmp(pool->pool_type, NT_("archive")) != 0) &&
          (strcasecmp(pool->pool_type, NT_("migration")) != 0) &&
          (strcasecmp(pool->pool_type, NT_("scratch")) != 0))
      {
         Jmsg(NULL, M_FATAL, 0, _("Invalid PoolType \"%s\" in Pool resource \"%s\".\n"), pool->pool_type, pool->hdr.name);
         OK = false;
      }

      if (pool->NextPool && strcmp(pool->NextPool->name(), "Scratch") == 0) {
         Jmsg(NULL, M_FATAL, 0,
              _("NextPool \"Scratch\" not valid in Pool \"%s\".\n"),
              pool->name());
         OK = false;
      }
   }

   UnlockRes();
   if (OK) {
      close_msg(NULL);                /* close temp message handler */
      init_msg(NULL, newDirector->messages); /* open daemon message handler */
      last_reload_time = time(NULL);
   }
   return OK;
}

/*
 * In this routine,
 *  - we can check the connection (mode=CHECK_CONNECTION)
 *  - we can synchronize the catalog with the configuration (mode=UPDATE_CATALOG)
 *  - we can synchronize, and fix old job records (mode=UPDATE_AND_FIX)
 *  - we hook up the Autochange children with the parent, and
 *    we hook the shared autochangers together.
 */
static bool check_catalog(cat_op mode)
{
   bool OK = true;
   bool need_tls;
   STORE *store, *ac_child;

   /* Loop over databases */
   CAT *catalog;
   const char *BDB_db_driver = NULL; // global dbdriver from BDB class
   int db_driver_len = 0;

   foreach_res(catalog, R_CATALOG) {
      BDB *db;
      /*
       * Make sure we can open catalog, otherwise print a warning
       * message because the server is probably not running.
       */
      db = db_init_database(NULL, catalog->db_driver, catalog->db_name, 
              catalog->db_user,
              catalog->db_password, catalog->db_address,
              catalog->db_port, catalog->db_socket,
              catalog->db_ssl_mode, catalog->db_ssl_key,
              catalog->db_ssl_cert, catalog->db_ssl_ca,
              catalog->db_ssl_capath, catalog->db_ssl_cipher,
              catalog->mult_db_connections,
              catalog->disable_batch_insert);

      /*  To fill appropriate "dbdriver" field into "CAT" catalog resource class */

      if (db) {
         /* To fetch dbdriver from "BDB" Catalog DB Interface class (global)
          * filled with database passed during bacula compilation
          */
         BDB_db_driver = db_get_engine_name(db);
         db_driver_len = strlen(BDB_db_driver);

         if (catalog->db_driver == NULL) { // dbdriver  field not present in bacula director conf file
            catalog->db_driver = (char *)malloc(db_driver_len + 1);
            memset(catalog->db_driver, 0 , (db_driver_len + 1));
         } else { // dbdriver  field present in bacula director conf file
            if (strlen(catalog->db_driver) == 0) {  // dbdriver  field present but empty in bacula director conf file
                /* warning message displayed on Console while running bacula command:
                 * "bacula-dir -tc", in case "dbdriver" field  is empty in director
                 * configuration file while database argument is passed during compile time
                 */
               Pmsg1(000, _("Dbdriver field within director config file is empty " \
                  "but Database argument \"%s\" is " \
                  "passed during Bacula compilation. \n"),
                  BDB_db_driver);
               /* warning message displayed in log file (bacula.log) while running
                * bacula command: "bacula-dir -tc", in case "dbdriver" field  is empty in director
                * configuration file while database argument is passed during compile time
                */
               Jmsg(NULL, M_WARNING, 0, _("Dbdriver field within director config file " \
                  "is empty but Database argument \"%s\" is " \
                  "passed during Bacula compilation. \n"),
                  BDB_db_driver);

            } else if (strcasecmp(catalog->db_driver, BDB_db_driver)) { // dbdriver  field mismatch in bacula director conf file
               /* warning message displayed on Console while running bacula command:
                * "bacula-dir -tc", in case "catalog->db_driver" field  doesn’t match
                * with database argument  during compile time
                */
               Pmsg2(000, _("Dbdriver field within director config file \"%s\" " \
                  "mismatched with the Database argument \"%s\" " \
                  "passed during Bacula compilation. \n"),
                  catalog->db_driver, BDB_db_driver);
               /* warning message displayed on log file (bacula.log) while running
                * bacula command: "bacula-dir -tc", in case "catalog->db_driver" field
                * doesn’t match with database argument  during compile time
                */
               Jmsg(NULL, M_WARNING, 0, _("Dbdriver field within director config file \"%s\" " \
                  "mismatched with the Database argument \"%s\" " \
                  "passed during Bacula compilation. \n"),
                  catalog->db_driver, BDB_db_driver);
            }
            catalog->db_driver = (char *)realloc(catalog->db_driver, (db_driver_len + 1));
            memset(catalog->db_driver, 0 , (db_driver_len + 1));
         }
         if (catalog->db_driver) {
           /* To copy dbdriver field into "CAT" catalog resource class (local)
            * from dbdriver in "BDB" catalog DB Interface class (global)
            */
            strncpy(catalog->db_driver, BDB_db_driver, db_driver_len);
         }
      }

      if (!db || !db_open_database(NULL, db)) {
         Pmsg2(000, _("Could not open Catalog \"%s\", database \"%s\".\n"),
              catalog->name(), catalog->db_name);
         Jmsg(NULL, M_FATAL, 0, _("Could not open Catalog \"%s\", database \"%s\".\n"),
              catalog->name(), catalog->db_name);
         if (db) {
            Jmsg(NULL, M_FATAL, 0, _("%s"), db_strerror(db));
            Pmsg1(000, "%s", db_strerror(db));
            db_close_database(NULL, db);
         }
         OK = false;
         continue;
      }

      /* Display a message if the db max_connections is too low */
      if (!db_check_max_connections(NULL, db, director->MaxConcurrentJobs)) {
         Pmsg1(000, "Warning, settings problem for Catalog=%s\n", catalog->name());
         Pmsg1(000, "%s", db_strerror(db));
      }

      /* we are in testing mode, so don't touch anything in the catalog */
      if (mode == CHECK_CONNECTION) {
         if (db) db_close_database(NULL, db);
         continue;
      }

      /* Loop over all pools, defining/updating them in each database */
      POOL *pool;
      foreach_res(pool, R_POOL) {
         /*
          * If the Pool has a catalog resource create the pool only
          *   in that catalog.
          */
         if (!pool->catalog || pool->catalog == catalog) {
            create_pool(NULL, db, pool, POOL_OP_UPDATE);  /* update request */
         }
      }

      /* Once they are created, we can loop over them again, updating
       * references (RecyclePool)
       */
      foreach_res(pool, R_POOL) {
         /*
          * If the Pool has a catalog resource update the pool only
          *   in that catalog.
          */
         if (!pool->catalog || pool->catalog == catalog) {
            update_pool_references(NULL, db, pool);
         }
      }

      /* Ensure basic client record is in DB */
      CLIENT *client;
      foreach_res(client, R_CLIENT) {
         CLIENT_DBR cr;
         /* Create clients only if they use the current catalog */
         if (client->catalog != catalog) {
            Dmsg3(500, "Skip client=%s with cat=%s not catalog=%s\n",
                  client->name(), client->catalog->name(), catalog->name());
            continue;
         }
         Dmsg2(500, "create cat=%s for client=%s\n",
               client->catalog->name(), client->name());
         memset(&cr, 0, sizeof(cr));
         bstrncpy(cr.Name, client->name(), sizeof(cr.Name));
         cr.AutoPrune = client->AutoPrune;
         cr.FileRetention = client->FileRetention;
         cr.JobRetention = client->JobRetention;

         db_create_client_record(NULL, db, &cr);

         /* If the record doesn't reflect the current settings
          * we can adjust the catalog record.
          */
         if (cr.AutoPrune     != client->AutoPrune     ||
             cr.JobRetention  != client->JobRetention  ||
             cr.FileRetention != client->FileRetention)
         {
            cr.AutoPrune = client->AutoPrune;
            cr.FileRetention = client->FileRetention;
            cr.JobRetention = client->JobRetention;
            db_update_client_record(NULL, db, &cr);
         }
      }

      /* Ensure basic storage record is in DB */
      foreach_res(store, R_STORAGE) {
         STORAGE_DBR sr;
         MEDIATYPE_DBR mtr;
         memset(&sr, 0, sizeof(sr));
         memset(&mtr, 0, sizeof(mtr));
         if (store->media_type) {
            bstrncpy(mtr.MediaType, store->media_type, sizeof(mtr.MediaType));
            mtr.ReadOnly = 0;
            db_create_mediatype_record(NULL, db, &mtr);
         } else {
            mtr.MediaTypeId = 0;
         }
         bstrncpy(sr.Name, store->name(), sizeof(sr.Name));
         sr.AutoChanger = store->autochanger;
         if (!db_create_storage_record(NULL, db, &sr)) {
            Jmsg(NULL, M_FATAL, 0, _("Could not create storage record for %s\n"),
                 store->name());
            OK = false;
         }
         store->StorageId = sr.StorageId;   /* set storage Id */
         if (!sr.created) {                 /* if not created, update it */
            sr.AutoChanger = store->autochanger;
            if (!db_update_storage_record(NULL, db, &sr)) {
               Jmsg(NULL, M_FATAL, 0, _("Could not update storage record for %s\n"),
                    store->name());
               OK = false;
            }
         }

         /* tls_require implies tls_enable */
         if (store->tls_require) {
            if (have_tls) {
               store->tls_enable = true;
            } else {
               Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
               OK = false;
            }
         }

         need_tls = store->tls_enable || store->tls_authenticate;

         if ((!store->tls_ca_certfile && !store->tls_ca_certdir) && need_tls) {
            Jmsg(NULL, M_FATAL, 0, _("Neither \"TLS CA Certificate\""
                 " or \"TLS CA Certificate Dir\" are defined for Storage \"%s\" in %s.\n"),
                 store->name(), configfile);
            OK = false;
         }

         /* If everything is well, attempt to initialize our per-resource TLS context */
         if (OK && (need_tls || store->tls_require)) {
           /* Initialize TLS context:
            * Args: CA certfile, CA certdir, Certfile, Keyfile,
            * Keyfile PEM Callback, Keyfile CB Userdata, DHfile, Verify Peer */
            store->tls_ctx = new_tls_context(store->tls_ca_certfile,
               store->tls_ca_certdir, store->tls_certfile,
               store->tls_keyfile, NULL, NULL, NULL, true);

            if (!store->tls_ctx) {
               Jmsg(NULL, M_FATAL, 0, _("Failed to initialize TLS context for Storage \"%s\" in %s.\n"),
                    store->name(), configfile);
               OK = false;
            }
         }
      }

      /* Link up all the children for each changer */
      foreach_res(store, R_STORAGE) {
         char sid[50];
         if (store->changer == store) {  /* we are a real Autochanger */
            store->ac_group = get_pool_memory(PM_FNAME);
            store->ac_group[0] = 0;
            pm_strcat(store->ac_group, edit_int64(store->StorageId, sid));
            /* Now look for children who point to this storage */
            foreach_res(ac_child, R_STORAGE) {
               if (ac_child != store && ac_child->changer == store) {
                  /* Found a child -- add StorageId */
                  pm_strcat(store->ac_group, ",");
                  pm_strcat(store->ac_group, edit_int64(ac_child->StorageId, sid));
               }
            }
         }
      }

      /* Link up all the shared storage devices */
      foreach_res(store, R_STORAGE) {
         if (store->ac_group) {  /* we are a real Autochanger */
            /* Now look for Shared Storage who point to this storage */
            foreach_res(ac_child, R_STORAGE) {
               if (ac_child->shared_storage == store && ac_child->ac_group &&
                   ac_child->shared_storage != ac_child) {
                  pm_strcat(store->ac_group, ",");
                  pm_strcat(store->ac_group, ac_child->ac_group);
               }
            }
         }
      }

      /* Loop over all counters, defining them in each database */
      /* Set default value in all counters */
      COUNTER *counter;
      foreach_res(counter, R_COUNTER) {
         /* Write to catalog? */
         if (!counter->created && counter->Catalog == catalog) {
            COUNTER_DBR cr;
            bstrncpy(cr.Counter, counter->name(), sizeof(cr.Counter));
            cr.MinValue = counter->MinValue;
            cr.MaxValue = counter->MaxValue;
            cr.CurrentValue = counter->MinValue;
            if (counter->WrapCounter) {
               bstrncpy(cr.WrapCounter, counter->WrapCounter->name(), sizeof(cr.WrapCounter));
            } else {
               cr.WrapCounter[0] = 0;  /* empty string */
            }
            if (db_create_counter_record(NULL, db, &cr)) {
               counter->CurrentValue = cr.CurrentValue;
               counter->created = true;
               Dmsg2(100, "Create counter %s val=%d\n", counter->name(), counter->CurrentValue);
            }
         }
         if (!counter->created) {
            counter->CurrentValue = counter->MinValue;  /* default value */
         }
      }
      /* cleanup old job records */
      if (mode == UPDATE_AND_FIX) {
         db_sql_query(db, cleanup_created_job, NULL, NULL);
         db_sql_query(db, cleanup_running_job, NULL, NULL);
      }

      /* Set SQL engine name in global for debugging */
      set_db_engine_name(db_get_engine_name(db));
      if (db) db_close_database(NULL, db);
   }
   return OK;
}

static void cleanup_old_files()
{
   DIR* dp;
   int rc, name_max;
   int my_name_len = strlen(my_name);
   int len = strlen(director->working_directory);
   POOL_MEM dname(PM_FNAME);
   POOLMEM *cleanup = get_pool_memory(PM_MESSAGE);
   POOLMEM *basename = get_pool_memory(PM_MESSAGE);
   regex_t preg1;
   char prbuf[500];
   const int nmatch = 30;
   regmatch_t pmatch[nmatch];

   /* Exclude spaces and look for .mail, .tmp or .restore.xx.bsr files */
   const char *pat1 = "^[^ ]+\\.(restore\\.[^ ]+\\.bsr|mail|tmp)$";

   /* Setup working directory prefix */
   pm_strcpy(basename, director->working_directory);
   if (len > 0 && !IsPathSeparator(director->working_directory[len-1])) {
      pm_strcat(basename, "/");
   }

   /* Compile regex expressions */
   rc = regcomp(&preg1, pat1, REG_EXTENDED);
   if (rc != 0) {
      regerror(rc, &preg1, prbuf, sizeof(prbuf));
      Pmsg2(000,  _("Could not compile regex pattern \"%s\" ERR=%s\n"),
           pat1, prbuf);
      goto get_out2;
   }

   name_max = pathconf(".", _PC_NAME_MAX);
   if (name_max < 1024) {
      name_max = 1024;
   }

   if (!(dp = opendir(director->working_directory))) {
      berrno be;
      Pmsg2(000, "Failed to open working dir %s for cleanup: ERR=%s\n",
            director->working_directory, be.bstrerror());
      goto get_out1;
   }

   while (1) {
      if (breaddir(dp, dname.addr()) != 0) {
         break;
      }
      /* Exclude any name with ., .., not my_name or containing a space */
      if (strcmp(dname.c_str(), ".") == 0 || strcmp(dname.c_str(), "..") == 0 ||
          strncmp(dname.c_str(), my_name, my_name_len) != 0) {
         Dmsg1(500, "Skipped: %s\n", dname.c_str());
         continue;
      }

      /* Unlink files that match regexes */
      if (regexec(&preg1, dname.c_str(), nmatch, pmatch,  0) == 0) {
         pm_strcpy(cleanup, basename);
         pm_strcat(cleanup, dname);
         Dmsg1(100, "Unlink: %s\n", cleanup);
         unlink(cleanup);
      }
   }

   closedir(dp);
/* Be careful to free up the correct resources */
get_out1:
   regfree(&preg1);
get_out2:
   free_pool_memory(cleanup);
   free_pool_memory(basename);
}
