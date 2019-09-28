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
 * Bacula Catalog Database Update record interface routines
 *
 *    Written by Kern Sibbald, March 2000
 */

#include  "bacula.h"

#if HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL
 
#include  "cats.h"

#define dbglevel1  100
#define dbglevel2  400

/* -----------------------------------------------------------------------
 *
 *   Generic Routines (or almost generic)
 *
 * -----------------------------------------------------------------------
 */

/* -----------------------------------------------------------------------
 *
 *   Generic Routines (or almost generic)
 *
 * -----------------------------------------------------------------------
 */
/* Update the attributes record by adding the file digest */
int BDB::bdb_add_digest_to_file_record(JCR *jcr, FileId_t FileId, char *digest,
                          int type)
{
   int ret;
   char ed1[50];
   int len = strlen(digest);

   bdb_lock();
   esc_name = check_pool_memory_size(esc_name, len*2+1);
   bdb_escape_string(jcr, esc_name, digest, len);
   Mmsg(cmd, "UPDATE File SET MD5='%s' WHERE FileId=%s", esc_name,
        edit_int64(FileId, ed1));
   ret = UpdateDB(jcr, cmd, false);
   bdb_unlock();
   return ret;
}

/* Mark the file record as being visited during database
 * verify compare. Stuff JobId into the MarkId field
 */
int BDB::bdb_mark_file_record(JCR *jcr, FileId_t FileId, JobId_t JobId)
{
   int stat;
   char ed1[50], ed2[50];

   bdb_lock();
   Mmsg(cmd, "UPDATE File SET MarkId=%s WHERE FileId=%s",
      edit_int64(JobId, ed1), edit_int64(FileId, ed2));
   stat = UpdateDB(jcr, cmd, false);
   bdb_unlock();
   return stat;
}

/*
 * Update the Job record at start of Job
 *
 *  Returns: false on failure
 *           true  on success
 */
bool BDB::bdb_update_job_start_record(JCR *jcr, JOB_DBR *jr)
{
   char dt[MAX_TIME_LENGTH];
   time_t stime;
   struct tm tm;
   btime_t JobTDate;
   int stat;
   char ed1[50], ed2[50], ed3[50], ed4[50], ed5[50];

   stime = jr->StartTime;
   (void)localtime_r(&stime, &tm);
   strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", &tm);
   JobTDate = (btime_t)stime;

   bdb_lock();
   Mmsg(cmd, "UPDATE Job SET JobStatus='%c',Level='%c',StartTime='%s',"
"ClientId=%s,JobTDate=%s,PoolId=%s,FileSetId=%s WHERE JobId=%s",
      (char)(jcr->JobStatus),
      (char)(jr->JobLevel), dt,
      edit_int64(jr->ClientId, ed1),
      edit_uint64(JobTDate, ed2),
      edit_int64(jr->PoolId, ed3),
      edit_int64(jr->FileSetId, ed4),
      edit_int64(jr->JobId, ed5));

   stat = UpdateDB(jcr, cmd, false);
   changes = 0;
   bdb_unlock();
   return stat;
}

/*
 * Update Long term statistics with all jobs that were run before
 * age seconds
 */
int BDB::bdb_update_stats(JCR *jcr, utime_t age)
{
   char ed1[30];
   int rows;

   utime_t now = (utime_t)time(NULL);
   edit_uint64(now - age, ed1);

   bdb_lock();

   Mmsg(cmd, fill_jobhisto, ed1);
   QueryDB(jcr, cmd); /* TODO: get a message ? */
   rows = sql_affected_rows();

   bdb_unlock();

   return rows;
}

/*
 * Update the Job record at end of Job
 *
 *  Returns: 0 on failure
 *           1 on success
 */
int BDB::bdb_update_job_end_record(JCR *jcr, JOB_DBR *jr)
{
   char dt[MAX_TIME_LENGTH];
   char rdt[MAX_TIME_LENGTH];
   time_t ttime;
   struct tm tm;
   int stat;
   char ed1[30], ed2[30], ed3[50], ed4[50];
   btime_t JobTDate;
   char PriorJobId[50];

   if (jr->PriorJobId) {
      bstrncpy(PriorJobId, edit_int64(jr->PriorJobId, ed1), sizeof(PriorJobId));
   } else {
      bstrncpy(PriorJobId, "0", sizeof(PriorJobId));
   }

   ttime = jr->EndTime;
   (void)localtime_r(&ttime, &tm);
   strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", &tm);

   if (jr->RealEndTime == 0 || jr->RealEndTime < jr->EndTime) {
      jr->RealEndTime = jr->EndTime;
   }
   ttime = jr->RealEndTime;
   (void)localtime_r(&ttime, &tm);
   strftime(rdt, sizeof(rdt), "%Y-%m-%d %H:%M:%S", &tm);

   JobTDate = ttime;

   bdb_lock();
   Mmsg(cmd,
      "UPDATE Job SET JobStatus='%c',EndTime='%s',"
"ClientId=%u,JobBytes=%s,ReadBytes=%s,JobFiles=%u,JobErrors=%u,VolSessionId=%u,"
"VolSessionTime=%u,PoolId=%u,FileSetId=%u,JobTDate=%s,"
"RealEndTime='%s',PriorJobId=%s,HasBase=%u,PurgedFiles=%u WHERE JobId=%s",
      (char)(jr->JobStatus), dt, jr->ClientId, edit_uint64(jr->JobBytes, ed1),
      edit_uint64(jr->ReadBytes, ed4),
      jr->JobFiles, jr->JobErrors, jr->VolSessionId, jr->VolSessionTime,
      jr->PoolId, jr->FileSetId, edit_uint64(JobTDate, ed2),
      rdt, PriorJobId, jr->HasBase, jr->PurgedFiles,
      edit_int64(jr->JobId, ed3));

   stat = UpdateDB(jcr, cmd, false);

   bdb_unlock();
   return stat;
}

/*
 * Update Client record
 *   Returns: 0 on failure
 *            1 on success
 */
int BDB::bdb_update_client_record(JCR *jcr, CLIENT_DBR *cr)
{
   int stat;
   char ed1[50], ed2[50];
   char esc_name[MAX_ESCAPE_NAME_LENGTH];
   char esc_uname[MAX_ESCAPE_NAME_LENGTH];
   CLIENT_DBR tcr;

   bdb_lock();
   memcpy(&tcr, cr, sizeof(tcr));
   if (!bdb_create_client_record(jcr, &tcr)) {
      bdb_unlock();
      return 0;
   }

   bdb_escape_string(jcr, esc_name, cr->Name, strlen(cr->Name));
   bdb_escape_string(jcr, esc_uname, cr->Uname, strlen(cr->Uname));
   Mmsg(cmd,
"UPDATE Client SET AutoPrune=%d,FileRetention=%s,JobRetention=%s,"
"Uname='%s' WHERE Name='%s'",
      cr->AutoPrune,
      edit_uint64(cr->FileRetention, ed1),
      edit_uint64(cr->JobRetention, ed2),
      esc_uname, esc_name);

   stat = UpdateDB(jcr, cmd, false);
   bdb_unlock();
   return stat;
}


/*
 * Update Counters record
 *   Returns: 0 on failure
 *            1 on success
 */
int BDB::bdb_update_counter_record(JCR *jcr, COUNTER_DBR *cr)
{
   char esc[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   bdb_escape_string(jcr, esc, cr->Counter, strlen(cr->Counter));
   Mmsg(cmd, update_counter_values[bdb_get_type_index()],
      cr->MinValue, cr->MaxValue, cr->CurrentValue,
      cr->WrapCounter, esc);

   int stat = UpdateDB(jcr, cmd, false);
   bdb_unlock();
   return stat;
}


int BDB::bdb_update_pool_record(JCR *jcr, POOL_DBR *pr)
{
   int stat;
   char ed1[50], ed2[50], ed3[50], ed4[50], ed5[50], ed6[50], ed7[50];
   char esc[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   bdb_escape_string(jcr, esc, pr->LabelFormat, strlen(pr->LabelFormat));

   Mmsg(cmd, "SELECT count(*) from Media WHERE PoolId=%s",
      edit_int64(pr->PoolId, ed4));
   pr->NumVols = get_sql_record_max(jcr, this);
   Dmsg1(dbglevel2, "NumVols=%d\n", pr->NumVols);

   Mmsg(cmd,
"UPDATE Pool SET NumVols=%u,MaxVols=%u,UseOnce=%d,UseCatalog=%d,"
"AcceptAnyVolume=%d,VolRetention='%s',VolUseDuration='%s',"
"MaxVolJobs=%u,MaxVolFiles=%u,MaxVolBytes=%s,Recycle=%d,"
"AutoPrune=%d,LabelType=%d,LabelFormat='%s',RecyclePoolId=%s,"
"ScratchPoolId=%s,ActionOnPurge=%d,CacheRetention='%s' WHERE PoolId=%s",
      pr->NumVols, pr->MaxVols, pr->UseOnce, pr->UseCatalog,
      pr->AcceptAnyVolume, edit_uint64(pr->VolRetention, ed1),
      edit_uint64(pr->VolUseDuration, ed2),
      pr->MaxVolJobs, pr->MaxVolFiles,
      edit_uint64(pr->MaxVolBytes, ed3),
      pr->Recycle, pr->AutoPrune, pr->LabelType,
      esc, edit_int64(pr->RecyclePoolId,ed5),
      edit_int64(pr->ScratchPoolId,ed6),
      pr->ActionOnPurge,
      edit_uint64(pr->CacheRetention, ed7),
      ed4);
   stat = UpdateDB(jcr, cmd, false);
   bdb_unlock();
   return stat;
}

bool BDB::bdb_update_storage_record(JCR *jcr, STORAGE_DBR *sr)
{
   int stat;
   char ed1[50];

   bdb_lock();
   Mmsg(cmd, "UPDATE Storage SET AutoChanger=%d WHERE StorageId=%s",
      sr->AutoChanger, edit_int64(sr->StorageId, ed1));

   stat = UpdateDB(jcr, cmd, false);
   bdb_unlock();
   return stat;
}


/*
 * Update the Media Record at end of Session
 *
 * Returns: 0 on failure
 *          numrows on success
 */
int BDB::bdb_update_media_record(JCR *jcr, MEDIA_DBR *mr)
{
   char dt[MAX_TIME_LENGTH];
   time_t ttime;
   struct tm tm;
   int stat;
   char ed1[50], ed2[50],  ed3[50],  ed4[50];
   char ed5[50], ed6[50],  ed7[50],  ed8[50];
   char ed9[50], ed10[50], ed11[50], ed12[50];
   char ed13[50], ed14[50], ed15[50], ed16[50];
   char esc_name[MAX_ESCAPE_NAME_LENGTH];
   char esc_status[MAX_ESCAPE_NAME_LENGTH];

   Dmsg1(dbglevel1, "update_media: FirstWritten=%d\n", mr->FirstWritten);
   bdb_lock();
   bdb_escape_string(jcr, esc_name, mr->VolumeName, strlen(mr->VolumeName));
   bdb_escape_string(jcr, esc_status, mr->VolStatus, strlen(mr->VolStatus));

   if (mr->set_first_written) {
      Dmsg1(dbglevel2, "Set FirstWritten Vol=%s\n", mr->VolumeName);
      ttime = mr->FirstWritten;
      (void)localtime_r(&ttime, &tm);
      strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", &tm);
      Mmsg(cmd, "UPDATE Media SET FirstWritten='%s'"
           " WHERE VolumeName='%s'", dt, esc_name);
      stat = UpdateDB(jcr, cmd, false);
      Dmsg1(dbglevel2, "Firstwritten=%d\n", mr->FirstWritten);
   }

   /* Label just done? */
   if (mr->set_label_date) {
      ttime = mr->LabelDate;
      if (ttime == 0) {
         ttime = time(NULL);
      }
      (void)localtime_r(&ttime, &tm);
      strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", &tm);
      Mmsg(cmd, "UPDATE Media SET LabelDate='%s' "
           "WHERE VolumeName='%s'", dt, esc_name);
      UpdateDB(jcr, cmd, false);
   }

   if (mr->LastWritten != 0) {
      ttime = mr->LastWritten;
      (void)localtime_r(&ttime, &tm);
      strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", &tm);
      Mmsg(cmd, "UPDATE Media Set LastWritten='%s' "
           "WHERE VolumeName='%s'", dt, esc_name);
      UpdateDB(jcr, cmd, false);
   }

   /* sanity checks for #1066 */
   if (mr->VolReadTime < 0) {
      mr->VolReadTime = 0;
   }
   if (mr->VolWriteTime < 0) {
      mr->VolWriteTime = 0;
   }

   Mmsg(cmd, "UPDATE Media SET VolJobs=%u,"
        "VolFiles=%u,VolBlocks=%u,VolBytes=%s,VolABytes=%s,"
        "VolHoleBytes=%s,VolHoles=%u,VolMounts=%u,VolErrors=%u,"
        "VolWrites=%s,MaxVolBytes=%s,VolStatus='%s',"
        "Slot=%d,InChanger=%d,VolReadTime=%s,VolWriteTime=%s,VolType=%d,"
        "VolParts=%d,VolCloudParts=%d,LastPartBytes=%s,"
        "LabelType=%d,StorageId=%s,PoolId=%s,VolRetention=%s,VolUseDuration=%s,"
        "MaxVolJobs=%d,MaxVolFiles=%d,Enabled=%d,LocationId=%s,"
        "ScratchPoolId=%s,RecyclePoolId=%s,RecycleCount=%d,Recycle=%d,"
        "ActionOnPurge=%d,CacheRetention=%s,EndBlock=%u"
        " WHERE VolumeName='%s'",
        mr->VolJobs, mr->VolFiles, mr->VolBlocks,
        edit_uint64(mr->VolBytes, ed1),
        edit_uint64(mr->VolABytes, ed2),
        edit_uint64(mr->VolHoleBytes, ed3),
        mr->VolHoles, mr->VolMounts, mr->VolErrors,
        edit_uint64(mr->VolWrites, ed4),
        edit_uint64(mr->MaxVolBytes, ed5),
        esc_status, mr->Slot, mr->InChanger,
        edit_int64(mr->VolReadTime, ed6),
        edit_int64(mr->VolWriteTime, ed7),
        mr->VolType,
        mr->VolParts,
        mr->VolCloudParts,
        edit_uint64(mr->LastPartBytes, ed8),
        mr->LabelType,
        edit_int64(mr->StorageId, ed9),
        edit_int64(mr->PoolId, ed10),
        edit_uint64(mr->VolRetention, ed11),
        edit_uint64(mr->VolUseDuration, ed12),
        mr->MaxVolJobs, mr->MaxVolFiles,
        mr->Enabled, edit_uint64(mr->LocationId, ed13),
        edit_uint64(mr->ScratchPoolId, ed14),
        edit_uint64(mr->RecyclePoolId, ed15),
        mr->RecycleCount,mr->Recycle, mr->ActionOnPurge,
        edit_uint64(mr->CacheRetention, ed16),
        mr->EndBlock,
        esc_name);

   Dmsg1(dbglevel1, "%s\n", cmd);

   stat = UpdateDB(jcr, cmd, false);

   /* Make sure InChanger is 0 for any record having the same Slot */
   db_make_inchanger_unique(jcr, this, mr);

   bdb_unlock();
   return stat;
}

/*
 * Update the Media Record Default values from Pool
 *
 * Returns: 0 on failure
 *          numrows on success
 */
int BDB::bdb_update_media_defaults(JCR *jcr, MEDIA_DBR *mr)
{
   int stat;
   char ed1[50], ed2[50], ed3[50], ed4[50], ed5[50], ed6[50];
   char esc[MAX_ESCAPE_NAME_LENGTH];
   bool can_be_empty;

   bdb_lock();
   if (mr->VolumeName[0]) {
      bdb_escape_string(jcr, esc, mr->VolumeName, strlen(mr->VolumeName));
      Mmsg(cmd, "UPDATE Media SET "
           "ActionOnPurge=%d, Recycle=%d,VolRetention=%s,VolUseDuration=%s,"
           "MaxVolJobs=%u,MaxVolFiles=%u,MaxVolBytes=%s,RecyclePoolId=%s,CacheRetention=%s"
           " WHERE VolumeName='%s'",
           mr->ActionOnPurge, mr->Recycle,edit_uint64(mr->VolRetention, ed1),
           edit_uint64(mr->VolUseDuration, ed2),
           mr->MaxVolJobs, mr->MaxVolFiles,
           edit_uint64(mr->MaxVolBytes, ed3),
           edit_uint64(mr->RecyclePoolId, ed4),
           edit_uint64(mr->CacheRetention, ed5),
           esc);
      can_be_empty = false;

   } else {
      Mmsg(cmd, "UPDATE Media SET "
           "ActionOnPurge=%d, Recycle=%d,VolRetention=%s,VolUseDuration=%s,"
           "MaxVolJobs=%u,MaxVolFiles=%u,MaxVolBytes=%s,RecyclePoolId=%s,CacheRetention=%s"
           " WHERE PoolId=%s",
           mr->ActionOnPurge, mr->Recycle,edit_uint64(mr->VolRetention, ed1),
           edit_uint64(mr->VolUseDuration, ed2),
           mr->MaxVolJobs, mr->MaxVolFiles,
           edit_uint64(mr->MaxVolBytes, ed3),
           edit_int64(mr->RecyclePoolId, ed4),
           edit_uint64(mr->CacheRetention, ed5),
           edit_int64(mr->PoolId, ed6));
      can_be_empty = true;
   }

   Dmsg1(dbglevel1, "%s\n", cmd);

   stat = UpdateDB(jcr, cmd, can_be_empty);

   bdb_unlock();
   return stat;
}


/*
 * If we have a non-zero InChanger, ensure that no other Media
 *  record has InChanger set on the same Slot.
 *
 * This routine assumes the database is already locked.
 */
void BDB::bdb_make_inchanger_unique(JCR *jcr, MEDIA_DBR *mr)
{
   char ed1[50];
   char esc[MAX_ESCAPE_NAME_LENGTH];

   if (mr->InChanger != 0 && mr->Slot != 0 && mr->StorageId != 0) {
       if (!mr->sid_group) {
          mr->sid_group = edit_int64(mr->StorageId, mr->sid);
       }
       if (mr->MediaId != 0) {
          Mmsg(cmd, "UPDATE Media SET InChanger=0, Slot=0 WHERE "
               "Slot=%d AND StorageId IN (%s) AND MediaId!=%s",
               mr->Slot,
               mr->sid_group, edit_int64(mr->MediaId, ed1));

       } else if (*mr->VolumeName) {
          bdb_escape_string(jcr, esc,mr->VolumeName,strlen(mr->VolumeName));
          Mmsg(cmd, "UPDATE Media SET InChanger=0, Slot=0 WHERE "
               "Slot=%d AND StorageId IN (%s) AND VolumeName!='%s'",
               mr->Slot, mr->sid_group, esc);

       } else {  /* used by ua_label to reset all volume with this slot */
          Mmsg(cmd, "UPDATE Media SET InChanger=0, Slot=0 WHERE "
               "Slot=%d AND StorageId IN (%s)",
               mr->Slot, mr->sid_group, mr->VolumeName);
       }
       Dmsg1(dbglevel1, "%s\n", cmd);
       UpdateDB(jcr, cmd, true);
   }
}

/* Update only Retention */
bool BDB::bdb_update_snapshot_record(JCR *jcr, SNAPSHOT_DBR *sr)
{
   int stat, len;
   char ed1[50], ed2[50];

   len = strlen(sr->Comment);
   bdb_lock();

   esc_name = check_pool_memory_size(esc_name, len*2+1);
   bdb_escape_string(jcr, esc_name, sr->Comment, len);

   Mmsg(cmd, "UPDATE Snapshot SET Retention=%s, Comment='%s' WHERE SnapshotId=%s",
        edit_int64(sr->Retention, ed2), sr->Comment, edit_int64(sr->SnapshotId, ed1));

   stat = UpdateDB(jcr, cmd, false);
   bdb_unlock();
   return stat;
}

#endif /* HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL */
