/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2019 Kern Sibbald

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
/**
 *   Bacula Director -- restore.c -- responsible for restoring files
 *
 *     Written by Kern Sibbald, November MM
 *
 *    This routine is run as a separate thread.
 *
 * Current implementation is Catalog verification only (i.e. no
 *  verification versus tape).
 *
 *  Basic tasks done here:
 *     Open DB
 *     Open Message Channel with Storage daemon to tell him a job will be starting.
 *     Open connection with File daemon and pass him commands
 *       to do the restore.
 *     Update the DB according to what files where restored????
 *
 */


#include "bacula.h"
#include "dird.h"
#include "lib/ini.h"

/* Imported variables */
extern struct s_kw ReplaceOptions[];

/* Commands sent to File daemon */
static char restorecmd[]  = "restore %sreplace=%c prelinks=%d where=%s\n";
static char restorecmdR[] = "restore %sreplace=%c prelinks=%d regexwhere=%s\n";
static char storaddr[]    = "storage address=%s port=%d ssl=%d Authorization=%s\n";

/* Responses received from File daemon */
static char OKrestore[]   = "2000 OK restore\n";
static char OKstore[]     = "2000 OK storage\n";
static char OKstoreend[]  = "2000 OK storage end\n";

/* Responses received from the Storage daemon */
static char OKbootstrap[] = "3000 OK bootstrap\n";

static void get_restore_params(JCR *jcr, POOL_MEM &ret_where, char *ret_replace, char **ret_restorecmd)
{
   char replace, *where, *cmd;
   char empty = '\0';

   /* Build the restore command */

   if (jcr->replace != 0) {
      replace = jcr->replace;
   } else if (jcr->job->replace != 0) {
      replace = jcr->job->replace;
   } else {
      replace = REPLACE_ALWAYS;       /* always replace */
   }

   if (jcr->RegexWhere) {
      where = jcr->RegexWhere;             /* override */
      cmd = restorecmdR;
   } else if (jcr->job->RegexWhere) {
      where = jcr->job->RegexWhere;   /* no override take from job */
      cmd = restorecmdR;

   } else if (jcr->where) {
      where = jcr->where;             /* override */
      cmd = restorecmd;
   } else if (jcr->job->RestoreWhere) {
      where = jcr->job->RestoreWhere; /* no override take from job */
      cmd = restorecmd;

   } else {                           /* nothing was specified */
      where = &empty;                 /* use default */
      cmd   = restorecmd;
   }

   pm_strcpy(ret_where, where); /* Where can be a local variable */
   if (ret_replace) {
      *ret_replace = replace;
   }
   if (ret_restorecmd) {
      *ret_restorecmd = cmd;
   }
}

static void build_restore_command(JCR *jcr, POOL_MEM &ret)
{
   POOL_MEM where;
   char replace;
   char *cmd;
   char files[100];

   get_restore_params(jcr, where, &replace, &cmd);

   jcr->prefix_links = jcr->job->PrefixLinks;

   bash_spaces(where.c_str());
   if (jcr->FDVersion < 7) {
      Mmsg(ret, cmd, "", replace, jcr->prefix_links, where.c_str());
   } else {
      snprintf(files, sizeof(files), "files=%d ", jcr->ExpectedFiles);
      Mmsg(ret, cmd, files, replace, jcr->prefix_links, where.c_str());
   }
   unbash_spaces(where.c_str());
}

struct bootstrap_info
{
   FILE *bs;
   UAContext *ua;
   char storage[MAX_NAME_LENGTH+1];
};

#define UA_CMD_SIZE 1000

/**
 * Open the bootstrap file and find the first Storage=
 * Returns ok if able to open
 * It fills the storage name (should be the first line)
 * and the file descriptor to the bootstrap file,
 * it should be used for next operations, and need to be closed
 * at the end.
 */
static bool open_bootstrap_file(JCR *jcr, bootstrap_info &info)
{
   FILE *bs;
   UAContext *ua;
   info.bs = NULL;
   info.ua = NULL;

   if (!jcr->RestoreBootstrap) {
      return false;
   }
   strncpy(info.storage, jcr->rstore->name(), MAX_NAME_LENGTH);

   bs = bfopen(jcr->RestoreBootstrap, "rb");
   if (!bs) {
      berrno be;
      Jmsg(jcr, M_FATAL, 0, _("Could not open bootstrap file %s: ERR=%s\n"),
         jcr->RestoreBootstrap, be.bstrerror());
      jcr->setJobStatus(JS_ErrorTerminated);
      return false;
   }

   ua = new_ua_context(jcr);
   ua->cmd = check_pool_memory_size(ua->cmd, UA_CMD_SIZE+1);
   while (!fgets(ua->cmd, UA_CMD_SIZE, bs)) {
      parse_ua_args(ua);
      if (ua->argc != 1) {
         continue;
      }
      if (!strcasecmp(ua->argk[0], "Storage")) {
         strncpy(info.storage, ua->argv[0], MAX_NAME_LENGTH);
         break;
      }
   }
   info.bs = bs;
   info.ua = ua;
   fseek(bs, 0, SEEK_SET);      /* return to the top of the file */
   return true;
}

/**
 * This function compare the given storage name with the
 * the current one. We compare the name and the address:port.
 * Returns true if we use the same storage.
 */
static bool is_on_same_storage(JCR *jcr, char *new_one)
{
   STORE *new_store;

   /* with old FD, we send the whole bootstrap to the storage */
   if (jcr->FDVersion < 2) {
      return true;
   }
   /* we are in init loop ? shoudn't fail here */
   if (!*new_one) {
      return true;
   }
   /* same name */
   if (!strcmp(new_one, jcr->rstore->name())) {
      return true;
   }
   new_store = (STORE *)GetResWithName(R_STORAGE, new_one);
   if (!new_store) {
      Jmsg(jcr, M_WARNING, 0,
           _("Could not get storage resource '%s'.\n"), new_one);
      /* If not storage found, use last one */
      return true;
   }
   /* if Port and Hostname/IP are same, we are talking to the same
    * Storage Daemon
    */
   if (jcr->rstore->SDport != new_store->SDport ||
       strcmp(jcr->rstore->address, new_store->address))
   {
      return false;
   }
   return true;
}

/**
 * Check if the current line contains Storage="xxx", and compare the
 * result to the current storage. We use UAContext to analyse the bsr
 * string.
 *
 * Returns true if we need to change the storage, and it set the new
 * Storage resource name in "storage" arg.
 */
static bool check_for_new_storage(JCR *jcr, bootstrap_info &info)
{
   UAContext *ua = info.ua;
   parse_ua_args(ua);
   if (ua->argc != 1) {
      return false;
   }
   if (!strcasecmp(ua->argk[0], "Storage")) {
      /* Continue if this is a volume from the same storage. */
      if (is_on_same_storage(jcr, ua->argv[0])) {
         return false;
      }
      /* note the next storage name */
      strncpy(info.storage, ua->argv[0], MAX_NAME_LENGTH);
      Dmsg1(5, "Change storage to %s\n", info.storage);
      return true;
   }
   return false;
}

/**
 * Send bootstrap file to Storage daemon section by section.
 */
static bool send_bootstrap_file(JCR *jcr, BSOCK *sock,
                                bootstrap_info &info)
{
   boffset_t pos;
   const char *bootstrap = "bootstrap\n";
   UAContext *ua = info.ua;
   FILE *bs = info.bs;

   Dmsg1(400, "send_bootstrap_file: %s\n", jcr->RestoreBootstrap);
   if (!jcr->RestoreBootstrap) {
      return false;
   }
   sock->fsend(bootstrap);
   pos = ftello(bs);
   while(fgets(ua->cmd, UA_CMD_SIZE, bs)) {
      if (check_for_new_storage(jcr, info)) {
         /* Otherwise, we need to contact another storage daemon.
          * Reset bs to the beginning of the current segment.
          */
         fseeko(bs, pos, SEEK_SET);
         break;
      }
      sock->fsend("%s", ua->cmd);
      pos = ftello(bs);
   }
   sock->signal(BNET_EOD);
   return true;
}

#define MAX_TRIES 6 * 360   /* 6 hours */

/**
 * Change the read storage resource for the current job.
 */
static bool select_rstore(JCR *jcr, bootstrap_info &info)
{
   USTORE ustore;
   int i;


   if (!strcmp(jcr->rstore->name(), info.storage)) {
      return true;                 /* same SD nothing to change */
   }

   if (!(ustore.store = (STORE *)GetResWithName(R_STORAGE,info.storage))) {
      Jmsg(jcr, M_FATAL, 0,
           _("Could not get storage resource '%s'.\n"), info.storage);
      jcr->setJobStatus(JS_ErrorTerminated);
      return false;
   }

   /*
    * This releases the store_bsock between calls to the SD.
    *  I think.
    */
   free_bsock(jcr->store_bsock);

   /*
    * release current read storage and get a new one
    */
   dec_read_store(jcr);
   free_rstorage(jcr);
   set_rstorage(jcr, &ustore);
   jcr->setJobStatus(JS_WaitSD);
   /*
    * Wait for up to 6 hours to increment read stoage counter
    */
   for (i=0; i < MAX_TRIES; i++) {
      /* try to get read storage counter incremented */
      if (inc_read_store(jcr)) {
         jcr->setJobStatus(JS_Running);
         return true;
      }
      bmicrosleep(10, 0);       /* sleep 10 secs */
      if (job_canceled(jcr)) {
         free_rstorage(jcr);
         return false;
      }
   }
   /* Failed to inc_read_store() */
   free_rstorage(jcr);
   Jmsg(jcr, M_FATAL, 0,
      _("Could not acquire read storage lock for \"%s\""), info.storage);
   return false;
}

/*
 * Clean the bootstrap_info struct
 */
static void close_bootstrap_file(bootstrap_info &info)
{
   if (info.bs) {
      fclose(info.bs);
   }
   if (info.ua) {
      free_ua_context(info.ua);
   }
}

/**
 * The bootstrap is stored in a file, so open the file, and loop
 *   through it processing each storage device in turn. If the
 *   storage is different from the prior one, we open a new connection
 *   to the new storage and do a restore for that part.
 * This permits handling multiple storage daemons for a single
 *   restore.  E.g. your Full is stored on tape, and Incrementals
 *   on disk.
 */
bool restore_bootstrap(JCR *jcr)
{
   int tls_need = BNET_TLS_NONE;
   BSOCK *fd = NULL;
   BSOCK *sd;
   char *store_address;
   uint32_t store_port;
   bool first_time = true;
   bootstrap_info info;
   POOL_MEM restore_cmd(PM_MESSAGE);
   bool ret = false;


   /* Open the bootstrap file */
   if (!open_bootstrap_file(jcr, info)) {
      goto bail_out;
   }
   /* Read the bootstrap file */
   while (!feof(info.bs)) {

      if (!select_rstore(jcr, info)) {
         goto bail_out;
      }

      /**
       * Open a message channel connection with the Storage
       * daemon. This is to let him know that our client
       * will be contacting him for a backup  session.
       *
       */
      Dmsg0(10, "Open connection with storage daemon\n");
      jcr->setJobStatus(JS_WaitSD);
      /*
       * Start conversation with Storage daemon
       */
      if (!connect_to_storage_daemon(jcr, 10, SDConnectTimeout, 1)) {
         goto bail_out;
      }
      sd = jcr->store_bsock;
      /*
       * Now start a job with the Storage daemon
       */
      if (!start_storage_daemon_job(jcr, jcr->rstorage, NULL)) {
         goto bail_out;
      }

      if (first_time) {
         /*
          * Start conversation with File daemon
          */
         jcr->setJobStatus(JS_WaitFD);
         jcr->keep_sd_auth_key = true; /* don't clear the sd_auth_key now */
         if (!connect_to_file_daemon(jcr, 10, FDConnectTimeout, 1)) {
            goto bail_out;
         }
         fd = jcr->file_bsock;
         build_restore_command(jcr, restore_cmd);
      }

      jcr->setJobStatus(JS_Running);

      /*
       * Send the bootstrap file -- what Volumes/files to restore
       */
      if (!send_bootstrap_file(jcr, sd, info) ||
          !response(jcr, sd, OKbootstrap, "Bootstrap", DISPLAY_ERROR)) {
         goto bail_out;
      }

      if (jcr->sd_calls_client) {
         /*
          * SD must call "client" i.e. FD
          */
         if (jcr->FDVersion < 10) {
            Jmsg(jcr, M_FATAL, 0, _("The File daemon does not support SDCallsClient.\n"));
            goto bail_out;
         }
         if (!send_client_addr_to_sd(jcr)) {
            goto bail_out;
         }
         if (!run_storage_and_start_message_thread(jcr, sd)) {
            goto bail_out;
         }

         store_address = jcr->rstore->address;  /* dummy */
         store_port = 0;                        /* flag that SD calls FD */

      } else {
         /*
          * Default case where FD must call the SD
          */
         if (!run_storage_and_start_message_thread(jcr, sd)) {
            goto bail_out;
         }

         /*
          * send Storage daemon address to the File daemon,
          *   then wait for File daemon to make connection
          *   with Storage daemon.
          */
         if (jcr->rstore->SDDport == 0) {
            jcr->rstore->SDDport = jcr->rstore->SDport;
         }

         store_address = get_storage_address(jcr->client, jcr->rstore);
         store_port = jcr->rstore->SDDport;
      }

      /* TLS Requirement */
      if (jcr->rstore->tls_enable) {
         if (jcr->rstore->tls_require) {
            tls_need = BNET_TLS_REQUIRED;
         } else {
            tls_need = BNET_TLS_OK;
         }
      }

      /*
       * Send storage address to FD
       *  if port==0 FD must wait for SD to call it.
       */
      fd->fsend(storaddr, store_address, store_port, tls_need, jcr->sd_auth_key);
      memset(jcr->sd_auth_key, 0, strlen(jcr->sd_auth_key));
      Dmsg1(6, "dird>filed: %s\n", fd->msg);
      if (!response(jcr, fd, OKstore, "Storage", DISPLAY_ERROR)) {
         goto bail_out;
      }

      /* Declare the job started to start the MaxRunTime check */
      jcr->setJobStarted();

      /* Only pass "global" commands to the FD once */
      if (first_time) {
         first_time = false;
         if (!send_runscripts_commands(jcr)) {
            goto bail_out;
         }
         if (!send_component_info(jcr)) {
            Pmsg0(000, "FAIL: Send component info\n");
            goto bail_out;
         }
         if (!send_restore_objects(jcr)) {
            Pmsg0(000, "FAIL: Send restore objects\n");
            goto bail_out;
         }
      }

      fd->fsend("%s", restore_cmd.c_str());
      if (!response(jcr, fd, OKrestore, "Restore", DISPLAY_ERROR)) {
         goto bail_out;
      }

      if (jcr->FDVersion < 2) { /* Old FD */
         break;                 /* we do only one loop */
      } else {
         if (!response(jcr, fd, OKstoreend, "Store end", DISPLAY_ERROR)) {
            goto bail_out;
         }
         wait_for_storage_daemon_termination(jcr);
      }
   } /* the whole boostrap has been send */

   if (fd && jcr->FDVersion >= 2) {
      fd->fsend("endrestore");
   }

   ret = true;

bail_out:
   close_bootstrap_file(info);
   return ret;
}

/**
 * Do a restore of the specified files
 *
 *  Returns:  0 on failure
 *            1 on success
 */
bool do_restore(JCR *jcr)
{
   JOB_DBR rjr;                       /* restore job record */
   int stat;

   free_wstorage(jcr);                /* we don't write */

   if (!allow_duplicate_job(jcr)) {
      goto bail_out;
   }

   memset(&rjr, 0, sizeof(rjr));
   jcr->jr.JobLevel = L_FULL;         /* Full restore */
   if (!db_update_job_start_record(jcr, jcr->db, &jcr->jr)) {
      Jmsg(jcr, M_FATAL, 0, "%s", db_strerror(jcr->db));
      goto bail_out;
   }
   Dmsg0(20, "Updated job start record\n");

   Dmsg1(20, "RestoreJobId=%d\n", jcr->job->RestoreJobId);

   if (!jcr->RestoreBootstrap) {
      Jmsg(jcr, M_FATAL, 0, _("Cannot restore without a bootstrap file.\n"
          "You probably ran a restore job directly. All restore jobs must\n"
          "be run using the restore command.\n"));
      goto bail_out;
   }


   /* Print Job Start message */
   Jmsg(jcr, M_INFO, 0, _("Start Restore Job %s\n"), jcr->Job);

   if (jcr->JobIds) {
      Jmsg(jcr, M_INFO, 0, _("Restoring files from JobId(s) %s\n"), jcr->JobIds);
   }

   if (jcr->client) {
      jcr->sd_calls_client = jcr->client->sd_calls_client;
   }

   /* Read the bootstrap file and do the restore */
   if (!restore_bootstrap(jcr)) {
      goto bail_out;
   }

   /* Wait for Job Termination */
   stat = wait_for_job_termination(jcr);
   restore_cleanup(jcr, stat);
   return true;

bail_out:
   restore_cleanup(jcr, JS_ErrorTerminated);
   return false;
}

/* Create a Plugin Config RestoreObject, will be sent
 * at restore time to the Plugin
 */
static void plugin_create_restoreobject(JCR *jcr, plugin_config_item *elt)
{
   ROBJECT_DBR ro;
   memset(&ro, 0, sizeof(ro));
   ro.FileIndex = 1;
   ro.JobId = jcr->JobId;
   ro.FileType = FT_PLUGIN_CONFIG_FILLED;
   ro.object_index = 1;
   ro.object_full_len = ro.object_len = strlen(elt->content);
   ro.object_compression = 0;
   ro.plugin_name = elt->plugin_name;
   ro.object_name = (char*)INI_RESTORE_OBJECT_NAME;
   ro.object = elt->content;
   db_create_restore_object_record(jcr, jcr->db, &ro);
   Dmsg1(50, "Creating restore object for %s\n", elt->plugin_name);
}

bool do_restore_init(JCR *jcr)
{
   /* Will add RestoreObject used for the Plugin configuration */
   if (jcr->plugin_config) {

      plugin_config_item *elt;
      foreach_alist(elt, jcr->plugin_config) {
         plugin_create_restoreobject(jcr, elt);
         free_plugin_config_item(elt);
      }

      delete jcr->plugin_config;
      jcr->plugin_config = NULL;
   }
   free_wstorage(jcr);
   return true;
}

/**
 * Release resources allocated during restore.
 *
 */
void restore_cleanup(JCR *jcr, int TermCode)
{
   POOL_MEM where;
   char creplace;
   const char *replace;
   char sdt[MAX_TIME_LENGTH], edt[MAX_TIME_LENGTH];
   char ec1[30], ec2[30], ec3[30], ec4[30], elapsed[50];
   char term_code[100], fd_term_msg[100], sd_term_msg[100];
   const char *term_msg;
   int msg_type = M_INFO;
   double kbps;
   utime_t RunTime;

   Dmsg0(20, "In restore_cleanup\n");
   update_job_end(jcr, TermCode);

   if (jcr->component_fd) {
      fclose(jcr->component_fd);
      jcr->component_fd = NULL;
   }
   if (jcr->component_fname && *jcr->component_fname) {
      unlink(jcr->component_fname);
   }
   free_and_null_pool_memory(jcr->component_fname);

   if (jcr->unlink_bsr && jcr->RestoreBootstrap) {
      unlink(jcr->RestoreBootstrap);
      jcr->unlink_bsr = false;
   }

   if (job_canceled(jcr)) {
      cancel_storage_daemon_job(jcr);
   }

   switch (TermCode) {
   case JS_Terminated:
      if (jcr->ExpectedFiles > jcr->jr.JobFiles) {
         term_msg = _("Restore OK -- warning file count mismatch");

      } else if (jcr->JobErrors > 0 || jcr->SDErrors > 0) {
         term_msg = _("Restore OK -- with errors");

      } else {
         term_msg = _("Restore OK");
      }
      break;
   case JS_Warnings:
         term_msg = _("Restore OK -- with warnings");
         break;
   case JS_FatalError:
   case JS_ErrorTerminated:
      term_msg = _("*** Restore Error ***");
      msg_type = M_ERROR;          /* Generate error message */
      terminate_sd_msg_chan_thread(jcr);
      break;
   case JS_Canceled:
      term_msg = _("Restore Canceled");
      terminate_sd_msg_chan_thread(jcr);
      break;
   case JS_Incomplete:
      term_msg = _("Restore Incomplete");
      break;
   default:
      term_msg = term_code;
      sprintf(term_code, _("Inappropriate term code: %c\n"), TermCode);
      break;
   }
   bstrftimes(sdt, sizeof(sdt), jcr->jr.StartTime);
   bstrftimes(edt, sizeof(edt), jcr->jr.EndTime);

   RunTime = jcr->jr.EndTime - jcr->jr.StartTime;
   if (RunTime <= 0) {
      RunTime = 1;
   }
   kbps = (double)jcr->jr.JobBytes / (1000.0 * (double)RunTime);
   if (kbps < 0.05) {
      kbps = 0;
   }

   get_restore_params(jcr, where, &creplace, NULL);

   replace = ReplaceOptions[0].name;     /* default */
   for (int i=0; ReplaceOptions[i].name; i++) {
      if (ReplaceOptions[i].token == (int)creplace) {
         replace = ReplaceOptions[i].name;
      }
   }

   jobstatus_to_ascii(jcr->FDJobStatus, fd_term_msg, sizeof(fd_term_msg));
   jobstatus_to_ascii(jcr->SDJobStatus, sd_term_msg, sizeof(sd_term_msg));

   Jmsg(jcr, msg_type, 0, _("%s %s %s (%s):\n"
"  Build OS:               %s %s %s\n"
"  JobId:                  %d\n"
"  Job:                    %s\n"
"  Restore Client:         %s\n"
"  Where:                  %s\n"
"  Replace:                %s\n"
"  Start time:             %s\n"
"  End time:               %s\n"
"  Elapsed time:           %s\n"
"  Files Expected:         %s\n"
"  Files Restored:         %s\n"
"  Bytes Restored:         %s (%sB)\n"
"  Rate:                   %.1f KB/s\n"
"  FD Errors:              %d\n"
"  FD termination status:  %s\n"
"  SD termination status:  %s\n"
"  Termination:            %s\n\n"),
        BACULA, my_name, VERSION, LSMDATE,
        HOST_OS, DISTNAME, DISTVER,
        jcr->jr.JobId,
        jcr->jr.Job,
        jcr->client->name(),
        where.c_str(),
        replace,
        sdt,
        edt,
        edit_utime(RunTime, elapsed, sizeof(elapsed)),
        edit_uint64_with_commas((uint64_t)jcr->ExpectedFiles, ec1),
        edit_uint64_with_commas((uint64_t)jcr->jr.JobFiles, ec2),
        edit_uint64_with_commas(jcr->jr.JobBytes, ec3), edit_uint64_with_suffix(jcr->jr.JobBytes, ec4),
        (float)kbps,
        jcr->JobErrors,
        fd_term_msg,
        sd_term_msg,
        term_msg);

   Dmsg0(20, "Leaving restore_cleanup\n");
}
