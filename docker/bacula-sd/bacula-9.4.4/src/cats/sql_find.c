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
 * Bacula Catalog Database Find record interface routines
 *
 *  Note, generally, these routines are more complicated
 *        that a simple search by name or id. Such simple
 *        request are in get.c
 *
 *    Written by Kern Sibbald, December 2000
 */

#include  "bacula.h"

#if HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL

#include "cats.h"

/* -----------------------------------------------------------------------
 *
 *   Generic Routines (or almost generic)
 *
 * -----------------------------------------------------------------------
 */

/*
 * Find the most recent successful real end time for a job given.
 *
 *  RealEndTime is returned in etime
 *  Job name is returned in job (MAX_NAME_LENGTH)
 *
 * Returns: false on failure
 *          true  on success, jr is unchanged, but etime and job are set
 */
bool BDB::bdb_find_last_job_end_time(JCR *jcr, JOB_DBR *jr, POOLMEM **etime, 
          char *job)
{
   SQL_ROW row;
   char ed1[50], ed2[50];
   char esc_name[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   bdb_escape_string(jcr, esc_name, jr->Name, strlen(jr->Name));
   pm_strcpy(etime, "0000-00-00 00:00:00");   /* default */
   job[0] = 0;

   Mmsg(cmd,
        "SELECT RealEndTime, Job FROM Job WHERE JobStatus IN ('T','W') AND Type='%c' AND "
        "Level IN ('%c','%c','%c') AND Name='%s' AND ClientId=%s AND FileSetId=%s "
        "ORDER BY RealEndTime DESC LIMIT 1", jr->JobType, 
        L_FULL, L_DIFFERENTIAL, L_INCREMENTAL, esc_name, 
        edit_int64(jr->ClientId, ed1), edit_int64(jr->FileSetId, ed2));

   if (!QueryDB(jcr, cmd)) {
      Mmsg2(&errmsg, _("Query error for end time request: ERR=%s\nCMD=%s\n"),
         sql_strerror(), cmd);
      goto bail_out;
   }
   if ((row = sql_fetch_row()) == NULL) {
      sql_free_result();
      Mmsg(errmsg, _("No prior backup Job record found.\n"));
      goto bail_out;
   }
   Dmsg1(100, "Got end time: %s\n", row[0]);
   pm_strcpy(etime, row[0]);
   bstrncpy(job, row[1], MAX_NAME_LENGTH);

   sql_free_result();
   bdb_unlock();
   return true;

bail_out:
   bdb_unlock();
   return false;
}


/*
 * Find job start time if JobId specified, otherwise
 * find last Job start time Incremental and Differential saves.
 *
 *  StartTime is returned in stime
 *  Job name is returned in job (MAX_NAME_LENGTH)
 *
 * Returns: false on failure
 *          true  on success, jr is unchanged, but stime and job are set
 */
bool BDB::bdb_find_job_start_time(JCR *jcr, JOB_DBR *jr, POOLMEM **stime, char *job)
{
   SQL_ROW row;
   char ed1[50], ed2[50];
   char esc_name[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   bdb_escape_string(jcr, esc_name, jr->Name, strlen(jr->Name));
   pm_strcpy(stime, "0000-00-00 00:00:00");   /* default */
   job[0] = 0;

   /* If no Id given, we must find corresponding job */
   if (jr->JobId == 0) {
      /* Differential is since last Full backup */
      Mmsg(cmd,
"SELECT StartTime, Job FROM Job WHERE JobStatus IN ('T','W') AND Type='%c' AND "
"Level='%c' AND Name='%s' AND ClientId=%s AND FileSetId=%s "
"ORDER BY StartTime DESC LIMIT 1",
           jr->JobType, L_FULL, esc_name,
           edit_int64(jr->ClientId, ed1), edit_int64(jr->FileSetId, ed2));

      if (jr->JobLevel == L_DIFFERENTIAL) {
         /* SQL cmd for Differential backup already edited above */

      /* Incremental is since last Full, Incremental, or Differential */
      } else if (jr->JobLevel == L_INCREMENTAL) {
         /*
          * For an Incremental job, we must first ensure
          *  that a Full backup was done (cmd edited above)
          *  then we do a second look to find the most recent
          *  backup
          */
         if (!QueryDB(jcr, cmd)) {
            Mmsg2(&errmsg, _("Query error for start time request: ERR=%s\nCMD=%s\n"),
               sql_strerror(), cmd);
            goto bail_out;
         }
         if ((row = sql_fetch_row()) == NULL) {
            sql_free_result();
            Mmsg(errmsg, _("No prior Full backup Job record found.\n"));
            goto bail_out;
         }
         sql_free_result();
         /* Now edit SQL command for Incremental Job */
         Mmsg(cmd,
"SELECT StartTime, Job FROM Job WHERE JobStatus IN ('T','W') AND Type='%c' AND "
"Level IN ('%c','%c','%c') AND Name='%s' AND ClientId=%s "
"AND FileSetId=%s ORDER BY StartTime DESC LIMIT 1",
            jr->JobType, L_INCREMENTAL, L_DIFFERENTIAL, L_FULL, esc_name,
            edit_int64(jr->ClientId, ed1), edit_int64(jr->FileSetId, ed2));
      } else {
         Mmsg1(errmsg, _("Unknown level=%d\n"), jr->JobLevel);
         goto bail_out;
      }
   } else {
      Dmsg1(100, "Submitting: %s\n", cmd);
      Mmsg(cmd, "SELECT StartTime, Job FROM Job WHERE Job.JobId=%s",
           edit_int64(jr->JobId, ed1));
   }

   if (!QueryDB(jcr, cmd)) {
      pm_strcpy(stime, "");                   /* set EOS */
      Mmsg2(&errmsg, _("Query error for start time request: ERR=%s\nCMD=%s\n"),
         sql_strerror(),  cmd);
      goto bail_out;
   }

   if ((row = sql_fetch_row()) == NULL) {
      Mmsg2(&errmsg, _("No Job record found: ERR=%s\nCMD=%s\n"),
         sql_strerror(),  cmd);
      sql_free_result();
      goto bail_out;
   }
   Dmsg2(100, "Got start time: %s, job: %s\n", row[0], row[1]);
   pm_strcpy(stime, row[0]);
   bstrncpy(job, row[1], MAX_NAME_LENGTH);

   sql_free_result();

   bdb_unlock();
   return true;

bail_out:
   bdb_unlock();
   return false;
}


/*
 * Find the last job start time for the specified JobLevel
 *
 *  StartTime is returned in stime
 *  Job name is returned in job (MAX_NAME_LENGTH)
 *
 * Returns: false on failure
 *          true  on success, jr is unchanged, but stime and job are set
 */
bool BDB::bdb_find_last_job_start_time(JCR *jcr, JOB_DBR *jr,
                            POOLMEM **stime, char *job, int JobLevel)
{
   SQL_ROW row;
   char ed1[50], ed2[50];
   char esc_name[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   bdb_escape_string(jcr, esc_name, jr->Name, strlen(jr->Name));
   pm_strcpy(stime, "0000-00-00 00:00:00");   /* default */
   job[0] = 0;

   Mmsg(cmd,
"SELECT StartTime, Job FROM Job WHERE JobStatus IN ('T','W') AND Type='%c' AND "
"Level='%c' AND Name='%s' AND ClientId=%s AND FileSetId=%s "
"ORDER BY StartTime DESC LIMIT 1",
      jr->JobType, JobLevel, esc_name,
      edit_int64(jr->ClientId, ed1), edit_int64(jr->FileSetId, ed2));
   if (!QueryDB(jcr, cmd)) {
      Mmsg2(&errmsg, _("Query error for start time request: ERR=%s\nCMD=%s\n"),
         sql_strerror(), cmd);
      goto bail_out;
   }
   if ((row = sql_fetch_row()) == NULL) {
      sql_free_result();
      Mmsg(errmsg, _("No prior Full backup Job record found.\n"));
      goto bail_out;
   }
   Dmsg1(100, "Got start time: %s\n", row[0]);
   pm_strcpy(stime, row[0]);
   bstrncpy(job, row[1], MAX_NAME_LENGTH);

   sql_free_result();
   bdb_unlock();
   return true;

bail_out:
   bdb_unlock();
   return false;
}

/*
 * Find last failed job since given start-time
 *   it must be either Full or Diff.
 *
 * Returns: false on failure
 *          true  on success, jr is unchanged and stime unchanged
 *                level returned in JobLevel
 */
bool BDB::bdb_find_failed_job_since(JCR *jcr, JOB_DBR *jr, POOLMEM *stime, int &JobLevel)
{
   SQL_ROW row;
   char ed1[50], ed2[50];
   char esc_name[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   bdb_escape_string(jcr, esc_name, jr->Name, strlen(jr->Name));

   /* Differential is since last Full backup */
   Mmsg(cmd,
   "SELECT Level FROM Job WHERE JobStatus IN ('%c','%c', '%c', '%c') AND "
      "Type='%c' AND Level IN ('%c','%c') AND Name='%s' AND ClientId=%s "
      "AND FileSetId=%s AND StartTime>'%s' "
      "ORDER BY StartTime DESC LIMIT 1",
         JS_Canceled, JS_ErrorTerminated, JS_Error, JS_FatalError,
         jr->JobType, L_FULL, L_DIFFERENTIAL, esc_name,
         edit_int64(jr->ClientId, ed1), edit_int64(jr->FileSetId, ed2),
         stime);
   if (!QueryDB(jcr, cmd)) {
      bdb_unlock();
      return false;
   }

   if ((row = sql_fetch_row()) == NULL) {
      sql_free_result();
      bdb_unlock();
      return false;
   }
   JobLevel = (int)*row[0];
   sql_free_result();

   bdb_unlock();
   return true;
}


/*
 * Find JobId of last job that ran.  E.g. for
 *   VERIFY_CATALOG we want the JobId of the last INIT.
 *   For VERIFY_VOLUME_TO_CATALOG, we want the JobId of the last Job.
 *
 * Returns: true  on success
 *          false on failure
 */
bool BDB::bdb_find_last_jobid(JCR *jcr, const char *Name, JOB_DBR *jr)
{
   SQL_ROW row;
   char ed1[50];
   char esc_name[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   /* Find last full */
   Dmsg2(100, "JobLevel=%d JobType=%d\n", jr->JobLevel, jr->JobType);
   if (jr->JobLevel == L_VERIFY_CATALOG) {
      bdb_escape_string(jcr, esc_name, jr->Name, strlen(jr->Name));
      Mmsg(cmd,
"SELECT JobId FROM Job WHERE Type='V' AND Level='%c' AND "
" JobStatus IN ('T','W') AND Name='%s' AND "
"ClientId=%s ORDER BY StartTime DESC LIMIT 1",
           L_VERIFY_INIT, esc_name,
           edit_int64(jr->ClientId, ed1));
   } else if (jr->JobLevel == L_VERIFY_VOLUME_TO_CATALOG ||
              jr->JobLevel == L_VERIFY_DISK_TO_CATALOG ||
              jr->JobLevel == L_VERIFY_DATA ||
              jr->JobType == JT_BACKUP) {
      if (Name) {
         bdb_escape_string(jcr, esc_name, (char*)Name,
                               MIN(strlen(Name), sizeof(esc_name)));
         Mmsg(cmd,
"SELECT JobId FROM Job WHERE Type='B' AND JobStatus IN ('T','W') AND "
"Name='%s' ORDER BY StartTime DESC LIMIT 1", esc_name);
      } else {
         Mmsg(cmd,
"SELECT JobId FROM Job WHERE Type='B' AND JobStatus IN ('T','W') AND "
"ClientId=%s ORDER BY StartTime DESC LIMIT 1",
           edit_int64(jr->ClientId, ed1));
      }
   } else {
      Mmsg1(&errmsg, _("Unknown Job level=%d\n"), jr->JobLevel);
      bdb_unlock();
      return false;
   }
   Dmsg1(100, "Query: %s\n", cmd);
   if (!QueryDB(jcr, cmd)) {
      bdb_unlock();
      return false;
   }
   if ((row = sql_fetch_row()) == NULL) {
      Mmsg1(&errmsg, _("No Job found for: %s.\n"), cmd);
      sql_free_result();
      bdb_unlock();
      return false;
   }

   jr->JobId = str_to_int64(row[0]);
   sql_free_result();

   Dmsg1(100, "db_get_last_jobid: got JobId=%d\n", jr->JobId);
   if (jr->JobId <= 0) {
      Mmsg1(&errmsg, _("No Job found for: %s\n"), cmd);
      bdb_unlock();
      return false;
   }

   bdb_unlock();
   return true;
}

/*
 * Find Available Media (Volume) for Pool
 *
 * Find a Volume for a given PoolId, MediaType, and Status.
 *
 * Returns: 0 on failure
 *          numrows on success
 */
int BDB::bdb_find_next_volume(JCR *jcr, int item, bool InChanger, MEDIA_DBR *mr)
{
   SQL_ROW row = NULL;
   int numrows;
   const char *order;
   char esc_type[MAX_ESCAPE_NAME_LENGTH];
   char esc_status[MAX_ESCAPE_NAME_LENGTH];
   char ed1[50];

   bdb_lock();
   bdb_escape_string(jcr, esc_type, mr->MediaType, strlen(mr->MediaType));
   bdb_escape_string(jcr, esc_status, mr->VolStatus, strlen(mr->VolStatus));

   if (item == -1) {       /* find oldest volume */
      /* Find oldest volume */
      Mmsg(cmd, "SELECT MediaId,VolumeName,VolJobs,VolFiles,VolBlocks,"
         "VolBytes,VolMounts,VolErrors,VolWrites,MaxVolBytes,VolCapacityBytes,"
         "MediaType,VolStatus,PoolId,VolRetention,VolUseDuration,MaxVolJobs,"
         "MaxVolFiles,Recycle,Slot,FirstWritten,LastWritten,InChanger,"
         "EndFile,EndBlock,VolType,VolParts,VolCloudParts,LastPartBytes,"
         "LabelType,LabelDate,StorageId,"
         "Enabled,LocationId,RecycleCount,InitialWrite,"
         "ScratchPoolId,RecyclePoolId,VolReadTime,VolWriteTime,ActionOnPurge,CacheRetention "
         "FROM Media WHERE PoolId=%s AND MediaType='%s' "
         " AND (VolStatus IN ('Full', 'Append', 'Used') OR (VolStatus IN ('Recycle', 'Purged', 'Used') AND Recycle=1)) "
         " AND Enabled=1 "
         "ORDER BY LastWritten LIMIT 1",
         edit_int64(mr->PoolId, ed1), esc_type);
     item = 1;
   } else {
      POOL_MEM changer(PM_FNAME);
      POOL_MEM voltype(PM_FNAME);
      POOL_MEM exclude(PM_FNAME);
      /* Find next available volume */
      /* ***FIXME***
       * replace switch with
       *  if (StorageId == 0)
       *    break;
       * else use mr->sid_group;  but it must be set!!!
       */
      if (InChanger) {
         ASSERT(mr->sid_group);
         if (mr->sid_group) {
            Mmsg(changer, " AND InChanger=1 AND StorageId IN (%s) ",
                 mr->sid_group);
         } else {
            Mmsg(changer, " AND InChanger=1 AND StorageId=%s ",
                 edit_int64(mr->StorageId, ed1));
         }
      }
      /* Volumes will be automatically excluded from the query, we just take the
       * first one of the list 
       */
      if (mr->exclude_list && *mr->exclude_list) {
         item = 1;
         Mmsg(exclude, " AND MediaId NOT IN (%s) ", mr->exclude_list);
      }
      if (strcmp(mr->VolStatus, "Recycle") == 0 ||
          strcmp(mr->VolStatus, "Purged") == 0) {
         order = "AND Recycle=1 ORDER BY LastWritten ASC,MediaId";  /* take oldest that can be recycled */
      } else {
         order = sql_media_order_most_recently_written[bdb_get_type_index()];    /* take most recently written */
      }
      if (mr->VolType == 0) {
         Mmsg(voltype, "");
      } else {
         Mmsg(voltype, "AND VolType IN (0,%d)", mr->VolType);
      }
      Mmsg(cmd, "SELECT MediaId,VolumeName,VolJobs,VolFiles,VolBlocks,"
         "VolBytes,VolMounts,VolErrors,VolWrites,MaxVolBytes,VolCapacityBytes,"
         "MediaType,VolStatus,PoolId,VolRetention,VolUseDuration,MaxVolJobs,"
         "MaxVolFiles,Recycle,Slot,FirstWritten,LastWritten,InChanger,"
         "EndFile,EndBlock,VolType,VolParts,VolCloudParts,LastPartBytes,"
         "LabelType,LabelDate,StorageId,"
         "Enabled,LocationId,RecycleCount,InitialWrite,"
         "ScratchPoolId,RecyclePoolId,VolReadTime,VolWriteTime,ActionOnPurge,CacheRetention "
         "FROM Media WHERE PoolId=%s AND MediaType='%s' AND Enabled=1 "
         "AND VolStatus='%s' "
         "%s "
         "%s "
         "%s "
         "%s LIMIT %d",
         edit_int64(mr->PoolId, ed1), esc_type,
         esc_status,
         voltype.c_str(),
         changer.c_str(), exclude.c_str(), order, item);
   }
   Dmsg1(100, "fnextvol=%s\n", cmd);
   if (!QueryDB(jcr, cmd)) {
      bdb_unlock();
      return 0;
   }

   numrows = sql_num_rows();
   if (item > numrows || item < 1) {
      Dmsg2(050, "item=%d got=%d\n", item, numrows);
      Mmsg2(&errmsg, _("Request for Volume item %d greater than max %d or less than 1\n"),
         item, numrows);
      bdb_unlock();
      return 0;
   }

   /* Note, we previously seeked to the row using:
    *  sql_data_seek(item-1);
    * but this failed on PostgreSQL, so now we loop
    * over all the records.  This should not be too horrible since
    * the maximum Volumes we look at in any case is 20.
    */
   while (item-- > 0) {
      if ((row = sql_fetch_row()) == NULL) {
         Dmsg1(050, "Fail fetch item=%d\n", item+1);
         Mmsg1(&errmsg, _("No Volume record found for item %d.\n"), item);
         sql_free_result();
         bdb_unlock();
         return 0;
      }
   }

   /* Return fields in Media Record */
   mr->MediaId = str_to_int64(row[0]);
   bstrncpy(mr->VolumeName, row[1]!=NULL?row[1]:"", sizeof(mr->VolumeName));
   mr->VolJobs = str_to_int64(row[2]);
   mr->VolFiles = str_to_int64(row[3]);
   mr->VolBlocks = str_to_int64(row[4]);
   mr->VolBytes = str_to_uint64(row[5]);
   mr->VolMounts = str_to_int64(row[6]);
   mr->VolErrors = str_to_int64(row[7]);
   mr->VolWrites = str_to_int64(row[8]);
   mr->MaxVolBytes = str_to_uint64(row[9]);
   mr->VolCapacityBytes = str_to_uint64(row[10]);
   bstrncpy(mr->MediaType, row[11]!=NULL?row[11]:"", sizeof(mr->MediaType));
   bstrncpy(mr->VolStatus, row[12]!=NULL?row[12]:"", sizeof(mr->VolStatus));
   mr->PoolId = str_to_int64(row[13]);
   mr->VolRetention = str_to_uint64(row[14]);
   mr->VolUseDuration = str_to_uint64(row[15]);
   mr->MaxVolJobs = str_to_int64(row[16]);
   mr->MaxVolFiles = str_to_int64(row[17]);
   mr->Recycle = str_to_int64(row[18]);
   mr->Slot = str_to_int64(row[19]);
   bstrncpy(mr->cFirstWritten, row[20]!=NULL?row[20]:"", sizeof(mr->cFirstWritten));
   mr->FirstWritten = (time_t)str_to_utime(mr->cFirstWritten);
   bstrncpy(mr->cLastWritten, row[21]!=NULL?row[21]:"", sizeof(mr->cLastWritten));
   mr->LastWritten = (time_t)str_to_utime(mr->cLastWritten);
   mr->InChanger = str_to_uint64(row[22]);
   mr->EndFile = str_to_uint64(row[23]);
   mr->EndBlock = str_to_uint64(row[24]);
   mr->VolType = str_to_int64(row[25]);
   mr->VolParts = str_to_int64(row[26]);
   mr->VolCloudParts = str_to_int64(row[27]);
   mr->LastPartBytes = str_to_int64(row[28]);
   mr->LabelType = str_to_int64(row[29]);
   bstrncpy(mr->cLabelDate, row[30]!=NULL?row[30]:"", sizeof(mr->cLabelDate));
   mr->LabelDate = (time_t)str_to_utime(mr->cLabelDate);
   mr->StorageId = str_to_int64(row[31]);
   mr->Enabled = str_to_int64(row[32]);
   mr->LocationId = str_to_int64(row[33]);
   mr->RecycleCount = str_to_int64(row[34]);
   bstrncpy(mr->cInitialWrite, row[35]!=NULL?row[35]:"", sizeof(mr->cInitialWrite));
   mr->InitialWrite = (time_t)str_to_utime(mr->cInitialWrite);
   mr->ScratchPoolId = str_to_int64(row[36]);
   mr->RecyclePoolId = str_to_int64(row[37]);
   mr->VolReadTime = str_to_int64(row[38]);
   mr->VolWriteTime = str_to_int64(row[39]);
   mr->ActionOnPurge = str_to_int64(row[40]);
   mr->CacheRetention = str_to_int64(row[41]);

   sql_free_result();

   bdb_unlock();
   Dmsg1(050, "Rtn numrows=%d\n", numrows);
   return numrows;
}

#endif /* HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL */
