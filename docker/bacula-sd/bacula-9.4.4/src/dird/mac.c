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
 *   Bacula Director -- mac.c -- responsible for doing
 *     migration and copy jobs.
 *
 *   Also handles Copy jobs (March MMVIII)
 *
 *   Written by Kern Sibbald, September MMIV
 *
 *  Basic tasks done here:
 *     Open DB and create records for this job.
 *     Open Message Channel with Storage daemon to tell him a job will be starting.
 *     Open connection with Storage daemon and pass him commands
 *       to do the backup.
 *     When the Storage daemon finishes the job, update the DB.
 */

#include "bacula.h"
#include "dird.h"
#include "ua.h"

static const int dbglevel = 10;
static char storaddr[] = "storage address=%s port=%d ssl=%d Job=%s Authentication=%s\n";
static char OKstore[]  = "2000 OK storage\n";

/* Imported subroutines */
extern int getJob_to_migrate(JCR *jcr);
extern bool regex_find_jobids(JCR *jcr, idpkt *ids, const char *query1,
                 const char *query2, const char *type);
extern bool find_mediaid_then_jobids(JCR *jcr, idpkt *ids, const char *query1,
                 const char *type);
extern bool find_jobids_of_pool_uncopied_jobs(JCR *jcr, idpkt *ids);

static bool set_mac_next_pool(JCR *jcr, POOL **pool);

/*
 * Called here before the job is run to do the job
 *   specific setup.  Note, one of the important things to
 *   complete in this init code is to make the definitive
 *   choice of input and output storage devices.  This is
 *   because immediately after the init, the job is queued
 *   in the jobq.c code, and it checks that all the resources
 *   (storage resources in particular) are available, so these
 *   must all be properly defined.
 *
 *  previous_jr refers to the job DB record of the Job that is
 *    going to be migrated.
 *  prev_job refers to the job resource of the Job that is
 *    going to be migrated.
 *  jcr is the jcr for the current "migration" job.  It is a
 *    control job that is put in the DB as a migration job, which
 *    means that this job migrated a previous job to a new job.
 *    No Volume or File data is associated with this control
 *    job.
 *  wjcr refers to the migrate/copy job that is writing and is run by
 *    the current jcr.  It is a backup job that writes the
 *    data written for the previous_jr into the new pool.  This
 *    job (wjcr) becomes the new backup job that replaces
 *    the original backup job. Note, this jcr is not really run. It
 *    is simply attached to the current jcr.  It will show up in
 *    the Director's status output, but not in the SD or FD, both of
 *    which deal only with the current migration job (i.e. jcr).
 */
bool do_mac_init(JCR *jcr)
{
   POOL *pool = NULL;
   JOB *job, *prev_job;
   JCR *wjcr;                     /* jcr of writing job */
   int count;


   apply_pool_overrides(jcr);

   if (!allow_duplicate_job(jcr)) {
      return false;
   }

   jcr->jr.PoolId = get_or_create_pool_record(jcr, jcr->pool->name());
   if (jcr->jr.PoolId == 0) {
      Dmsg1(dbglevel, "JobId=%d no PoolId\n", (int)jcr->JobId);
      Jmsg(jcr, M_FATAL, 0, _("Could not get or create a Pool record.\n"));
      return false;
   }
   /*
    * Note, at this point, pool is the pool for this job.  We
    *  transfer it to rpool (read pool), and a bit later,
    *  pool will be changed to point to the write pool,
    *  which comes from pool->NextPool.
    */
   jcr->rpool = jcr->pool;            /* save read pool */
   pm_strcpy(jcr->rpool_source, jcr->pool_source);
   Dmsg2(dbglevel, "Read pool=%s (From %s)\n", jcr->rpool->name(), jcr->rpool_source);

   if (!get_or_create_fileset_record(jcr)) {
      Dmsg1(dbglevel, "JobId=%d no FileSet\n", (int)jcr->JobId);
      Jmsg(jcr, M_FATAL, 0, _("Could not get or create the FileSet record.\n"));
      return false;
   }

   /* If we find a job or jobs to migrate it is previous_jr.JobId */
   count = getJob_to_migrate(jcr);
   if (count < 0) {
      return false;
   }
   if (count == 0) {
      set_mac_next_pool(jcr, &pool);
      return true;                    /* no work */
   }

   Dmsg1(dbglevel, "Back from getJob_to_migrate JobId=%d\n", (int)jcr->JobId);

   if (jcr->previous_jr.JobId == 0) {
      Dmsg1(dbglevel, "JobId=%d no previous JobId\n", (int)jcr->JobId);
      Jmsg(jcr, M_INFO, 0, _("No previous Job found to %s.\n"), jcr->get_ActionName(0));
      set_mac_next_pool(jcr, &pool);
      return true;                    /* no work */
   }

   if (create_restore_bootstrap_file(jcr) < 0) {
      Jmsg(jcr, M_FATAL, 0, _("Create bootstrap file failed.\n"));
      return false;
   }

   if (jcr->previous_jr.JobId == 0 || jcr->ExpectedFiles == 0) {
      jcr->setJobStatus(JS_Terminated);
      Dmsg1(dbglevel, "JobId=%d expected files == 0\n", (int)jcr->JobId);
      if (jcr->previous_jr.JobId == 0) {
         Jmsg(jcr, M_INFO, 0, _("No previous Job found to %s.\n"), jcr->get_ActionName(0));
      } else {
         Jmsg(jcr, M_INFO, 0, _("Previous Job has no data to %s.\n"), jcr->get_ActionName(0));
      }
      set_mac_next_pool(jcr, &pool);
      return true;                    /* no work */
   }


   Dmsg5(dbglevel, "JobId=%d: Current: Name=%s JobId=%d Type=%c Level=%c\n",
      (int)jcr->JobId,
      jcr->jr.Name, (int)jcr->jr.JobId,
      jcr->jr.JobType, jcr->jr.JobLevel);

   LockRes();
   job = (JOB *)GetResWithName(R_JOB, jcr->jr.Name);
   prev_job = (JOB *)GetResWithName(R_JOB, jcr->previous_jr.Name);
   UnlockRes();
   if (!job) {
      Jmsg(jcr, M_FATAL, 0, _("Job resource not found for \"%s\".\n"), jcr->jr.Name);
      return false;
   }
   if (!prev_job) {
      Jmsg(jcr, M_FATAL, 0, _("Previous Job resource not found for \"%s\".\n"),
           jcr->previous_jr.Name);
      return false;
   }


   /* Create a write jcr */
   wjcr = jcr->wjcr = new_jcr(sizeof(JCR), dird_free_jcr);
   memcpy(&wjcr->previous_jr, &jcr->previous_jr, sizeof(wjcr->previous_jr));

   /*
    * Turn the wjcr into a "real" job that takes on the aspects of
    *   the previous backup job "prev_job".
    */
   set_jcr_defaults(wjcr, prev_job);
   /* fix MA 987 cannot copy/migrate jobs with a Level=VF in the job resource
    * If the prev_job level definition is VirtualFull,
    * change it to Incremental, otherwise the writing SD would do a VF
    */
   if (wjcr->getJobLevel() == L_VIRTUAL_FULL) {
      wjcr->setJobLevel(L_INCREMENTAL);
   }

   /* Don't check for duplicates on this jobs. We do it before setup_job(),
    * because we check allow_duplicate_job() here.
    */
   wjcr->IgnoreDuplicateJobChecking = true;

   if (!setup_job(wjcr)) {
      Jmsg(jcr, M_FATAL, 0, _("setup job failed.\n"));
      return false;
   }

   /* Now reset the job record from the previous job */
   memcpy(&wjcr->jr, &jcr->previous_jr, sizeof(wjcr->jr));
   /* Update the jr to reflect the new values of PoolId and JobId. */
   wjcr->jr.PoolId = jcr->jr.PoolId;
   wjcr->jr.JobId = wjcr->JobId;
   wjcr->sd_client = true;
   //wjcr->setJobType(jcr->getJobType());
   wjcr->setJobLevel(jcr->getJobLevel());
   wjcr->spool_data = job->spool_data;     /* turn on spooling if requested in job */
   wjcr->spool_size = jcr->spool_size;
   jcr->spool_size = 0;

   /* Don't let WatchDog checks Max*Time value on this Job */
   wjcr->no_maxtime = true;
   Dmsg4(dbglevel, "wjcr: Name=%s JobId=%d Type=%c Level=%c\n",
      wjcr->jr.Name, (int)wjcr->jr.JobId,
      wjcr->jr.JobType, wjcr->jr.JobLevel);

   if (set_mac_next_pool(jcr, &pool)) {
      /* If pool storage specified, use it for restore */
      copy_rstorage(wjcr, pool->storage, _("Pool resource"));
      copy_rstorage(jcr, pool->storage, _("Pool resource"));

      wjcr->pool = jcr->pool;
      wjcr->next_pool = jcr->next_pool;
      wjcr->jr.PoolId = jcr->jr.PoolId;
   }

   return true;
}

/*
 * set_mac_next_pool() called by do_mac_init()
 * at differents stages.
 * The  idea here is to make a common subroutine for the
 *   NextPool's search code and to permit do_mac_init()
 *   to return with NextPool set in jcr struct.
 */
static bool set_mac_next_pool(JCR *jcr, POOL **retpool)
{
   POOL_DBR pr;
   POOL *pool;
   char ed1[100];

   /*
    * Get the PoolId used with the original job. Then
    *  find the pool name from the database record.
    */
   bmemset(&pr, 0, sizeof(pr));
   pr.PoolId = jcr->jr.PoolId;
   if (!db_get_pool_record(jcr, jcr->db, &pr)) {
      Jmsg(jcr, M_FATAL, 0, _("Pool for JobId %s not in database. ERR=%s\n"),
            edit_int64(pr.PoolId, ed1), db_strerror(jcr->db));
         return false;
   }
   /* Get the pool resource corresponding to the original job */
   pool = (POOL *)GetResWithName(R_POOL, pr.Name);
   *retpool = pool;
   if (!pool) {
      Jmsg(jcr, M_FATAL, 0, _("Pool resource \"%s\" not found.\n"), pr.Name);
      return false;
   }

   if (!apply_wstorage_overrides(jcr, pool)) {
      return false;
   }

   Dmsg2(dbglevel, "Write pool=%s read rpool=%s\n", jcr->pool->name(), jcr->rpool->name());

   return true;
}

/*
 * Send storage address and authentication to deblock the other
 *   job.
 */
static bool send_store_addr_to_sd(JCR *jcr, char *Job, char *sd_auth_key,
                 STORE *store, char *store_address, uint32_t store_port)
{
   int tls_need = BNET_TLS_NONE;

   /* TLS Requirement */
   if (store->tls_enable) {
      if (store->tls_require) {
         tls_need = BNET_TLS_REQUIRED;
      } else {
         tls_need = BNET_TLS_OK;
      }
   }

   /*
    * Send Storage address to the SD client
    */
   Dmsg2(200, "=== Job=%s sd auth key=%s\n", Job, sd_auth_key);
   jcr->store_bsock->fsend(storaddr, store_address, store_port,
      tls_need, Job, sd_auth_key);
   if (!response(jcr, jcr->store_bsock, OKstore, "Storage", DISPLAY_ERROR)) {
      Dmsg4(050, "Response fail for: JobId=%d storeaddr=%s:%d Job=%s\n",
           jcr->JobId, store_address, store_port, Job);
      Jmsg3(jcr, M_FATAL, 0, "Response failure: storeddr=%s:%d Job=%s\n",
            store_address, store_port, Job);

      return false;
   }
   return true;
}

/*
 * Do a Migration and Copy of a previous job
 *
 *  Returns:  false on failure
 *            true  on success
 */
bool do_mac(JCR *jcr)
{
   char ed1[100];
   BSOCK *sd, *wsd;
   JCR *wjcr = jcr->wjcr;    /* newly migrated job */
   bool ok = false;
   STORE *store;
   char *store_address;
   uint32_t store_port;

   /*
    * If wjcr is NULL, there is nothing to do for this job,
    *  so set a normal status, cleanup and return OK.
    */
   if (!wjcr) {
      jcr->setJobStatus(JS_Terminated);
      mac_cleanup(jcr, JS_Terminated, JS_Terminated);
      return true;
   }

   if (!db_get_job_record(jcr, jcr->db, &jcr->previous_jr)) {
      Jmsg(jcr, M_FATAL, 0, _("Could not get job record for JobId %s to %s. ERR=%s"),
           edit_int64(jcr->previous_jr.JobId, ed1),
           jcr->get_ActionName(0),
           db_strerror(jcr->db));
      jcr->setJobStatus(JS_Terminated);
      mac_cleanup(jcr, JS_Terminated, JS_Terminated);
      return true;
   }
   /* Make sure this job was not already migrated */
   if (jcr->previous_jr.JobType != JT_BACKUP &&
       jcr->previous_jr.JobType != JT_JOB_COPY) {
      Jmsg(jcr, M_INFO, 0, _("JobId %s already %s probably by another Job. %s stopped.\n"),
         edit_int64(jcr->previous_jr.JobId, ed1),
         jcr->get_ActionName(1),
         jcr->get_OperationName());
      jcr->setJobStatus(JS_Terminated);
      mac_cleanup(jcr, JS_Terminated, JS_Terminated);
      return true;
   }

   /* Print Job Start message */
   Jmsg(jcr, M_INFO, 0, _("Start %s JobId %s, Job=%s\n"),
        jcr->get_OperationName(), edit_uint64(jcr->JobId, ed1), jcr->Job);

   Dmsg3(200, "Start %s JobId %s, Job=%s\n",
        jcr->get_OperationName(), edit_uint64(jcr->JobId, ed1), jcr->Job);


   /*
    * Now separate the read and write storages. jcr has no wstor...
    *  they all go into wjcr.
    */
   free_rwstorage(wjcr);
   wjcr->rstore = NULL;
   wjcr->wstore = jcr->wstore;
   jcr->wstore = NULL;
   wjcr->wstorage = jcr->wstorage;
   jcr->wstorage = NULL;

   /* TODO: See priority with bandwidth parameter */
   if (jcr->job->max_bandwidth > 0) {
      jcr->max_bandwidth = jcr->job->max_bandwidth;
   } else if (jcr->client && jcr->client->max_bandwidth > 0) {
      jcr->max_bandwidth = jcr->client->max_bandwidth;
   }

   if (jcr->max_bandwidth > 0) {
      send_bwlimit(jcr, jcr->Job); /* Old clients don't have this command */
   }

   /*
    * Open a message channel connection with the Storage
    * daemon. This is to let him know that our client
    * will be contacting him for a backup  session.
    *
    */
   jcr->setJobStatus(JS_WaitSD);
   wjcr->setJobStatus(JS_WaitSD);

   /*
    * Start conversation with write Storage daemon
    */
   Dmsg0(200, "Connect to write (wjcr) storage daemon.\n");
   if (!connect_to_storage_daemon(wjcr, 10, SDConnectTimeout, 1)) {
      goto bail_out;
   }
   wsd = wjcr->store_bsock;

   /*
    * Start conversation with read Storage daemon
    */
   Dmsg1(200, "Connect to read (jcr) storage daemon. Jid=%d\n", jcr->JobId);
   if (!connect_to_storage_daemon(jcr, 10, SDConnectTimeout, 1)) {
      goto bail_out;
   }
   sd = jcr->store_bsock;
   if (jcr->client) {
      jcr->sd_calls_client = jcr->client->sd_calls_client;
   }

   Dmsg2(dbglevel, "Read store=%s, write store=%s\n",
      ((STORE *)jcr->rstorage->first())->name(),
      ((STORE *)wjcr->wstorage->first())->name());

   /*
    * Now start a job with the read Storage daemon sending the bsr.
    *  This call returns the sd_auth_key
    */
   Dmsg1(200, "Start job with read (jcr) storage daemon. Jid=%d\n", jcr->JobId);
   if (!start_storage_daemon_job(jcr, jcr->rstorage, NULL, /*send_bsr*/true)) {
      goto bail_out;
   }
   Dmsg0(150, "Read storage daemon connection OK\n");

   if (jcr->sd_calls_client) {
      wjcr->sd_calls_client = true;
      wjcr->sd_client = false;
   } else {
      wjcr->sd_calls_client = true;
      wjcr->sd_client = true;
   }

   /*
    * Now start a job with the write Storage daemon sending.
    */
   Dmsg1(200, "Start Job with write (wjcr) storage daemon. Jid=%d\n", jcr->JobId);
   if (!start_storage_daemon_job(wjcr, NULL, wjcr->wstorage, /*no_send_bsr*/false)) {
      goto bail_out;
   }
   Dmsg0(150, "Write storage daemon connection OK\n");


   /* Declare the job started to start the MaxRunTime check */
   jcr->setJobStarted();

   /*
    * We re-update the job start record so that the start
    *  time is set after the run before job.  This avoids
    *  that any files created by the run before job will
    *  be saved twice.  They will be backed up in the current
    *  job, but not in the next one unless they are changed.
    *  Without this, they will be backed up in this job and
    *  in the next job run because in that case, their date
    *   is after the start of this run.
    */
   jcr->start_time = time(NULL);
   jcr->jr.StartTime = jcr->start_time;
   jcr->jr.JobTDate = jcr->start_time;
   jcr->setJobStatus(JS_Running);

   /* Update job start record for this mac control job */
   if (!db_update_job_start_record(jcr, jcr->db, &jcr->jr)) {
      Jmsg(jcr, M_FATAL, 0, "%s", db_strerror(jcr->db));
      goto bail_out;
   }

   /* Declare the job started to start the MaxRunTime check */
   jcr->setJobStarted();

   wjcr->start_time = time(NULL);
   wjcr->jr.StartTime = wjcr->start_time;
   wjcr->jr.JobTDate = wjcr->start_time;
   wjcr->setJobStatus(JS_Running);


   /* Update job start record for the real mac backup job */
   if (!db_update_job_start_record(wjcr, wjcr->db, &wjcr->jr)) {
      Jmsg(jcr, M_FATAL, 0, "%s", db_strerror(wjcr->db));
      goto bail_out;
   }

   Dmsg4(dbglevel, "wjcr: Name=%s JobId=%d Type=%c Level=%c\n",
      wjcr->jr.Name, (int)wjcr->jr.JobId,
      wjcr->jr.JobType, wjcr->jr.JobLevel);


   if (jcr->sd_calls_client) {
      /*
       * Reading SD must call the "client" i.e. the writing SD
       */
      if (jcr->SDVersion < 3) {
         Jmsg(jcr, M_FATAL, 0, _("The Storage daemon does not support SDCallsClient.\n"));
         goto bail_out;
      }

      /* Setup the storage address and port */
      store = wjcr->wstore;
      if (store->SDDport == 0) {
         store->SDDport = store->SDport;
      }
      store_address = store->address;   /* note: store points to wstore */

      Dmsg2(200, "Start write message thread jid=%d Job=%s\n", wjcr->JobId, wjcr->Job);
      if (!run_storage_and_start_message_thread(wjcr, wsd)) {
         goto bail_out;
      }

      store_port = store->SDDport;

      /*
       * Send writing SD address to the reading SD
       */
      /* Send and wait for connection */
      /* ***FIXME*** this should probably be jcr->rstore, store_address, ...
       *   to get TLS right */
      if (!send_store_addr_to_sd(jcr, wjcr->Job, wjcr->sd_auth_key,
           store, store_address, store_port)) {
         goto bail_out;
      }

      /* Start read message thread */
      Dmsg2(200, "Start read message thread jid=%d Job=%s\n", jcr->JobId, jcr->Job);
      if (!run_storage_and_start_message_thread(jcr, sd)) {
         goto bail_out;
      }

   } else {
      /*
       * Writing SD must simulate an FD and call the reading SD
       *
       * Send Storage daemon address to the writing SD
       */
      store = jcr->rstore;
      if (store->SDDport == 0) {
         store->SDDport = store->SDport;
      }
      store_address = get_storage_address(jcr->client, store);
      store_port = store->SDDport;

      /* Start read message thread */
      Dmsg2(200, "Start read message thread jid=%d Job=%s\n", jcr->JobId, jcr->Job);
      if (!run_storage_and_start_message_thread(jcr, sd)) {
         goto bail_out;
      }

      /* Attempt connection for one hour */
      if (!send_store_addr_to_sd(wjcr, jcr->Job, jcr->sd_auth_key,
                                 store, store_address, store_port)) {
         goto bail_out;
      }
      /* Start write message thread */
      Dmsg2(200, "Start write message thread jid=%d Job=%s\n", wjcr->JobId, wjcr->Job);
      if (!run_storage_and_start_message_thread(wjcr, wsd)) {
         goto bail_out;
      }
   }

   jcr->setJobStatus(JS_Running);
   wjcr->setJobStatus(JS_Running);

   /* Pickup Job termination data */
   /* Note, the SD stores in jcr->JobFiles/ReadBytes/JobBytes/JobErrors */
   wait_for_storage_daemon_termination(wjcr);
   wjcr->setJobStatus(wjcr->SDJobStatus);
   wait_for_storage_daemon_termination(jcr);
   jcr->setJobStatus(jcr->SDJobStatus);

   flush_file_records(wjcr);     /* cached attribute + batch insert */

   ok = jcr->is_JobStatus(JS_Terminated) && wjcr->is_JobStatus(JS_Terminated);

bail_out:
   /* Put back jcr write storages for proper cleanup */
   jcr->wstorage = wjcr->wstorage;
   jcr->wstore = wjcr->wstore;
   wjcr->wstore = NULL;
   wjcr->wstorage = NULL;
   wjcr->file_bsock = NULL;

   if (ok) {
      mac_cleanup(jcr, jcr->JobStatus, wjcr->JobStatus);
   }
   return ok;
}

/*
 * Called from mac_sql.c for each migration/copy job to start
 */
void start_mac_job(JCR *jcr)
{
   UAContext *ua = new_ua_context(jcr);
   char ed1[50];
   char args[MAX_NAME_LENGTH + 50];

   ua->batch = true;
   Mmsg(ua->cmd, "run job=\"%s\" jobid=%s ignoreduplicatecheck=yes pool=\"%s\"",
        jcr->job->name(), edit_uint64(jcr->MigrateJobId, ed1),
        jcr->pool->name());
   if (jcr->next_pool) {
      bsnprintf(args, sizeof(args), " nextpool=\"%s\"", jcr->next_pool->name());
      pm_strcat(ua->cmd, args);
   }
   Dmsg2(dbglevel, "=============== %s cmd=%s\n", jcr->get_OperationName(), ua->cmd);
   parse_ua_args(ua);                 /* parse command */
   JobId_t jobid = run_cmd(ua, ua->cmd);
   if (jobid == 0) {
      Jmsg(jcr, M_ERROR, 0, _("Could not start migration/copy job.\n"));
   } else {
      Jmsg(jcr, M_INFO, 0, _("%s JobId %d started.\n"), jcr->get_OperationName(), (int)jobid);
   }
   free_ua_context(ua);
}

/*
 * Release resources allocated during backup.
 */
/* ***FIXME*** implement writeTermCode */
void mac_cleanup(JCR *jcr, int TermCode, int writeTermCode)
{
   char sdt[MAX_TIME_LENGTH], edt[MAX_TIME_LENGTH];
   char ec1[30], ec2[30], ec3[30], ec4[30], ec5[30], elapsed[50];
   char ec6[50], ec7[50], ec8[50], ec9[30], ec10[30], edl[50];
   char sd_term_msg[100];
   POOL_MEM term_code;
   POOL_MEM term_msg;
   int msg_type = M_INFO;
   MEDIA_DBR mr;
   double kbps;
   utime_t RunTime;
   bool goterrors=false;
   JCR *wjcr = jcr->wjcr;
   POOL_MEM query(PM_MESSAGE);
   POOL_MEM vol_info;

   remove_dummy_jobmedia_records(jcr);

   Dmsg2(100, "Enter mac_cleanup %d %c\n", TermCode, TermCode);
   update_job_end(jcr, TermCode);

   /*
    * Check if we actually did something.
    *  wjcr is jcr of the newly migrated job.
    */
   if (wjcr) {
      char old_jobid[50], new_jobid[50];

      edit_uint64(jcr->previous_jr.JobId, old_jobid);
      edit_uint64(wjcr->jr.JobId, new_jobid);

      wjcr->JobFiles = jcr->JobFiles = wjcr->SDJobFiles;
      wjcr->JobBytes = jcr->JobBytes = wjcr->SDJobBytes;
      wjcr->jr.RealEndTime = 0;
      wjcr->jr.PriorJobId = jcr->previous_jr.JobId;
      if (jcr->previous_jr.PriorJob[0]) {
         bstrncpy(wjcr->jr.PriorJob, jcr->previous_jr.PriorJob, sizeof(wjcr->jr.PriorJob));
      } else {
         bstrncpy(wjcr->jr.PriorJob, jcr->previous_jr.Job, sizeof(wjcr->jr.PriorJob));
      }
      wjcr->JobErrors += wjcr->SDErrors;
      update_job_end(wjcr, TermCode);

      /* Update final items to set them to the previous job's values */
      Mmsg(query, "UPDATE Job SET StartTime='%s',EndTime='%s',"
                  "JobTDate=%s WHERE JobId=%s",
         jcr->previous_jr.cStartTime, jcr->previous_jr.cEndTime,
         edit_uint64(jcr->previous_jr.JobTDate, ec1),
         new_jobid);
      db_sql_query(wjcr->db, query.c_str(), NULL, NULL);

      goterrors = jcr->SDErrors > 0 || jcr->JobErrors > 0 ||
         jcr->SDJobStatus == JS_Canceled ||
         jcr->SDJobStatus == JS_ErrorTerminated ||
         jcr->SDJobStatus == JS_FatalError ||
         jcr->JobStatus == JS_FatalError;

      if (goterrors && jcr->getJobType() == JT_MIGRATE && jcr->JobStatus == JS_Terminated) {
         Jmsg(jcr, M_WARNING, 0, _("Found errors during the migration process. "
                                   "The original job %s will be kept in the catalog "
                                   "and the Migration job will be marked in Error\n"), old_jobid);
      }

      /*
       * If we terminated a migration normally:
       *   - mark the previous job as migrated
       *   - move any Log records to the new JobId
       *   - Purge the File records from the previous job
       */
      if (!goterrors && jcr->getJobType() == JT_MIGRATE && jcr->JobStatus == JS_Terminated) {
         Mmsg(query, "UPDATE Job SET Type='%c' WHERE JobId=%s",
              (char)JT_MIGRATED_JOB, old_jobid);
         db_sql_query(wjcr->db, query.c_str(), NULL, NULL);
         UAContext *ua = new_ua_context(jcr);
         /* Move JobLog to new JobId */
         Mmsg(query, "UPDATE Log SET JobId=%s WHERE JobId=%s",
           new_jobid, old_jobid);
         db_sql_query(wjcr->db, query.c_str(), NULL, NULL);

         /* Move RestoreObjects */
         Mmsg(query, "UPDATE RestoreObject SET JobId=%s WHERE JobId=%s",
           new_jobid, old_jobid);
         db_sql_query(wjcr->db, query.c_str(), NULL, NULL);

         if (jcr->job->PurgeMigrateJob) {
            /* Purge old Job record */
            purge_jobs_from_catalog(ua, old_jobid);
         } else {
            /* Purge all old file records, but leave Job record */
            purge_files_from_jobs(ua, old_jobid);
         }

         free_ua_context(ua);
      }

      /*
       * If we terminated a Copy (rather than a Migration) normally:
       *   - copy any Log records to the new JobId
       *   - set type="Job Copy" for the new job
       */
      if (goterrors || (jcr->getJobType() == JT_COPY && jcr->JobStatus == JS_Terminated)) {
         /* Copy JobLog to new JobId */
         Mmsg(query, "INSERT INTO Log (JobId, Time, LogText ) "
                      "SELECT %s, Time, LogText FROM Log WHERE JobId=%s",
              new_jobid, old_jobid);
         db_sql_query(wjcr->db, query.c_str(), NULL, NULL);

         /* We are in a real copy job */
         Mmsg(query, "UPDATE Job SET Type='%c' WHERE JobId=%s",
              (char)JT_JOB_COPY, new_jobid);
         db_sql_query(wjcr->db, query.c_str(), NULL, NULL);

         /* Copy RestoreObjects */
         Mmsg(query, "INSERT INTO RestoreObject (ObjectName,PluginName,RestoreObject,"
              "ObjectLength,ObjectFullLength,ObjectIndex,ObjectType,"
              "ObjectCompression,FileIndex,JobId) "
        "SELECT ObjectName,PluginName,RestoreObject,"
          "ObjectLength,ObjectFullLength,ObjectIndex,ObjectType,"
          "ObjectCompression,FileIndex,%s FROM RestoreObject WHERE JobId=%s",
           new_jobid, old_jobid);
         db_sql_query(wjcr->db, query.c_str(), NULL, NULL);
      }

      if (!db_get_job_record(jcr, jcr->db, &jcr->jr)) {
         Jmsg(jcr, M_WARNING, 0, _("Error getting Job record for Job report: ERR=%s"),
            db_strerror(jcr->db));
         jcr->setJobStatus(JS_ErrorTerminated);
      }

      update_bootstrap_file(wjcr);

      if (!db_get_job_volume_names(wjcr, wjcr->db, wjcr->jr.JobId, &wjcr->VolumeName)) {
         /*
          * Note, if the job has failed, most likely it did not write any
          *  tape, so suppress this "error" message since in that case
          *  it is normal.  Or look at it the other way, only for a
          *  normal exit should we complain about this error.
          */
         if (jcr->JobStatus == JS_Terminated && jcr->jr.JobBytes) {
            Jmsg(jcr, M_ERROR, 0, "%s", db_strerror(wjcr->db));
         }
         wjcr->VolumeName[0] = 0;         /* none */
      }

      if (wjcr->VolumeName[0]) {
         /* Find last volume name. Multiple vols are separated by | */
         char *p = strrchr(wjcr->VolumeName, '|');
         if (p) {
            p++;                         /* skip | */
         } else {
            p = wjcr->VolumeName;     /* no |, take full name */
         }
         bstrncpy(mr.VolumeName, p, sizeof(mr.VolumeName));
         if (!db_get_media_record(jcr, jcr->db, &mr)) {
            Jmsg(jcr, M_WARNING, 0, _("Error getting Media record for Volume \"%s\": ERR=%s"),
               mr.VolumeName, db_strerror(jcr->db));
         }
      }

      /* We keep all information in the catalog because most of the work is
       * done, and users might restore things from what we have
       */
      if (goterrors) {
         jcr->setJobStatus(JS_ErrorTerminated);
         Mmsg(query, "UPDATE Job SET JobStatus='%c' WHERE JobId=%s",
              JS_ErrorTerminated, new_jobid);
         db_sql_query(wjcr->db, query.c_str(), NULL, NULL);
      }
   }

   switch (jcr->JobStatus) {
   case JS_Terminated:
      if (jcr->JobErrors || jcr->SDErrors) {
         Mmsg(term_msg, _("%%s OK -- %s"), jcr->StatusErrMsg[0] ? jcr->StatusErrMsg : _("with warnings"));
      } else {
         Mmsg(term_msg, _("%%s OK"));
      }
      break;
   case JS_FatalError:
   case JS_ErrorTerminated:
      Mmsg(term_msg, _("*** %%s Error ***"));
      msg_type = M_ERROR;          /* Generate error message */
      terminate_sd_msg_chan_thread(jcr);
      terminate_sd_msg_chan_thread(wjcr);
      break;
   case JS_Canceled:
      Mmsg(term_msg, _("%%s Canceled"));
      terminate_sd_msg_chan_thread(jcr);
      terminate_sd_msg_chan_thread(wjcr);
      break;
   default:
      Mmsg(term_msg, _("Inappropriate %s term code"));
      break;
   }

   if (!wjcr) {                 /* We did nothing */
      goterrors = jcr->JobErrors > 0 || jcr->JobStatus == JS_FatalError;
      if (!goterrors) {
         if (jcr->getJobType() == JT_MIGRATE && jcr->previous_jr.JobId != 0) {
            /* Mark previous job as migrated */
            Mmsg(query, "UPDATE Job SET Type='%c' WHERE JobId=%s",
                 (char)JT_MIGRATED_JOB, edit_uint64(jcr->previous_jr.JobId, ec1));
            db_sql_query(jcr->db, query.c_str(), NULL, NULL);
         }
         Mmsg(term_msg, _("%%s -- no files to %%s"));
      }
   }

   Mmsg(term_code, term_msg.c_str(), jcr->get_OperationName(), jcr->get_ActionName(0));
   bstrftimes(sdt, sizeof(sdt), jcr->jr.StartTime);
   bstrftimes(edt, sizeof(edt), jcr->jr.EndTime);
   RunTime = jcr->jr.EndTime - jcr->jr.StartTime;
   if (RunTime <= 0) {
      RunTime = 1;
   }
   kbps = (double)jcr->SDJobBytes / (1000.0 * (double)RunTime);

   jobstatus_to_ascii(jcr->SDJobStatus, sd_term_msg, sizeof(sd_term_msg));

   /* Edit string for last volume size */
   if (mr.VolABytes != 0) {
      Mmsg(vol_info, _("meta: %s (%sB) aligned: %s (%sB)"),
        edit_uint64_with_commas(mr.VolBytes, ec4),
        edit_uint64_with_suffix(mr.VolBytes, ec5),
        edit_uint64_with_commas(mr.VolABytes, ec9),
        edit_uint64_with_suffix(mr.VolBytes, ec10));
   } else {
     Mmsg(vol_info, _("%s (%sB)"),
        edit_uint64_with_commas(mr.VolBytes, ec4),
        edit_uint64_with_suffix(mr.VolBytes, ec5));
   }

   Jmsg(jcr, msg_type, 0, _("%s %s %s (%s):\n"
"  Build OS:               %s %s %s\n"
"  Prev Backup JobId:      %s\n"
"  Prev Backup Job:        %s\n"
"  New Backup JobId:       %s\n"
"  Current JobId:          %s\n"
"  Current Job:            %s\n"
"  Backup Level:           %s%s\n"
"  Client:                 %s\n"
"  FileSet:                \"%s\" %s\n"
"  Read Pool:              \"%s\" (From %s)\n"
"  Read Storage:           \"%s\" (From %s)\n"
"  Write Pool:             \"%s\" (From %s)\n"
"  Write Storage:          \"%s\" (From %s)\n"
"  Catalog:                \"%s\" (From %s)\n"
"  Start time:             %s\n"
"  End time:               %s\n"
"  Elapsed time:           %s\n"
"  Priority:               %d\n"
"  SD Files Written:       %s\n"
"  SD Bytes Written:       %s (%sB)\n"
"  Rate:                   %.1f KB/s\n"
"  Volume name(s):         %s\n"
"  Volume Session Id:      %d\n"
"  Volume Session Time:    %d\n"
"  Last Volume Bytes:      %s\n"
"  SD Errors:              %d\n"
"  SD termination status:  %s\n"
"  Termination:            %s\n\n"),
        BACULA, my_name, VERSION, LSMDATE,
        HOST_OS, DISTNAME, DISTVER,
        edit_uint64(jcr->previous_jr.JobId, ec6),
        jcr->previous_jr.Job,
        wjcr ? edit_uint64(wjcr->jr.JobId, ec7) : "0",
        edit_uint64(jcr->jr.JobId, ec8),
        jcr->jr.Job,
        level_to_str(edl, sizeof(edl), jcr->getJobLevel()), jcr->since,
        jcr->client->name(),
        jcr->fileset->name(), jcr->FSCreateTime,
        jcr->rpool->name(), jcr->rpool_source,
        jcr->rstore?jcr->rstore->name():"*None*",
        NPRT(jcr->rstore_source),
        jcr->pool->name(), jcr->pool_source,
        jcr->wstore?jcr->wstore->name():"*None*",
        NPRT(jcr->wstore_source),
        jcr->catalog->name(), jcr->catalog_source,
        sdt,
        edt,
        edit_utime(RunTime, elapsed, sizeof(elapsed)),
        jcr->JobPriority,
        edit_uint64_with_commas(jcr->SDJobFiles, ec1),
        edit_uint64_with_commas(jcr->SDJobBytes, ec2),
        edit_uint64_with_suffix(jcr->SDJobBytes, ec3),
        (float)kbps,
        wjcr ? wjcr->VolumeName : "",
        jcr->VolSessionId,
        jcr->VolSessionTime,
        vol_info.c_str(),
        jcr->SDErrors,
        sd_term_msg,
        term_code.c_str());

   Dmsg0(100, "Leave migrate_cleanup()\n");
}

bool set_mac_wstorage(UAContext *ua, JCR *jcr, POOL *pool, POOL *next_pool,
         const char *source)
{
   if (!next_pool) {
      if (ua) {
         ua->error_msg(_("No Next Pool specification found in Pool \"%s\".\n"),
           pool->hdr.name);
      } else {
         Jmsg(jcr, M_FATAL, 0, _("No Next Pool specification found in Pool \"%s\".\n"),
            pool->hdr.name);
      }
      return false;
   }

   if (!next_pool->storage || next_pool->storage->size() == 0) {
      Jmsg(jcr, M_FATAL, 0, _("No Storage specification found in Next Pool \"%s\".\n"),
         next_pool->name());
      return false;
   }

   /* If pool storage specified, use it instead of job storage for backup */
   copy_wstorage(jcr, next_pool->storage, source);

   return true;
}
