/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2015 Kern Sibbald

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
 *
 *   Bacula Director -- mac.c -- responsible for doing
 *     migration and copy jobs.
 *
 *   Also handles Copy jobs (March MMVIII)
 *
 *     Kern Sibbald, September MMIV
 *
 *  Basic tasks done here:
 *     Open DB and create records for this job.
 *     Open Message Channel with Storage daemon to tell him a job will be starting.
 *     Open connection with Storage daemon and pass him commands
 *       to do the backup.
 *     When the Storage daemon finishes the job, update the DB.
 *
 */

#include "bacula.h"
#include "dird.h"
#include "ua.h"
#ifndef HAVE_REGEX_H
#include "lib/bregex.h"
#else
#include <regex.h>
#endif

struct uitem {
   dlink link;
   char *item;
};

/* Imported functions */
extern void start_mac_job(JCR*);

static const int dbglevel = 10;

/* Forware referenced functions */
static bool find_mediaid_then_jobids(JCR *jcr, idpkt *ids, const char *query1,
                 const char *type);
static bool regex_find_jobids(JCR *jcr, idpkt *ids, const char *query1,
                 const char *query2, const char *type);
static int get_next_dbid_from_list(char **p, DBId_t *DBId);
static int unique_dbid_handler(void *ctx, int num_fields, char **row);
static int unique_name_handler(void *ctx, int num_fields, char **row);
static bool find_jobids_from_mediaid_list(JCR *jcr, idpkt *ids, const char *type);
static bool find_jobids_of_pool_uncopied_jobs(JCR *jcr, idpkt *ids);

/* Get Job names in Pool */
static const char *sql_job =
   "SELECT DISTINCT Job.Name from Job,Pool"
   " WHERE Pool.Name='%s' AND Job.PoolId=Pool.PoolId";

/* Get JobIds from regex'ed Job names */
static const char *sql_jobids_from_job =
   "SELECT DISTINCT Job.JobId,Job.StartTime FROM Job,Pool"
   " WHERE Job.Name='%s' AND Pool.Name='%s' AND Job.PoolId=Pool.PoolId"
   " ORDER by Job.StartTime";

/* Get Client names in Pool */
static const char *sql_client =
   "SELECT DISTINCT Client.Name from Client,Pool,Job"
   " WHERE Pool.Name='%s' AND Job.ClientId=Client.ClientId AND"
   " Job.PoolId=Pool.PoolId";

/* Get JobIds from regex'ed Client names */
static const char *sql_jobids_from_client =
   "SELECT DISTINCT Job.JobId,Job.StartTime FROM Job,Pool,Client"
   " WHERE Client.Name='%s' AND Pool.Name='%s' AND Job.PoolId=Pool.PoolId"
   " AND Job.ClientId=Client.ClientId AND Job.Type IN ('B','C')"
   " AND Job.JobStatus IN ('T','W')"
   " ORDER by Job.StartTime";

/* Get Volume names in Pool */
static const char *sql_vol =
   "SELECT DISTINCT VolumeName FROM Media,Pool WHERE"
   " VolStatus in ('Full','Used','Error') AND Media.Enabled=1 AND"
   " Media.PoolId=Pool.PoolId AND Pool.Name='%s'";

/* Get JobIds from regex'ed Volume names */
static const char *sql_jobids_from_vol =
   "SELECT DISTINCT Job.JobId,Job.StartTime FROM Media,JobMedia,Job"
   " WHERE Media.VolumeName='%s' AND Media.MediaId=JobMedia.MediaId"
   " AND JobMedia.JobId=Job.JobId AND Job.Type IN ('B','C')"
   " AND Job.JobStatus IN ('T','W') AND Media.Enabled=1"
   " ORDER by Job.StartTime";

static const char *sql_smallest_vol =
   "SELECT Media.MediaId FROM Media,Pool,JobMedia WHERE"
   " Media.MediaId in (SELECT DISTINCT MediaId from JobMedia) AND"
   " Media.VolStatus in ('Full','Used','Error') AND Media.Enabled=1 AND"
   " Media.PoolId=Pool.PoolId AND Pool.Name='%s'"
   " ORDER BY VolBytes ASC LIMIT 1";

static const char *sql_oldest_vol =
   "SELECT Media.MediaId FROM Media,Pool,JobMedia WHERE"
   " Media.MediaId in (SELECT DISTINCT MediaId from JobMedia) AND"
   " Media.VolStatus in ('Full','Used','Error') AND Media.Enabled=1 AND"
   " Media.PoolId=Pool.PoolId AND Pool.Name='%s'"
   " ORDER BY LastWritten ASC LIMIT 1";

/* Get JobIds when we have selected MediaId */
static const char *sql_jobids_from_mediaid =
   "SELECT DISTINCT Job.JobId,Job.StartTime FROM JobMedia,Job"
   " WHERE JobMedia.JobId=Job.JobId AND JobMedia.MediaId IN (%s)"
   " AND Job.Type IN ('B','C') AND Job.JobStatus IN ('T','W')"
   " ORDER by Job.StartTime";

/* Get the number of bytes in the pool */
static const char *sql_pool_bytes =
   "SELECT SUM(JobBytes) FROM Job WHERE JobId IN"
   " (SELECT DISTINCT Job.JobId from Pool,Job,Media,JobMedia WHERE"
   " Pool.Name='%s' AND Media.PoolId=Pool.PoolId AND"
   " VolStatus in ('Full','Used','Error','Append') AND Media.Enabled=1 AND"
   " Job.Type IN ('B','C') AND Job.JobStatus IN ('T','W') AND"
   " JobMedia.JobId=Job.JobId AND Job.PoolId=Media.PoolId)";

/* Get the number of bytes in the Jobs */
static const char *sql_job_bytes =
   "SELECT SUM(JobBytes) FROM Job WHERE JobId IN (%s)";

/* Get Media Ids in Pool */
static const char *sql_mediaids =
   "SELECT MediaId FROM Media,Pool WHERE"
   " VolStatus in ('Full','Used','Error') AND Media.Enabled=1 AND"
   " Media.PoolId=Pool.PoolId AND Pool.Name='%s' ORDER BY LastWritten ASC";

/* Get JobIds in Pool longer than specified time */
static const char *sql_pool_time =
   "SELECT DISTINCT Job.JobId FROM Pool,Job,Media,JobMedia WHERE"
   " Pool.Name='%s' AND Media.PoolId=Pool.PoolId AND"
   " VolStatus IN ('Full','Used','Error') AND Media.Enabled=1 AND"
   " Job.Type IN ('B','C') AND Job.JobStatus IN ('T','W') AND"
   " JobMedia.JobId=Job.JobId AND Job.PoolId=Media.PoolId"
   " AND Job.RealEndTime<='%s'";

/* Get JobIds from successfully completed backup jobs which have not been copied before */
static const char *sql_jobids_of_pool_uncopied_jobs =
   "SELECT DISTINCT Job.JobId,Job.StartTime FROM Job,Pool"
   " WHERE Pool.Name = '%s' AND Pool.PoolId = Job.PoolId"
   " AND Job.Type = 'B' AND Job.JobStatus IN ('T','W')"
   " AND Job.jobBytes > 0"
   " AND Job.JobId NOT IN"
   " (SELECT PriorJobId FROM Job WHERE"
   " Type IN ('B','C') AND Job.JobStatus IN ('T','W')"
   " AND PriorJobId != 0)"
   " ORDER by Job.StartTime";

/*
 *
 * This is the central piece of code that finds a job or jobs
 *   actually JobIds to migrate.  It first looks to see if one
 *   has been "manually" specified in jcr->MigrateJobId, and if
 *   so, it returns that JobId to be run.  Otherwise, it
 *   examines the Selection Type to see what kind of migration
 *   we are doing (Volume, Job, Client, ...) and applies any
 *   Selection Pattern if appropriate to obtain a list of JobIds.
 *   Finally, it will loop over all the JobIds found, except the last
 *   one starting a new job with MigrationJobId set to that JobId, and
 *   finally, it returns the last JobId to the caller.
 *
 * Returns: -1  on error
 *           0  if no jobs to migrate
 *           1  if OK and jcr->previous_jr filled in
 */
int getJob_to_migrate(JCR *jcr)
{
   char ed1[30], ed2[30];
   POOL_MEM query(PM_MESSAGE);
   JobId_t JobId;
   DBId_t DBId = 0;
   int stat;
   char *p;
   idpkt ids, mid, jids;
   db_int64_ctx ctx;
   int64_t pool_bytes;
   time_t ttime;
   struct tm tm;
   char dt[MAX_TIME_LENGTH];
   int count = 0;
   int limit = jcr->job->MaxSpawnedJobs;   /* limit is max jobs to start */

   ids.list = get_pool_memory(PM_MESSAGE);
   ids.list[0] = 0;
   ids.count = 0;
   mid.list = get_pool_memory(PM_MESSAGE);
   mid.list[0] = 0;
   mid.count = 0;
   jids.list = get_pool_memory(PM_MESSAGE);
   jids.list[0] = 0;
   jids.count = 0;

   /*
    * If MigrateJobId is set, then we migrate only that Job,
    *  otherwise, we go through the full selection of jobs to
    *  migrate.
    */
   if (jcr->MigrateJobId != 0) {
      Dmsg1(dbglevel, "At Job start previous jobid=%u\n", jcr->MigrateJobId);
      JobId = jcr->MigrateJobId;
   } else {
      switch (jcr->job->selection_type) {
      case MT_JOB:
         if (!regex_find_jobids(jcr, &ids, sql_job, sql_jobids_from_job, "Job")) {
            goto bail_out;
         }
         break;
      case MT_CLIENT:
         if (!regex_find_jobids(jcr, &ids, sql_client, sql_jobids_from_client, "Client")) {
            goto bail_out;
         }
         break;
      case MT_VOLUME:
         if (!regex_find_jobids(jcr, &ids, sql_vol, sql_jobids_from_vol, "Volume")) {
            goto bail_out;
         }
         break;
      case MT_SQLQUERY:
         if (!jcr->job->selection_pattern) {
            Jmsg(jcr, M_FATAL, 0, _("No %s SQL selection pattern specified.\n"), jcr->get_OperationName());
            goto bail_out;
         }
         Dmsg1(dbglevel, "SQL=%s\n", jcr->job->selection_pattern);
         if (!db_sql_query(jcr->db, jcr->job->selection_pattern,
              unique_dbid_handler, (void *)&ids)) {
            Jmsg(jcr, M_FATAL, 0,
                 _("SQL failed. ERR=%s\n"), db_strerror(jcr->db));
            goto bail_out;
         }
         break;
      case MT_SMALLEST_VOL:
         if (!find_mediaid_then_jobids(jcr, &ids, sql_smallest_vol, "Smallest Volume")) {
            goto bail_out;
         }
         break;
      case MT_OLDEST_VOL:
         if (!find_mediaid_then_jobids(jcr, &ids, sql_oldest_vol, "Oldest Volume")) {
            goto bail_out;
         }
         break;
      case MT_POOL_OCCUPANCY:
         ctx.count = 0;
         /* Find count of bytes in pool */
         Mmsg(query, sql_pool_bytes, jcr->rpool->name());
         if (!db_sql_query(jcr->db, query.c_str(), db_int64_handler, (void *)&ctx)) {
            Jmsg(jcr, M_FATAL, 0, _("SQL failed. ERR=%s\n"), db_strerror(jcr->db));
            goto bail_out;
         }
         if (ctx.count == 0) {
            Jmsg(jcr, M_INFO, 0, _("No Volumes found to %s.\n"), jcr->get_ActionName(0));
            goto ok_out;
         }
         pool_bytes = ctx.value;
         Dmsg2(dbglevel, "highbytes=%lld pool=%lld\n", jcr->rpool->MigrationHighBytes,
               pool_bytes);
         if (pool_bytes < (int64_t)jcr->rpool->MigrationHighBytes) {
            Jmsg(jcr, M_INFO, 0, _("No Volumes found to %s.\n"), jcr->get_ActionName(0));
            goto ok_out;
         }
         Dmsg0(dbglevel, "We should do Occupation migration.\n");

         ids.count = 0;
         /* Find a list of MediaIds that could be migrated */
         Mmsg(query, sql_mediaids, jcr->rpool->name());
         Dmsg1(dbglevel, "query=%s\n", query.c_str());
         if (!db_sql_query(jcr->db, query.c_str(), unique_dbid_handler, (void *)&ids)) {
            Jmsg(jcr, M_FATAL, 0, _("SQL failed. ERR=%s\n"), db_strerror(jcr->db));
            goto bail_out;
         }
         if (ids.count == 0) {
            Jmsg(jcr, M_INFO, 0, _("No Volumes found to %s.\n"), jcr->get_ActionName(0));
            goto ok_out;
         }
         Dmsg2(dbglevel, "Pool Occupancy ids=%d MediaIds=%s\n", ids.count, ids.list);

         if (!find_jobids_from_mediaid_list(jcr, &ids, "Volume")) {
            goto bail_out;
         }
         /* ids == list of jobs  */
         p = ids.list;
         for (int i=0; i < (int)ids.count; i++) {
            stat = get_next_dbid_from_list(&p, &DBId);
            Dmsg2(dbglevel, "get_next_dbid stat=%d JobId=%u\n", stat, (uint32_t)DBId);
            if (stat < 0) {
               Jmsg(jcr, M_FATAL, 0, _("Invalid JobId found.\n"));
               goto bail_out;
            } else if (stat == 0) {
               break;
            }

            mid.count = 1;
            Mmsg(mid.list, "%s", edit_int64(DBId, ed1));
            if (jids.count > 0) {
               pm_strcat(jids.list, ",");
            }
            pm_strcat(jids.list, mid.list);
            jids.count += mid.count;

            /* Find count of bytes from Jobs */
            Mmsg(query, sql_job_bytes, mid.list);
            Dmsg1(dbglevel, "Jobbytes query: %s\n", query.c_str());
            if (!db_sql_query(jcr->db, query.c_str(), db_int64_handler, (void *)&ctx)) {
               Jmsg(jcr, M_FATAL, 0, _("SQL failed. ERR=%s\n"), db_strerror(jcr->db));
               goto bail_out;
            }
            pool_bytes -= ctx.value;
            Dmsg2(dbglevel, "Total %s Job bytes=%s\n", jcr->get_ActionName(0), edit_int64_with_commas(ctx.value, ed1));
            Dmsg2(dbglevel, "lowbytes=%s poolafter=%s\n",
                  edit_int64_with_commas(jcr->rpool->MigrationLowBytes, ed1),
                  edit_int64_with_commas(pool_bytes, ed2));
            if (pool_bytes <= (int64_t)jcr->rpool->MigrationLowBytes) {
               Dmsg0(dbglevel, "We should be done.\n");
               break;
            }
         }
         /* Transfer jids to ids, where the jobs list is expected */
         ids.count = jids.count;
         pm_strcpy(ids.list, jids.list);
         Dmsg2(dbglevel, "Pool Occupancy ids=%d JobIds=%s\n", ids.count, ids.list);
         break;
      case MT_POOL_TIME:
         ttime = time(NULL) - (time_t)jcr->rpool->MigrationTime;
         (void)localtime_r(&ttime, &tm);
         strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", &tm);

         ids.count = 0;
         Mmsg(query, sql_pool_time, jcr->rpool->name(), dt);
         Dmsg1(dbglevel, "query=%s\n", query.c_str());
         if (!db_sql_query(jcr->db, query.c_str(), unique_dbid_handler, (void *)&ids)) {
            Jmsg(jcr, M_FATAL, 0, _("SQL failed. ERR=%s\n"), db_strerror(jcr->db));
            goto bail_out;
         }
         if (ids.count == 0) {
            Jmsg(jcr, M_INFO, 0, _("No Volumes found to %s.\n"), jcr->get_ActionName(0));
            goto ok_out;
         }
         Dmsg2(dbglevel, "PoolTime ids=%d JobIds=%s\n", ids.count, ids.list);
         break;
      case MT_POOL_UNCOPIED_JOBS:
         if (!find_jobids_of_pool_uncopied_jobs(jcr, &ids)) {
            goto bail_out;
         }
         break;
      default:
         Jmsg(jcr, M_FATAL, 0, _("Unknown %s Selection Type.\n"), jcr->get_OperationName());
         goto bail_out;
      }

      /*
       * Loop over all jobids except the last one, sending
       * them to start_mac_job(), which will start a job
       * for each of them.  For the last JobId, we handle it below.
       */
      p = ids.list;
      if (ids.count == 0) {
         Jmsg(jcr, M_INFO, 0, _("No JobIds found to %s.\n"), jcr->get_ActionName(0));
         goto ok_out;
      }

      Jmsg(jcr, M_INFO, 0, _("The following %u JobId%s chosen to be %s: %s\n"),
         ids.count, (ids.count < 2) ? _(" was") : _("s were"),
         jcr->get_ActionName(1), ids.list);

      Dmsg2(dbglevel, "Before loop count=%d ids=%s\n", ids.count, ids.list);
      /*
       * Note: to not over load the system, limit the number
       *  of new jobs started to Maximum Spawned Jobs
       */
      for (int i=1; i < (int)ids.count; i++) {
         JobId = 0;
         stat = get_next_jobid_from_list(&p, &JobId);
         Dmsg3(dbglevel, "getJobid_no=%d stat=%d JobId=%u\n", i, stat, JobId);
         if (stat < 0) {
            Jmsg(jcr, M_FATAL, 0, _("Invalid JobId found.\n"));
            goto bail_out;
         } else if (stat == 0) {
            Jmsg(jcr, M_INFO, 0, _("No JobIds found to %s.\n"), jcr->get_ActionName(0));
            goto ok_out;
         }
         jcr->MigrateJobId = JobId;
         /* Don't start any more when limit reaches zero */
         limit--;
         if (limit > 0) {
            start_mac_job(jcr);
            Dmsg0(dbglevel, "Back from start_mac_job\n");
         }
      }

      /* Now get the last JobId and handle it in the current job */
      JobId = 0;
      stat = get_next_jobid_from_list(&p, &JobId);
      Dmsg2(dbglevel, "Last get_next_jobid stat=%d JobId=%u\n", stat, (int)JobId);
      if (stat < 0) {
         Jmsg(jcr, M_FATAL, 0, _("Invalid JobId found.\n"));
         goto bail_out;
      } else if (stat == 0) {
         Jmsg(jcr, M_INFO, 0, _("No JobIds found to %s.\n"), jcr->get_ActionName(0));
         goto ok_out;
      }
   }

   jcr->previous_jr.JobId = JobId;
   Dmsg1(dbglevel, "Previous jobid=%d\n", (int)jcr->previous_jr.JobId);

   if (!db_get_job_record(jcr, jcr->db, &jcr->previous_jr)) {
      Jmsg(jcr, M_FATAL, 0, _("Could not get job record for JobId %s to %s. ERR=%s"),
           edit_int64(jcr->previous_jr.JobId, ed1),
           jcr->get_ActionName(0),
           db_strerror(jcr->db));
      goto bail_out;
   }

   Jmsg(jcr, M_INFO, 0, _("%s using JobId=%s Job=%s\n"),
      jcr->get_OperationName(),
      edit_int64(jcr->previous_jr.JobId, ed1), jcr->previous_jr.Job);
   Dmsg4(dbglevel, "%s JobId=%d  using JobId=%s Job=%s\n",
      jcr->get_OperationName(),
      jcr->JobId,
      edit_int64(jcr->previous_jr.JobId, ed1), jcr->previous_jr.Job);
   count = 1;

ok_out:
   goto out;

bail_out:
   count = -1;

out:
   free_pool_memory(ids.list);
   free_pool_memory(mid.list);
   free_pool_memory(jids.list);
   return count;
}

/*
 * This routine returns:
 *    false       if an error occurred
 *    true        otherwise
 *    ids.count   number of jobids found (may be zero)
 */
static bool find_jobids_from_mediaid_list(JCR *jcr, idpkt *ids, const char *type)
{
   bool ok = false;
   POOL_MEM query(PM_MESSAGE);

   Mmsg(query, sql_jobids_from_mediaid, ids->list);
   ids->count = 0;
   if (!db_sql_query(jcr->db, query.c_str(), unique_dbid_handler, (void *)ids)) {
      Jmsg(jcr, M_FATAL, 0, _("SQL failed. ERR=%s\n"), db_strerror(jcr->db));
      goto bail_out;
   }
   if (ids->count == 0) {
      Jmsg(jcr, M_INFO, 0, _("No %ss found to %s.\n"), type, jcr->get_ActionName(0));
   }
   ok = true;

bail_out:
   return ok;
}

/*
 * This routine returns:
 *    false       if an error occurred
 *    true        otherwise
 *    ids.count   number of jobids found (may be zero)
 */
static bool find_jobids_of_pool_uncopied_jobs(JCR *jcr, idpkt *ids)
{
   bool ok = false;
   POOL_MEM query(PM_MESSAGE);

   /* Only a copy job is allowed */
   if (jcr->getJobType() != JT_COPY) {
      Jmsg(jcr, M_FATAL, 0,
           _("Selection Type 'pooluncopiedjobs' only applies to Copy Jobs"));
      goto bail_out;
   }

   Dmsg1(dbglevel, "copy selection pattern=%s\n", jcr->rpool->name());
   Mmsg(query, sql_jobids_of_pool_uncopied_jobs, jcr->rpool->name());
   Dmsg1(dbglevel, "get uncopied jobs query=%s\n", query.c_str());
   if (!db_sql_query(jcr->db, query.c_str(), unique_dbid_handler, (void *)ids)) {
      Jmsg(jcr, M_FATAL, 0,
           _("SQL to get uncopied jobs failed. ERR=%s\n"), db_strerror(jcr->db));
      goto bail_out;
   }
   ok = true;

bail_out:
   return ok;
}

static bool regex_find_jobids(JCR *jcr, idpkt *ids, const char *query1,
                 const char *query2, const char *type)
{
   dlist *item_chain;
   uitem *item = NULL;
   uitem *last_item = NULL;
   regex_t preg;
   char prbuf[500];
   int rc;
   bool ok = false;
   POOL_MEM query(PM_MESSAGE);

   item_chain = New(dlist(item, &item->link));
   if (!jcr->job->selection_pattern) {
      Jmsg(jcr, M_FATAL, 0, _("No %s %s selection pattern specified.\n"),
         jcr->get_OperationName(), type);
      goto bail_out;
   }
   Dmsg1(dbglevel, "regex-sel-pattern=%s\n", jcr->job->selection_pattern);
   /* Basic query for names */
   Mmsg(query, query1, jcr->rpool->name());
   Dmsg1(dbglevel, "get name query1=%s\n", query.c_str());
   if (!db_sql_query(jcr->db, query.c_str(), unique_name_handler,
        (void *)item_chain)) {
      Jmsg(jcr, M_FATAL, 0,
           _("SQL to get %s failed. ERR=%s\n"), type, db_strerror(jcr->db));
      goto bail_out;
   }
   Dmsg1(dbglevel, "query1 returned %d names\n", item_chain->size());
   if (item_chain->size() == 0) {
      Jmsg(jcr, M_INFO, 0, _("Query of Pool \"%s\" returned no Jobs to %s.\n"),
           jcr->rpool->name(), jcr->get_ActionName(0));
      ok = true;
      goto bail_out;               /* skip regex match */
   } else {
      /* Compile regex expression */
      rc = regcomp(&preg, jcr->job->selection_pattern, REG_EXTENDED);
      if (rc != 0) {
         regerror(rc, &preg, prbuf, sizeof(prbuf));
         Jmsg(jcr, M_FATAL, 0, _("Could not compile regex pattern \"%s\" ERR=%s\n"),
              jcr->job->selection_pattern, prbuf);
         goto bail_out;
      }
      /* Now apply the regex to the names and remove any item not matched */
      foreach_dlist(item, item_chain) {
         const int nmatch = 30;
         regmatch_t pmatch[nmatch];
         if (last_item) {
            Dmsg1(dbglevel, "Remove item %s\n", last_item->item);
            free(last_item->item);
            item_chain->remove(last_item);
            free(last_item);
         }
         Dmsg1(dbglevel, "get name Item=%s\n", item->item);
         rc = regexec(&preg, item->item, nmatch, pmatch,  0);
         if (rc == 0) {
            last_item = NULL;   /* keep this one */
         } else {
            last_item = item;
         }
      }
      if (last_item) {
         Dmsg1(dbglevel, "Remove item %s\n", last_item->item);
         free(last_item->item);
         item_chain->remove(last_item);
         free(last_item);
      }
      regfree(&preg);
   }
   if (item_chain->size() == 0) {
      Jmsg(jcr, M_INFO, 0, _("Regex pattern matched no Jobs to %s.\n"), jcr->get_ActionName(0));
      ok = true;
      goto bail_out;               /* skip regex match */
   }

   /*
    * At this point, we have a list of items in item_chain
    *  that have been matched by the regex, so now we need
    *  to look up their jobids.
    */
   ids->count = 0;
   foreach_dlist(item, item_chain) {
      Dmsg2(dbglevel, "Got %s: %s\n", type, item->item);
      Mmsg(query, query2, item->item, jcr->rpool->name());
      Dmsg1(dbglevel, "get id from name query2=%s\n", query.c_str());
      if (!db_sql_query(jcr->db, query.c_str(), unique_dbid_handler, (void *)ids)) {
         Jmsg(jcr, M_FATAL, 0,
              _("SQL failed. ERR=%s\n"), db_strerror(jcr->db));
         goto bail_out;
      }
   }
   if (ids->count == 0) {
      Jmsg(jcr, M_INFO, 0, _("No %ss found to %s.\n"), type, jcr->get_ActionName(0));
   }
   ok = true;

bail_out:
   Dmsg2(dbglevel, "Count=%d Jobids=%s\n", ids->count, ids->list);
   foreach_dlist(item, item_chain) {
      free(item->item);
   }
   delete item_chain;
   return ok;
}

static bool find_mediaid_then_jobids(JCR *jcr, idpkt *ids, const char *query1,
                 const char *type)
{
   bool ok = false;
   POOL_MEM query(PM_MESSAGE);

   ids->count = 0;
   /* Basic query for MediaId */
   Mmsg(query, query1, jcr->rpool->name());
   if (!db_sql_query(jcr->db, query.c_str(), unique_dbid_handler, (void *)ids)) {
      Jmsg(jcr, M_FATAL, 0, _("SQL failed. ERR=%s\n"), db_strerror(jcr->db));
      goto bail_out;
   }
   if (ids->count == 0) {
      Jmsg(jcr, M_INFO, 0, _("No %s found to %s.\n"), type, jcr->get_ActionName(0));
      ok = true;         /* Not an error */
      goto bail_out;
   } else if (ids->count != 1) {
      Jmsg(jcr, M_FATAL, 0, _("SQL error. Expected 1 MediaId got %d\n"), ids->count);
      goto bail_out;
   }
   Dmsg2(dbglevel, "%s MediaIds=%s\n", type, ids->list);

   ok = find_jobids_from_mediaid_list(jcr, ids, type);

bail_out:
   return ok;
}

/*
* const char *sql_ujobid =
*   "SELECT DISTINCT Job.Job from Client,Pool,Media,Job,JobMedia "
*   " WHERE Media.PoolId=Pool.PoolId AND Pool.Name='%s' AND"
*   " JobMedia.JobId=Job.JobId AND Job.PoolId=Media.PoolId";
*/

/* Add an item to the list if it is unique */
static void add_unique_id(idpkt *ids, char *item)
{
   const int maxlen = 30;
   char id[maxlen+1];
   char *q = ids->list;

   /* Walk through current list to see if each item is the same as item */
   for ( ; *q; ) {
       id[0] = 0;
       for (int i=0; i<maxlen; i++) {
          if (*q == 0) {
             break;
          } else if (*q == ',') {
             q++;
             break;
          }
          id[i] = *q++;
          id[i+1] = 0;
       }
       if (strcmp(item, id) == 0) {
          return;
       }
   }
   /* Did not find item, so add it to list */
   if (ids->count == 0) {
      ids->list[0] = 0;
   } else {
      pm_strcat(ids->list, ",");
   }
   pm_strcat(ids->list, item);
   ids->count++;
// Dmsg3(0, "add_uniq count=%d Ids=%p %s\n", ids->count, ids->list, ids->list);
   return;
}

/*
 * Callback handler make list of DB Ids
 */
static int unique_dbid_handler(void *ctx, int num_fields, char **row)
{
   idpkt *ids = (idpkt *)ctx;

   /* Sanity check */
   if (!row || !row[0]) {
      Dmsg0(dbglevel, "dbid_hdlr error empty row\n");
      return 1;              /* stop calling us */
   }

   add_unique_id(ids, row[0]);
   Dmsg3(dbglevel, "dbid_hdlr count=%d Ids=%p %s\n", ids->count, ids->list, ids->list);
   return 0;
}

static int item_compare(void *item1, void *item2)
{
   uitem *i1 = (uitem *)item1;
   uitem *i2 = (uitem *)item2;
   return strcmp(i1->item, i2->item);
}

static int unique_name_handler(void *ctx, int num_fields, char **row)
{
   dlist *list = (dlist *)ctx;

   uitem *new_item = (uitem *)malloc(sizeof(uitem));
   uitem *item;

   memset(new_item, 0, sizeof(uitem));
   new_item->item = bstrdup(row[0]);
   Dmsg1(dbglevel, "Unique_name_hdlr Item=%s\n", row[0]);
   item = (uitem *)list->binary_insert((void *)new_item, item_compare);
   if (item != new_item) {            /* already in list */
      free(new_item->item);
      free((char *)new_item);
      return 0;
   }
   return 0;
}

/*
 * Return next DBId from comma separated list
 *
 * Returns:
 *   1 if next DBId returned
 *   0 if no more DBIds are in list
 *  -1 there is an error
 */
static int get_next_dbid_from_list(char **p, DBId_t *DBId)
{
   const int maxlen = 30;
   char id[maxlen+1];
   char *q = *p;

   id[0] = 0;
   for (int i=0; i<maxlen; i++) {
      if (*q == 0) {
         break;
      } else if (*q == ',') {
         q++;
         break;
      }
      id[i] = *q++;
      id[i+1] = 0;
   }
   if (id[0] == 0) {
      return 0;
   } else if (!is_a_number(id)) {
      return -1;                      /* error */
   }
   *p = q;
   *DBId = str_to_int64(id);
   return 1;
}
