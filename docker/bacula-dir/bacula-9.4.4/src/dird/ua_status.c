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
 *   Bacula Director -- User Agent Status Command
 *
 *     Kern Sibbald, August MMI
 */


#include "bacula.h"
#include "dird.h"

extern void *start_heap;
extern utime_t last_reload_time;

static void list_scheduled_jobs(UAContext *ua);
static void llist_scheduled_jobs(UAContext *ua);
static void list_running_jobs(UAContext *ua);
static void list_terminated_jobs(UAContext *ua);
static void do_storage_status(UAContext *ua, STORE *store, char *cmd);
static void do_client_status(UAContext *ua, CLIENT *client, char *cmd);
static void do_director_status(UAContext *ua);
static void do_all_status(UAContext *ua);
void status_slots(UAContext *ua, STORE *store);
void status_content(UAContext *ua, STORE *store);

static char OKqstatus[]   = "1000 OK .status\n";
static char DotStatusJob[] = "JobId=%s JobStatus=%c JobErrors=%d\n";

/*
 * .status command
 */

bool dot_status_cmd(UAContext *ua, const char *cmd)
{
   STORE *store;
   CLIENT *client;
   JCR* njcr = NULL;
   s_last_job* job;
   char ed1[50];

   Dmsg2(20, "status=\"%s\" argc=%d\n", cmd, ua->argc);

   if (ua->argc < 3) {
      ua->send_msg("1900 Bad .status command, missing arguments.\n");
      return false;
   }

   if (strcasecmp(ua->argk[1], "dir") == 0) {
      if (strcasecmp(ua->argk[2], "current") == 0) {
         ua->send_msg(OKqstatus, ua->argk[2]);
         foreach_jcr(njcr) {
            if (!njcr->is_internal_job() && acl_access_ok(ua, Job_ACL, njcr->job->name())) {
               ua->send_msg(DotStatusJob, edit_int64(njcr->JobId, ed1),
                        njcr->JobStatus, njcr->JobErrors);
            }
         }
         endeach_jcr(njcr);
      } else if (strcasecmp(ua->argk[2], "last") == 0) {
         ua->send_msg(OKqstatus, ua->argk[2]);
         if ((last_jobs) && (last_jobs->size() > 0)) {
            job = (s_last_job*)last_jobs->last();
            if (acl_access_ok(ua, Job_ACL, job->Job)) {
               ua->send_msg(DotStatusJob, edit_int64(job->JobId, ed1),
                     job->JobStatus, job->Errors);
            }
         }
      } else if (strcasecmp(ua->argk[2], "header") == 0) {
          list_dir_status_header(ua);
      } else if (strcasecmp(ua->argk[2], "scheduled") == 0) {
          list_scheduled_jobs(ua);
      } else if (strcasecmp(ua->argk[2], "running") == 0) {
          list_running_jobs(ua);
      } else if (strcasecmp(ua->argk[2], "terminated") == 0) {
          list_terminated_jobs(ua);
      } else {
         ua->send_msg("1900 Bad .status command, wrong argument.\n");
         return false;
      }
   } else if (strcasecmp(ua->argk[1], "client") == 0) {
      client = get_client_resource(ua, JT_BACKUP_RESTORE);
      if (client) {
         Dmsg2(200, "Client=%s arg=%s\n", client->name(), NPRT(ua->argk[2]));
         do_client_status(ua, client, ua->argk[2]);
      }
   } else if (strcasecmp(ua->argk[1], "storage") == 0) {
      store = get_storage_resource(ua, false /*no default*/, true/*unique*/);
      if (!store) {
         ua->send_msg("1900 Bad .status command, wrong argument.\n");
         return false;
      }
      do_storage_status(ua, store, ua->argk[2]);
   } else {
      ua->send_msg("1900 Bad .status command, wrong argument.\n");
      return false;
   }

   return true;
}

/* Test the network between FD and SD */
static int do_network_status(UAContext *ua)
{
   CLIENT *client = NULL;
   USTORE  store;
   JCR *jcr = ua->jcr;
   char *store_address, ed1[50];
   uint32_t store_port;
   uint64_t nb = 50 * 1024 * 1024;
   POOL_MEM buf;

   int i = find_arg_with_value(ua, "bytes");
   if (i > 0) {
      if (!size_to_uint64(ua->argv[i], strlen(ua->argv[i]), &nb)) {
         return 1;
      }
   }
   
   client = get_client_resource(ua, JT_BACKUP_RESTORE);
   if (!client) {
      return 1;
   }

   store.store = get_storage_resource(ua, false, true);
   if (!store.store) {
      return 1;
   }

   jcr->client = client;
   set_wstorage(jcr, &store);

   if (!ua->api) {
      ua->send_msg(_("Connecting to Storage %s at %s:%d\n"),
                   store.store->name(), store.store->address, store.store->SDport);
   }

   if (!connect_to_storage_daemon(jcr, 10, SDConnectTimeout, 1)) {
      ua->error_msg(_("Failed to connect to Storage.\n"));
      goto bail_out;
   }

   if (!start_storage_daemon_job(jcr, NULL, NULL)) {
      goto bail_out;
   }

   /*
    * Note startup sequence of SD/FD is different depending on
    *  whether the SD listens (normal) or the SD calls the FD.
    */
   if (!client->sd_calls_client) {
      if (!run_storage_and_start_message_thread(jcr, jcr->store_bsock)) {
         goto bail_out;
      }
   } /* Else it's done in init_storage_job() */

   if (!ua->api) {
      ua->send_msg(_("Connecting to Client %s at %s:%d\n"),
                   client->name(), client->address(buf.addr()), client->FDport);
   }

   if (!connect_to_file_daemon(jcr, 1, 15, 0)) {
      ua->error_msg(_("Failed to connect to Client.\n"));
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
      if (!run_storage_and_start_message_thread(jcr, jcr->store_bsock)) {
         goto bail_out;
      }

      store_address = store.store->address;  /* dummy */
      store_port = 0;                        /* flag that SD calls FD */

   } else {
      /*
       * send Storage daemon address to the File daemon,
       *   then wait for File daemon to make connection
       *   with Storage daemon.
       */
      if (store.store->SDDport == 0) {
         store.store->SDDport = store.store->SDport;
      }

      store_address = get_storage_address(jcr->client, store.store);
      store_port = store.store->SDDport;
   }

   if (!send_store_addr_to_fd(jcr, store.store, store_address, store_port)) {
      goto bail_out;
   }

   if (!ua->api) {
      ua->info_msg(_("Running network test between Client=%s and Storage=%s with %sB ...\n"),
                   client->name(), store.store->name(), edit_uint64_with_suffix(nb, ed1));
   }

   if (!jcr->file_bsock->fsend("testnetwork bytes=%lld\n", nb)) {
      goto bail_out;
   }

   while (jcr->file_bsock->recv() > 0) {
      ua->info_msg(jcr->file_bsock->msg);
   }
   
bail_out:
   if (jcr->file_bsock) {
      jcr->file_bsock->signal(BNET_TERMINATE);
   }
   if (jcr->store_bsock) {
      jcr->store_bsock->signal(BNET_TERMINATE);
   }
   wait_for_storage_daemon_termination(jcr);

   free_bsock(jcr->file_bsock);
   free_bsock(jcr->store_bsock);

   jcr->client = NULL;
   free_wstorage(jcr);
   return 1;
}

/* This is the *old* command handler, so we must return
 *  1 or it closes the connection
 */
int qstatus_cmd(UAContext *ua, const char *cmd)
{
   dot_status_cmd(ua, cmd);
   return 1;
}

/*
 * status command
 */
int status_cmd(UAContext *ua, const char *cmd)
{
   STORE *store;
   CLIENT *client;
   int item, i;

   Dmsg1(20, "status:%s:\n", cmd);

   for (i=1; i<ua->argc; i++) {
      if (strcasecmp(ua->argk[i], NT_("network")) == 0) {
         do_network_status(ua);
         return 1;
      } else if (strcasecmp(ua->argk[i], NT_("schedule")) == 0 ||
          strcasecmp(ua->argk[i], NT_("scheduled")) == 0) {
         llist_scheduled_jobs(ua);
         return 1;
      } else if (strcasecmp(ua->argk[i], NT_("all")) == 0) {
         do_all_status(ua);
         return 1;
      } else if (strcasecmp(ua->argk[i], NT_("dir")) == 0 ||
                 strcasecmp(ua->argk[i], NT_("director")) == 0) {
         do_director_status(ua);
         return 1;
      } else if (strcasecmp(ua->argk[i], NT_("client")) == 0) {
         client = get_client_resource(ua, JT_BACKUP_RESTORE);
         if (client) {
            do_client_status(ua, client, NULL);
         }
         return 1;
      } else {
         store = get_storage_resource(ua, false/*no default*/, true/*unique*/);
         if (store) {
            if (find_arg(ua, NT_("slots")) > 0) {
               status_slots(ua, store);
            } else {
               do_storage_status(ua, store, NULL);
            }
         }
         return 1;
      }
   }
   /* If no args, ask for status type */
   if (ua->argc == 1) {
       char prmt[MAX_NAME_LENGTH];

      start_prompt(ua, _("Status available for:\n"));
      add_prompt(ua, NT_("Director"));
      add_prompt(ua, NT_("Storage"));
      add_prompt(ua, NT_("Client"));
      add_prompt(ua, NT_("Scheduled"));
      add_prompt(ua, NT_("Network"));
      add_prompt(ua, NT_("All"));
      Dmsg0(20, "do_prompt: select daemon\n");
      if ((item=do_prompt(ua, "",  _("Select daemon type for status"), prmt, sizeof(prmt))) < 0) {
         return 1;
      }
      Dmsg1(20, "item=%d\n", item);
      switch (item) {
      case 0:                         /* Director */
         do_director_status(ua);
         break;
      case 1:
         store = select_storage_resource(ua, true/*unique*/);
         if (store) {
            do_storage_status(ua, store, NULL);
         }
         break;
      case 2:
         client = select_client_resource(ua, JT_BACKUP_RESTORE);
         if (client) {
            do_client_status(ua, client, NULL);
         }
         break;
      case 3:
         llist_scheduled_jobs(ua);
         break;
      case 4:
         do_network_status(ua);
         break;
      case 5:
         do_all_status(ua);
         break;
      default:
         break;
      }
   }
   return 1;
}

static void do_all_status(UAContext *ua)
{
   STORE *store, **unique_store;
   CLIENT *client, **unique_client;
   POOL_MEM buf1, buf2;
   int i, j;
   bool found;

   do_director_status(ua);

   /* Count Storage items */
   LockRes();
   i = 0;
   foreach_res(store, R_STORAGE) {
      i++;
   }
   unique_store = (STORE **) malloc(i * sizeof(STORE));
   /* Find Unique Storage address/port */
   i = 0;
   foreach_res(store, R_STORAGE) {
      found = false;
      if (!acl_access_ok(ua, Storage_ACL, store->name())) {
         continue;
      }
      for (j=0; j<i; j++) {
         if (strcmp(unique_store[j]->address, store->address) == 0 &&
             unique_store[j]->SDport == store->SDport) {
            found = true;
            break;
         }
      }
      if (!found) {
         unique_store[i++] = store;
         Dmsg2(40, "Stuffing: %s:%d\n", store->address, store->SDport);
      }
   }
   UnlockRes();

   /* Call each unique Storage daemon */
   for (j=0; j<i; j++) {
      do_storage_status(ua, unique_store[j], NULL);
   }
   free(unique_store);

   /* Count Client items */
   LockRes();
   i = 0;
   foreach_res(client, R_CLIENT) {
      i++;
   }
   unique_client = (CLIENT **)malloc(i * sizeof(CLIENT));
   /* Find Unique Client address/port */
   i = 0;
   foreach_res(client, R_CLIENT) {
      found = false;
      if (!acl_access_client_ok(ua, client->name(), JT_BACKUP_RESTORE)) {
         continue;
      }
      for (j=0; j<i; j++) {
         if (strcmp(unique_client[j]->address(buf1.addr()), client->address(buf2.addr())) == 0 &&
             unique_client[j]->FDport == client->FDport) {
            found = true;
            break;
         }
      }
      if (!found) {
         unique_client[i++] = client;
         Dmsg2(40, "Stuffing: %s:%d\n", client->address(buf1.addr()), client->FDport);
      }
   }
   UnlockRes();

   /* Call each unique File daemon */
   for (j=0; j<i; j++) {
      do_client_status(ua, unique_client[j], NULL);
   }
   free(unique_client);

}

static void api_list_dir_status_header(UAContext *ua)
{
   OutputWriter wt(ua->api_opts);
   wt.start_group("header");
   wt.get_output(
      OT_STRING, "name",        my_name,
      OT_STRING, "version",     VERSION " (" BDATE ")",
      OT_STRING, "uname",       HOST_OS " " DISTNAME " " DISTVER,
      OT_UTIME,  "started",     daemon_start_time,
      OT_UTIME,  "reloaded",    last_reload_time,
      OT_INT64,  "pid",         (int64_t)getpid(),
      OT_INT,    "jobs_run",    num_jobs_run,
      OT_INT,    "jobs_running",job_count(),
      OT_INT,    "nclients",    ((rblist *)res_head[R_CLIENT-r_first]->res_list)->size(),
      OT_INT,    "nstores",     ((rblist *)res_head[R_STORAGE-r_first]->res_list)->size(),
      OT_INT,    "npools",      ((rblist *)res_head[R_POOL-r_first]->res_list)->size(),
      OT_INT,    "ncats",       ((rblist *)res_head[R_CATALOG-r_first]->res_list)->size(),
      OT_INT,    "nfset",       ((rblist *)res_head[R_FILESET-r_first]->res_list)->size(),
      OT_INT,    "nscheds",     ((rblist *)res_head[R_SCHEDULE-r_first]->res_list)->size(),
      OT_PLUGINS,"plugins",     b_plugin_list,
      OT_END);

   ua->send_msg("%s", wt.end_group());
}

void list_dir_status_header(UAContext *ua)
{
   char dt[MAX_TIME_LENGTH], dt1[MAX_TIME_LENGTH];
   char b1[35], b2[35], b3[35], b4[35], b5[35];

   if (ua->api > 1) {
      api_list_dir_status_header(ua);
      return;
   }

   ua->send_msg(_("%s %sVersion: %s (%s) %s %s %s\n"), my_name,
            "", VERSION, BDATE, HOST_OS, DISTNAME, DISTVER);
   bstrftime_nc(dt, sizeof(dt), daemon_start_time);
   bstrftimes(dt1, sizeof(dt1), last_reload_time);
   ua->send_msg(_("Daemon started %s, conf reloaded %s\n"), dt, dt1);
   ua->send_msg(_(" Jobs: run=%d, running=%d mode=%d,%d\n"),
      num_jobs_run, job_count(), (int)DEVELOPER_MODE, 0);
   ua->send_msg(_(" Heap: heap=%s smbytes=%s max_bytes=%s bufs=%s max_bufs=%s\n"),
      edit_uint64_with_commas((char *)sbrk(0)-(char *)start_heap, b1),
      edit_uint64_with_commas(sm_bytes, b2),
      edit_uint64_with_commas(sm_max_bytes, b3),
      edit_uint64_with_commas(sm_buffers, b4),
      edit_uint64_with_commas(sm_max_buffers, b5));
   ua->send_msg(_(" Res: njobs=%d nclients=%d nstores=%d npools=%d ncats=%d"
                  " nfsets=%d nscheds=%d\n"),
      ((rblist *)res_head[R_JOB-r_first]->res_list)->size(),
      ((rblist *)res_head[R_CLIENT-r_first]->res_list)->size(),
      ((rblist *)res_head[R_STORAGE-r_first]->res_list)->size(),
      ((rblist *)res_head[R_POOL-r_first]->res_list)->size(),
      ((rblist *)res_head[R_CATALOG-r_first]->res_list)->size(),
      ((rblist *)res_head[R_FILESET-r_first]->res_list)->size(),
      ((rblist *)res_head[R_SCHEDULE-r_first]->res_list)->size());


   /* TODO: use this function once for all daemons */
   if (b_plugin_list && b_plugin_list->size() > 0) {
      int len;
      Plugin *plugin;
      POOL_MEM msg(PM_FNAME);
      pm_strcpy(msg, " Plugin: ");
      foreach_alist(plugin, b_plugin_list) {
         len = pm_strcat(msg, plugin->file);
         if (len > 80) {
            pm_strcat(msg, "\n   ");
         } else {
            pm_strcat(msg, " ");
         }
      }
      ua->send_msg("%s\n", msg.c_str());
   }
}

static void do_director_status(UAContext *ua)
{
   list_dir_status_header(ua);

   /*
    * List scheduled Jobs
    */
   list_scheduled_jobs(ua);

   /*
    * List running jobs
    */
   list_running_jobs(ua);

   /*
    * List terminated jobs
    */
   list_terminated_jobs(ua);
   ua->send_msg("====\n");
}

static void do_storage_status(UAContext *ua, STORE *store, char *cmd)
{
   BSOCK *sd;
   USTORE lstore;


   if (!acl_access_ok(ua, Storage_ACL, store->name())) {
      ua->error_msg(_("No authorization for Storage \"%s\"\n"), store->name());
      return;
   }
   /*
    * The Storage daemon is problematic because it shows information
    *  related to multiple Job, so if there is a Client or Job
    *  ACL restriction, we forbid all access to the Storage.
    */
   if (have_restricted_acl(ua, Client_ACL) ||
       have_restricted_acl(ua, Job_ACL)) {
      ua->error_msg(_("Restricted Client or Job does not permit access to  Storage daemons\n"));
      return;
   }
   lstore.store = store;
   pm_strcpy(lstore.store_source, _("unknown source"));
   set_wstorage(ua->jcr, &lstore);
   /* Try connecting for up to 15 seconds */
   if (!ua->api) ua->send_msg(_("Connecting to Storage daemon %s at %s:%d\n"),
      store->name(), store->address, store->SDport);
   if (!connect_to_storage_daemon(ua->jcr, 1, 15, 0)) {
      ua->send_msg(_("\nFailed to connect to Storage daemon %s.\n====\n"),
         store->name());
      free_bsock(ua->jcr->store_bsock);
      return;
   }
   Dmsg0(20, "Connected to storage daemon\n");
   sd = ua->jcr->store_bsock;
   if (cmd) {
      POOL_MEM devname;
      /*
       * For .status storage=xxx shstore list
       *  send .status shstore list xxx-device
       */
      if (strcasecmp(cmd, "shstore") == 0) {
         if (!ua->argk[3]) {
            ua->send_msg(_("Must have three arguments\n"));
            return;
         }
         pm_strcpy(devname, store->dev_name());
         bash_spaces(devname.c_str());
         sd->fsend(".status %s %s %s api=%d api_opts=%s",
                   cmd, ua->argk[3], devname.c_str(),
                   ua->api, ua->api_opts);
      } else {
         int i = find_arg_with_value(ua, "device");
         if (i>0) {
            Mmsg(devname, "device=%s", ua->argv[i]);
            bash_spaces(devname.c_str());
         }
         sd->fsend(".status %s api=%d api_opts=%s %s",
                   cmd, ua->api, ua->api_opts, devname.c_str());
      }
   } else {
      sd->fsend("status");
   }
   while (sd->recv() >= 0) {
      ua->send_msg("%s", sd->msg);
   }
   sd->signal(BNET_TERMINATE);
   free_bsock(ua->jcr->store_bsock);
   return;
}

static void do_client_status(UAContext *ua, CLIENT *client, char *cmd)
{
   BSOCK *fd;
   POOL_MEM buf;

   if (!acl_access_client_ok(ua, client->name(), JT_BACKUP_RESTORE)) {
      ua->error_msg(_("No authorization for Client \"%s\"\n"), client->name());
      return;
   }
   /* Connect to File daemon */
   ua->jcr->client = client;
   /* Release any old dummy key */
   if (ua->jcr->sd_auth_key) {
      free(ua->jcr->sd_auth_key);
   }
   /* Create a new dummy SD auth key */
   ua->jcr->sd_auth_key = bstrdup("dummy");

   /* Try to connect for 15 seconds */
   if (!ua->api) ua->send_msg(_("Connecting to Client %s at %s:%d\n"),
                              client->name(), client->address(buf.addr()), client->FDport);
   if (!connect_to_file_daemon(ua->jcr, 1, 15, 0)) {
      ua->send_msg(_("Failed to connect to Client %s.\n====\n"),
         client->name());
      free_bsock(ua->jcr->file_bsock);
      return;
   }
   Dmsg0(20, _("Connected to file daemon\n"));
   fd = ua->jcr->file_bsock;
   if (cmd) {
      fd->fsend(".status %s api=%d api_opts=%s", cmd, ua->api, ua->api_opts);
   } else {
      fd->fsend("status");
   }
   while (fd->recv() >= 0) {
      ua->send_msg("%s", fd->msg);
   }
   fd->signal(BNET_TERMINATE);
   free_bsock(ua->jcr->file_bsock);

   return;
}

static void prt_runhdr(UAContext *ua)
{
   if (!ua->api) {
      ua->send_msg(_("\nScheduled Jobs:\n"));
      ua->send_msg(_("Level          Type     Pri  Scheduled          Job Name           Volume\n"));
      ua->send_msg(_("===================================================================================\n"));
   }
}

static void prt_lrunhdr(UAContext *ua)
{
   if (!ua->api) {
      ua->send_msg(_("\nScheduled Jobs:\n"));
      ua->send_msg(_("Level          Type     Pri  Scheduled          Job Name           Schedule\n"));
      ua->send_msg(_("=====================================================================================\n"));
   }
}


/* Scheduling packet */
struct sched_pkt {
   dlink link;                        /* keep this as first item!!! */
   JOB *job;
   int level;
   int priority;
   utime_t runtime;
   POOL *pool;
   STORE *store;
};

static void prt_runtime(UAContext *ua, sched_pkt *sp, OutputWriter *ow)
{
   char dt[MAX_TIME_LENGTH], edl[50];
   const char *level_ptr;
   bool ok = false;
   bool close_db = false;
   JCR *jcr = ua->jcr;
   MEDIA_DBR mr;
   POOL_MEM errmsg;
   int orig_jobtype;

   orig_jobtype = jcr->getJobType();
   if (sp->job->JobType == JT_BACKUP) {
      jcr->db = NULL;
      ok = complete_jcr_for_job(jcr, sp->job, sp->pool);
      Dmsg1(250, "Using pool=%s\n", jcr->pool->name());
      if (jcr->db) {
         close_db = true;             /* new db opened, remember to close it */
      }
      if (ok) {
         mr.PoolId = jcr->jr.PoolId;
         jcr->wstore = sp->store;
         set_storageid_in_mr(jcr->wstore, &mr);
         Dmsg0(250, "call find_next_volume_for_append\n");
         /* no need to set ScratchPoolId, since we use fnv_no_create_vol */
         ok = find_next_volume_for_append(jcr, &mr, 1, fnv_no_create_vol, fnv_no_prune, errmsg);
      }
      if (!ok) {
         bstrncpy(mr.VolumeName, "*unknown*", sizeof(mr.VolumeName));
      }
   }
   bstrftime_nc(dt, sizeof(dt), sp->runtime);
   switch (sp->job->JobType) {
   case JT_ADMIN:
      level_ptr = "Admin";
      break;
   case JT_RESTORE:
      level_ptr = "Restore";
      break;
   default:
      level_ptr = level_to_str(edl, sizeof(edl), sp->level);
      break;
   }
   if (ua->api == 1) {
      ua->send_msg(_("%-14s\t%-8s\t%3d\t%-18s\t%-18s\t%s\n"),
         level_ptr, job_type_to_str(sp->job->JobType), sp->priority, dt,
         sp->job->name(), mr.VolumeName);

   } else if (ua->api > 1) {
      ua->send_msg("%s",
                   ow->get_output(OT_CLEAR,
                      OT_START_OBJ,
                      OT_STRING,    "name",     sp->job->name(),
                      OT_JOBLEVEL,  "level",   sp->level,
                      OT_JOBTYPE,   "type",    sp->job->JobType,
                      OT_INT,       "priority",sp->priority,
                      OT_UTIME,     "schedtime", sp->runtime,
                      OT_STRING,    "volume",  mr.VolumeName,
                      OT_STRING,    "pool",    jcr->pool?jcr->pool->name():"",
                      OT_STRING,    "storage", jcr->wstore?jcr->wstore->name():"",
                      OT_END_OBJ,
                      OT_END));


   } else {
      ua->send_msg(_("%-14s %-8s %3d  %-18s %-18s %s\n"),
         level_ptr, job_type_to_str(sp->job->JobType), sp->priority, dt,
         sp->job->name(), mr.VolumeName);
   }
   if (close_db) {
      db_close_database(jcr, jcr->db);
   }
   jcr->db = ua->db;                  /* restore ua db to jcr */
   jcr->setJobType(orig_jobtype);
}

/* We store each job schedule into a rblist to display them ordered */
typedef struct {
   rblink  lnk;
   btime_t time;
   int     prio;
   int     level;
   SCHED  *sched; 
   JOB    *job;
} schedule;

static int compare(void *i1, void *i2)
{
   int ret;
   schedule *item1 = (schedule *)i1;
   schedule *item2 = (schedule *)i2;

   /* Order by time */
   if (item1->time > item2->time) {
      return 1;
   } else if (item1->time < item2->time) {
      return -1;
   }

   /* If same time, order by priority */
   if (item1->prio > item2->prio) {
      return 1;
   } else if (item1->prio < item2->prio) {
      return -1;
   }

   /* If same priority, order by name */
   ret = strcmp(item1->job->name(), item2->job->name());
   if (ret != 0) {
      return ret;
   }

   return 1;           /* If same name, same time, same prio => insert after */
}

static bool is_included(const char *str, alist *list)
{
   char *v;
   if (list->size() == 0) {       /* The list is empty, we take everything */
      return true;
   }
   foreach_alist(v, list) {
      if (strcmp(v, str) == 0) {
         return true;
      }
   }
   return false;
}

/*
 * Detailed listing of all scheduler jobs
 */
static void llist_scheduled_jobs(UAContext *ua)
{
   utime_t runtime=0;
   RUN *run;
   JOB *job;
   int level, num_jobs = 0;
   int priority;
   bool limit_set = false;
   char sched_name[MAX_NAME_LENGTH] = {0}, edl[50];
   char *n, *p;
   SCHED *sched;
   int days=10, limit=30;
   time_t now = time(NULL);
   time_t next;
   rblist *list;
   alist clients(10, not_owned_by_alist);
   alist jobs(10, not_owned_by_alist);
   schedule *item = NULL;

   Dmsg0(200, "enter list_sched_jobs()\n");

   for (int i=0; i < ua->argc ; i++) {
      if (strcmp(ua->argk[i], NT_("limit")) == 0) {
         limit = atoi(ua->argv[i]);
         if (((limit < 0) || (limit > 2000)) && !ua->api) {
            ua->send_msg(_("Ignoring invalid value for limit. Max is 2000.\n"));
            limit = 2000;
         }
         limit_set = true;

      } else if (strcmp(ua->argk[i], NT_("days")) == 0) {
         days = atoi(ua->argv[i]);
         if (((days < 0) || (days > 3000)) && !ua->api) {
            ua->send_msg(_("Ignoring invalid value for days. Max is 3000.\n"));
            days = 3000;
         }
         if (!limit_set) {
            limit = 0;              /* Disable limit if not set explicitely */
         }

      } else if (strcmp(ua->argk[i], NT_("time")) == 0) {
         now = str_to_utime(ua->argv[i]);
         if (now == 0) {
            ua->send_msg(_("Ignoring invalid time.\n"));
            now = time(NULL);
         }

      } else if (strcmp(ua->argk[i], NT_("schedule")) == 0 && ua->argv[i]) {
         bstrncpy(sched_name, ua->argv[i], sizeof(sched_name));

      } else if (strcmp(ua->argk[i], NT_("job")) == 0) {
         p = ua->argv[i];
         while ((n = next_name(&p)) != NULL) {
            jobs.append(n);
         }

      } else if (strcmp(ua->argk[i], NT_("client")) == 0) {
         p = ua->argv[i];
         while ((n = next_name(&p)) != NULL) {
            clients.append(n);
         }
      }
   }

   list = New(rblist(item, &item->lnk));

   /* Loop through all jobs */
   LockRes();
   foreach_res(job, R_JOB) {
      if (!acl_access_ok(ua, Job_ACL, job->name())) {
         continue;
      }
      sched = job->schedule;
      if (!sched || !job->is_enabled() || (sched && !sched->is_enabled()) ||
         (job->client && !job->client->is_enabled())) {
         continue;                    /* no, skip this job */
      }
      if (sched_name[0] && strcmp(sched_name, sched->name()) != 0) {
         continue;
      }
      if (!is_included(job->name(), &jobs)) {
         continue;
      }
      if (!is_included(job->client->name(), &clients)) {
         continue;
      }
      for (run=sched->run; run; run=run->next) {
         next = now;
         for (int i=0; i<days; i++) {
            struct tm tm;
            int mday, wday, month, wom, woy, ldom;
            bool ok;

            /* compute values for next time */
            (void)localtime_r(&next, &tm);
            mday = tm.tm_mday - 1;
            wday = tm.tm_wday;
            month = tm.tm_mon;
            wom = mday / 7;
            woy = tm_woy(next);                    /* get week of year */
            ldom = tm_ldom(month, tm.tm_year + 1900);

//#define xxx_debug
#ifdef xxx_debug
            Dmsg6(000, "m=%d md=%d wd=%d wom=%d woy=%d ldom=%d\n",
               month, mday, wday, wom, woy, ldom);
            Dmsg6(000, "bitset bsm=%d bsmd=%d bswd=%d bswom=%d bswoy=%d bsldom=%d\n",
               bit_is_set(month, run->month),
               bit_is_set(mday, run->mday),
               bit_is_set(wday, run->wday),
               bit_is_set(wom, run->wom),
               bit_is_set(woy, run->woy),
               bit_is_set(31, run->mday));
#endif

            ok = (bit_is_set(mday, run->mday) &&
                  bit_is_set(wday, run->wday) &&
                  bit_is_set(month, run->month) &&
                  bit_is_set(wom, run->wom) &&
                  bit_is_set(woy, run->woy)) ||
                 (bit_is_set(month, run->month) &&
                  bit_is_set(31, run->mday) && mday == ldom);
            if (!ok) {
               next += 24 * 60 * 60;   /* Add one day */
               continue;
            }
            for (int j=0; j < 24; j++) {
               if (bit_is_set(j, run->hour)) {
                  tm.tm_hour = j;
                  tm.tm_min = run->minute;
                  tm.tm_sec = 0;
                  runtime = mktime(&tm);
                  break;
               }
            }

            level = job->JobLevel;
            if (run->level) {
               level = run->level;
            }
            priority = job->Priority;
            if (run->Priority) {
               priority = run->Priority;
            }

            item = (schedule *) malloc(sizeof(schedule));
            item->time = runtime;
            item->prio = priority;
            item->job = job;
            item->sched = sched;
            item->level = level;
            list->insert(item, compare);

            next += 24 * 60 * 60;   /* Add one day */
            num_jobs++;
            if (limit > 0 && num_jobs >= limit) {
               goto get_out;
            }
         }
      } /* end loop over run pkts */
   } /* end for loop over resources */
get_out:
   UnlockRes();
   prt_lrunhdr(ua);
   OutputWriter ow(ua->api_opts);
   if (ua->api > 1) {
      ua->send_msg("%s", ow.start_group("scheduled"));
   }
   foreach_rblist(item, list) {
      char dt[MAX_TIME_LENGTH];
      const char *level_ptr;
      bstrftime_dn(dt, sizeof(dt), item->time);

      switch (item->job->JobType) {
      case JT_ADMIN:
         level_ptr = "Admin";
         break;
      case JT_RESTORE:
         level_ptr = "Restore";
         break;
      default:
         level_ptr = level_to_str(edl, sizeof(edl), item->level);
         break;
      }

      if (ua->api > 1) {
         bool use_client =
            item->job->JobType == JT_BACKUP ||
            item->job->JobType == JT_RESTORE;

         ua->send_msg("%s", ow.get_output(OT_CLEAR,
                         OT_START_OBJ,
                         OT_JOBLEVEL ,"level",  item->level,
                         OT_JOBTYPE, "type",      item->job->JobType,
                         OT_STRING,  "name",       item->job->name(),
                         OT_STRING, "client", use_client?item->job->client->name() : "",
                         OT_STRING,  "fileset",   item->job->fileset->name(),
                         OT_UTIME,   "schedtime", item->time,
                         OT_INT32,   "priority",  item->prio,
                         OT_STRING,  "schedule", item->sched->name(),
                         OT_END_OBJ,
                         OT_END));

      } else if (ua->api) {
         ua->send_msg(_("%-14s\t%-8s\t%3d\t%-18s\t%-18s\t%s\n"),
                      level_ptr, job_type_to_str(item->job->JobType),
                      item->prio, dt,
                      item->job->name(), item->sched->name());
      } else {
         ua->send_msg(_("%-14s %-8s %3d  %-18s %-18s %s\n"),
                      level_ptr, job_type_to_str(item->job->JobType), item->prio, dt,
                      item->job->name(), item->sched->name());
      }
   }
   if (ua->api > 1) {
      ua->send_msg("%s", ow.end_group());
   }
   delete list;

   if (num_jobs == 0 && !ua->api) {
      ua->send_msg(_("No Scheduled Jobs.\n"));
   }
   if (!ua->api) ua->send_msg("====\n");
   Dmsg0(200, "Leave ;list_sched_jobs_runs()\n");
}

/*
 * Sort items by runtime, priority
 */
static int my_compare(void *item1, void *item2)
{
   sched_pkt *p1 = (sched_pkt *)item1;
   sched_pkt *p2 = (sched_pkt *)item2;
   if (p1->runtime < p2->runtime) {
      return -1;
   } else if (p1->runtime > p2->runtime) {
      return 1;
   }
   if (p1->priority < p2->priority) {
      return -1;
   } else if (p1->priority > p2->priority) {
      return 1;
   }
   return 0;
}

/*
 * Find all jobs to be run in roughly the
 *  next 24 hours.
 */
static void list_scheduled_jobs(UAContext *ua)
{
   OutputWriter ow(ua->api_opts);
   utime_t runtime;
   RUN *run;
   JOB *job;
   int level, num_jobs = 0;
   int priority;
   char sched_name[MAX_NAME_LENGTH];
   dlist sched;
   sched_pkt *sp;
   int days, i;

   Dmsg0(200, "enter list_sched_jobs()\n");

   days = 1;
   i = find_arg_with_value(ua, NT_("days"));
   if (i >= 0) {
     days = atoi(ua->argv[i]);
     if (((days < 0) || (days > 500)) && !ua->api) {
       ua->send_msg(_("Ignoring invalid value for days. Max is 500.\n"));
       days = 1;
     }
   }
   i = find_arg_with_value(ua, NT_("schedule"));
   if (i >= 0) {
      bstrncpy(sched_name, ua->argv[i], sizeof(sched_name));
   } else {
      sched_name[0] = 0;
   }

   /* Loop through all jobs */
   LockRes();
   foreach_res(job, R_JOB) {
      if (!acl_access_ok(ua, Job_ACL, job->name()) || !job->is_enabled()) {
         continue;
      }
      if (sched_name[0] && job->schedule &&
          strcasecmp(job->schedule->name(), sched_name) != 0) {
         continue;
      }
      for (run=NULL; (run = find_next_run(run, job, runtime, days)); ) {
         USTORE store;
         level = job->JobLevel;
         if (run->level) {
            level = run->level;
         }
         priority = job->Priority;
         if (run->Priority) {
            priority = run->Priority;
         }
         sp = (sched_pkt *)malloc(sizeof(sched_pkt));
         sp->job = job;
         sp->level = level;
         sp->priority = priority;
         sp->runtime = runtime;
         sp->pool = run->pool;
         get_job_storage(&store, job, run);
         sp->store = store.store;
         Dmsg3(250, "job=%s store=%s MediaType=%s\n", job->name(), sp->store->name(), sp->store->media_type);
         sched.binary_insert_multiple(sp, my_compare);
         num_jobs++;
      }
   } /* end for loop over resources */
   UnlockRes();
   prt_runhdr(ua);
   foreach_dlist(sp, &sched) {
      prt_runtime(ua, sp, &ow);
   }
   if (num_jobs == 0 && !ua->api) {
      ua->send_msg(_("No Scheduled Jobs.\n"));
   }
   if (!ua->api) ua->send_msg("====\n");
   Dmsg0(200, "Leave list_sched_jobs_runs()\n");
}

static void list_running_jobs(UAContext *ua)
{
   JCR *jcr;
   int njobs = 0;
   int i;
   int32_t status;
   const char *msg, *msgdir;
   char *emsg;                        /* edited message */
   char dt[MAX_TIME_LENGTH];
   char level[10];
   bool pool_mem = false;
   OutputWriter ow(ua->api_opts);
   JobId_t jid = 0;

   if ((i = find_arg_with_value(ua, "jobid")) >= 0) {
      jid = str_to_int64(ua->argv[i]);
   }

   Dmsg0(200, "enter list_run_jobs()\n");

   if (!ua->api) {
      ua->send_msg(_("\nRunning Jobs:\n"));
      foreach_jcr(jcr) {
         if (jcr->JobId == 0) {      /* this is us */
            /* this is a console or other control job. We only show console
             * jobs in the status output.
             */
            if (jcr->getJobType() == JT_CONSOLE) {
               bstrftime_nc(dt, sizeof(dt), jcr->start_time);
               ua->send_msg(_("Console connected %sat %s\n"),
                            (ua->UA_sock && ua->UA_sock->tls)?_("using TLS "):"",
                            dt);
            }
            continue;
         }
      }
      endeach_jcr(jcr);
   }

   njobs = 0; /* count the number of job really displayed */
   foreach_jcr(jcr) {
      if (jcr->JobId == 0 || !jcr->job || !acl_access_ok(ua, Job_ACL, jcr->job->name())) {
         continue;
      }
      /* JobId keyword found in command line */
      if (jid > 0 && jcr->JobId != jid) {
         continue;
      }

      if (++njobs == 1) {
         /* display the header for the first job */
         if (!ua->api) {
            ua->send_msg(_(" JobId  Type Level     Files     Bytes  Name              Status\n"));
            ua->send_msg(_("======================================================================\n"));

         } else if (ua->api > 1) {
            ua->send_msg(ow.start_group("running", false));
         }
      }
      status = jcr->JobStatus;
      switch (status) {
      case JS_Created:
         msg = _("is waiting execution");
         break;
      case JS_Running:
         msg = _("is running");
         break;
      case JS_Blocked:
         msg = _("is blocked");
         break;
      case JS_Terminated:
         msg = _("has terminated");
         break;
      case JS_Warnings:
         msg = _("has terminated with warnings");
         break;
      case JS_Incomplete:
         msg = _("has terminated in incomplete state");
         break;
      case JS_ErrorTerminated:
         msg = _("has erred");
         break;
      case JS_Error:
         msg = _("has errors");
         break;
      case JS_FatalError:
         msg = _("has a fatal error");
         break;
      case JS_Differences:
         msg = _("has verify differences");
         break;
      case JS_Canceled:
         msg = _("has been canceled");
         break;
      case JS_WaitFD:
         emsg = (char *) get_pool_memory(PM_FNAME);
         if (!jcr->client) {
            Mmsg(emsg, _("is waiting on Client"));
         } else {
            Mmsg(emsg, _("is waiting on Client %s"), jcr->client->name());
         }
         pool_mem = true;
         msg = emsg;
         break;
      case JS_WaitSD:
         emsg = (char *) get_pool_memory(PM_FNAME);
         if (jcr->wstore) {
            Mmsg(emsg, _("is waiting on Storage \"%s\""), jcr->wstore->name());
         } else if (jcr->rstore) {
            Mmsg(emsg, _("is waiting on Storage \"%s\""), jcr->rstore->name());
         } else {
            Mmsg(emsg, _("is waiting on Storage"));
         }
         pool_mem = true;
         msg = emsg;
         break;
      case JS_WaitStoreRes:
         msg = _("is waiting on max Storage jobs");
         break;
      case JS_WaitClientRes:
         msg = _("is waiting on max Client jobs");
         break;
      case JS_WaitJobRes:
         msg = _("is waiting on max Job jobs");
         break;
      case JS_WaitMaxJobs:
         msg = _("is waiting on max total jobs");
         break;
      case JS_WaitStartTime:
         emsg = (char *) get_pool_memory(PM_FNAME);
         Mmsg(emsg, _("is waiting for its start time (%s)"),
              bstrftime_ny(dt, sizeof(dt), jcr->sched_time));
         pool_mem = true;
         msg = emsg;
         break;
      case JS_WaitPriority:
         msg = _("is waiting for higher priority jobs to finish");
         break;
      case JS_WaitDevice:
         msg = _("is waiting for a Shared Storage device");
         break;
      case JS_DataCommitting:
         msg = _("SD committing Data");
         break;
      case JS_DataDespooling:
         msg = _("SD despooling Data");
         break;
      case JS_AttrDespooling:
         msg = _("SD despooling Attributes");
         break;
      case JS_AttrInserting:
         msg = _("Dir inserting Attributes");
         break;

      default:
         emsg = (char *)get_pool_memory(PM_FNAME);
         Mmsg(emsg, _("is in unknown state %c"), jcr->JobStatus);
         pool_mem = true;
         msg = emsg;
         break;
      }
      msgdir = msg;             /* Keep it to know if we update the status variable */
      /*
       * Now report Storage daemon status code
       */
      switch (jcr->SDJobStatus) {
      case JS_WaitMount:
         if (pool_mem) {
            free_pool_memory(emsg);
            pool_mem = false;
         }
         msg = _("is waiting for a mount request");
         break;
      case JS_WaitMedia:
         if (pool_mem) {
            free_pool_memory(emsg);
            pool_mem = false;
         }
         msg = _("is waiting for an appendable Volume");
         break;
      case JS_WaitFD:
         /* Special case when JobStatus=JS_WaitFD, we don't have a FD link yet 
          * we need to stay in WaitFD status See bee mantis #1414 */
         if (jcr->JobStatus != JS_WaitFD) {
            if (!pool_mem) {
               emsg = (char *)get_pool_memory(PM_FNAME);
               pool_mem = true;
            }
            if (!jcr->client || !jcr->wstore) {
               Mmsg(emsg, _("is waiting for Client to connect to Storage daemon"));
            } else {
               Mmsg(emsg, _("is waiting for Client %s to connect to Storage %s"),
                    jcr->client->name(), jcr->wstore->name());
            }
            msg = emsg;
         }
        break;
      case JS_DataCommitting:
         msg = _("SD committing Data");
         break;
      case JS_DataDespooling:
         msg = _("SD despooling Data");
         break;
      case JS_AttrDespooling:
         msg = _("SD despooling Attributes");
         break;
      case JS_AttrInserting:
         msg = _("Dir inserting Attributes");
         break;
      }
      if (msg != msgdir) {
         status = jcr->SDJobStatus;
      }
      switch (jcr->getJobType()) {
      case JT_ADMIN:
         bstrncpy(level, "Admin", sizeof(level));
         break;
      case JT_RESTORE:
         bstrncpy(level, "Restore", sizeof(level));
         break;
      default:
         level_to_str(level, sizeof(level), jcr->getJobLevel());
         level[7] = 0;
         break;
      }

      if (ua->api == 1) {
         bash_spaces(jcr->comment);
         ua->send_msg(_("%6d\t%-6s\t%-20s\t%s\t%s\n"),
                      jcr->JobId, level, jcr->Job, msg, jcr->comment);
         unbash_spaces(jcr->comment);

      } else if (ua->api > 1) {
         ua->send_msg("%s", ow.get_output(OT_CLEAR,
                         OT_START_OBJ,
                         OT_INT32,   "jobid",     jcr->JobId,
                         OT_JOBLEVEL,"level",     jcr->getJobLevel(),
                         OT_JOBTYPE, "type",      jcr->getJobType(),
                         OT_JOBSTATUS,"status",   status,
                         OT_STRING,  "status_desc",msg,
                         OT_STRING,  "comment",   jcr->comment,
                         OT_SIZE,    "jobbytes",  jcr->JobBytes,
                         OT_INT32,   "jobfiles",  jcr->JobFiles,
                         OT_STRING,  "job",       jcr->Job,
                         OT_STRING,  "name",      jcr->job->name(),
                         OT_STRING,  "clientname",jcr->client?jcr->client->name():"",
                         OT_STRING,  "fileset",   jcr->fileset?jcr->fileset->name():"",
                         OT_STRING,  "storage",   jcr->wstore?jcr->wstore->name():"",
                         OT_STRING,  "rstorage",  jcr->rstore?jcr->rstore->name():"",
                         OT_UTIME,   "schedtime", jcr->sched_time,
                         OT_UTIME,   "starttime", jcr->start_time,
                         OT_INT32,   "priority",  jcr->JobPriority,
                         OT_INT32,   "errors",    jcr->JobErrors,
                         OT_END_OBJ,
                         OT_END));

      } else {
         char b1[50], b2[50], b3[50];
         level[4] = 0;
         bstrncpy(b1, job_type_to_str(jcr->getJobType()), sizeof(b1));
         b1[4] = 0;
         ua->send_msg(_("%6d  %-4s %-3s %10s %10s %-17s %s\n"),
            jcr->JobId, b1, level,
            edit_uint64_with_commas(jcr->JobFiles, b2),
            edit_uint64_with_suffix(jcr->JobBytes, b3),
            jcr->job->name(), msg);
      }

      if (pool_mem) {
         free_pool_memory(emsg);
         pool_mem = false;
      }
   }
   endeach_jcr(jcr);

   if (njobs == 0) {
      /* Note the following message is used in regress -- don't change */
      ua->send_msg(_("No Jobs running.\n====\n"));
      Dmsg0(200, "leave list_run_jobs()\n");
      return;
   } else {
      /* display a closing header */
      if (!ua->api) {
         ua->send_msg("====\n");
      } else if (ua->api > 1) {
         ua->send_msg(ow.end_group(false));
      }
   }
   Dmsg0(200, "leave list_run_jobs()\n");
}

static void list_terminated_jobs(UAContext *ua)
{
   char dt[MAX_TIME_LENGTH], b1[30], b2[30];
   char level[10];
   OutputWriter ow(ua->api_opts);

   if (last_jobs->empty()) {
      if (!ua->api) ua->send_msg(_("No Terminated Jobs.\n"));
      return;
   }
   lock_last_jobs_list();
   struct s_last_job *je;
   if (!ua->api) {
      ua->send_msg(_("\nTerminated Jobs:\n"));
      ua->send_msg(_(" JobId  Level      Files    Bytes   Status   Finished        Name \n"));
      ua->send_msg(_("====================================================================\n"));
   } else if (ua->api > 1) {
      ua->send_msg(ow.start_group("terminated"));
   }
   foreach_dlist(je, last_jobs) {
      char JobName[MAX_NAME_LENGTH];
      const char *termstat;

      bstrncpy(JobName, je->Job, sizeof(JobName));
      /* There are three periods after the Job name */
      char *p;
      for (int i=0; i<3; i++) {
         if ((p=strrchr(JobName, '.')) != NULL) {
            *p = 0;
         }
      }

      if (!acl_access_ok(ua, Job_ACL, JobName)) {
         continue;
      }

      bstrftime_nc(dt, sizeof(dt), je->end_time);
      switch (je->JobType) {
      case JT_ADMIN:
         bstrncpy(level, "Admin", sizeof(level));
         break;
      case JT_RESTORE:
         bstrncpy(level, "Restore", sizeof(level));
         break;
      default:
         level_to_str(level, sizeof(level), je->JobLevel);
         level[4] = 0;
         break;
      }
      switch (je->JobStatus) {
      case JS_Created:
         termstat = _("Created");
         break;
      case JS_FatalError:
      case JS_ErrorTerminated:
         termstat = _("Error");
         break;
      case JS_Differences:
         termstat = _("Diffs");
         break;
      case JS_Canceled:
         termstat = _("Cancel");
         break;
      case JS_Terminated:
         termstat = _("OK");
         break;
      case JS_Warnings:
         termstat = _("OK -- with warnings");
         break;
      case JS_Incomplete:
         termstat = _("Incomplete");
         break;
      default:
         termstat = _("Other");
         break;
      }
      if (ua->api == 1) {
         ua->send_msg(_("%7d\t%-6s\t%8s\t%10s\t%-7s\t%-8s\t%s\n"),
            je->JobId,
            level,
            edit_uint64_with_commas(je->JobFiles, b1),
            edit_uint64_with_suffix(je->JobBytes, b2),
            termstat,
            dt, JobName);
      } else if (ua->api > 1) {
         ua->send_msg("%s",
                      ow.get_output(OT_CLEAR,
                                    OT_START_OBJ,
                                    OT_INT32,   "jobid",     je->JobId,
                                    OT_JOBLEVEL,"level",     je->JobLevel,
                                    OT_JOBTYPE, "type",      je->JobType,
                                    OT_JOBSTATUS,"status",   je->JobStatus,
                                    OT_STRING,  "status_desc",termstat,
                                    OT_SIZE,    "jobbytes",  je->JobBytes,
                                    OT_INT32,   "jobfiles",  je->JobFiles,
                                    OT_STRING,  "job",       je->Job,
                                    OT_UTIME,   "starttime", je->start_time,
                                    OT_UTIME,   "endtime",   je->end_time,
                                    OT_INT32,   "errors",    je->Errors,
                                    OT_END_OBJ,
                                    OT_END));

      } else {
         ua->send_msg(_("%6d  %-7s %8s %10s  %-7s  %-8s %s\n"),
            je->JobId,
            level,
            edit_uint64_with_commas(je->JobFiles, b1),
            edit_uint64_with_suffix(je->JobBytes, b2),
            termstat,
            dt, JobName);
      }
   }
   if (!ua->api) {
      ua->send_msg(_("\n"));
   } else if (ua->api > 1) {
      ua->send_msg(ow.end_group(false));
   }
   unlock_last_jobs_list();
}
