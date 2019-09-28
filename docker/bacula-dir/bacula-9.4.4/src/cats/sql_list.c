/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

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
 * Bacula Catalog Database List records interface routines
 *
 *    Written by Kern Sibbald, March 2000
 *
 */

#include  "bacula.h"

#if HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL

#include  "cats.h"

/* -----------------------------------------------------------------------
 *
 *   Generic Routines (or almost generic)
 *
 * -----------------------------------------------------------------------
 */

#define append_filter(buf, sql)  \
   do {                          \
      if (*buf) {                \
         pm_strcat(buf, " AND ");\
      } else {                   \
         pm_strcpy(buf, " WHERE ");\
      }                          \
      pm_strcat(buf, sql);       \
   } while (0)

/*
 * Submit general SQL query
 */
int BDB::bdb_list_sql_query(JCR *jcr, const char *query, DB_LIST_HANDLER *sendit,
                      void *ctx, int verbose, e_list_type type)
{
   bdb_lock();
   if (!sql_query(query, QF_STORE_RESULT)) {
      Mmsg(errmsg, _("Query failed: %s\n"), sql_strerror());
      if (verbose) {
         sendit(ctx, errmsg);
      }
      bdb_unlock();
      return 0;
   }

   list_result(jcr,this, sendit, ctx, type);
   sql_free_result();
   bdb_unlock();
   return 1;
}

void BDB::bdb_list_pool_records(JCR *jcr, POOL_DBR *pdbr,
                     DB_LIST_HANDLER *sendit, void *ctx, e_list_type type)
{
   char esc[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   bdb_escape_string(jcr, esc, pdbr->Name, strlen(pdbr->Name));

   if (type == VERT_LIST) {
      if (pdbr->Name[0] != 0) {
         Mmsg(cmd, "SELECT PoolId,Name,NumVols,MaxVols,UseOnce,UseCatalog,"
            "AcceptAnyVolume,VolRetention,VolUseDuration,MaxVolJobs,MaxVolBytes,"
            "AutoPrune,Recycle,PoolType,LabelFormat,Enabled,ScratchPoolId,"
            "RecyclePoolId,LabelType,ActionOnPurge,CacheRetention "
            " FROM Pool WHERE Name='%s'", esc);
      } else {
         Mmsg(cmd, "SELECT PoolId,Name,NumVols,MaxVols,UseOnce,UseCatalog,"
            "AcceptAnyVolume,VolRetention,VolUseDuration,MaxVolJobs,MaxVolBytes,"
            "AutoPrune,Recycle,PoolType,LabelFormat,Enabled,ScratchPoolId,"
            "RecyclePoolId,LabelType,ActionOnPurge,CacheRetention "
            " FROM Pool ORDER BY PoolId");
      }
   } else {
      if (pdbr->Name[0] != 0) {
         Mmsg(cmd, "SELECT PoolId,Name,NumVols,MaxVols,MaxVolBytes,VolRetention,Enabled,PoolType,LabelFormat "
           "FROM Pool WHERE Name='%s'", esc);
      } else {
         Mmsg(cmd, "SELECT PoolId,Name,NumVols,MaxVols,MaxVolBytes,VolRetention,Enabled,PoolType,LabelFormat "
           "FROM Pool ORDER BY PoolId");
      }
   }

   if (!QueryDB(jcr, cmd)) {
      bdb_unlock();
      return;
   }

   list_result(jcr, this, sendit, ctx, type);

   sql_free_result();
   bdb_unlock();
}

void BDB::bdb_list_client_records(JCR *jcr, DB_LIST_HANDLER *sendit, void *ctx, e_list_type type)
{
   bdb_lock();
   if (type == VERT_LIST) {
      Mmsg(cmd, "SELECT ClientId,Name,Uname,AutoPrune,FileRetention,"
         "JobRetention "
         "FROM Client ORDER BY ClientId");
   } else {
      Mmsg(cmd, "SELECT ClientId,Name,FileRetention,JobRetention "
         "FROM Client ORDER BY ClientId");
   }

   if (!QueryDB(jcr, cmd)) {
      bdb_unlock();
      return;
   }

   list_result(jcr, this, sendit, ctx, type);

   sql_free_result();
   bdb_unlock();
}

/*
 * List restore objects
 *
 * JobId | JobIds: List RestoreObjects for specific Job(s)
 * It is possible to specify the ObjectType using FileType field.
 */
void BDB::bdb_list_restore_objects(JCR *jcr, ROBJECT_DBR *rr, DB_LIST_HANDLER *sendit, void *ctx, e_list_type type)
{
   POOL_MEM filter;
   char  ed1[50];
   char *jobid;

   if (rr->JobIds && is_a_number_list(rr->JobIds)) {
      jobid = rr->JobIds;

   } else if (rr->JobId) {
      jobid = edit_int64(rr->JobId, ed1);

   } else {
      return;
   }

   if (rr->FileType > 0) {
      Mmsg(filter, "AND ObjectType = %d ", rr->FileType);
   }

   bdb_lock();
   if (type == VERT_LIST) {
      Mmsg(cmd, "SELECT JobId, RestoreObjectId, ObjectName, "
           "PluginName, ObjectType "
           "FROM RestoreObject JOIN Job USING (JobId) WHERE JobId IN (%s) %s "
           "ORDER BY JobTDate ASC, RestoreObjectId",
           jobid, filter.c_str());
   } else {
      Mmsg(cmd, "SELECT JobId, RestoreObjectId, ObjectName, "
           "PluginName, ObjectType, ObjectLength "
           "FROM RestoreObject JOIN Job USING (JobId) WHERE JobId IN (%s) %s "
           "ORDER BY JobTDate ASC, RestoreObjectId",
           jobid, filter.c_str());
   }

   if (!QueryDB(jcr, cmd)) {
      bdb_unlock();
      return;
   }

   list_result(jcr, this, sendit, ctx, type);

   sql_free_result();
   bdb_unlock();
}

/*
 * If VolumeName is non-zero, list the record for that Volume
 *   otherwise, list the Volumes in the Pool specified by PoolId
 */
void BDB::bdb_list_media_records(JCR *jcr, MEDIA_DBR *mdbr,
                      DB_LIST_HANDLER *sendit, void *ctx, e_list_type type)
{
   char ed1[50];
   char esc[MAX_ESCAPE_NAME_LENGTH];
   const char *expiresin = expires_in[bdb_get_type_index()];

   bdb_lock();
   bdb_escape_string(jcr, esc, mdbr->VolumeName, strlen(mdbr->VolumeName));
   const char *join = "";
   const char *where = "";

   if (type == VERT_LIST) {
      if (mdbr->VolumeName[0] != 0) {
         Mmsg(cmd, "SELECT MediaId,VolumeName,Slot,PoolId,"
            "MediaType,MediaTypeId,FirstWritten,LastWritten,LabelDate,VolJobs,"
            "VolFiles,VolBlocks,VolParts,VolCloudParts,Media.CacheRetention,VolMounts,VolBytes,"
            "VolABytes,VolAPadding,"
            "VolHoleBytes,VolHoles,LastPartBytes,VolErrors,VolWrites,"
            "VolCapacityBytes,VolStatus,Media.Enabled,Media.Recycle,Media.VolRetention,"
            "Media.VolUseDuration,Media.MaxVolJobs,Media.MaxVolFiles,Media.MaxVolBytes,InChanger,"
            "EndFile,EndBlock,VolType,Media.LabelType,StorageId,DeviceId,"
            "MediaAddressing,VolReadTime,VolWriteTime,"
            "LocationId,RecycleCount,InitialWrite,Media.ScratchPoolId,Media.RecyclePoolId, "
            "Media.ActionOnPurge,%s AS ExpiresIn, Comment"
           " FROM Media %s WHERE Media.VolumeName='%s' %s",
              expiresin,
              join,
              esc,
              where
            );
      } else {
         Mmsg(cmd, "SELECT MediaId,VolumeName,Slot,PoolId,"
            "MediaType,MediaTypeId,FirstWritten,LastWritten,LabelDate,VolJobs,"
            "VolFiles,VolBlocks,VolParts,VolCloudParts,Media.CacheRetention,VolMounts,VolBytes,"
            "VolABytes,VolAPadding,"
            "VolHoleBytes,VolHoles,LastPartBytes,VolErrors,VolWrites,"
            "VolCapacityBytes,VolStatus,Media.Enabled,Media.Recycle,Media.VolRetention,"
            "Media.VolUseDuration,Media.MaxVolJobs,Media.MaxVolFiles,Media.MaxVolBytes,InChanger,"
            "EndFile,EndBlock,VolType,Media.LabelType,StorageId,DeviceId,"
            "MediaAddressing,VolReadTime,VolWriteTime,"
            "LocationId,RecycleCount,InitialWrite,Media.ScratchPoolId,Media.RecyclePoolId, "
            "Media.ActionOnPurge,%s AS ExpiresIn, Comment"
            " FROM Media %s WHERE Media.PoolId=%s %s ORDER BY MediaId",
              expiresin,
              join,
              edit_int64(mdbr->PoolId, ed1),
              where
            );
      }
   } else {
      if (mdbr->VolumeName[0] != 0) {
         Mmsg(cmd, "SELECT MediaId,VolumeName,VolStatus,Media.Enabled,"
            "VolBytes,VolFiles,Media.VolRetention,Media.Recycle,Slot,InChanger,MediaType,VolType,"
              "VolParts,%s AS ExpiresIn "
              "FROM Media %s WHERE Media.VolumeName='%s' %s",
              expiresin,
              join,
              esc,
              where
            );
      } else {
         Mmsg(cmd, "SELECT MediaId,VolumeName,VolStatus,Media.Enabled,"
            "VolBytes,VolFiles,Media.VolRetention,Media.Recycle,Slot,InChanger,MediaType,VolType,"
            "VolParts,LastWritten,%s AS ExpiresIn "
            "FROM Media %s WHERE Media.PoolId=%s %s ORDER BY MediaId",
              expiresin,
              join,
              edit_int64(mdbr->PoolId, ed1),
              where
            );
      }
   }
   Dmsg1(DT_SQL|50, "q=%s\n", cmd);
   if (!QueryDB(jcr, cmd)) {
      bdb_unlock();
      return;
   }

   list_result(jcr, this, sendit, ctx, type);

   sql_free_result();
   bdb_unlock();
}

void BDB::bdb_list_jobmedia_records(JCR *jcr, uint32_t JobId,
                              DB_LIST_HANDLER *sendit, void *ctx, e_list_type type)
{
   char ed1[50];

   bdb_lock();
   const char *join = "";
   const char *where = "";

   if (type == VERT_LIST) {
      if (JobId > 0) {                   /* do by JobId */
         Mmsg(cmd, "SELECT JobMediaId,JobId,Media.MediaId,Media.VolumeName,"
            "FirstIndex,LastIndex,StartFile,JobMedia.EndFile,StartBlock,"
            "JobMedia.EndBlock "
            "FROM JobMedia JOIN Media USING (MediaId) %s "
            "WHERE JobMedia.JobId=%s %s",
              join,
              edit_int64(JobId, ed1),
              where);
      } else {
         Mmsg(cmd, "SELECT JobMediaId,JobId,Media.MediaId,Media.VolumeName,"
            "FirstIndex,LastIndex,StartFile,JobMedia.EndFile,StartBlock,"
            "JobMedia.EndBlock "
            "FROM JobMedia JOIN Media USING (MediaId) %s %s",
              join,
              where);
      }

   } else {
      if (JobId > 0) {                   /* do by JobId */
         Mmsg(cmd, "SELECT JobId,Media.VolumeName,FirstIndex,LastIndex "
            "FROM JobMedia JOIN Media USING (MediaId) %s WHERE "
            "JobMedia.JobId=%s %s",
              join,
              edit_int64(JobId, ed1),
              where);
      } else {
         Mmsg(cmd, "SELECT JobId,Media.VolumeName,FirstIndex,LastIndex "
              "FROM JobMedia JOIN Media USING (MediaId) %s %s",
              join,
              where);
      }
   }
   Dmsg1(DT_SQL|50, "q=%s\n", cmd);

   if (!QueryDB(jcr, cmd)) {
      bdb_unlock();
      return;
   }

   list_result(jcr, this, sendit, ctx, type);

   sql_free_result();
   bdb_unlock();
}


void BDB::bdb_list_copies_records(JCR *jcr, uint32_t limit, char *JobIds,
                            DB_LIST_HANDLER *sendit, void *ctx, e_list_type type)
{
   POOL_MEM str_limit(PM_MESSAGE);
   POOL_MEM str_jobids(PM_MESSAGE);

   if (limit > 0) {
      Mmsg(str_limit, " LIMIT %d", limit);
   }

   if (JobIds && JobIds[0]) {
      Mmsg(str_jobids, " AND (Job.PriorJobId IN (%s) OR Job.JobId IN (%s)) ",
           JobIds, JobIds);
   }

   bdb_lock();
   Mmsg(cmd,
   "SELECT DISTINCT Job.PriorJobId AS JobId, Job.Job, "
                   "Job.JobId AS CopyJobId, Media.MediaType "
     "FROM Job "
     "JOIN JobMedia USING (JobId) "
     "JOIN Media    USING (MediaId) "
    "WHERE Job.Type = '%c' %s ORDER BY Job.PriorJobId DESC %s",
        (char) JT_JOB_COPY, str_jobids.c_str(), str_limit.c_str());

   if (!QueryDB(jcr, cmd)) {
      goto bail_out;
   }

   if (sql_num_rows()) {
      if (JobIds && JobIds[0]) {
         sendit(ctx, _("These JobIds have copies as follows:\n"));
      } else {
         sendit(ctx, _("The catalog contains copies as follows:\n"));
      }

      list_result(jcr, this, sendit, ctx, type);
   }

   sql_free_result();

bail_out:
   bdb_unlock();
}

void BDB::bdb_list_joblog_records(JCR *jcr, uint32_t JobId,
                              DB_LIST_HANDLER *sendit, void *ctx, e_list_type type)
{
   char ed1[50];
 
   if (JobId <= 0) {
      return;
   }
   bdb_lock();
   if (type == VERT_LIST) {
      Mmsg(cmd, "SELECT Time,LogText FROM Log "
           "WHERE Log.JobId=%s ORDER BY LogId ASC", edit_int64(JobId, ed1));
   } else {
      Mmsg(cmd, "SELECT LogText FROM Log "
           "WHERE Log.JobId=%s ORDER BY LogId ASC", edit_int64(JobId, ed1));
   }
   if (!QueryDB(jcr, cmd)) {
      goto bail_out;
   }

   list_result(jcr, this, sendit, ctx, type);

   sql_free_result();

bail_out:
   bdb_unlock();
}

/*
 * List Job record(s) that match JOB_DBR
 *
 *  Currently, we return all jobs or if jr->JobId is set,
 *  only the job with the specified id.
 */
alist *BDB::bdb_list_job_records(JCR *jcr, JOB_DBR *jr, DB_LIST_HANDLER *sendit,
                    void *ctx, e_list_type type)
{
   char ed1[50];
   char limit[50];
   char esc[MAX_ESCAPE_NAME_LENGTH];
   alist *list = NULL;
   POOLMEM *where  = get_pool_memory(PM_MESSAGE);
   POOLMEM *tmp    = get_pool_memory(PM_MESSAGE);
   const char *order = "ASC";
   *where = 0;

   bdb_lock();
   if (jr->order == 1) {
      order = "DESC";
   }
   if (jr->limit > 0) {
      snprintf(limit, sizeof(limit), " LIMIT %d", jr->limit);
   } else {
      limit[0] = 0;
   }
   if (jr->Name[0]) {
      bdb_escape_string(jcr, esc, jr->Name, strlen(jr->Name));
      Mmsg(tmp, " Job.Name='%s' ", esc);
      append_filter(where, tmp);

   } else if (jr->JobId != 0) {
      Mmsg(tmp, " Job.JobId=%s ", edit_int64(jr->JobId, ed1));
      append_filter(where, tmp);

   } else if (jr->Job[0] != 0) {
      bdb_escape_string(jcr, esc, jr->Job, strlen(jr->Job));
      Mmsg(tmp, " Job.Job='%s' ", esc);
      append_filter(where, tmp);
   }

   if (type == INCOMPLETE_JOBS && jr->JobStatus == JS_FatalError) {
      Mmsg(tmp, " Job.JobStatus IN ('E', 'f') ");
      append_filter(where, tmp);

   } else if (jr->JobStatus) {
      Mmsg(tmp, " Job.JobStatus='%c' ", jr->JobStatus);
      append_filter(where, tmp);
   }

   if (jr->JobType) {
      Mmsg(tmp, " Job.Type='%c' ", jr->JobType);
      append_filter(where, tmp);
   }

   if (jr->JobErrors > 0) {
      Mmsg(tmp, " Job.JobErrors > 0 ");
      append_filter(where, tmp);
   }

   if (jr->ClientId > 0) {
      Mmsg(tmp, " Job.ClientId=%s ", edit_int64(jr->ClientId, ed1));
      append_filter(where, tmp);
   }

   switch (type) {
   case VERT_LIST:
      Mmsg(cmd,
           "SELECT JobId,Job,Job.Name,PurgedFiles,Type,Level,"
           "Job.ClientId,Client.Name as ClientName,JobStatus,SchedTime,"
           "StartTime,EndTime,RealEndTime,JobTDate,"
           "VolSessionId,VolSessionTime,JobFiles,JobBytes,ReadBytes,JobErrors,"
           "JobMissingFiles,Job.PoolId,Pool.Name as PoolName,PriorJobId,"
           "Job.FileSetId,FileSet.FileSet,Job.HasBase,Job.HasCache,Job.Comment "
           "FROM Job JOIN Client USING (ClientId) LEFT JOIN Pool USING (PoolId) "
           "LEFT JOIN FileSet USING (FileSetId) %s "
           "ORDER BY StartTime %s %s", where, order, limit);
      break;
   case HORZ_LIST:
      Mmsg(cmd,
           "SELECT JobId,Name,StartTime,Type,Level,JobFiles,JobBytes,JobStatus "
           "FROM Job %s ORDER BY StartTime %s,JobId %s %s", where, order, order, limit);
      break;
   case INCOMPLETE_JOBS:
      Mmsg(cmd,
           "SELECT JobId,Name,StartTime,Type,Level,JobFiles,JobBytes,JobStatus "
             "FROM Job %s ORDER BY StartTime %s,JobId %s %s",
           where, order, order, limit);
      break;
   default:
      break;
   }

   free_pool_memory(tmp);
   free_pool_memory(where);

   Dmsg1(100, "SQL: %s\n", cmd);
   if (!QueryDB(jcr, cmd)) {
      bdb_unlock();
      return NULL;
   }
   if (type == INCOMPLETE_JOBS) {
      SQL_ROW row;
      list = New(alist(10));
      sql_data_seek(0);
      for (int i=0; (row=sql_fetch_row()) != NULL; i++) {
         list->append(bstrdup(row[0]));
      }
   }
   sql_data_seek(0);
   list_result(jcr, this, sendit, ctx, type);
   sql_free_result();
   bdb_unlock();
   return list;
}

/*
 * List Job totals
 *
 */
void BDB::bdb_list_job_totals(JCR *jcr, JOB_DBR *jr, DB_LIST_HANDLER *sendit, void *ctx)
{
   bdb_lock();

   /* List by Job */
   Mmsg(cmd, "SELECT  count(*) AS Jobs,sum(JobFiles) "
      "AS Files,sum(JobBytes) AS Bytes,Name AS Job FROM Job GROUP BY Name");

   if (!QueryDB(jcr, cmd)) {
      bdb_unlock();
      return;
   }

   list_result(jcr, this, sendit, ctx, HORZ_LIST);

   sql_free_result();

   /* Do Grand Total */
   Mmsg(cmd, "SELECT count(*) AS Jobs,sum(JobFiles) "
        "AS Files,sum(JobBytes) As Bytes FROM Job");

   if (!QueryDB(jcr, cmd)) {
      bdb_unlock();
      return;
   }

   list_result(jcr, this, sendit, ctx, HORZ_LIST);

   sql_free_result();
   bdb_unlock();
}

/* List all file records from a job
 * "deleted" values are described just below
 */
void BDB::bdb_list_files_for_job(JCR *jcr, JobId_t jobid, int deleted, DB_LIST_HANDLER *sendit, void *ctx)
{
   char ed1[50];
   const char *opt;
   LIST_CTX lctx(jcr, this, sendit, ctx, HORZ_LIST);

   switch (deleted) {
   case 0:                      /* Show only actual files */
      opt = " AND FileIndex > 0 ";
      break;
   case 1:                      /* Show only deleted files */
      opt = " AND FileIndex <= 0 ";
      break;
   default:                     /* Show everything */
      opt = "";
      break;
   }

   bdb_lock();

   /*
    * MySQL is different with no || operator
    */
   if (bdb_get_type_index() == SQL_TYPE_MYSQL) {
      Mmsg(cmd, "SELECT CONCAT(Path.Path,Filename.Name) AS Filename "
           "FROM (SELECT PathId, FilenameId FROM File WHERE JobId=%s %s "
                  "UNION ALL "
                 "SELECT PathId, FilenameId "
                   "FROM BaseFiles JOIN File "
                         "ON (BaseFiles.FileId = File.FileId) "
                  "WHERE BaseFiles.JobId = %s"
           ") AS F, Filename,Path "
           "WHERE Filename.FilenameId=F.FilenameId "
           "AND Path.PathId=F.PathId",
           edit_int64(jobid, ed1), opt, ed1);
   } else {
      Mmsg(cmd, "SELECT Path.Path||Filename.Name AS Filename "
           "FROM (SELECT PathId, FilenameId FROM File WHERE JobId=%s %s "
                  "UNION ALL "
                 "SELECT PathId, FilenameId "
                   "FROM BaseFiles JOIN File "
                         "ON (BaseFiles.FileId = File.FileId) "
                  "WHERE BaseFiles.JobId = %s"
           ") AS F, Filename,Path "
           "WHERE Filename.FilenameId=F.FilenameId "
           "AND Path.PathId=F.PathId",
           edit_int64(jobid, ed1), opt, ed1);
   }
   Dmsg1(100, "q=%s\n", cmd);
   if (!bdb_big_sql_query(cmd, list_result, &lctx)) {
       bdb_unlock();
       return;
   }

   lctx.send_dashes();

   sql_free_result();
   bdb_unlock();
}

void BDB::bdb_list_base_files_for_job(JCR *jcr, JobId_t jobid, DB_LIST_HANDLER *sendit, void *ctx)
{
   char ed1[50];
   LIST_CTX lctx(jcr, this, sendit, ctx, HORZ_LIST);

   bdb_lock();

   /*
    * Stupid MySQL is NON-STANDARD !
    */
   if (bdb_get_type_index() == SQL_TYPE_MYSQL) {
      Mmsg(cmd, "SELECT CONCAT(Path.Path,Filename.Name) AS Filename "
           "FROM BaseFiles, File, Filename, Path "
           "WHERE BaseFiles.JobId=%s AND BaseFiles.BaseJobId = File.JobId "
           "AND BaseFiles.FileId = File.FileId "
           "AND Filename.FilenameId=File.FilenameId "
           "AND Path.PathId=File.PathId",
         edit_int64(jobid, ed1));
   } else {
      Mmsg(cmd, "SELECT Path.Path||Filename.Name AS Filename "
           "FROM BaseFiles, File, Filename, Path "
           "WHERE BaseFiles.JobId=%s AND BaseFiles.BaseJobId = File.JobId "
           "AND BaseFiles.FileId = File.FileId "
           "AND Filename.FilenameId=File.FilenameId "
           "AND Path.PathId=File.PathId",
           edit_int64(jobid, ed1));
   }

   if (!bdb_big_sql_query(cmd, list_result, &lctx)) {
       bdb_unlock();
       return;
   }

   lctx.send_dashes();

   sql_free_result();
   bdb_unlock();
}

void BDB::bdb_list_snapshot_records(JCR *jcr, SNAPSHOT_DBR *sdbr,
              DB_LIST_HANDLER *sendit, void *ctx, e_list_type type)
{
   POOLMEM *filter = get_pool_memory(PM_MESSAGE);
   POOLMEM *tmp    = get_pool_memory(PM_MESSAGE);
   POOLMEM *esc    = get_pool_memory(PM_MESSAGE);
   char ed1[50];

   bdb_lock();
   *filter = 0;

   if (sdbr->Name[0]) {
      bdb_escape_string(jcr, esc, sdbr->Name, strlen(sdbr->Name));
      Mmsg(tmp, "Name='%s'", esc);
      append_filter(filter, tmp);
   }
   if (sdbr->SnapshotId > 0) {
      Mmsg(tmp, "Snapshot.SnapshotId=%d", sdbr->SnapshotId);
      append_filter(filter, tmp);
   }
   if (sdbr->ClientId > 0) {
      Mmsg(tmp, "Snapshot.ClientId=%d", sdbr->ClientId);
      append_filter(filter, tmp);
   }
   if (sdbr->JobId > 0) {
      Mmsg(tmp, "Snapshot.JobId=%d", sdbr->JobId);
      append_filter(filter, tmp);
   }
   if (*sdbr->Client) {
      bdb_escape_string(jcr, esc, sdbr->Client, strlen(sdbr->Client));
      Mmsg(tmp, "Client.Name='%s'", esc);
      append_filter(filter, tmp);
   }
   if (sdbr->Device && *(sdbr->Device)) {
      esc = check_pool_memory_size(esc, strlen(sdbr->Device) * 2 + 1);
      bdb_escape_string(jcr, esc, sdbr->Device, strlen(sdbr->Device));
      Mmsg(tmp, "Device='%s'", esc);
      append_filter(filter, tmp);
   }
   if (*sdbr->Type) {
      bdb_escape_string(jcr, esc, sdbr->Type, strlen(sdbr->Type));
      Mmsg(tmp, "Type='%s'", esc);
      append_filter(filter, tmp);
   }
   if (*sdbr->created_before) {
      bdb_escape_string(jcr, esc, sdbr->created_before, strlen(sdbr->created_before));
      Mmsg(tmp, "CreateDate <= '%s'", esc);
      append_filter(filter, tmp);
   }
   if (*sdbr->created_after) {
      bdb_escape_string(jcr, esc, sdbr->created_after, strlen(sdbr->created_after));
      Mmsg(tmp, "CreateDate >= '%s'", esc);
      append_filter(filter, tmp);
   }
   if (sdbr->expired) {
      Mmsg(tmp, "CreateTDate < (%s - Retention)", edit_int64(time(NULL), ed1));
      append_filter(filter, tmp);
   }
   if (*sdbr->CreateDate) {
      bdb_escape_string(jcr, esc, sdbr->CreateDate, strlen(sdbr->CreateDate));
      Mmsg(tmp, "CreateDate = '%s'", esc);
      append_filter(filter, tmp);
   }

   if (sdbr->sorted_client) {
      pm_strcat(filter, " ORDER BY Client.Name, SnapshotId DESC");

   } else {
      pm_strcat(filter, " ORDER BY SnapshotId DESC");
   }

   if (type == VERT_LIST || type == ARG_LIST) {
      Mmsg(cmd, "SELECT SnapshotId, Snapshot.Name, CreateDate, Client.Name AS Client, "
           "FileSet.FileSet AS FileSet, JobId, Volume, Device, Type, Retention, Comment "
           "FROM Snapshot JOIN Client USING (ClientId) LEFT JOIN FileSet USING (FileSetId) %s", filter);

   } else if (type == HORZ_LIST) {
      Mmsg(cmd, "SELECT SnapshotId, Snapshot.Name, CreateDate, Client.Name AS Client, "
           "Device, Type "
           "FROM Snapshot JOIN Client USING (ClientId) %s", filter);
   }

   if (!QueryDB(jcr, cmd)) {
      goto bail_out;
   }

   list_result(jcr, this, sendit, ctx, type);

bail_out:
   sql_free_result();
   bdb_unlock();

   free_pool_memory(filter);
   free_pool_memory(esc);
   free_pool_memory(tmp);
}

#endif /* HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL */
