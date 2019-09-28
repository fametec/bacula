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
 * Bacula Catalog Database Create record interface routines
 *
 *    Written by Kern Sibbald, March 2000
 */

#include  "bacula.h"

static const int dbglevel = 160;

#if HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL
 
#include  "cats.h"

/* -----------------------------------------------------------------------
 *
 *   Generic Routines (or almost generic)
 *
 * -----------------------------------------------------------------------
 */

/** Create a new record for the Job
 *  Returns: false on failure
 *          true  on success
 */
bool BDB::bdb_create_job_record(JCR *jcr, JOB_DBR *jr)
{
   POOL_MEM buf;
   char dt[MAX_TIME_LENGTH];
   time_t stime;
   struct tm tm;
   bool ok;
   int len;
   utime_t JobTDate;
   char ed1[30],ed2[30];
   char esc_job[MAX_ESCAPE_NAME_LENGTH];
   char esc_name[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();

   stime = jr->SchedTime;
   ASSERT(stime != 0);

   (void)localtime_r(&stime, &tm);
   strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", &tm);
   JobTDate = (utime_t)stime;

   len = strlen(jcr->comment);  /* TODO: use jr instead of jcr to get comment */
   buf.check_size(len*2+1);
   bdb_escape_string(jcr, buf.c_str(), jcr->comment, len);

   bdb_escape_string(jcr, esc_job, jr->Job, strlen(jr->Job));
   bdb_escape_string(jcr, esc_name, jr->Name, strlen(jr->Name));

   /* Must create it */
   Mmsg(cmd,
"INSERT INTO Job (Job,Name,Type,Level,JobStatus,SchedTime,JobTDate,"
                 "ClientId,Comment) "
"VALUES ('%s','%s','%c','%c','%c','%s',%s,%s,'%s')",
           esc_job, esc_name, (char)(jr->JobType), (char)(jr->JobLevel),
           (char)(jr->JobStatus), dt, edit_uint64(JobTDate, ed1),
           edit_int64(jr->ClientId, ed2), buf.c_str());

   if ((jr->JobId = sql_insert_autokey_record(cmd, NT_("Job"))) == 0) {
      Mmsg2(&errmsg, _("Create DB Job record %s failed. ERR=%s\n"),
            cmd, sql_strerror());
      ok = false;
   } else {
      ok = true;
   }
   bdb_unlock();
   return ok;
}


/** Create a JobMedia record for medium used this job
 *  Returns: false on failure
 *          true  on success
 */
bool BDB::bdb_create_jobmedia_record(JCR *jcr, JOBMEDIA_DBR *jm)
{
   bool ok = true;
   int count;
   char ed1[50], ed2[50];

   bdb_lock();

   /* Now get count for VolIndex */
   Mmsg(cmd, "SELECT MAX(VolIndex) from JobMedia WHERE JobId=%s",
        edit_int64(jm->JobId, ed1));
   count = get_sql_record_max(jcr, this);
   if (count < 0) {
      count = 0;
   }
   count++;

   Mmsg(cmd,
        "INSERT INTO JobMedia (JobId,MediaId,FirstIndex,LastIndex,"
        "StartFile,EndFile,StartBlock,EndBlock,VolIndex) "
        "VALUES (%s,%s,%u,%u,%u,%u,%u,%u,%u)",
        edit_int64(jm->JobId, ed1),
        edit_int64(jm->MediaId, ed2),
        jm->FirstIndex, jm->LastIndex,
        jm->StartFile, jm->EndFile, jm->StartBlock, jm->EndBlock,count);

   Dmsg0(300, cmd);
   if (!InsertDB(jcr, cmd)) {
      Mmsg2(&errmsg, _("Create JobMedia record %s failed: ERR=%s\n"), cmd,
         sql_strerror());
      ok = false;
   } else {
      /* Worked, now update the Media record with the EndFile and EndBlock */
      Mmsg(cmd,
           "UPDATE Media SET EndFile=%lu, EndBlock=%lu WHERE MediaId=%lu",
           jm->EndFile, jm->EndBlock, jm->MediaId);
      if (!UpdateDB(jcr, cmd, false)) {
         Mmsg2(&errmsg, _("Update Media record %s failed: ERR=%s\n"), cmd,
              sql_strerror());
         ok = false;
      }
   }
   bdb_unlock();
   Dmsg0(300, "Return from JobMedia\n");
   return ok;
}

/** Create Unique Pool record
 *  Returns: false on failure
 *          true  on success
 */
bool BDB::bdb_create_pool_record(JCR *jcr, POOL_DBR *pr)
{
   bool stat;
   char ed1[30], ed2[30], ed3[50], ed4[50], ed5[50], ed6[50];
   char esc_name[MAX_ESCAPE_NAME_LENGTH];
   char esc_lf[MAX_ESCAPE_NAME_LENGTH];

   Dmsg0(200, "In create pool\n");
   bdb_lock();
   bdb_escape_string(jcr, esc_name, pr->Name, strlen(pr->Name));
   bdb_escape_string(jcr, esc_lf, pr->LabelFormat, strlen(pr->LabelFormat));
   Mmsg(cmd, "SELECT PoolId,Name FROM Pool WHERE Name='%s'", esc_name);
   Dmsg1(200, "selectpool: %s\n", cmd);

   if (QueryDB(jcr, cmd)) {
      if (sql_num_rows() > 0) { 
         Mmsg1(&errmsg, _("pool record %s already exists\n"), pr->Name);
         sql_free_result();
         bdb_unlock();
         Dmsg1(200, "%s", errmsg); /* pool already exists */
         return false;
      }
      sql_free_result();
   }

   /* Must create it */
   Mmsg(cmd,
"INSERT INTO Pool (Name,NumVols,MaxVols,UseOnce,UseCatalog,"
"AcceptAnyVolume,AutoPrune,Recycle,VolRetention,VolUseDuration,"
"MaxVolJobs,MaxVolFiles,MaxVolBytes,PoolType,LabelType,LabelFormat,"
"RecyclePoolId,ScratchPoolId,ActionOnPurge,CacheRetention) "
"VALUES ('%s',%u,%u,%d,%d,%d,%d,%d,%s,%s,%u,%u,%s,'%s',%d,'%s',%s,%s,%d,%s)",
                  esc_name,
                  pr->NumVols, pr->MaxVols,
                  pr->UseOnce, pr->UseCatalog,
                  pr->AcceptAnyVolume,
                  pr->AutoPrune, pr->Recycle,
                  edit_uint64(pr->VolRetention, ed1),
                  edit_uint64(pr->VolUseDuration, ed2),
                  pr->MaxVolJobs, pr->MaxVolFiles,
                  edit_uint64(pr->MaxVolBytes, ed3),
                  pr->PoolType, pr->LabelType, esc_lf,
                  edit_int64(pr->RecyclePoolId,ed4),
                  edit_int64(pr->ScratchPoolId,ed5),
                  pr->ActionOnPurge,
                  edit_uint64(pr->CacheRetention, ed6)
      );
   Dmsg1(200, "Create Pool: %s\n", cmd);
   if ((pr->PoolId = sql_insert_autokey_record(cmd, NT_("Pool"))) == 0) {
      Mmsg2(&errmsg, _("Create db Pool record %s failed: ERR=%s\n"),
            cmd, sql_strerror());
      stat = false;
   } else {
      stat = true;
   }
   bdb_unlock();
   return stat;
}

/**
 * Create Unique Device record
 * Returns: false on failure
 *          true  on success
 */
bool BDB::bdb_create_device_record(JCR *jcr, DEVICE_DBR *dr)
{
   bool ok;
   char ed1[30], ed2[30];
   char esc[MAX_ESCAPE_NAME_LENGTH];

   Dmsg0(200, "In create Device\n");
   bdb_lock();
   bdb_escape_string(jcr, esc, dr->Name, strlen(dr->Name));
   Mmsg(cmd, "SELECT DeviceId,Name FROM Device WHERE Name='%s'", esc);
   Dmsg1(200, "selectdevice: %s\n", cmd);

   if (QueryDB(jcr, cmd)) {
      if (sql_num_rows() > 0) { 
         Mmsg1(&errmsg, _("Device record %s already exists\n"), dr->Name);
         sql_free_result();
         bdb_unlock();
         return false;
      }
      sql_free_result();
   }

   /* Must create it */
   Mmsg(cmd,
"INSERT INTO Device (Name,MediaTypeId,StorageId) VALUES ('%s',%s,%s)",
                  esc,
                  edit_uint64(dr->MediaTypeId, ed1),
                  edit_int64(dr->StorageId, ed2));
   Dmsg1(200, "Create Device: %s\n", cmd);
   if ((dr->DeviceId = sql_insert_autokey_record(cmd, NT_("Device"))) == 0) {
      Mmsg2(&errmsg, _("Create db Device record %s failed: ERR=%s\n"),
            cmd, sql_strerror());
      ok = false;
   } else {
      ok = true;
   }
   bdb_unlock();
   return ok;
}



/**
 * Create a Unique record for Storage -- no duplicates
 * Returns: false on failure
 *          true  on success with id in sr->StorageId
 */
bool BDB::bdb_create_storage_record(JCR *jcr, STORAGE_DBR *sr)
{
   SQL_ROW row;
   bool ok;
   char esc[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   bdb_escape_string(jcr, esc, sr->Name, strlen(sr->Name));
   Mmsg(cmd, "SELECT StorageId,AutoChanger FROM Storage WHERE Name='%s'",esc);

   sr->StorageId = 0;
   sr->created = false;
   /* Check if it already exists */
   if (QueryDB(jcr, cmd)) {
      /* If more than one, report error, but return first row */
      if (sql_num_rows() > 1) { 
         Mmsg1(&errmsg, _("More than one Storage record!: %d\n"), sql_num_rows());
         Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      }
      if (sql_num_rows() >= 1) { 
         if ((row = sql_fetch_row()) == NULL) {
            Mmsg1(&errmsg, _("error fetching Storage row: %s\n"), sql_strerror());
            Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
            sql_free_result();
            bdb_unlock();
            return false;
         }
         sr->StorageId = str_to_int64(row[0]);
         sr->AutoChanger = atoi(row[1]);   /* bool */
         sql_free_result();
         bdb_unlock();
         return true;
      }
      sql_free_result();
   }

   /* Must create it */
   Mmsg(cmd, "INSERT INTO Storage (Name,AutoChanger)"
        " VALUES ('%s',%d)", esc, sr->AutoChanger);

   if ((sr->StorageId = sql_insert_autokey_record(cmd, NT_("Storage"))) == 0) {
      Mmsg2(&errmsg, _("Create DB Storage record %s failed. ERR=%s\n"),
            cmd, sql_strerror());
      Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      ok = false;
   } else {
      sr->created = true;
      ok = true;
   }
   bdb_unlock();
   return ok;
}


/**
 * Create Unique MediaType record
 * Returns: false on failure
 *          true  on success
 */
bool BDB::bdb_create_mediatype_record(JCR *jcr, MEDIATYPE_DBR *mr)
{
   bool stat;
   char esc[MAX_ESCAPE_NAME_LENGTH];

   Dmsg0(200, "In create mediatype\n");
   bdb_lock();
   bdb_escape_string(jcr, esc, mr->MediaType, strlen(mr->MediaType));
   Mmsg(cmd, "SELECT MediaTypeId,MediaType FROM MediaType WHERE MediaType='%s'", esc);
   Dmsg1(200, "selectmediatype: %s\n", cmd);

   if (QueryDB(jcr, cmd)) {
      if (sql_num_rows() > 0) { 
         Mmsg1(&errmsg, _("mediatype record %s already exists\n"), mr->MediaType);
         sql_free_result();
         bdb_unlock();
         return false;
      }
      sql_free_result();
   }

   /* Must create it */
   Mmsg(cmd,
"INSERT INTO MediaType (MediaType,ReadOnly) "
"VALUES ('%s',%d)",
                  mr->MediaType,
                  mr->ReadOnly);
   Dmsg1(200, "Create mediatype: %s\n", cmd);
   if ((mr->MediaTypeId = sql_insert_autokey_record(cmd, NT_("MediaType"))) == 0) {
      Mmsg2(&errmsg, _("Create db mediatype record %s failed: ERR=%s\n"),
            cmd, sql_strerror());
      stat = false;
   } else {
      stat = true;
   }
   bdb_unlock();
   return stat;
}


/**
 * Create Media record. VolumeName and non-zero Slot must be unique
 *
 * Returns: 0 on failure
 *          1 on success
 */
int BDB::bdb_create_media_record(JCR *jcr, MEDIA_DBR *mr)
{
   int stat;
   char ed1[50], ed2[50], ed3[50], ed4[50], ed5[50], ed6[50], ed7[50], ed8[50];
   char ed9[50], ed10[50], ed11[50], ed12[50], ed13[50], ed14[50];
   struct tm tm;
   char esc_name[MAX_ESCAPE_NAME_LENGTH];
   char esc_mtype[MAX_ESCAPE_NAME_LENGTH];
   char esc_status[MAX_ESCAPE_NAME_LENGTH];


   bdb_lock();
   bdb_escape_string(jcr, esc_name, mr->VolumeName, strlen(mr->VolumeName));
   bdb_escape_string(jcr, esc_mtype, mr->MediaType, strlen(mr->MediaType));
   bdb_escape_string(jcr, esc_status, mr->VolStatus, strlen(mr->VolStatus));

   Mmsg(cmd, "SELECT MediaId FROM Media WHERE VolumeName='%s'", esc_name);
   Dmsg1(500, "selectpool: %s\n", cmd);

   if (QueryDB(jcr, cmd)) {
      if (sql_num_rows() > 0) { 
         Mmsg1(&errmsg, _("Volume \"%s\" already exists.\n"), mr->VolumeName);
         sql_free_result();
         bdb_unlock();
         return 0;
      }
      sql_free_result();
   }

   /* Must create it */
   Mmsg(cmd,
"INSERT INTO Media (VolumeName,MediaType,MediaTypeId,PoolId,MaxVolBytes,"
"VolCapacityBytes,Recycle,VolRetention,VolUseDuration,MaxVolJobs,MaxVolFiles,"
"VolStatus,Slot,VolBytes,InChanger,VolReadTime,VolWriteTime,VolType,"
"VolParts,VolCloudParts,LastPartBytes,"
"EndFile,EndBlock,LabelType,StorageId,DeviceId,LocationId,"
"ScratchPoolId,RecyclePoolId,Enabled,ActionOnPurge,CacheRetention)"
"VALUES ('%s','%s',0,%u,%s,%s,%d,%s,%s,%u,%u,'%s',%d,%s,%d,%s,%s,%d,"
        "%d,%d,'%s',%d,%d,%d,%s,%s,%s,%s,%s,%d,%d,%s)",
          esc_name,
          esc_mtype, mr->PoolId,
          edit_uint64(mr->MaxVolBytes,ed1),
          edit_uint64(mr->VolCapacityBytes, ed2),
          mr->Recycle,
          edit_uint64(mr->VolRetention, ed3),
          edit_uint64(mr->VolUseDuration, ed4),
          mr->MaxVolJobs,
          mr->MaxVolFiles,
          esc_status,
          mr->Slot,
          edit_uint64(mr->VolBytes, ed5),
          mr->InChanger,
          edit_int64(mr->VolReadTime, ed6),
          edit_int64(mr->VolWriteTime, ed7),
          mr->VolType,
          mr->VolParts,
          mr->VolCloudParts,
          edit_uint64(mr->LastPartBytes, ed8),
          mr->EndFile,
          mr->EndBlock,
          mr->LabelType,
          edit_int64(mr->StorageId, ed9),
          edit_int64(mr->DeviceId, ed10),
          edit_int64(mr->LocationId, ed11),
          edit_int64(mr->ScratchPoolId, ed12),
          edit_int64(mr->RecyclePoolId, ed13),
          mr->Enabled, 
          mr->ActionOnPurge,
          edit_uint64(mr->CacheRetention, ed14)
          );

   Dmsg1(500, "Create Volume: %s\n", cmd);
   if ((mr->MediaId = sql_insert_autokey_record(cmd, NT_("Media"))) == 0) {
      Mmsg2(&errmsg, _("Create DB Media record %s failed. ERR=%s\n"),
            cmd, sql_strerror());
      stat = 0;
   } else {
      stat = 1;
      if (mr->set_label_date) {
         char dt[MAX_TIME_LENGTH];
         if (mr->LabelDate == 0) {
            mr->LabelDate = time(NULL);
         }
         (void)localtime_r(&mr->LabelDate, &tm);
         strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", &tm);
         Mmsg(cmd, "UPDATE Media SET LabelDate='%s' "
              "WHERE MediaId=%lu", dt, mr->MediaId);
         stat = UpdateDB(jcr, cmd, false);
      }
      /*
       * Make sure that if InChanger is non-zero any other identical slot
       *   has InChanger zero.
       */
      db_make_inchanger_unique(jcr, this, mr);
   }

   bdb_unlock();
   return stat;
}

/**
 * Create a Unique record for the client -- no duplicates
 * Returns: 0 on failure
 *          1 on success with id in cr->ClientId
 */
int BDB::bdb_create_client_record(JCR *jcr, CLIENT_DBR *cr)
{
   SQL_ROW row;
   int stat;
   char ed1[50], ed2[50];
   char esc_name[MAX_ESCAPE_NAME_LENGTH];
   char esc_uname[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   bdb_escape_string(jcr, esc_name, cr->Name, strlen(cr->Name));
   bdb_escape_string(jcr, esc_uname, cr->Uname, strlen(cr->Uname));
   Mmsg(cmd, "SELECT ClientId,Uname,AutoPrune,"
        "FileRetention,JobRetention FROM Client WHERE Name='%s'",esc_name);

   cr->ClientId = 0;
   if (QueryDB(jcr, cmd)) {
      /* If more than one, report error, but return first row */
      if (sql_num_rows() > 1) { 
         Mmsg1(&errmsg, _("More than one Client!: %d\n"), sql_num_rows());
         Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      }
      if (sql_num_rows() >= 1) { 
         if ((row = sql_fetch_row()) == NULL) {
            Mmsg1(&errmsg, _("error fetching Client row: %s\n"), sql_strerror());
            Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
            sql_free_result();
            bdb_unlock();
            return 0;
         }
         cr->ClientId = str_to_int64(row[0]);
         if (row[1]) {
            bstrncpy(cr->Uname, row[1], sizeof(cr->Uname));
         } else {
            cr->Uname[0] = 0;         /* no name */
         }
         cr->AutoPrune = str_to_int64(row[2]);
         cr->FileRetention = str_to_int64(row[3]);
         cr->JobRetention = str_to_int64(row[4]);
         sql_free_result();
         bdb_unlock();
         return 1;
      }
      sql_free_result();
   }

   /* Must create it */
   Mmsg(cmd, "INSERT INTO Client (Name,Uname,AutoPrune,"
"FileRetention,JobRetention) VALUES "
"('%s','%s',%d,%s,%s)", esc_name, esc_uname, cr->AutoPrune,
      edit_uint64(cr->FileRetention, ed1),
      edit_uint64(cr->JobRetention, ed2));

   if ((cr->ClientId = sql_insert_autokey_record(cmd, NT_("Client"))) == 0) {
      Mmsg2(&errmsg, _("Create DB Client record %s failed. ERR=%s\n"),
            cmd, sql_strerror());
      Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      stat = 0;
   } else {
      stat = 1;
   }
   bdb_unlock();
   return stat;
}


/** Create a Unique record for the Path -- no duplicates */
int BDB::bdb_create_path_record(JCR *jcr, ATTR_DBR *ar)
{
   SQL_ROW row;
   int stat;

   errmsg[0] = 0;
   esc_name = check_pool_memory_size(esc_name, 2*pnl+2);
   bdb_escape_string(jcr, esc_name, path, pnl);

   if (cached_path_id != 0 && cached_path_len == pnl &&
       strcmp(cached_path, path) == 0) {
      ar->PathId = cached_path_id;
      return 1;
   }

   Mmsg(cmd, "SELECT PathId FROM Path WHERE Path='%s'", esc_name);

   if (QueryDB(jcr, cmd)) {
      if (sql_num_rows() > 1) { 
         char ed1[30];
         Mmsg2(&errmsg, _("More than one Path!: %s for path: %s\n"),
            edit_uint64(sql_num_rows(), ed1), path);
         Jmsg(jcr, M_WARNING, 0, "%s", errmsg);
      }
      /* Even if there are multiple paths, take the first one */
      if (sql_num_rows() >= 1) { 
         if ((row = sql_fetch_row()) == NULL) {
            Mmsg1(&errmsg, _("error fetching row: %s\n"), sql_strerror());
            Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
            sql_free_result();
            ar->PathId = 0;
            ASSERT2(ar->PathId,
                    "Your Path table is broken. "
                    "Please, use dbcheck to correct it.");
            return 0;
         }
         ar->PathId = str_to_int64(row[0]);
         sql_free_result();
         /* Cache path */
         if (ar->PathId != cached_path_id) {
            cached_path_id = ar->PathId;
            cached_path_len = pnl;
            pm_strcpy(cached_path, path);
         }
         ASSERT(ar->PathId);
         return 1;
      }
      sql_free_result();
   }

   Mmsg(cmd, "INSERT INTO Path (Path) VALUES ('%s')", esc_name);

   if ((ar->PathId = sql_insert_autokey_record(cmd, NT_("Path"))) == 0) {
      Mmsg2(&errmsg, _("Create db Path record %s failed. ERR=%s\n"),
         cmd, sql_strerror());
      Jmsg(jcr, M_FATAL, 0, "%s", errmsg);
      ar->PathId = 0;
      stat = 0;
   } else {
      stat = 1;
   }

   /* Cache path */
   if (stat && ar->PathId != cached_path_id) {
      cached_path_id = ar->PathId;
      cached_path_len = pnl;
      pm_strcpy(cached_path, path);
   }
   return stat;
}

/**
 * Create a Unique record for the counter -- no duplicates
 * Returns: 0 on failure
 *          1 on success with counter filled in
 */
int BDB::bdb_create_counter_record(JCR *jcr, COUNTER_DBR *cr)
{
   char esc[MAX_ESCAPE_NAME_LENGTH];
   COUNTER_DBR mcr;
   int stat;

   bdb_lock();
   memset(&mcr, 0, sizeof(mcr));
   bstrncpy(mcr.Counter, cr->Counter, sizeof(mcr.Counter));
   if (bdb_get_counter_record(jcr, &mcr)) {
      memcpy(cr, &mcr, sizeof(COUNTER_DBR));
      bdb_unlock();
      return 1;
   }
   bdb_escape_string(jcr, esc, cr->Counter, strlen(cr->Counter));
 
   /* Must create it */
   Mmsg(cmd, insert_counter_values[bdb_get_type_index()],
        esc, cr->MinValue, cr->MaxValue, cr->CurrentValue,
        cr->WrapCounter);

   if (!InsertDB(jcr, cmd)) {
      Mmsg2(&errmsg, _("Create DB Counters record %s failed. ERR=%s\n"),
            cmd, sql_strerror());
      Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      stat = 0;
   } else {
      stat = 1;
   }
   bdb_unlock();
   return stat;
}

/**
 * Create a FileSet record. This record is unique in the
 *  name and the MD5 signature of the include/exclude sets.
 *  Returns: 0 on failure
 *           1 on success with FileSetId in record
 */
bool BDB::bdb_create_fileset_record(JCR *jcr, FILESET_DBR *fsr)
{
   SQL_ROW row;
   bool stat;
   struct tm tm;
   char esc_fs[MAX_ESCAPE_NAME_LENGTH];
   char esc_md5[MAX_ESCAPE_NAME_LENGTH];

   /* TODO: Escape FileSet and MD5 */
   bdb_lock();
   fsr->created = false;
   bdb_escape_string(jcr, esc_fs, fsr->FileSet, strlen(fsr->FileSet));
   bdb_escape_string(jcr, esc_md5, fsr->MD5, strlen(fsr->MD5));
   Mmsg(cmd, "SELECT FileSetId,CreateTime FROM FileSet WHERE "
                  "FileSet='%s' AND MD5='%s'", esc_fs, esc_md5);

   fsr->FileSetId = 0;
   if (QueryDB(jcr, cmd)) {
      if (sql_num_rows() > 1) { 
         Mmsg1(&errmsg, _("More than one FileSet!: %d\n"), sql_num_rows());
         Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      }
      if (sql_num_rows() >= 1) { 
         if ((row = sql_fetch_row()) == NULL) {
            Mmsg1(&errmsg, _("error fetching FileSet row: ERR=%s\n"), sql_strerror());
            Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
            sql_free_result();
            bdb_unlock();
            return false;
         }
         fsr->FileSetId = str_to_int64(row[0]);
         if (row[1] == NULL) {
            fsr->cCreateTime[0] = 0;
         } else {
            bstrncpy(fsr->cCreateTime, row[1], sizeof(fsr->cCreateTime));
         }
         sql_free_result();
         bdb_unlock();
         return true;
      }
      sql_free_result();
   }

   if (fsr->CreateTime == 0 && fsr->cCreateTime[0] == 0) {
      fsr->CreateTime = time(NULL);
   }
   (void)localtime_r(&fsr->CreateTime, &tm);
   strftime(fsr->cCreateTime, sizeof(fsr->cCreateTime), "%Y-%m-%d %H:%M:%S", &tm);

   /* Must create it */
      Mmsg(cmd, "INSERT INTO FileSet (FileSet,MD5,CreateTime) "
"VALUES ('%s','%s','%s')", esc_fs, esc_md5, fsr->cCreateTime);

   if ((fsr->FileSetId = sql_insert_autokey_record(cmd, NT_("FileSet"))) == 0) {
      Mmsg2(&errmsg, _("Create DB FileSet record %s failed. ERR=%s\n"),
            cmd, sql_strerror());
      Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      stat = false;
   } else {
      fsr->created = true;
      stat = true;
   }

   bdb_unlock();
   return stat;
}


/**
 *  struct stat
 *  {
 *      dev_t         st_dev;       * device *
 *      ino_t         st_ino;       * inode *
 *      mode_t        st_mode;      * protection *
 *      nlink_t       st_nlink;     * number of hard links *
 *      uid_t         st_uid;       * user ID of owner *
 *      gid_t         st_gid;       * group ID of owner *
 *      dev_t         st_rdev;      * device type (if inode device) *
 *      off_t         st_size;      * total size, in bytes *
 *      unsigned long st_blksize;   * blocksize for filesystem I/O *
 *      unsigned long st_blocks;    * number of blocks allocated *
 *      time_t        st_atime;     * time of last access *
 *      time_t        st_mtime;     * time of last modification *
 *      time_t        st_ctime;     * time of last inode change *
 *  };
 */

/* For maintenance, we can put batch mode in hold */
static bool batch_mode_enabled = true;

void bdb_disable_batch_insert(bool enabled)
{
   batch_mode_enabled = enabled;
}

/*
 * All sql_batch_xx functions are used to do bulk batch 
 *  insert in File/Filename/Path tables.
 *
 *  To sum up :
 *   - bulk load a temp table
 *   - insert missing filenames into filename with a single query (lock filenames 
 *   - table before that to avoid possible duplicate inserts with concurrent update)
 *   - insert missing paths into path with another single query
 *   - then insert the join between the temp, filename and path tables into file.
 */

/*
 *  Returns true if OK
 *          false if failed
 */
bool bdb_write_batch_file_records(JCR *jcr)
{
   bool retval = false; 
   int JobStatus = jcr->JobStatus;

   if (!jcr->batch_started) {         /* no files to backup ? */
      Dmsg0(50,"db_write_batch_file_records: no files\n");
      return true;
   }

   if (job_canceled(jcr)) {
      goto bail_out; 
   }

   jcr->JobStatus = JS_AttrInserting;

   /* Check if batch mode is on hold */
   while (!batch_mode_enabled) {
      Dmsg0(50, "batch mode is on hold\n");
      bmicrosleep(10, 0);

      if (job_canceled(jcr)) {
         goto bail_out; 
      }
   }

   Dmsg1(50,"db_write_batch_file_records changes=%u\n",jcr->db_batch->changes);

   if (!jcr->db_batch->sql_batch_end(jcr, NULL)) {
      Jmsg1(jcr, M_FATAL, 0, "Batch end %s\n", jcr->db_batch->errmsg);
      goto bail_out; 
   }
   if (job_canceled(jcr)) {
      goto bail_out; 
   }

   /* We have to lock tables */
   if (!jcr->db_batch->bdb_sql_query(batch_lock_path_query[jcr->db_batch->bdb_get_type_index()], NULL, NULL)) {
      Jmsg1(jcr, M_FATAL, 0, "Lock Path table %s\n", jcr->db_batch->errmsg);
      goto bail_out; 
   }

   if (!jcr->db_batch->bdb_sql_query(batch_fill_path_query[jcr->db_batch->bdb_get_type_index()], NULL, NULL)) {
      Jmsg1(jcr, M_FATAL, 0, "Fill Path table %s\n",jcr->db_batch->errmsg);
      jcr->db_batch->bdb_sql_query(batch_unlock_tables_query[jcr->db_batch->bdb_get_type_index()], NULL, NULL);
      goto bail_out; 
   }

   if (!jcr->db_batch->bdb_sql_query(batch_unlock_tables_query[jcr->db_batch->bdb_get_type_index()], NULL, NULL)) {
      Jmsg1(jcr, M_FATAL, 0, "Unlock Path table %s\n", jcr->db_batch->errmsg);
      goto bail_out; 
   }

   /* We have to lock tables */
   if (!db_sql_query(jcr->db_batch, batch_lock_filename_query[db_get_type_index(jcr->db_batch)], NULL, NULL)) { 
      Jmsg1(jcr, M_FATAL, 0, "Lock Filename table %s\n", jcr->db_batch->errmsg);
      goto bail_out; 
   }
   
   if (!db_sql_query(jcr->db_batch, batch_fill_filename_query[db_get_type_index(jcr->db_batch)], NULL, NULL)) { 
      Jmsg1(jcr,M_FATAL,0,"Fill Filename table %s\n",jcr->db_batch->errmsg);
      db_sql_query(jcr->db_batch, batch_unlock_tables_query[db_get_type_index(jcr->db_batch)], NULL, NULL); 
      goto bail_out; 
   }

   if (!db_sql_query(jcr->db_batch, batch_unlock_tables_query[db_get_type_index(jcr->db_batch)], NULL, NULL)) { 
      Jmsg1(jcr, M_FATAL, 0, "Unlock Filename table %s\n", jcr->db_batch->errmsg);
      goto bail_out; 
   }
   
   if (!db_sql_query(jcr->db_batch, 
"INSERT INTO File (FileIndex, JobId, PathId, FilenameId, LStat, MD5, DeltaSeq) "
    "SELECT batch.FileIndex, batch.JobId, Path.PathId, "
           "Filename.FilenameId,batch.LStat, batch.MD5, batch.DeltaSeq "
      "FROM batch "
      "JOIN Path ON (batch.Path = Path.Path) "
      "JOIN Filename ON (batch.Name = Filename.Name)",
         NULL, NULL))
   {
      Jmsg1(jcr, M_FATAL, 0, "Fill File table %s\n", jcr->db_batch->errmsg);
      goto bail_out; 
   }

   jcr->JobStatus = JobStatus;    /* reset entry status */
   retval = true; 
 
bail_out: 
   jcr->db_batch->bdb_sql_query("DROP TABLE batch", NULL,NULL);
   jcr->batch_started = false;

   return retval; 
}

/*
 * Create File record in BDB
 *
 *  In order to reduce database size, we store the File attributes,
 *  the FileName, and the Path separately.  In principle, there
 *  is a single FileName record and a single Path record, no matter
 *  how many times it occurs.  This is this subroutine, we separate
 *  the file and the path and fill temporary tables with this three records.
 *
 *  Note: all routines that call this expect to be able to call
 *    db_strerror(mdb) to get the error message, so the error message
 *    MUST be edited into errmsg before returning an error status.
 */
bool BDB::bdb_create_batch_file_attributes_record(JCR *jcr, ATTR_DBR *ar)
{
   ASSERT(ar->FileType != FT_BASE);
   Dmsg2(dbglevel, "FileIndex=%d Fname=%s\n", ar->FileIndex, ar->fname);
   Dmsg0(dbglevel, "put_file_into_catalog\n");

   if (jcr->batch_started && jcr->db_batch->changes > 500000) {
      bdb_write_batch_file_records(jcr);
      jcr->db_batch->changes = 0;
   }

   /* Open the dedicated connexion */
   if (!jcr->batch_started) {
      if (!bdb_open_batch_connexion(jcr)) {
         return false;     /* error already printed */
      }
      if (!jcr->db_batch->sql_batch_start(jcr)) {
         Mmsg1(&errmsg,
              "Can't start batch mode: ERR=%s", jcr->db_batch->bdb_strerror());
         Jmsg(jcr, M_FATAL, 0, "%s", errmsg);
         return false;
      }
      jcr->batch_started = true;
   }

   split_path_and_file(jcr, jcr->db_batch, ar->fname);

   return jcr->db_batch->sql_batch_insert(jcr, ar);
}

/**
 * Create File record in BDB
 *
 *  In order to reduce database size, we store the File attributes,
 *  the FileName, and the Path separately.  In principle, there
 *  is a single FileName record and a single Path record, no matter
 *  how many times it occurs.  This is this subroutine, we separate
 *  the file and the path and create three database records.
 */
bool BDB::bdb_create_file_attributes_record(JCR *jcr, ATTR_DBR *ar)
{
   bdb_lock();
   Dmsg2(dbglevel, "FileIndex=%d Fname=%s\n", ar->FileIndex, ar->fname);
   Dmsg0(dbglevel, "put_file_into_catalog\n");

   split_path_and_file(jcr, this, ar->fname);

   if (!bdb_create_filename_record(jcr, ar)) {
      goto bail_out;
   }
   Dmsg1(dbglevel, "bdb_create_filename_record: %s\n", esc_name);


   if (!bdb_create_path_record(jcr, ar)) {
      goto bail_out;
   }
   Dmsg1(dbglevel, "bdb_create_path_record: %s\n", esc_name);

   /* Now create master File record */
   if (!bdb_create_file_record(jcr, ar)) {
      goto bail_out; 
   }
   Dmsg0(dbglevel, "db_create_file_record OK\n");

   Dmsg3(dbglevel, "CreateAttributes Path=%s File=%s FilenameId=%d\n", path, fname, ar->FilenameId);
   bdb_unlock();
   return true;

bail_out: 
   bdb_unlock();
   return false;
}
/**
 * This is the master File entry containing the attributes.
 *  The filename and path records have already been created.
 */
int BDB::bdb_create_file_record(JCR *jcr, ATTR_DBR *ar)
{
   int stat;
   static const char *no_digest = "0";
   const char *digest;

   ASSERT(ar->JobId);
   ASSERT(ar->PathId);
   ASSERT(ar->FilenameId);

   if (ar->Digest == NULL || ar->Digest[0] == 0) {
      digest = no_digest;
   } else {
      digest = ar->Digest;
   }

   /* Must create it */
   Mmsg(cmd,
        "INSERT INTO File (FileIndex,JobId,PathId,FilenameId,"
        "LStat,MD5,DeltaSeq) VALUES (%d,%u,%u,%u,'%s','%s',%u)",
        ar->FileIndex, ar->JobId, ar->PathId, ar->FilenameId,
        ar->attr, digest, ar->DeltaSeq);

   if ((ar->FileId = sql_insert_autokey_record(cmd, NT_("File"))) == 0) {
      Mmsg2(&errmsg, _("Create db File record %s failed. ERR=%s"),
         cmd, sql_strerror());
      Jmsg(jcr, M_FATAL, 0, "%s", errmsg);
      stat = 0;
   } else {
      stat = 1;
   }
   return stat;
}

/** Create a Unique record for the filename -- no duplicates */
int BDB::bdb_create_filename_record(JCR *jcr, ATTR_DBR *ar)
{
   SQL_ROW row;

   errmsg[0] = 0;
   esc_name = check_pool_memory_size(esc_name, 2*fnl+2);
   bdb_escape_string(jcr, esc_name, fname, fnl);
   
   Mmsg(cmd, "SELECT FilenameId FROM Filename WHERE Name='%s'", esc_name);

   if (QueryDB(jcr, cmd)) {
      if (sql_num_rows() > 1) {
         char ed1[30];
         Mmsg2(&errmsg, _("More than one Filename! %s for file: %s\n"),
            edit_uint64(sql_num_rows(), ed1), fname);
         Jmsg(jcr, M_WARNING, 0, "%s", errmsg);
      }
      if (sql_num_rows() >= 1) {
         if ((row = sql_fetch_row()) == NULL) {
            Mmsg2(&errmsg, _("Error fetching row for file=%s: ERR=%s\n"),
                fname, sql_strerror());
            Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
            ar->FilenameId = 0;
         } else {
            ar->FilenameId = str_to_int64(row[0]);
         }
         sql_free_result();
         return ar->FilenameId > 0;
      }
      sql_free_result();
   }

   Mmsg(cmd, "INSERT INTO Filename (Name) VALUES ('%s')", esc_name);

   ar->FilenameId = sql_insert_autokey_record(cmd, NT_("Filename"));
   if (ar->FilenameId == 0) { 
      Mmsg2(&errmsg, _("Create db Filename record %s failed. ERR=%s\n"),
            cmd, sql_strerror());
      Jmsg(jcr, M_FATAL, 0, "%s", errmsg);
   }
   return ar->FilenameId > 0;
}

/* 
 * Create file attributes record, or base file attributes record
 */
bool BDB::bdb_create_attributes_record(JCR *jcr, ATTR_DBR *ar)
{
   bool ret;

   Dmsg2(dbglevel, "FileIndex=%d Fname=%s\n", ar->FileIndex, ar->fname);
   errmsg[0] = 0;
   /*
    * Make sure we have an acceptable attributes record.
    */
   if (!(ar->Stream == STREAM_UNIX_ATTRIBUTES ||
         ar->Stream == STREAM_UNIX_ATTRIBUTES_EX)) {
      Mmsg1(&errmsg, _("Attempt to put non-attributes into catalog. Stream=%d\n"),
         ar->Stream);
      Jmsg(jcr, M_FATAL, 0, "%s", errmsg);
      return false;
   }

   if (ar->FileType != FT_BASE) {
      if (batch_insert_available()) {
         ret = bdb_create_batch_file_attributes_record(jcr, ar);
         /* Error message already printed */
      } else { 
         ret = bdb_create_file_attributes_record(jcr, ar);
      } 
   } else if (jcr->HasBase) {
      ret = bdb_create_base_file_attributes_record(jcr, ar);
   } else {
      Mmsg0(&errmsg, _("Cannot Copy/Migrate job using BaseJob.\n"));
      Jmsg(jcr, M_FATAL, 0, "%s", errmsg);
      ret = true;               /* in copy/migration what do we do ? */
   }

   return ret;
}

/**
 * Create Base File record in BDB
 *
 */
bool BDB::bdb_create_base_file_attributes_record(JCR *jcr, ATTR_DBR *ar)
{
   bool ret;

   Dmsg1(dbglevel, "create_base_file Fname=%s\n", ar->fname);
   Dmsg0(dbglevel, "put_base_file_into_catalog\n");

   bdb_lock();
   split_path_and_file(jcr, this, ar->fname);

   esc_name = check_pool_memory_size(esc_name, fnl*2+1);
   bdb_escape_string(jcr, esc_name, fname, fnl);

   esc_path = check_pool_memory_size(esc_path, pnl*2+1);
   bdb_escape_string(jcr, esc_path, path, pnl);

   Mmsg(cmd, "INSERT INTO basefile%lld (Path, Name) VALUES ('%s','%s')",
        (uint64_t)jcr->JobId, esc_path, esc_name);

   ret = InsertDB(jcr, cmd);
   bdb_unlock();

   return ret;
}

/**
 * Cleanup the base file temporary tables
 */
static void db_cleanup_base_file(JCR *jcr, BDB *mdb)
{
   POOL_MEM buf(PM_MESSAGE);
   Mmsg(buf, "DROP TABLE new_basefile%lld", (uint64_t) jcr->JobId);
   mdb->bdb_sql_query(buf.c_str(), NULL, NULL);

   Mmsg(buf, "DROP TABLE basefile%lld", (uint64_t) jcr->JobId);
   mdb->bdb_sql_query(buf.c_str(), NULL, NULL);
}

/**
 * Put all base file seen in the backup to the BaseFile table
 * and cleanup temporary tables
 */
bool BDB::bdb_commit_base_file_attributes_record(JCR *jcr)
{
   bool ret;
   char ed1[50];

   bdb_lock();

   Mmsg(cmd,
  "INSERT INTO BaseFiles (BaseJobId, JobId, FileId, FileIndex) "
   "SELECT B.JobId AS BaseJobId, %s AS JobId, "
          "B.FileId, B.FileIndex "
     "FROM basefile%s AS A, new_basefile%s AS B "
    "WHERE A.Path = B.Path "
      "AND A.Name = B.Name "
    "ORDER BY B.FileId",
        edit_uint64(jcr->JobId, ed1), ed1, ed1);
   ret = bdb_sql_query(cmd, NULL, NULL);
   /*
    * Display error now, because the subsequent cleanup destroys the
    *  error message from the above query.
    */
   if (!ret) {
      Jmsg1(jcr, M_FATAL, 0, "%s", jcr->db->bdb_strerror());
   }
   jcr->nb_base_files_used = sql_affected_rows();
   db_cleanup_base_file(jcr, this);

   bdb_unlock();
   return ret;
}

/**
 * Find the last "accurate" backup state with Base jobs
 * 1) Get all files with jobid in list (F subquery)
 * 2) Take only the last version of each file (Temp subquery) => accurate list is ok
 * 3) Put the result in a temporary table for the end of job
 *
 */
bool BDB::bdb_create_base_file_list(JCR *jcr, char *jobids)
{
   POOL_MEM buf;
   bool ret = false;

   bdb_lock();

   if (!*jobids) {
      Mmsg(errmsg, _("ERR=JobIds are empty\n"));
      goto bail_out; 
   }

   Mmsg(cmd, create_temp_basefile[bdb_get_type_index()], (uint64_t) jcr->JobId);
   if (!bdb_sql_query(cmd, NULL, NULL)) {
      goto bail_out; 
   }
   Mmsg(buf, select_recent_version[bdb_get_type_index()], jobids, jobids);
   Mmsg(cmd, create_temp_new_basefile[bdb_get_type_index()], (uint64_t)jcr->JobId, buf.c_str());

   ret = bdb_sql_query(cmd, NULL, NULL);
bail_out: 
   bdb_unlock();
   return ret;
}

/**
 * Create Restore Object record in BDB
 *
 */
bool BDB::bdb_create_restore_object_record(JCR *jcr, ROBJECT_DBR *ro)
{
   bool stat;
   int plug_name_len;
   POOLMEM *esc_plug_name = get_pool_memory(PM_MESSAGE);

   bdb_lock();

   Dmsg1(dbglevel, "Oname=%s\n", ro->object_name);
   Dmsg0(dbglevel, "put_object_into_catalog\n");

   fnl = strlen(ro->object_name);
   esc_name = check_pool_memory_size(esc_name, fnl*2+1);
   bdb_escape_string(jcr, esc_name, ro->object_name, fnl);

   bdb_escape_object(jcr, ro->object, ro->object_len);

   plug_name_len = strlen(ro->plugin_name);
   esc_plug_name = check_pool_memory_size(esc_plug_name, plug_name_len*2+1);
   bdb_escape_string(jcr, esc_plug_name, ro->plugin_name, plug_name_len);

   Mmsg(cmd,
        "INSERT INTO RestoreObject (ObjectName,PluginName,RestoreObject,"
        "ObjectLength,ObjectFullLength,ObjectIndex,ObjectType,"
        "ObjectCompression,FileIndex,JobId) "
        "VALUES ('%s','%s','%s',%d,%d,%d,%d,%d,%d,%u)",
        esc_name, esc_plug_name, esc_obj,
        ro->object_len, ro->object_full_len, ro->object_index,
        ro->FileType, ro->object_compression, ro->FileIndex, ro->JobId);

   ro->RestoreObjectId = sql_insert_autokey_record(cmd, NT_("RestoreObject"));
   if (ro->RestoreObjectId == 0) {
      Mmsg2(&errmsg, _("Create db Object record %s failed. ERR=%s"),
         cmd, sql_strerror());
      Jmsg(jcr, M_FATAL, 0, "%s", errmsg);
      stat = false;
   } else {
      stat = true;
   }
   bdb_unlock();
   free_pool_memory(esc_plug_name);
   return stat;
}

bool BDB::bdb_create_snapshot_record(JCR *jcr, SNAPSHOT_DBR *snap)
{
   bool status = false;
   char esc_name[MAX_ESCAPE_NAME_LENGTH];
   POOLMEM *esc_vol = get_pool_memory(PM_MESSAGE);
   POOLMEM *esc_dev = get_pool_memory(PM_MESSAGE);
   POOLMEM *esc_type = get_pool_memory(PM_MESSAGE);
   POOLMEM *esc_client = get_pool_memory(PM_MESSAGE);
   POOLMEM *esc_fs = get_pool_memory(PM_MESSAGE);
   char esc_comment[MAX_ESCAPE_NAME_LENGTH];
   char dt[MAX_TIME_LENGTH], ed1[50], ed2[50];
   time_t stime;
   struct tm tm;

   bdb_lock();

   esc_vol = check_pool_memory_size(esc_vol, strlen(snap->Volume) * 2 + 1);
   bdb_escape_string(jcr, esc_vol, snap->Volume, strlen(snap->Volume));

   esc_dev = check_pool_memory_size(esc_dev, strlen(snap->Device) * 2 + 1);
   bdb_escape_string(jcr, esc_dev, snap->Device, strlen(snap->Device));

   esc_type = check_pool_memory_size(esc_type, strlen(snap->Type) * 2 + 1);
   bdb_escape_string(jcr, esc_type, snap->Type, strlen(snap->Type));

   bdb_escape_string(jcr, esc_comment, snap->Comment, strlen(snap->Comment));

   if (*snap->Client) {
      bdb_escape_string(jcr, esc_name, snap->Client, strlen(snap->Client));
      Mmsg(esc_client, "(SELECT ClientId FROM Client WHERE Name='%s')", esc_name);

   } else {
      Mmsg(esc_client, "%d", snap->ClientId);
   }

   if (*snap->FileSet) {
      bdb_escape_string(jcr, esc_name, snap->FileSet, strlen(snap->FileSet));
      Mmsg(esc_fs, "(SELECT FileSetId FROM FileSet WHERE FileSet='%s' ORDER BY CreateTime DESC LIMIT 1)", esc_name);

   } else {
      Mmsg(esc_fs, "%d", snap->FileSetId);
   }

   bdb_escape_string(jcr, esc_name, snap->Name, strlen(snap->Name));

   stime = snap->CreateTDate;
   (void)localtime_r(&stime, &tm);
   strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M:%S", &tm);

   Mmsg(cmd, "INSERT INTO Snapshot "
        "(Name, JobId, CreateTDate, CreateDate, ClientId, FileSetId, Volume, Device, Type, Retention, Comment) "
 "VALUES ('%s', %s, %d, '%s', %s, %s, '%s', '%s', '%s', %s, '%s')",
        esc_name, edit_uint64(snap->JobId, ed2), stime, dt, esc_client, esc_fs, esc_vol,
        esc_dev, esc_type, edit_int64(snap->Retention, ed1), esc_comment);

   if (bdb_sql_query(cmd, NULL, NULL)) {
      snap->SnapshotId = sql_insert_autokey_record(cmd, NT_("Snapshot"));
      status = true;
   }

   bdb_unlock();

   free_pool_memory(esc_vol);
   free_pool_memory(esc_dev);
   free_pool_memory(esc_type);
   free_pool_memory(esc_client);
   free_pool_memory(esc_fs);

   return status;
}

#endif /* HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL */
