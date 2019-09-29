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
/*
 *   Bacula Director -- vbackup.c -- responsible for doing virtual
 *     backup jobs or in other words, consolidation or synthetic
 *     backups.
 *
 *     Kern Sibbald, July MMVIII
 *
 *  Basic tasks done here:
 *     Open DB and create records for this job.
 *     Figure out what Jobs to copy.
 *     Open Message Channel with Storage daemon to tell him a job will be starting.
 *     Open connection with File daemon and pass him commands
 *       to do the backup.
 *     When the File daemon finishes the job, update the DB.
 */

#include "bacula.h"
#include "dird.h"
#include "ua.h"

static const int dbglevel = 10;

static bool create_bootstrap_file(JCR *jcr, char *jobids);
void vbackup_cleanup(JCR *jcr, int TermCode);

/*
 * Called here before the job is run to do the job
 *   specific setup.
 */
bool do_vbackup_init(JCR *jcr)
{

   if (!get_or_create_fileset_record(jcr)) {
      Dmsg1(dbglevel, "JobId=%d no FileSet\n", (int)jcr->JobId);
      return false;
   }

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

   /* If pool storage specified, use it for virtual full */
   copy_rstorage(jcr, jcr->pool->storage, _("Pool resource"));

   Dmsg2(dbglevel, "Read pool=%s (From %s)\n", jcr->rpool->name(), jcr->rpool_source);

   jcr->start_time = time(NULL);
   jcr->jr.StartTime = jcr->start_time;
   jcr->jr.JobLevel = L_FULL;      /* we want this to appear as a Full backup */
   if (!db_update_job_start_record(jcr, jcr->db, &jcr->jr)) {
      Jmsg(jcr, M_FATAL, 0, "%s", db_strerror(jcr->db));
   }

   if (!apply_wstorage_overrides(jcr, jcr->pool)) {
      return false;
   }

   Dmsg2(dbglevel, "Write pool=%s read rpool=%s\n", jcr->pool->name(), jcr->rpool->name());

   return true;
}

/*
 * Do a virtual backup, which consolidates all previous backups into
 *  a sort of synthetic Full.
 *
 *  Returns:  false on failure
 *            true  on success
 */
bool do_vbackup(JCR *jcr)
{
   char        level_computed = L_FULL;
   char        ed1[100];
   BSOCK      *sd;
   char       *p;
   sellist     sel;
   db_list_ctx jobids;
   UAContext *ua;

   Dmsg2(100, "rstorage=%p wstorage=%p\n", jcr->rstorage, jcr->wstorage);
   Dmsg2(100, "Read store=%s, write store=%s\n",
      ((STORE *)jcr->rstorage->first())->name(),
      ((STORE *)jcr->wstorage->first())->name());

   jcr->wasVirtualFull = true;        /* remember where we came from */

   /* Print Job Start message */
   Jmsg(jcr, M_INFO, 0, _("Start Virtual Backup JobId %s, Job=%s\n"),
        edit_uint64(jcr->JobId, ed1), jcr->Job);
   if (!jcr->accurate) {
      Jmsg(jcr, M_WARNING, 0,
_("This Job is not an Accurate backup so is not equivalent to a Full backup.\n"));
   }

   if (jcr->JobIds && *jcr->JobIds) {
      JOB_DBR jr;
      db_list_ctx status;
      POOL_MEM query(PM_MESSAGE);

      memset(&jr, 0, sizeof(jr));

      if (is_an_integer(jcr->JobIds)) {
         /* Single JobId, so start the accurate code based on this id */

         jr.JobId = str_to_int64(jcr->JobIds);
         if (!db_get_job_record(jcr, jcr->db, &jr)) {
            Jmsg(jcr, M_ERROR, 0,
                 _("Unable to get Job record for JobId=%s: ERR=%s\n"),
                 jcr->JobIds, db_strerror(jcr->db));
            return false;
         }
         Jmsg(jcr, M_INFO,0,_("Selecting jobs to build the Full state at %s\n"),
              jr.cStartTime);

         jr.JobLevel = L_INCREMENTAL; /* Take Full+Diff+Incr */
         db_get_accurate_jobids(jcr, jcr->db, &jr, &jobids);

      } else if (sel.set_string(jcr->JobIds, true)) {
         /* Found alljobid keyword */
         if (jcr->use_all_JobIds) {
            jobids.count = sel.size();
            pm_strcpy(jobids.list, sel.get_expanded_list());

         /* Need to apply some filter on the job name */
         } else {
            Mmsg(query,
                 "SELECT JobId FROM Job "
                  "WHERE Job.Name = '%s' "
                    "AND Job.JobId IN (%s) "
                  "ORDER BY JobTDate ASC",
                 jcr->job->name(),
                 sel.get_expanded_list());

            db_sql_query(jcr->db, query.c_str(),  db_list_handler, &jobids);
         }

         if (jobids.count == 0) {
            Jmsg(jcr, M_FATAL, 0, _("No valid Jobs found from user selection.\n"));
            return false;
         }

         Jmsg(jcr, M_INFO, 0, _("Using user supplied JobIds=%s\n"),
              jobids.list);

         /* Check status */
         Mmsg(query,
              "SELECT Level FROM Job "
               "WHERE Job.JobId IN (%s) "
               "GROUP BY Level",
              jobids.list);

         /* Will produce something like F,D,I or F,I */
         db_sql_query(jcr->db, query.c_str(),  db_list_handler, &status);

         /* If no full found in the list, we build a "virtualdiff" or
          * a "virtualinc".
          */
         if (strchr(status.list, L_FULL) == NULL) {
            if (strchr(status.list, L_DIFFERENTIAL)) {
               level_computed = L_DIFFERENTIAL;
               Jmsg(jcr, M_INFO, 0, _("No previous Full found in list, "
                                      "using Differential level\n"));

            } else {
               level_computed = L_INCREMENTAL;
               Jmsg(jcr, M_INFO, 0, _("No previous Full found in list, "
                                      "using Incremental level\n"));
            }
         }
      }

   } else {                     /* No argument provided */
      jcr->jr.JobLevel = L_VIRTUAL_FULL;
      /* We restrict the search of the JobIds to the current job */
      bstrncpy(jcr->jr.Name, jcr->job->name(), sizeof(jcr->jr.Name));
      db_get_accurate_jobids(jcr, jcr->db, &jcr->jr, &jobids);
      Dmsg1(10, "Accurate jobids=%s\n", jobids.list);
   }

   if (jobids.count == 0) {
      Jmsg(jcr, M_FATAL, 0, _("No previous Jobs found.\n"));
      return false;
   }
   jobids.count -= jcr->job->BackupsToKeep;
   if (jobids.count <= 0) {
      Jmsg(jcr, M_WARNING, 0, _("Insufficient Backups to Keep.\n"));
      return false;
   }
   if (jobids.count == 1) {
      Jmsg(jcr, M_WARNING, 0, _("Only one Job found. Consolidation not needed.\n"));
      return false;
   }

   /* Remove number of JobIds we want to keep */
   for (int i=0; i < (int)jcr->job->BackupsToKeep; i++) {
      p = strrchr(jobids.list, ',');    /* find last jobid */
      if (p == NULL) {
         break;
      } else {
         *p = 0;
      }
   }

   /* Full by default, or might be Incr/Diff when jobid= is used */
   jcr->jr.JobLevel = level_computed;

   Jmsg(jcr, M_INFO, 0, "Consolidating JobIds=%s\n", jobids.list);

   /*
    * Now we find the last job that ran and store it's info in
    *  the previous_jr record.  We will set our times to the
    *  values from that job so that anything changed after that
    *  time will be picked up on the next backup.
    */
   p = strrchr(jobids.list, ',');           /* find last jobid */
   if (p != NULL) {
      p++;
   } else {
      p = jobids.list;
   }
   memset(&jcr->previous_jr, 0, sizeof(jcr->previous_jr));
   jcr->previous_jr.JobId = str_to_int64(p);
   Dmsg1(10, "Previous JobId=%s\n", p);
   if (!db_get_job_record(jcr, jcr->db, &jcr->previous_jr)) {
      Jmsg(jcr, M_FATAL, 0, _("Error getting Job record for previous Job: ERR=%s"),
               db_strerror(jcr->db));
      return false;
   }

   if (!create_bootstrap_file(jcr, jobids.list)) {
      Jmsg(jcr, M_FATAL, 0, _("Could not get or create the FileSet record.\n"));
      return false;
   }

   /*
    * Open a message channel connection with the Storage
    * daemon. This is to let him know that our client
    * will be contacting him for a backup  session.
    *
    */
   Dmsg0(110, "Open connection with storage daemon\n");
   jcr->setJobStatus(JS_WaitSD);
   /*
    * Start conversation with Storage daemon
    */
   if (!connect_to_storage_daemon(jcr, 10, SDConnectTimeout, 1)) {
      return false;
   }
   sd = jcr->store_bsock;

   /*
    * Now start a job with the Storage daemon
    */
   if (!start_storage_daemon_job(jcr, jcr->rstorage, jcr->wstorage, /*send_bsr*/true)) {
      return false;
   }
   Dmsg0(100, "Storage daemon connection OK\n");

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

   /* Update job start record */
   if (!db_update_job_start_record(jcr, jcr->db, &jcr->jr)) {
      Jmsg(jcr, M_FATAL, 0, "%s", db_strerror(jcr->db));
      return false;
   }

   /* Declare the job started to start the MaxRunTime check */
   jcr->setJobStarted();

   /*
    * Start the job prior to starting the message thread below
    * to avoid two threads from using the BSOCK structure at
    * the same time.
    */
   if (!sd->fsend("run")) {
      return false;
   }

   /*
    * Now start a Storage daemon message thread
    */
   if (!start_storage_daemon_message_thread(jcr)) {
      return false;
   }

   jcr->setJobStatus(JS_Running);

   /* Pickup Job termination data */
   /* Note, the SD stores in jcr->JobFiles/ReadBytes/JobBytes/JobErrors */
   wait_for_storage_daemon_termination(jcr);
   jcr->setJobStatus(jcr->SDJobStatus);
   flush_file_records(jcr);     /* cached attribute + batch insert */

   if (jcr->JobStatus != JS_Terminated) {
      return false;
   }
   if (jcr->job->DeleteConsolidatedJobs) {
      ua = new_ua_context(jcr);
      purge_jobs_from_catalog(ua, jobids.list);
      free_ua_context(ua);
      Jmsg(jcr, M_INFO, 0, _("Deleted consolidated JobIds=%s\n"), jobids.list);
   }

   vbackup_cleanup(jcr, jcr->JobStatus);
   return true;
}


/*
 * Release resources allocated during backup.
 */
void vbackup_cleanup(JCR *jcr, int TermCode)
{
   char sdt[50], edt[50], schedt[50];
   char ec1[30], ec3[30], ec4[30], compress[50];
   char ec7[30], ec8[30], elapsed[50];
   char term_code[100], sd_term_msg[100];
   const char *term_msg;
   int msg_type = M_INFO;
   MEDIA_DBR mr;
   CLIENT_DBR cr;
   double kbps, compression;
   utime_t RunTime;
   POOL_MEM query(PM_MESSAGE);

   Dmsg2(100, "Enter vbackup_cleanup %d %c\n", TermCode, TermCode);
   memset(&cr, 0, sizeof(cr));

   jcr->jr.JobLevel = L_FULL;   /* we want this to appear as a Full backup */
   jcr->JobFiles = jcr->SDJobFiles;
   jcr->JobBytes = jcr->SDJobBytes;
   update_job_end(jcr, TermCode);

   /* Update final items to set them to the previous job's values */
   Mmsg(query, "UPDATE Job SET StartTime='%s',EndTime='%s',"
               "JobTDate=%s WHERE JobId=%s",
      jcr->previous_jr.cStartTime, jcr->previous_jr.cEndTime,
      edit_uint64(jcr->previous_jr.JobTDate, ec1),
      edit_uint64(jcr->JobId, ec3));
   db_sql_query(jcr->db, query.c_str(), NULL, NULL);

   /* Get the fully updated job record */
   if (!db_get_job_record(jcr, jcr->db, &jcr->jr)) {
      Jmsg(jcr, M_WARNING, 0, _("Error getting Job record for Job report: ERR=%s"),
         db_strerror(jcr->db));
      jcr->setJobStatus(JS_ErrorTerminated);
   }

   bstrncpy(cr.Name, jcr->client->name(), sizeof(cr.Name));
   if (!db_get_client_record(jcr, jcr->db, &cr)) {
      Jmsg(jcr, M_WARNING, 0, _("Error getting Client record for Job report: ERR=%s"),
         db_strerror(jcr->db));
   }

   bstrncpy(mr.VolumeName, jcr->VolumeName, sizeof(mr.VolumeName));
   if (!db_get_media_record(jcr, jcr->db, &mr)) {
      Jmsg(jcr, M_WARNING, 0, _("Error getting Media record for Volume \"%s\": ERR=%s"),
         mr.VolumeName, db_strerror(jcr->db));
      jcr->setJobStatus(JS_ErrorTerminated);
   }

   update_bootstrap_file(jcr);

   switch (jcr->JobStatus) {
      case JS_Terminated:
         if (jcr->JobErrors || jcr->SDErrors) {
            term_msg = _("Backup OK -- with warnings");
         } else {
            term_msg = _("Backup OK");
         }
         break;
      case JS_FatalError:
      case JS_ErrorTerminated:
         term_msg = _("*** Backup Error ***");
         msg_type = M_ERROR;          /* Generate error message */
         terminate_sd_msg_chan_thread(jcr);
         break;
      case JS_Canceled:
         term_msg = _("Backup Canceled");
         terminate_sd_msg_chan_thread(jcr);
         break;
      case JS_Incomplete:
         term_msg = _("Backup failed -- Incomplete");
         break;
      default:
         term_msg = term_code;
         sprintf(term_code, _("Inappropriate term code: %c\n"), jcr->JobStatus);
         break;
   }
   bstrftimes(schedt, sizeof(schedt), jcr->jr.SchedTime);
   bstrftimes(sdt, sizeof(sdt), jcr->jr.StartTime);
   bstrftimes(edt, sizeof(edt), jcr->jr.EndTime);
   RunTime = jcr->jr.EndTime - jcr->jr.StartTime;
   if (RunTime <= 0) {
      RunTime = 1;
   }
   kbps = ((double)jcr->jr.JobBytes) / (1000.0 * (double)RunTime);
   if (!db_get_job_volume_names(jcr, jcr->db, jcr->jr.JobId, &jcr->VolumeName)) {
      /*
       * Note, if the job has erred, most likely it did not write any
       *  tape, so suppress this "error" message since in that case
       *  it is normal.  Or look at it the other way, only for a
       *  normal exit should we complain about this error.
       */
      if (jcr->JobStatus == JS_Terminated && jcr->jr.JobBytes) {
         Jmsg(jcr, M_ERROR, 0, "%s", db_strerror(jcr->db));
      }
      jcr->VolumeName[0] = 0;         /* none */
   }

   if (jcr->ReadBytes == 0) {
      bstrncpy(compress, "None", sizeof(compress));
   } else {
      compression = (double)100 - 100.0 * ((double)jcr->JobBytes / (double)jcr->ReadBytes);
      if (compression < 0.5) {
         bstrncpy(compress, "None", sizeof(compress));
      } else {
         bsnprintf(compress, sizeof(compress), "%.1f %%", compression);
      }
   }
   jobstatus_to_ascii(jcr->SDJobStatus, sd_term_msg, sizeof(sd_term_msg));

   Jmsg(jcr, msg_type, 0, _("%s %s %s (%s):\n"
"  Build OS:               %s %s %s\n"
"  JobId:                  %d\n"
"  Job:                    %s\n"
"  Backup Level:           Virtual Full\n"
"  Client:                 \"%s\" %s\n"
"  FileSet:                \"%s\" %s\n"
"  Pool:                   \"%s\" (From %s)\n"
"  Catalog:                \"%s\" (From %s)\n"
"  Storage:                \"%s\" (From %s)\n"
"  Scheduled time:         %s\n"
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
"  Last Volume Bytes:      %s (%sB)\n"
"  SD Errors:              %d\n"
"  SD termination status:  %s\n"
"  Termination:            %s\n\n"),
        BACULA, my_name, VERSION, LSMDATE,
        HOST_OS, DISTNAME, DISTVER,
        jcr->jr.JobId,
        jcr->jr.Job,
        jcr->client->name(), cr.Uname,
        jcr->fileset->name(), jcr->FSCreateTime,
        jcr->pool->name(), jcr->pool_source,
        jcr->catalog->name(), jcr->catalog_source,
        jcr->wstore->name(), jcr->wstore_source,
        schedt,
        sdt,
        edt,
        edit_utime(RunTime, elapsed, sizeof(elapsed)),
        jcr->JobPriority,
        edit_uint64_with_commas(jcr->jr.JobFiles, ec1),
        edit_uint64_with_commas(jcr->jr.JobBytes, ec3),
        edit_uint64_with_suffix(jcr->jr.JobBytes, ec4),
        kbps,
        jcr->VolumeName,
        jcr->VolSessionId,
        jcr->VolSessionTime,
        edit_uint64_with_commas(mr.VolBytes, ec7),
        edit_uint64_with_suffix(mr.VolBytes, ec8),
        jcr->SDErrors,
        sd_term_msg,
        term_msg);

   Dmsg0(100, "Leave vbackup_cleanup()\n");
}

/*
 * This callback routine is responsible for inserting the
 *  items it gets into the bootstrap structure. For each JobId selected
 *  this routine is called once for each file. We do not allow
 *  duplicate filenames, but instead keep the info from the most
 *  recent file entered (i.e. the JobIds are assumed to be sorted)
 *
 *   See uar_sel_files in sql_cmds.c for query that calls us.
 *      row[0]=Path, row[1]=Filename, row[2]=FileIndex
 *      row[3]=JobId row[4]=LStat
 */
int insert_bootstrap_handler(void *ctx, int num_fields, char **row)
{
   JobId_t JobId;
   int FileIndex;
   rblist *bsr_list = (rblist *)ctx;

   JobId = str_to_int64(row[3]);
   FileIndex = str_to_int64(row[2]);
   add_findex(bsr_list, JobId, FileIndex);
   return 0;
}


static bool create_bootstrap_file(JCR *jcr, char *jobids)
{
   RESTORE_CTX rx;
   UAContext *ua;
   RBSR *bsr = NULL;

   memset(&rx, 0, sizeof(rx));
   rx.bsr_list = New(rblist(bsr, &bsr->link));
   ua = new_ua_context(jcr);
   rx.JobIds = jobids;

#define new_get_file_list
#ifdef new_get_file_list
   if (!db_open_batch_connexion(jcr, jcr->db)) {
      Jmsg0(jcr, M_FATAL, 0, "Can't get batch sql connexion");
      return false;
   }

   if (!db_get_file_list(jcr, jcr->db_batch, jobids, DBL_USE_DELTA | DBL_ALL_FILES,
                         insert_bootstrap_handler, (void *)rx.bsr_list))
   {
      Jmsg(jcr, M_ERROR, 0, "%s", db_strerror(jcr->db_batch));
   }
#else
   char *p;
   JobId_t JobId, last_JobId = 0;
   rx.query = get_pool_memory(PM_MESSAGE);
   for (p=rx.JobIds; get_next_jobid_from_list(&p, &JobId) > 0; ) {
      char ed1[50];

      if (JobId == last_JobId) {
         continue;                    /* eliminate duplicate JobIds */
      }
      last_JobId = JobId;
      /*
       * Find files for this JobId and insert them in the tree
       */
      Mmsg(rx.query, uar_sel_files, edit_int64(JobId, ed1));
      Dmsg1(100, "uar_sel_files=%s\n", rx.query);
      if (!db_sql_query(ua->db, rx.query, insert_bootstrap_handler, (void *)rx.bsr_list)) {
         Jmsg(jcr, M_ERROR, 0, "%s", db_strerror(ua->db));
      }
      free_pool_memory(rx.query);
      rx.query = NULL;
   }
#endif

   complete_bsr(ua, rx.bsr_list);
   jcr->ExpectedFiles = write_bsr_file(ua, rx);
   Jmsg(jcr, M_INFO, 0, _("Found %d files to consolidate into Virtual Full.\n"),
        jcr->ExpectedFiles);
   free_ua_context(ua);
   free_bsr(rx.bsr_list);
   return jcr->ExpectedFiles==0?false:true;
}
