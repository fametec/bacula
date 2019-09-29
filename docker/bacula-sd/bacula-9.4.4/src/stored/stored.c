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
 * Third generation Storage daemon.
 *
 *  Written by Kern Sibbald, MM
 *
 * It accepts a number of simple commands from the File daemon
 * and acts on them. When a request to append data is made,
 * it opens a data channel and accepts data from the
 * File daemon.
 *
 */

#include "bacula.h"
#include "stored.h"

/* TODO: fix problem with bls, bextract
 * that use findlib and already declare
 * filed plugins
 */
#include "sd_plugins.h"

/* Imported functions and variables */
extern bool parse_sd_config(CONFIG *config, const char *configfile, int exit_code);

/* Forward referenced functions */
void terminate_stored(int sig);
static int check_resources();
static void cleanup_old_files();

extern "C" void *device_initialization(void *arg);

#define CONFIG_FILE "bacula-sd.conf"  /* Default config file */

/* Global variables exported */
char OK_msg[]   = "3000 OK\n";
char TERM_msg[] = "3999 Terminate\n";
void *start_heap;
static bool test_config = false;


static uint32_t VolSessionId = 0;
uint32_t VolSessionTime;
char *configfile = NULL;
bool init_done = false;
static pthread_t server_tid;
static bool server_tid_valid = false;

/* Global static variables */
static bool foreground = false;
static bool make_pid_file = true;     /* create pid file */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static workq_t dird_workq;            /* queue for processing connections */
static CONFIG *config;


static void usage()
{
   fprintf(stderr, _(
      PROG_COPYRIGHT
      "\n%sVersion: %s (%s)\n\n"
      "Usage: bacula-sd [options] [-c config_file] [config_file]\n"
      "     -c <file>         use <file> as configuration file\n"
      "     -d <nn>[,<tags>]  set debug level to <nn>, debug tags to <tags>\n"
      "     -dt               print timestamp in debug output\n"
      "     -T                set trace on\n"
      "     -f                run in foreground (for debugging)\n"
      "     -g <group>        set groupid to group\n"
      "     -m                print kaboom output (for debugging)\n"
      "     -p                proceed despite I/O errors\n"
      "     -P                do not create pid file\n"
      "     -s                no signals (for debugging)\n"
      "     -t                test - read config and exit\n"
      "     -u <user>         userid to <user>\n"
      "     -v                verbose user messages\n"
      "     -?                print this message.\n"
      "\n"), 2000, "", VERSION, BDATE);
   exit(1);
}

/*
 * !!! WARNING !!! Use this function only when bacula is stopped.
 * ie, after a fatal signal and before exiting the program
 * Print information about a JCR
 */
static void sd_debug_print(JCR *jcr, FILE *fp)
{
   if (jcr->dcr) {
      DCR *dcr = jcr->dcr;
      fprintf(fp, "\tdcr=%p volumename=%s dev=%p newvol=%d reserved=%d locked=%d\n",
              dcr, dcr->VolumeName, dcr->dev, dcr->NewVol,
              dcr->is_reserved(),
              dcr->is_dev_locked());
   } else {
      fprintf(fp, "dcr=*None*\n");
   }
}

/*********************************************************************
 *
 *  Main Bacula Unix Storage Daemon
 *
 */
#if defined(HAVE_WIN32)
#define main BaculaMain
#endif

int main (int argc, char *argv[])
{
   int ch;
   bool no_signals = false;
   pthread_t thid;
   char *uid = NULL;
   char *gid = NULL;

   start_heap = sbrk(0);
   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");

   init_stack_dump();
   my_name_is(argc, argv, "bacula-sd");
   init_msg(NULL, NULL);
   daemon_start_time = time(NULL);
   setup_daemon_message_queue();

   /* Sanity checks */
   if (TAPE_BSIZE % B_DEV_BSIZE != 0 || TAPE_BSIZE / B_DEV_BSIZE == 0) {
      Jmsg2(NULL, M_ABORT, 0, _("Tape block size (%d) not multiple of system size (%d)\n"),
         TAPE_BSIZE, B_DEV_BSIZE);
   }
   if (TAPE_BSIZE != (1 << (ffs(TAPE_BSIZE)-1))) {
      Jmsg1(NULL, M_ABORT, 0, _("Tape block size (%d) is not a power of 2\n"), TAPE_BSIZE);
   }

   while ((ch = getopt(argc, argv, "c:d:fg:mpPstu:v?Ti")) != -1) {
      switch (ch) {
      case 'c':                    /* configuration file */
         if (configfile != NULL) {
            free(configfile);
         }
         configfile = bstrdup(optarg);
         break;

      case 'd':                    /* debug level */
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

      /* Temp code to enable new match_bsr() code, not documented */
      case 'i':
         use_new_match_all = 1;

         break;
      case 'm':                    /* print kaboom output */
         prt_kaboom = true;
         break;

      case 'p':                    /* proceed in spite of I/O errors */
         forge_on = true;
         break;

      case 'P':                    /* no pid file */
         make_pid_file = false;
         break;

      case 's':                    /* no signals */
         no_signals = true;
         break;

      case 't':
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
         break;
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
      daemon_start();                 /* become daemon */
      init_stack_dump();              /* pick up new pid */
   }

   if (!no_signals) {
      init_signals(terminate_stored);
   }

   if (configfile == NULL) {
      configfile = bstrdup(CONFIG_FILE);
   }

   config = New(CONFIG());
   parse_sd_config(config, configfile, M_ERROR_TERM);

   if (init_crypto() != 0) {
      Jmsg((JCR *)NULL, M_ERROR_TERM, 0, _("Cryptography library initialization failed.\n"));
   }

   if (!check_resources()) {
      Jmsg((JCR *)NULL, M_ERROR_TERM, 0, _("Please correct configuration file: %s\n"), configfile);
   }

   init_reservations_lock();

   if (test_config) {
      terminate_stored(0);
   }

   my_name_is(0, (char **)NULL, me->hdr.name);     /* Set our real name */

   if (make_pid_file) {
      create_pid_file(me->pid_directory, "bacula-sd",
                      get_first_port_host_order(me->sdaddrs));
   }
   read_state_file(me->working_directory, "bacula-sd",
                   get_first_port_host_order(me->sdaddrs));

   set_jcr_in_tsd(INVALID_JCR);
   /* Make sure on Solaris we can run concurrent, watch dog + servers + misc */
   set_thread_concurrency(me->max_concurrent_jobs * 2 + 4);
   lmgr_init_thread(); /* initialize the lockmanager stack */

   load_sd_plugins(me->plugin_directory);

   drop(uid, gid, false);

   cleanup_old_files();

   /* Ensure that Volume Session Time and Id are both
    * set and are both non-zero.
    */
   VolSessionTime = (uint32_t)daemon_start_time;
   if (VolSessionTime == 0) { /* paranoid */
      Jmsg0(NULL, M_ABORT, 0, _("Volume Session Time is ZERO!\n"));
   }

   /*
    * Start the device allocation thread
    */
   create_volume_lists();             /* do before device_init */
   if (pthread_create(&thid, NULL, device_initialization, NULL) != 0) {
      berrno be;
      Jmsg1(NULL, M_ABORT, 0, _("Unable to create thread. ERR=%s\n"), be.bstrerror());
   }

   start_watchdog();                  /* start watchdog thread */
   init_jcr_subsystem();              /* start JCR watchdogs etc. */
   dbg_jcr_add_hook(sd_debug_print); /* used to director variables */

   /* Single server used for Director and File daemon */
   server_tid = pthread_self();
   server_tid_valid = true;
   bnet_thread_server(me->sdaddrs, me->max_concurrent_jobs * 2 + 1,
                      &dird_workq, handle_connection_request);
   exit(1);                           /* to keep compiler quiet */
}

/* Return a new Session Id */
uint32_t newVolSessionId()
{
   uint32_t Id;

   P(mutex);
   VolSessionId++;
   Id = VolSessionId;
   V(mutex);
   return Id;
}

/* Check Configuration file for necessary info */
static int check_resources()
{
   bool OK = true;
   bool tls_needed;
   AUTOCHANGER *changer;
   DEVRES *device;

   me = (STORES *)GetNextRes(R_STORAGE, NULL);
   if (!me) {
      Jmsg1(NULL, M_ERROR, 0, _("No Storage resource defined in %s. Cannot continue.\n"),
         configfile);
      OK = false;
   }

   if (GetNextRes(R_STORAGE, (RES *)me) != NULL) {
      Jmsg1(NULL, M_ERROR, 0, _("Only one Storage resource permitted in %s\n"),
         configfile);
      OK = false;
   }
   if (GetNextRes(R_DIRECTOR, NULL) == NULL) {
      Jmsg1(NULL, M_ERROR, 0, _("No Director resource defined in %s. Cannot continue.\n"),
         configfile);
      OK = false;
   }
   if (GetNextRes(R_DEVICE, NULL) == NULL){
      Jmsg1(NULL, M_ERROR, 0, _("No Device resource defined in %s. Cannot continue.\n"),
           configfile);
      OK = false;
   }

   if (!me->messages) {
      me->messages = (MSGS *)GetNextRes(R_MSGS, NULL);
      if (!me->messages) {
         Jmsg1(NULL, M_ERROR, 0, _("No Messages resource defined in %s. Cannot continue.\n"),
            configfile);
         OK = false;
      }
   }

   if (!me->working_directory) {
      Jmsg1(NULL, M_ERROR, 0, _("No Working Directory defined in %s. Cannot continue.\n"),
         configfile);
      OK = false;
   }

   DIRRES *director;
   STORES *store;
   foreach_res(store, R_STORAGE) {
      /* tls_require implies tls_enable */
      if (store->tls_require) {
         if (have_tls) {
            store->tls_enable = true;
         } else {
            Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
            OK = false;
            continue;
         }
      }

      tls_needed = store->tls_enable || store->tls_authenticate;

      if (!store->tls_certfile && tls_needed) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Certificate\" file not defined for Storage \"%s\" in %s.\n"),
              store->hdr.name, configfile);
         OK = false;
      }

      if (!store->tls_keyfile && tls_needed) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Key\" file not defined for Storage \"%s\" in %s.\n"),
              store->hdr.name, configfile);
         OK = false;
      }

      if ((!store->tls_ca_certfile && !store->tls_ca_certdir) && tls_needed && store->tls_verify_peer) {
         Jmsg(NULL, M_FATAL, 0, _("Neither \"TLS CA Certificate\""
              " or \"TLS CA Certificate Dir\" are defined for Storage \"%s\" in %s."
              " At least one CA certificate store is required"
              " when using \"TLS Verify Peer\".\n"),
              store->hdr.name, configfile);
         OK = false;
      }

      /* If everything is well, attempt to initialize our per-resource TLS context */
      if (OK && (tls_needed || store->tls_require)) {
         /* Initialize TLS context:
          * Args: CA certfile, CA certdir, Certfile, Keyfile,
          * Keyfile PEM Callback, Keyfile CB Userdata, DHfile, Verify Peer */
         store->tls_ctx = new_tls_context(store->tls_ca_certfile,
            store->tls_ca_certdir, store->tls_certfile,
            store->tls_keyfile, NULL, NULL, store->tls_dhfile,
            store->tls_verify_peer);

         if (!store->tls_ctx) {
            Jmsg(NULL, M_FATAL, 0, _("Failed to initialize TLS context for Storage \"%s\" in %s.\n"),
                 store->hdr.name, configfile);
            OK = false;
         }
      }
   }

   foreach_res(director, R_DIRECTOR) {
      /* tls_require implies tls_enable */
      if (director->tls_require) {
         director->tls_enable = true;
      }

      tls_needed = director->tls_enable || director->tls_authenticate;

      if (!director->tls_certfile && tls_needed) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Certificate\" file not defined for Director \"%s\" in %s.\n"),
              director->hdr.name, configfile);
         OK = false;
      }

      if (!director->tls_keyfile && tls_needed) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Key\" file not defined for Director \"%s\" in %s.\n"),
              director->hdr.name, configfile);
         OK = false;
      }

      if ((!director->tls_ca_certfile && !director->tls_ca_certdir) && tls_needed && director->tls_verify_peer) {
         Jmsg(NULL, M_FATAL, 0, _("Neither \"TLS CA Certificate\""
              " or \"TLS CA Certificate Dir\" are defined for Director \"%s\" in %s."
              " At least one CA certificate store is required"
              " when using \"TLS Verify Peer\".\n"),
              director->hdr.name, configfile);
         OK = false;
      }

      /* If everything is well, attempt to initialize our per-resource TLS context */
      if (OK && (tls_needed || director->tls_require)) {
         /* Initialize TLS context:
          * Args: CA certfile, CA certdir, Certfile, Keyfile,
          * Keyfile PEM Callback, Keyfile CB Userdata, DHfile, Verify Peer */
         director->tls_ctx = new_tls_context(director->tls_ca_certfile,
            director->tls_ca_certdir, director->tls_certfile,
            director->tls_keyfile, NULL, NULL, director->tls_dhfile,
            director->tls_verify_peer);

         if (!director->tls_ctx) {
            Jmsg(NULL, M_FATAL, 0, _("Failed to initialize TLS context for Director \"%s\" in %s.\n"),
                 director->hdr.name, configfile);
            OK = false;
         }
      }
   }

   foreach_res(changer, R_AUTOCHANGER) {
      foreach_alist(device, changer->device) {
         device->cap_bits |= CAP_AUTOCHANGER;
      }
   }

   if (OK) {
      OK = init_autochangers();
   }

   if (OK) {
      close_msg(NULL);                   /* close temp message handler */
      init_msg(NULL, me->messages);      /* open daemon message handler */
      set_working_directory(me->working_directory);
   }

   return OK;
}

/*
 * Remove old .spool files written by me from the working directory.
 */
static void cleanup_old_files()
{
   DIR* dp;
   int rc, name_max;
   int my_name_len = strlen(my_name);
   int len = strlen(me->working_directory);
   POOLMEM *cleanup = get_pool_memory(PM_MESSAGE);
   POOLMEM *basename = get_pool_memory(PM_MESSAGE);
   POOL_MEM dname(PM_FNAME);
   regex_t preg1;
   char prbuf[500];
   const int nmatch = 30;
   regmatch_t pmatch[nmatch];
   berrno be;

   /* Look for .spool files but don't allow spaces */
   const char *pat1 = "^[^ ]+\\.spool$";

   /* Setup working directory prefix */
   pm_strcpy(basename, me->working_directory);
   if (len > 0 && !IsPathSeparator(me->working_directory[len-1])) {
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

   if (!(dp = opendir(me->working_directory))) {
      berrno be;
      Pmsg2(000, "Failed to open working dir %s for cleanup: ERR=%s\n",
            me->working_directory, be.bstrerror());
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

      /* Unlink files that match regex */
      if (regexec(&preg1, dname.c_str(), nmatch, pmatch,  0) == 0) {
         pm_strcpy(cleanup, basename);
         pm_strcat(cleanup, dname);
         Dmsg1(500, "Unlink: %s\n", cleanup);
         unlink(cleanup);
      }
   }
   closedir(dp);

get_out1:
   regfree(&preg1);
get_out2:
   free_pool_memory(cleanup);
   free_pool_memory(basename);
}

/*
 * Here we attempt to init and open each device. This is done
 *  once at startup in a separate thread.
 */
extern "C"
void *device_initialization(void *arg)
{
   DEVRES *device;
   DCR *dcr;
   JCR *jcr;
   DEVICE *dev;
   struct stat statp;

   pthread_detach(pthread_self());
   jcr = new_jcr(sizeof(JCR), stored_free_jcr);
   new_plugins(jcr);  /* instantiate plugins */
   jcr->setJobType(JT_SYSTEM);
   /* Initialize FD start condition variable */
   int errstat = pthread_cond_init(&jcr->job_start_wait, NULL);
   if (errstat != 0) {
      berrno be;
      Jmsg1(jcr, M_ABORT, 0, _("Unable to init job cond variable: ERR=%s\n"), be.bstrerror(errstat));
   }

   LockRes();

   foreach_res(device, R_DEVICE) {
      Dmsg1(90, "calling init_dev %s\n", device->hdr.name);
      dev = init_dev(NULL, device);
      Dmsg1(10, "SD init done %s\n", device->hdr.name);
      if (!dev) {
         Jmsg1(NULL, M_ERROR, 0, _("Could not initialize SD device \"%s\"\n"), device->hdr.name);
         continue;
     }

      jcr->dcr = dcr = new_dcr(jcr, NULL, dev);
      generate_plugin_event(jcr, bsdEventDeviceInit, dcr);

      if (device->control_name && stat(device->control_name, &statp) < 0) {
         berrno be;
         Jmsg2(jcr, M_ERROR_TERM, 0, _("Unable to stat ControlDevice %s: ERR=%s\n"),
            device->control_name, be.bstrerror());
      }

      if ((device->lock_command && device->control_name) &&
          !me->plugin_directory) {
         Jmsg0(jcr, M_ERROR_TERM, 0, _("No plugin directory configured for SAN shared storage\n"));
      }

      if (device->min_block_size > device->max_block_size) {
         Jmsg1(jcr, M_ERROR_TERM, 0, _("MaximumBlockSize must be greater or equal than MinimumBlockSize for Device \"%s\"\n"),
               dev->print_name());
      }

      if (device->min_block_size > device->max_block_size) {
         Jmsg1(jcr, M_ERROR_TERM, 0, _("MaximumBlockSize must be greater or equal than MinimumBlockSize for Device \"%s\"\n"),
               dev->print_name());
      }

      /*
       * Note: be careful setting the slot here. If the drive
       *  is shared storage, the contents can change before
       *  the drive is used.
       */
      if (device->cap_bits & CAP_ALWAYSOPEN) {
         if (dev->is_autochanger()) {
            /* If autochanger set slot in dev sturcture */
            get_autochanger_loaded_slot(dcr);
         }
         Dmsg1(20, "calling first_open_device %s\n", dev->print_name());
         if (generate_plugin_event(jcr, bsdEventDeviceOpen, dcr) != bRC_OK) {
            Jmsg(jcr, M_FATAL, 0, _("generate_plugin_event(bsdEventDeviceOpen) Failed\n"));
            continue;
         }

         if (!first_open_device(dcr)) {
            Jmsg1(NULL, M_ERROR, 0, _("Could not open device %s\n"), dev->print_name());
            Dmsg1(20, "Could not open device %s\n", dev->print_name());
            generate_plugin_event(jcr, bsdEventDeviceClose, dcr);
            free_dcr(dcr);
            jcr->dcr = NULL;
            continue;
         }
      } else {
         /* If not always open, we don't know what is in the drive */
         dev->clear_slot();
      }
      if (device->cap_bits & CAP_AUTOMOUNT && dev->is_open()) {
         switch (dev->read_dev_volume_label(dcr)) {
         case VOL_OK:
            memcpy(&dev->VolCatInfo, &dcr->VolCatInfo, sizeof(dev->VolCatInfo));
            volume_unused(dcr);             /* mark volume "released" */
            break;
         default:
            Jmsg1(NULL, M_WARNING, 0, _("Could not mount device %s\n"), dev->print_name());
            break;
         }
      }

      free_dcr(dcr);
      jcr->dcr = NULL;
   }

   UnlockRes();

#ifdef xxx
   if (jcr->dcr) {
      Pmsg1(000, "free_dcr=%p\n", jcr->dcr);
      free_dcr(jcr->dcr);
      jcr->dcr = NULL;
   }
#endif
   free_plugins(jcr);
   free_jcr(jcr);
   init_done = true;
   return NULL;
}


/* Clean up and then exit */
void terminate_stored(int sig)
{
   static bool in_here = false;
   DEVRES *device;
   JCR *jcr;

   if (in_here) {                     /* prevent loops */
      bmicrosleep(2, 0);              /* yield */
      exit(1);
   }
   in_here = true;
   debug_level = 0;                   /* turn off any debug */
   stop_watchdog();

   if (sig == SIGTERM || sig == SIGINT) { /* normal shutdown request? or ^C */
      /*
       * This is a normal shutdown request. We wiffle through
       *   all open jobs canceling them and trying to wake
       *   them up so that they will report back the correct
       *   volume status.
       */
      foreach_jcr(jcr) {
         BSOCK *fd;
         if (jcr->JobId == 0) {
            free_jcr(jcr);
            continue;                 /* ignore console */
         }
         if (jcr->dcr) {
            /* Make sure no device remains locked */
            generate_plugin_event(jcr, bsdEventDeviceClose, jcr->dcr);
         }
         jcr->setJobStatus(JS_Canceled);
         fd = jcr->file_bsock;
         if (fd) {
            fd->set_timed_out();
            jcr->my_thread_send_signal(TIMEOUT_SIGNAL);
            Dmsg1(100, "term_stored killing JobId=%d\n", jcr->JobId);
            /* ***FIXME*** wiffle through all dcrs */
            if (jcr->dcr && jcr->dcr->dev && jcr->dcr->dev->blocked()) {
               pthread_cond_broadcast(&jcr->dcr->dev->wait_next_vol);
               Dmsg1(100, "JobId=%u broadcast wait_device_release\n", (uint32_t)jcr->JobId);
               pthread_cond_broadcast(&wait_device_release);
            }
            if (jcr->read_dcr && jcr->read_dcr->dev && jcr->read_dcr->dev->blocked()) {
               pthread_cond_broadcast(&jcr->read_dcr->dev->wait_next_vol);
               pthread_cond_broadcast(&wait_device_release);
            }
            bmicrosleep(0, 50000);
         }
         free_jcr(jcr);
      }
      bmicrosleep(0, 500000);         /* give them 1/2 sec to clean up */
   }

   if (!test_config) {
      write_state_file(me->working_directory,
                       "bacula-sd", get_first_port_host_order(me->sdaddrs));
      if (make_pid_file) {
         delete_pid_file(me->pid_directory,
                         "bacula-sd", get_first_port_host_order(me->sdaddrs));
      }
   }

   Dmsg1(200, "In terminate_stored() sig=%d\n", sig);

   unload_plugins();
   free_volume_lists();

   free_daemon_message_queue();

   foreach_res(device, R_DEVICE) {
      Dmsg2(10, "Term device %s %s\n", device->hdr.name, device->device_name);
      if (device->dev) {
         device->dev->clear_volhdr();
         device->dev->term(NULL);
         device->dev = NULL;
      } else {
         Dmsg2(10, "No dev structure %s %s\n", device->hdr.name, device->device_name);
      }
   }
   if (server_tid_valid) {
      server_tid_valid = false;
      bnet_stop_thread_server(server_tid);
   }

   if (configfile) {
      free(configfile);
      configfile = NULL;
   }
   if (config) {
      delete config;
      config = NULL;
   }

   if (chk_dbglvl(10)) {
      print_memory_pool_stats();
   }
   term_msg();
   cleanup_crypto();
   term_reservations_lock();
   free(res_head);
   res_head = NULL;
   close_memory_pool();
   lmgr_cleanup_main();

   sm_dump(false);                    /* dump orphaned buffers */
   exit(sig);
}
