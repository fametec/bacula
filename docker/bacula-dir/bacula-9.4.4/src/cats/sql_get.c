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
/**
 * Bacula Catalog Database Get record interface routines
 *  Note, these routines generally get a record by id or
 *        by name.  If more logic is involved, the routine
 *        should be in find.c
 *
 *    Written by Kern Sibbald, March 2000
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

/*
 * Given a full filename (with path), look up the File record
 * (with attributes) in the database.
 *
 *  Returns: false on failure
 *           true on success with the File record in FILE_DBR
 */
bool BDB::bdb_get_file_attributes_record(JCR *jcr, char *afname, JOB_DBR *jr, FILE_DBR *fdbr)
{
   bool ok;

   Dmsg1(500, "db_get_file_att_record fname=%s \n", afname);

   bdb_lock();

   split_path_and_file(jcr, this, afname);

   fdbr->FilenameId = bdb_get_filename_record(jcr);

   fdbr->PathId = bdb_get_path_record(jcr);

   ok = bdb_get_file_record(jcr, jr, fdbr);

   bdb_unlock();

   return ok;
}


/*
 * Get a File record
 *
 *  DO NOT use Jmsg in this routine.
 *
 *  Note in this routine, we do not use Jmsg because it may be
 *    called to get attributes of a non-existent file, which is
 *    "normal" if a new file is found during Verify.
 *
 *  The following is a bit of a kludge: because we always backup a
 *    directory entry, we can end up with two copies of the directory
 *    in the backup. One is when we encounter the directory and find
 *    we cannot recurse into it, and the other is when we find an
 *    explicit mention of the directory. This can also happen if the
 *    use includes the directory twice.  In this case, Verify
 *    VolumeToCatalog fails because we have two copies in the catalog, and
 *    only the first one is marked (twice).  So, when calling from Verify,
 *    VolumeToCatalog jr is not NULL, and we know jr->FileIndex is the fileindex
 *    of the version of the directory/file we actually want and do
 *    a more explicit SQL search.
 *
 * Returns: false on failure
 *          true  on success
 *
 */
bool BDB::bdb_get_file_record(JCR *jcr, JOB_DBR *jr, FILE_DBR *fdbr)
{
   SQL_ROW row;
   bool ok = false;
   char ed1[50], ed2[50], ed3[50];

   switch (jcr->getJobLevel()) {
   case L_VERIFY_VOLUME_TO_CATALOG:
      Mmsg(cmd,
"SELECT FileId, LStat, MD5 FROM File WHERE File.JobId=%s AND File.PathId=%s AND "
"File.FilenameId=%s AND File.FileIndex=%d",
      edit_int64(fdbr->JobId, ed1),
      edit_int64(fdbr->PathId, ed2),
      edit_int64(fdbr->FilenameId,ed3),
      jr->FileIndex);
      break;
   case L_VERIFY_DISK_TO_CATALOG:
      Mmsg(cmd,
"SELECT FileId, LStat, MD5 FROM File,Job WHERE "
"File.JobId=Job.JobId AND File.PathId=%s AND "
"File.FilenameId=%s AND Job.Type='B' AND Job.JobStatus IN ('T','W') AND "
"ClientId=%s ORDER BY StartTime DESC LIMIT 1",
      edit_int64(fdbr->PathId, ed1), 
      edit_int64(fdbr->FilenameId, ed2), 
      edit_int64(jr->ClientId,ed3));
      break;
   default:
      Mmsg(cmd,
"SELECT FileId, LStat, MD5 FROM File WHERE File.JobId=%s AND File.PathId=%s AND "
"File.FilenameId=%s",
      edit_int64(fdbr->JobId, ed1),
      edit_int64(fdbr->PathId, ed2),
      edit_int64(fdbr->FilenameId,ed3));
      break;
   }

   Dmsg3(450, "Get_file_record JobId=%u FilenameId=%u PathId=%u\n",
      fdbr->JobId, fdbr->FilenameId, fdbr->PathId);

   Dmsg1(100, "Query=%s\n", cmd);

   if (QueryDB(jcr, cmd)) {
      Dmsg1(100, "get_file_record sql_num_rows()=%d\n", sql_num_rows());
      if (sql_num_rows() >= 1) {
         if ((row = sql_fetch_row()) == NULL) {
            Mmsg1(errmsg, _("Error fetching row: %s\n"), sql_strerror());
         } else {
            fdbr->FileId = (FileId_t)str_to_int64(row[0]);
            bstrncpy(fdbr->LStat, row[1], sizeof(fdbr->LStat));
            bstrncpy(fdbr->Digest, row[2], sizeof(fdbr->Digest));
            ok = true;
            if (sql_num_rows() > 1) {
               Mmsg3(errmsg, _("get_file_record want 1 got rows=%d PathId=%s FilenameId=%s\n"),
                  sql_num_rows(), 
                  edit_int64(fdbr->PathId, ed1), 
                  edit_int64(fdbr->FilenameId, ed2));
               Dmsg1(000, "=== Problem!  %s", errmsg);
            }
         }
      } else {
         Mmsg2(errmsg, _("File record for PathId=%s FilenameId=%s not found.\n"),
            edit_int64(fdbr->PathId, ed1), 
            edit_int64(fdbr->FilenameId, ed2));
      }
      sql_free_result();
   } else {
      Mmsg(errmsg, _("File record not found in Catalog.\n"));
   }
   return ok;
}

/*
 * Get Filename record
 * Returns: 0 on failure
 *          FilenameId on success
 *
 *   DO NOT use Jmsg in this routine (see notes for get_file_record)
 */
int BDB::bdb_get_filename_record(JCR *jcr)
{
   SQL_ROW row;
   int FilenameId = 0;

   esc_name = check_pool_memory_size(esc_name, 2*fnl+2);
   bdb_escape_string(jcr, esc_name, fname, fnl);

   Mmsg(cmd, "SELECT FilenameId FROM Filename WHERE Name='%s'", esc_name);
   if (QueryDB(jcr, cmd)) {
      char ed1[30];
      if (sql_num_rows() > 1) { 
         Mmsg2(errmsg, _("More than one Filename!: %s for file: %s\n"),
            edit_uint64(sql_num_rows(), ed1), fname);
         Jmsg(jcr, M_WARNING, 0, "%s", errmsg);
      }
      if (sql_num_rows() >= 1) { 
         if ((row = sql_fetch_row()) == NULL) {
            Mmsg1(errmsg, _("error fetching row: %s\n"), sql_strerror());
         } else {
            FilenameId = str_to_int64(row[0]);
            if (FilenameId <= 0) {
               Mmsg2(errmsg, _("Get DB Filename record %s found bad record: %d\n"),
                  cmd, FilenameId);
               FilenameId = 0;
            }
         }
      } else {
         Mmsg1(errmsg, _("Filename record: %s not found.\n"), fname);
      }
      sql_free_result();
   } else {
      Mmsg(errmsg, _("Filename record: %s not found in Catalog.\n"), fname);
   }
   return FilenameId;
}

/**
 * Get path record
 * Returns: 0 on failure
 *          PathId on success
 *
 *   DO NOT use Jmsg in this routine (see notes for get_file_record)
 */
int BDB::bdb_get_path_record(JCR *jcr)
{
   SQL_ROW row;
   uint32_t PathId = 0;

   esc_name = check_pool_memory_size(esc_name, 2*pnl+2);
   bdb_escape_string(jcr, esc_name, path, pnl);

   if (cached_path_id != 0 && cached_path_len == pnl &&
       strcmp(cached_path, path) == 0) {
      return cached_path_id;
   }

   Mmsg(cmd, "SELECT PathId FROM Path WHERE Path='%s'", esc_name);

   if (QueryDB(jcr, cmd)) {
      char ed1[30];
      if (sql_num_rows() > 1) {
         Mmsg2(errmsg, _("More than one Path!: %s for path: %s\n"),
            edit_uint64(sql_num_rows(), ed1), path);
         Jmsg(jcr, M_WARNING, 0, "%s", errmsg);
      }
      /* Even if there are multiple paths, take the first one */
      if (sql_num_rows() >= 1) {
         if ((row = sql_fetch_row()) == NULL) {
            Mmsg1(errmsg, _("error fetching row: %s\n"), sql_strerror());
         } else {
            PathId = str_to_int64(row[0]);
            if (PathId <= 0) {
               Mmsg2(errmsg, _("Get DB path record %s found bad record: %s\n"),
                  cmd, edit_int64(PathId, ed1));
               PathId = 0;
            } else {
               /* Cache path */
               if (PathId != cached_path_id) {
                  cached_path_id = PathId;
                  cached_path_len = pnl;
                  pm_strcpy(cached_path, path);
               }
            }
         }
      } else {
         Mmsg1(errmsg, _("Path record: %s not found.\n"), path);
      }
      sql_free_result();
   } else {
      Mmsg(errmsg, _("Path record: %s not found in Catalog.\n"), path);
   }
   return PathId;
}


/**
 * Get Job record for given JobId or Job name
 * Returns: false on failure
 *          true  on success
 */
bool BDB::bdb_get_job_record(JCR *jcr, JOB_DBR *jr)
{
   SQL_ROW row;
   char ed1[50];
   char esc[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   if (jr->JobId == 0) {
      bdb_escape_string(jcr, esc, jr->Job, strlen(jr->Job));
      Mmsg(cmd, "SELECT VolSessionId,VolSessionTime,"
"PoolId,StartTime,EndTime,JobFiles,JobBytes,JobTDate,Job,JobStatus,"
"Type,Level,ClientId,Name,PriorJobId,RealEndTime,JobId,FileSetId,"
"SchedTime,RealEndTime,ReadBytes,HasBase,PurgedFiles "
"FROM Job WHERE Job='%s'", esc);
    } else {
      Mmsg(cmd, "SELECT VolSessionId,VolSessionTime,"
"PoolId,StartTime,EndTime,JobFiles,JobBytes,JobTDate,Job,JobStatus,"
"Type,Level,ClientId,Name,PriorJobId,RealEndTime,JobId,FileSetId,"
"SchedTime,RealEndTime,ReadBytes,HasBase,PurgedFiles "
"FROM Job WHERE JobId=%s",
          edit_int64(jr->JobId, ed1));
    }

   if (!QueryDB(jcr, cmd)) {
      bdb_unlock();
      return false;                   /* failed */
   }
   if ((row = sql_fetch_row()) == NULL) {
      Mmsg1(errmsg, _("No Job found for JobId %s\n"), edit_int64(jr->JobId, ed1));
      sql_free_result();
      bdb_unlock();
      return false;                   /* failed */
   }

   jr->VolSessionId = str_to_uint64(row[0]);
   jr->VolSessionTime = str_to_uint64(row[1]);
   jr->PoolId = str_to_int64(row[2]);
   bstrncpy(jr->cStartTime, row[3]!=NULL?row[3]:"", sizeof(jr->cStartTime));
   bstrncpy(jr->cEndTime, row[4]!=NULL?row[4]:"", sizeof(jr->cEndTime));
   jr->JobFiles = str_to_int64(row[5]);
   jr->JobBytes = str_to_int64(row[6]);
   jr->JobTDate = str_to_int64(row[7]);
   bstrncpy(jr->Job, row[8]!=NULL?row[8]:"", sizeof(jr->Job));
   jr->JobStatus = row[9]!=NULL?(int)*row[9]:JS_FatalError;
   jr->JobType = row[10]!=NULL?(int)*row[10]:JT_BACKUP;
   jr->JobLevel = row[11]!=NULL?(int)*row[11]:L_NONE;
   jr->ClientId = str_to_uint64(row[12]!=NULL?row[12]:(char *)"");
   bstrncpy(jr->Name, row[13]!=NULL?row[13]:"", sizeof(jr->Name));
   jr->PriorJobId = str_to_uint64(row[14]!=NULL?row[14]:(char *)"");
   bstrncpy(jr->cRealEndTime, row[15]!=NULL?row[15]:"", sizeof(jr->cRealEndTime));
   if (jr->JobId == 0) {
      jr->JobId = str_to_int64(row[16]);
   }
   jr->FileSetId = str_to_int64(row[17]);
   bstrncpy(jr->cSchedTime, row[18]!=NULL?row[18]:"", sizeof(jr->cSchedTime));
   bstrncpy(jr->cRealEndTime, row[19]!=NULL?row[19]:"", sizeof(jr->cRealEndTime));
   jr->ReadBytes = str_to_int64(row[20]);
   jr->StartTime = str_to_utime(jr->cStartTime);
   jr->SchedTime = str_to_utime(jr->cSchedTime);
   jr->EndTime = str_to_utime(jr->cEndTime);
   jr->RealEndTime = str_to_utime(jr->cRealEndTime);
   jr->HasBase = str_to_int64(row[21]);
   jr->PurgedFiles = str_to_int64(row[22]);
   sql_free_result();

   bdb_unlock();
   return true;
}

/**
 * Find VolumeNames for a given JobId
 *  Returns: 0 on error or no Volumes found
 *           number of volumes on success
 *              Volumes are concatenated in VolumeNames
 *              separated by a vertical bar (|) in the order
 *              that they were written.
 *
 *  Returns: number of volumes on success
 */
int BDB::bdb_get_job_volume_names(JCR *jcr, JobId_t JobId, POOLMEM **VolumeNames)
{
   SQL_ROW row;
   char ed1[50];
   int stat = 0;
   int i;

   bdb_lock();
   /* Get one entry per VolumeName, but "sort" by VolIndex */
   Mmsg(cmd,
        "SELECT VolumeName,MAX(VolIndex) FROM JobMedia,Media WHERE "
        "JobMedia.JobId=%s AND JobMedia.MediaId=Media.MediaId "
        "GROUP BY VolumeName "
        "ORDER BY 2 ASC", edit_int64(JobId,ed1));

   Dmsg1(130, "VolNam=%s\n", cmd);
   *VolumeNames[0] = 0;
   if (QueryDB(jcr, cmd)) {
      Dmsg1(130, "Num rows=%d\n", sql_num_rows());
      if (sql_num_rows() <= 0) {
         Mmsg1(errmsg, _("No volumes found for JobId=%d\n"), JobId);
         stat = 0;
      } else {
         stat = sql_num_rows();
         for (i=0; i < stat; i++) {
            if ((row = sql_fetch_row()) == NULL) {
               Mmsg2(errmsg, _("Error fetching row %d: ERR=%s\n"), i, sql_strerror());
               Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
               stat = 0;
               break;
            } else {
               if (*VolumeNames[0] != 0) {
                  pm_strcat(VolumeNames, "|");
               }
               pm_strcat(VolumeNames, row[0]);
            }
         }
      }
      sql_free_result();
   } else {
      Mmsg(errmsg, _("No Volume for JobId %d found in Catalog.\n"), JobId);
   }
   bdb_unlock();
   return stat;
}

/**
 * Find Volume parameters for a give JobId
 *  Returns: 0 on error or no Volumes found
 *           number of volumes on success
 *           List of Volumes and start/end file/blocks (malloced structure!)
 *
 *  Returns: number of volumes on success
 */
int BDB::bdb_get_job_volume_parameters(JCR *jcr, JobId_t JobId, VOL_PARAMS **VolParams)
{
   SQL_ROW row;
   char ed1[50];
   int stat = 0;
   int i;
   VOL_PARAMS *Vols = NULL;

   bdb_lock();
   Mmsg(cmd,
"SELECT VolumeName,MediaType,FirstIndex,LastIndex,StartFile,"
"JobMedia.EndFile,StartBlock,JobMedia.EndBlock,"
"Slot,StorageId,InChanger"
" FROM JobMedia,Media WHERE JobMedia.JobId=%s"
" AND JobMedia.MediaId=Media.MediaId ORDER BY VolIndex,JobMediaId",
        edit_int64(JobId, ed1));

   Dmsg1(130, "VolNam=%s\n", cmd);
   if (QueryDB(jcr, cmd)) {
      Dmsg1(200, "Num rows=%d\n", sql_num_rows());
      if (sql_num_rows() <= 0) {
         Mmsg1(errmsg, _("No volumes found for JobId=%d\n"), JobId);
         stat = 0;
      } else {
         stat = sql_num_rows();
         DBId_t *SId = NULL;
         if (stat > 0) {
            *VolParams = Vols = (VOL_PARAMS *)malloc(stat * sizeof(VOL_PARAMS));
            SId = (DBId_t *)malloc(stat * sizeof(DBId_t));
         }
         for (i=0; i < stat; i++) {
            if ((row = sql_fetch_row()) == NULL) {
               Mmsg2(errmsg, _("Error fetching row %d: ERR=%s\n"), i, sql_strerror());
               Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
               stat = 0;
               break;
            } else {
               DBId_t StorageId;
               uint32_t StartBlock, EndBlock, StartFile, EndFile;
               bstrncpy(Vols[i].VolumeName, row[0], MAX_NAME_LENGTH);
               bstrncpy(Vols[i].MediaType, row[1], MAX_NAME_LENGTH);
               Vols[i].FirstIndex = str_to_uint64(row[2]);
               Vols[i].LastIndex = str_to_uint64(row[3]);
               StartFile = str_to_uint64(row[4]);
               EndFile = str_to_uint64(row[5]);
               StartBlock = str_to_uint64(row[6]);
               EndBlock = str_to_uint64(row[7]);
               Vols[i].StartAddr = (((uint64_t)StartFile)<<32) | StartBlock;
               Vols[i].EndAddr =   (((uint64_t)EndFile)<<32) | EndBlock;
               Vols[i].Slot = str_to_uint64(row[8]);
               StorageId = str_to_uint64(row[9]);
               Vols[i].InChanger = str_to_uint64(row[10]);
               Vols[i].Storage[0] = 0;
               SId[i] = StorageId;
            }
         }
         for (i=0; i < stat; i++) {
            if (SId[i] != 0) {
               Mmsg(cmd, "SELECT Name from Storage WHERE StorageId=%s",
                  edit_int64(SId[i], ed1));
               if (QueryDB(jcr, cmd)) {
                  if ((row = sql_fetch_row()) && row[0]) {
                     bstrncpy(Vols[i].Storage, row[0], MAX_NAME_LENGTH);
                  }
               }
            }
         }
         if (SId) {
            free(SId);
         }
      }
      sql_free_result();
   }
   bdb_unlock();
   return stat;
}


/**
 * Get JobMedia record
 *  Returns: false on error or no JobMedia found
 *           JobMedia in argument, use JobMediaId to find it
 *
 *  Returns: true on success
 */
bool BDB::bdb_get_jobmedia_record(JCR *jcr, JOBMEDIA_DBR *jmr)
{
   SQL_ROW row;
   char ed1[50];

   bdb_lock();
   Mmsg(cmd,
        "SELECT FirstIndex,LastIndex,StartFile,"
        "EndFile,StartBlock,EndBlock,VolIndex, JobId, MediaId"
        " FROM JobMedia WHERE JobMedia.JobMediaId=%s",
        edit_int64(jmr->JobMediaId, ed1));

   if (QueryDB(jcr, cmd)) {
      Dmsg1(200, "Num rows=%d\n", sql_num_rows());
      if (sql_num_rows() != 1) {
         Mmsg1(errmsg, _("No JobMedia found for JobMediaId=%d\n"), jmr->JobMediaId);
         sql_free_result();
         bdb_unlock();
         return false;
      }

      if ((row = sql_fetch_row()) == NULL) {
         Mmsg1(errmsg, _("No JobMedia found for JobMediaId %d\n"),
               edit_int64(jmr->JobMediaId, ed1));
         sql_free_result();
         bdb_unlock();
         return false;                   /* failed */
      }

      jmr->FirstIndex = str_to_uint64(row[0]);
      jmr->LastIndex = str_to_uint64(row[1]);
      jmr->StartFile = str_to_int64(row[2]);
      jmr->EndFile = str_to_int64(row[3]);
      jmr->StartBlock = str_to_int64(row[4]);
      jmr->EndBlock = str_to_int64(row[5]);
      jmr->VolIndex = str_to_int64(row[6]);
      jmr->JobId = str_to_int64(row[7]);
      jmr->MediaId = str_to_int64(row[8]);
      sql_free_result();
      bdb_unlock();
      return true;
   }
   return false;
}



/**
 * Get the number of pool records
 *
 * Returns: -1 on failure
 *          number on success
 */
int BDB::bdb_get_num_pool_records(JCR *jcr)
{
   int stat = 0;

   bdb_lock();
   Mmsg(cmd, "SELECT count(*) from Pool");
   stat = get_sql_record_max(jcr, this);
   bdb_unlock();
   return stat;
}

/**
 * This function returns a list of all the Pool record ids.
 *  The caller must free ids if non-NULL.
 *
 *  Returns 0: on failure
 *          1: on success
 */
int BDB::bdb_get_pool_ids(JCR *jcr, int *num_ids, uint32_t *ids[])
{
   SQL_ROW row;
   int stat = 0;
   int i = 0;
   uint32_t *id;

   bdb_lock();
   *ids = NULL;
   Mmsg(cmd, "SELECT PoolId FROM Pool ORDER By Name");
   if (QueryDB(jcr, cmd)) {
      *num_ids = sql_num_rows();
      if (*num_ids > 0) {
         id = (uint32_t *)malloc(*num_ids * sizeof(uint32_t));
         while ((row = sql_fetch_row()) != NULL) {
            id[i++] = str_to_uint64(row[0]);
         }
         *ids = id;
      }
      sql_free_result();
      stat = 1;
   } else {
      Mmsg(errmsg, _("Pool id select failed: ERR=%s\n"), sql_strerror());
      Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      stat = 0;
   }
   bdb_unlock();
   return stat;
}

/**
 * This function returns a list of all the Client record ids.
 *  The caller must free ids if non-NULL.
 *
 *  Returns 0: on failure
 *          1: on success
 */
int BDB::bdb_get_client_ids(JCR *jcr, int *num_ids, uint32_t *ids[])
{
   SQL_ROW row;
   int stat = 0;
   int i = 0;
   uint32_t *id;

   bdb_lock();
   *ids = NULL;
   Mmsg(cmd, "SELECT ClientId FROM Client ORDER BY Name ASC");
   if (QueryDB(jcr, cmd)) {
      *num_ids = sql_num_rows();
      if (*num_ids > 0) {
         id = (uint32_t *)malloc(*num_ids * sizeof(uint32_t));
         while ((row = sql_fetch_row()) != NULL) {
            id[i++] = str_to_uint64(row[0]);
         }
         *ids = id;
      }
      sql_free_result();
      stat = 1;
   } else {
      Mmsg(errmsg, _("Client id select failed: ERR=%s\n"), sql_strerror());
      Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      stat = 0;
   }
   bdb_unlock();
   return stat;
}

/**
 * Get Pool Id, Scratch Pool Id, Recycle Pool Id
 * Returns: false on failure
 *          true on success
 */
bool BDB::bdb_get_pool_record(JCR *jcr, POOL_DBR *pdbr)
{
   SQL_ROW row;
   bool ok = false;
   char ed1[50];
   char esc[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   if (pdbr->PoolId != 0) {               /* find by id */
      Mmsg(cmd,
"SELECT PoolId,Name,NumVols,MaxVols,UseOnce,UseCatalog,AcceptAnyVolume,"
"AutoPrune,Recycle,VolRetention,VolUseDuration,MaxVolJobs,MaxVolFiles,"
"MaxVolBytes,PoolType,LabelType,LabelFormat,RecyclePoolId,ScratchPoolId,"
"ActionOnPurge,CacheRetention FROM Pool WHERE Pool.PoolId=%s",
         edit_int64(pdbr->PoolId, ed1));
   } else {                           /* find by name */
      bdb_escape_string(jcr, esc, pdbr->Name, strlen(pdbr->Name));
      Mmsg(cmd,
"SELECT PoolId,Name,NumVols,MaxVols,UseOnce,UseCatalog,AcceptAnyVolume,"
"AutoPrune,Recycle,VolRetention,VolUseDuration,MaxVolJobs,MaxVolFiles,"
"MaxVolBytes,PoolType,LabelType,LabelFormat,RecyclePoolId,ScratchPoolId,"
"ActionOnPurge,CacheRetention FROM Pool WHERE Pool.Name='%s'", esc);
   }
   if (QueryDB(jcr, cmd)) {
      if (sql_num_rows() > 1) {
         char ed1[30];
         Mmsg1(errmsg, _("More than one Pool! Num=%s\n"),
            edit_uint64(sql_num_rows(), ed1));
         Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      } else if (sql_num_rows() == 1) {
         if ((row = sql_fetch_row()) == NULL) {
            Mmsg1(errmsg, _("error fetching row: %s\n"), sql_strerror());
            Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
         } else {
            pdbr->PoolId = str_to_int64(row[0]);
            bstrncpy(pdbr->Name, row[1]!=NULL?row[1]:"", sizeof(pdbr->Name));
            pdbr->NumVols = str_to_int64(row[2]);
            pdbr->MaxVols = str_to_int64(row[3]);
            pdbr->UseOnce = str_to_int64(row[4]);
            pdbr->UseCatalog = str_to_int64(row[5]);
            pdbr->AcceptAnyVolume = str_to_int64(row[6]);
            pdbr->AutoPrune = str_to_int64(row[7]);
            pdbr->Recycle = str_to_int64(row[8]);
            pdbr->VolRetention = str_to_int64(row[9]);
            pdbr->VolUseDuration = str_to_int64(row[10]);
            pdbr->MaxVolJobs = str_to_int64(row[11]);
            pdbr->MaxVolFiles = str_to_int64(row[12]);
            pdbr->MaxVolBytes = str_to_uint64(row[13]);
            bstrncpy(pdbr->PoolType, row[14]!=NULL?row[14]:"", sizeof(pdbr->PoolType));
            pdbr->LabelType = str_to_int64(row[15]);
            bstrncpy(pdbr->LabelFormat, row[16]!=NULL?row[16]:"", sizeof(pdbr->LabelFormat));
            pdbr->RecyclePoolId = str_to_int64(row[17]);
            pdbr->ScratchPoolId = str_to_int64(row[18]);
            pdbr->ActionOnPurge = str_to_int32(row[19]);
            pdbr->CacheRetention = str_to_int64(row[20]);
            ok = true;
         }
      }
      sql_free_result();
   }
   bdb_unlock();
   return ok;
}
/**
 * Get Pool numvols
 * If the PoolId is non-zero, we get its record,
 *  otherwise, we search on the PoolName and we compute the number of volumes
 *
 * Returns: false on failure
 *          true on success
 */
bool BDB::bdb_get_pool_numvols(JCR *jcr, POOL_DBR *pdbr)
{
   bool ok;
   char ed1[50];

   ok = db_get_pool_record(jcr, this, pdbr);

   bdb_lock();
   if (ok) {
      uint32_t NumVols;
      Mmsg(cmd, "SELECT count(*) from Media WHERE PoolId=%s",
         edit_int64(pdbr->PoolId, ed1));
      NumVols = get_sql_record_max(jcr, this);
      Dmsg2(400, "Actual NumVols=%d Pool NumVols=%d\n", NumVols, pdbr->NumVols);
      if (NumVols != pdbr->NumVols) {
         pdbr->NumVols = NumVols;
         db_update_pool_record(jcr, this, pdbr);
      }
   } else {
      Mmsg(errmsg, _("Pool record not found in Catalog.\n"));
   }
   bdb_unlock();
   return ok;
}

/*
 * Free restoreobject record (some fields are allocated through malloc)
 */
void db_free_restoreobject_record(JCR *jcr, ROBJECT_DBR *rr)
{
   if (rr->object) {
      free(rr->object);
   }
   if (rr->object_name) {
      free(rr->object_name);
   }
   if (rr->plugin_name) {
      free(rr->plugin_name);
   }
   rr->object = rr->plugin_name = rr->object_name = NULL;
}

/*
 * Get RestoreObject  Record
 * If the RestoreObjectId is non-zero, we get its record
 *
 * You must call db_free_restoreobject_record() after db_get_restoreobject_record()
 *
 * Returns: false on failure
 *          true  on success
 */
bool BDB::bdb_get_restoreobject_record(JCR *jcr, ROBJECT_DBR *rr)
{
   SQL_ROW row;
   int stat = false;
   char ed1[50];
   int32_t len;

   bdb_lock();
   Mmsg(cmd,
        "SELECT ObjectName, PluginName, ObjectType, JobId, ObjectCompression, "
               "RestoreObject, ObjectLength, ObjectFullLength, FileIndex "
          "FROM RestoreObject "
         "WHERE RestoreObjectId=%s",
           edit_int64(rr->RestoreObjectId, ed1));

   /* Using the JobId permits to check the Job name against ACLs and
    * make sure that the current user is authorized to see the Restore object
    */
   if (rr->JobId) {
      pm_strcat(cmd, " AND JobId=");
      pm_strcat(cmd, edit_int64(rr->JobId, ed1));

   } else if (rr->JobIds && is_a_number_list(rr->JobIds)) {
      pm_strcat(cmd, " AND JobId IN (");
      pm_strcat(cmd, rr->JobIds);
      pm_strcat(cmd, ")");
   }

   if (QueryDB(jcr, cmd)) {
      if (sql_num_rows() > 1) {
         char ed1[30];
         Mmsg1(errmsg, _("Error got %s RestoreObjects but expected only one!\n"),
            edit_uint64(sql_num_rows(), ed1));
         sql_data_seek(sql_num_rows()-1);
      }
      if ((row = sql_fetch_row()) == NULL) {
         Mmsg1(errmsg, _("RestoreObject record \"%d\" not found.\n"), rr->RestoreObjectId);
      } else {
         db_free_restoreobject_record(jcr, rr);
         rr->object_name = bstrdup(row[0]);
         rr->plugin_name = bstrdup(row[1]);
         rr->FileType = str_to_uint64(row[2]);
         rr->JobId = str_to_uint64(row[3]);
         rr->object_compression = str_to_int64(row[4]);
         rr->object_len  = str_to_uint64(row[6]);
         rr->object_full_len = str_to_uint64(row[7]);
         rr->object_index = str_to_uint64(row[8]);

         bdb_unescape_object(jcr,
                            row[5],                /* Object  */
                            rr->object_len,        /* Object length */
                            &cmd, &len);

         if (rr->object_compression > 0) {
            int out_len = rr->object_full_len + 100; /* full length */
            char *obj = (char *)malloc(out_len);
            Zinflate(cmd, rr->object_len, obj, out_len); /* out_len is updated */
            if (out_len != (int)rr->object_full_len) {
               Dmsg3(10, "Decompression failed. Len wanted=%d got=%d. Object=%s\n",
                     rr->object_full_len, out_len, rr->plugin_name);

               Mmsg(errmsg, _("Decompression failed. Len wanted=%d got=%d. Object=%s\n"),
                    rr->object_full_len, out_len, rr->plugin_name);
            }
            obj[out_len] = 0;
            rr->object = obj;
            rr->object_len = out_len;

         } else {
            rr->object = (char *)malloc(sizeof(char)*(len+1));
            memcpy(rr->object, cmd, len);
            rr->object[len] = 0;
            rr->object_len = len;
         }

         stat = true;
      }
      sql_free_result();
   } else {
      Mmsg(errmsg, _("RestoreObject record not found in Catalog.\n"));
   }
   bdb_unlock();
   return stat;
}

/**
 * Get Client Record
 * If the ClientId is non-zero, we get its record,
 *  otherwise, we search on the Client Name
 *
 * Returns: 0 on failure
 *          1 on success
 */
int BDB::bdb_get_client_record(JCR *jcr, CLIENT_DBR *cdbr)
{
   SQL_ROW row;
   int stat = 0;
   char ed1[50];
   char esc[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   if (cdbr->ClientId != 0) {               /* find by id */
      Mmsg(cmd,
"SELECT ClientId,Name,Uname,AutoPrune,FileRetention,JobRetention "
"FROM Client WHERE Client.ClientId=%s",
        edit_int64(cdbr->ClientId, ed1));
   } else {                           /* find by name */
      bdb_escape_string(jcr, esc, cdbr->Name, strlen(cdbr->Name));
      Mmsg(cmd,
"SELECT ClientId,Name,Uname,AutoPrune,FileRetention,JobRetention "
"FROM Client WHERE Client.Name='%s'", esc);
   }

   if (QueryDB(jcr, cmd)) {
      if (sql_num_rows() > 1) {
         Mmsg1(errmsg, _("More than one Client!: %s\n"),
            edit_uint64(sql_num_rows(), ed1));
         Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      } else if (sql_num_rows() == 1) {
         if ((row = sql_fetch_row()) == NULL) {
            Mmsg1(errmsg, _("error fetching row: %s\n"), sql_strerror());
            Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
         } else {
            cdbr->ClientId = str_to_int64(row[0]);
            bstrncpy(cdbr->Name, row[1]!=NULL?row[1]:"", sizeof(cdbr->Name));
            bstrncpy(cdbr->Uname, row[2]!=NULL?row[2]:"", sizeof(cdbr->Uname));
            cdbr->AutoPrune = str_to_int64(row[3]);
            cdbr->FileRetention = str_to_int64(row[4]);
            cdbr->JobRetention = str_to_int64(row[5]);
            stat = 1;
         }
      } else {
         Mmsg(errmsg, _("Client record not found in Catalog.\n"));
      }
      sql_free_result();
   } else {
      Mmsg(errmsg, _("Client record not found in Catalog.\n"));
   }
   bdb_unlock();
   return stat;
}

/**
 * Get Counter Record
 *
 * Returns: 0 on failure
 *          1 on success
 */
bool BDB::bdb_get_counter_record(JCR *jcr, COUNTER_DBR *cr)
{
   SQL_ROW row;
   char esc[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   bdb_escape_string(jcr, esc, cr->Counter, strlen(cr->Counter));

   Mmsg(cmd, select_counter_values[bdb_get_type_index()], esc);
   if (QueryDB(jcr, cmd)) {

      /* If more than one, report error, but return first row */
      if (sql_num_rows() > 1) {
         Mmsg1(errmsg, _("More than one Counter!: %d\n"), sql_num_rows());
         Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      }
      if (sql_num_rows() >= 1) {
         if ((row = sql_fetch_row()) == NULL) {
            Mmsg1(errmsg, _("error fetching Counter row: %s\n"), sql_strerror());
            Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
            sql_free_result();
            bdb_unlock();
            return false;
         }
         cr->MinValue = str_to_int64(row[0]);
         cr->MaxValue = str_to_int64(row[1]);
         cr->CurrentValue = str_to_int64(row[2]);
         if (row[3]) {
            bstrncpy(cr->WrapCounter, row[3], sizeof(cr->WrapCounter));
         } else {
            cr->WrapCounter[0] = 0;
         }
         sql_free_result();
         bdb_unlock();
         return true;
      }
      sql_free_result();
   } else {
      Mmsg(errmsg, _("Counter record: %s not found in Catalog.\n"), cr->Counter);
   }
   bdb_unlock();
   return false;
}


/**
 * Get FileSet Record
 * If the FileSetId is non-zero, we get its record,
 *  otherwise, we search on the name
 *
 * Returns: 0 on failure
 *          id on success
 */
int BDB::bdb_get_fileset_record(JCR *jcr, FILESET_DBR *fsr)
{
   SQL_ROW row;
   int stat = 0;
   char ed1[50];
   char esc[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   if (fsr->FileSetId != 0) {               /* find by id */
      Mmsg(cmd,
           "SELECT FileSetId,FileSet,MD5,CreateTime FROM FileSet "
           "WHERE FileSetId=%s",
           edit_int64(fsr->FileSetId, ed1));
   } else {                           /* find by name */
      bdb_escape_string(jcr, esc, fsr->FileSet, strlen(fsr->FileSet));
      Mmsg(cmd,
           "SELECT FileSetId,FileSet,MD5,CreateTime FROM FileSet "
           "WHERE FileSet='%s' ORDER BY CreateTime DESC LIMIT 1", esc);
   }

   if (QueryDB(jcr, cmd)) {
      if (sql_num_rows() > 1) {
         char ed1[30];
         Mmsg1(errmsg, _("Error got %s FileSets but expected only one!\n"),
            edit_uint64(sql_num_rows(), ed1));
         sql_data_seek(sql_num_rows()-1);
      }
      if ((row = sql_fetch_row()) == NULL) {
         Mmsg1(errmsg, _("FileSet record \"%s\" not found.\n"), fsr->FileSet);
      } else {
         fsr->FileSetId = str_to_int64(row[0]);
         bstrncpy(fsr->FileSet, row[1]!=NULL?row[1]:"", sizeof(fsr->FileSet));
         bstrncpy(fsr->MD5, row[2]!=NULL?row[2]:"", sizeof(fsr->MD5));
         bstrncpy(fsr->cCreateTime, row[3]!=NULL?row[3]:"", sizeof(fsr->cCreateTime));
         stat = fsr->FileSetId;
      }
      sql_free_result();
   } else {
      Mmsg(errmsg, _("FileSet record not found in Catalog.\n"));
   }
   bdb_unlock();
   return stat;
}


/**
 * Get the number of Media records
 *
 * Returns: -1 on failure
 *          number on success
 */
int BDB::bdb_get_num_media_records(JCR *jcr)
{
   int stat = 0;

   bdb_lock();
   Mmsg(cmd, "SELECT count(*) from Media");
   stat = get_sql_record_max(jcr, this);
   bdb_unlock();
   return stat;
}

/**
 * This function returns a list of all the Media record ids for
 *     the current Pool, the correct Media Type, Recyle, Enabled, StorageId, VolBytes
 *     VolumeName if specified
 *  The caller must free ids if non-NULL.
 *
 *  Returns false: on failure
 *          true:  on success
 */
bool BDB::bdb_get_media_ids(JCR *jcr, MEDIA_DBR *mr, int *num_ids, uint32_t *ids[])
{
   SQL_ROW row;
   int i = 0;
   uint32_t *id;
   char ed1[50];
   bool ok = false;
   char buf[MAX_NAME_LENGTH*3]; /* Can contain MAX_NAME_LENGTH*2+1 + AND ....='' */
   char esc[MAX_NAME_LENGTH*2+1];

   bdb_lock();
   *ids = NULL;

   Mmsg(cmd, "SELECT DISTINCT MediaId FROM Media WHERE Enabled=%d ",
        mr->Enabled);

   if (mr->Recycle >= 0) {
      bsnprintf(buf, sizeof(buf), "AND Recycle=%d ", mr->Recycle);
      pm_strcat(cmd, buf);
   }

   if (*mr->MediaType) {
      bdb_escape_string(jcr, esc, mr->MediaType, strlen(mr->MediaType));
      bsnprintf(buf, sizeof(buf), "AND MediaType='%s' ", esc);
      pm_strcat(cmd, buf);
   }

   if (mr->sid_group) {
      bsnprintf(buf, sizeof(buf), "AND StorageId IN (%s) ", mr->sid_group);
      pm_strcat(cmd, buf);
   } else if (mr->StorageId) {
      bsnprintf(buf, sizeof(buf), "AND StorageId=%s ", edit_uint64(mr->StorageId, ed1));
      pm_strcat(cmd, buf);
   }

   if (mr->PoolId) {
      bsnprintf(buf, sizeof(buf), "AND PoolId=%s ", edit_uint64(mr->PoolId, ed1));
      pm_strcat(cmd, buf);
   }

   if (mr->VolBytes) {
      bsnprintf(buf, sizeof(buf), "AND VolBytes > %s ", edit_uint64(mr->VolBytes, ed1));
      pm_strcat(cmd, buf);
   }

   if (*mr->VolumeName) {
      bdb_escape_string(jcr, esc, mr->VolumeName, strlen(mr->VolumeName));
      bsnprintf(buf, sizeof(buf), "AND VolumeName = '%s' ", esc);
      pm_strcat(cmd, buf);
   }

   if (*mr->VolStatus) {
      bdb_escape_string(jcr, esc, mr->VolStatus, strlen(mr->VolStatus));
      bsnprintf(buf, sizeof(buf), "AND VolStatus = '%s' ", esc);
      pm_strcat(cmd, buf);
   }

   /* Filter the volumes with the CacheRetention */
   if (mr->CacheRetention) {
      bsnprintf(buf, sizeof(buf), "AND %s ", prune_cache[bdb_get_type_index()]);
      pm_strcat(cmd, buf);
   }

   Dmsg1(100, "q=%s\n", cmd);

   if (QueryDB(jcr, cmd)) {
      *num_ids = sql_num_rows();
      if (*num_ids > 0) {
         id = (uint32_t *)malloc(*num_ids * sizeof(uint32_t));
         while ((row = sql_fetch_row()) != NULL) {
            id[i++] = str_to_uint64(row[0]);
         }
         *ids = id;
      }
      sql_free_result();
      ok = true;
   } else {
      Mmsg(errmsg, _("Media id select failed: ERR=%s\n"), sql_strerror());
      Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      ok = false;
   }
   bdb_unlock();
   return ok;
}

/**
 * This function returns a list of all the DBIds that are returned
 *   for the query.
 *
 *  Returns false: on failure
 *          true:  on success
 */
bool BDB::bdb_get_query_dbids(JCR *jcr, POOL_MEM &query, dbid_list &ids)
{
   SQL_ROW row;
   int i = 0;
   bool ok = false;

   bdb_lock();
   ids.num_ids = 0;
   if (QueryDB(jcr, query.c_str())) {
      ids.num_ids = sql_num_rows();
      if (ids.num_ids > 0) {
         if (ids.max_ids < ids.num_ids) {
            free(ids.DBId);
            ids.DBId = (DBId_t *)malloc(ids.num_ids * sizeof(DBId_t));
         }
         while ((row = sql_fetch_row()) != NULL) {
            ids.DBId[i++] = str_to_uint64(row[0]);
         }
      }
      sql_free_result();
      ok = true;
   } else {
      Mmsg(errmsg, _("query dbids failed: ERR=%s\n"), sql_strerror());
      Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      ok = false;
   }
   bdb_unlock();
   return ok;
}

/**
 * Get Media Record
 *
 * Returns: false: on failure
 *          true:  on success
 */
bool BDB::bdb_get_media_record(JCR *jcr, MEDIA_DBR *mr)
{
   SQL_ROW row;
   char ed1[50];
   bool ok = false;
   char esc[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   if (mr->MediaId == 0 && mr->VolumeName[0] == 0) {
      Mmsg(cmd, "SELECT count(*) from Media");
      mr->MediaId = get_sql_record_max(jcr, this);
      bdb_unlock();
      return true;
   }
   if (mr->MediaId != 0) {               /* find by id */
      Mmsg(cmd, "SELECT MediaId,VolumeName,VolJobs,VolFiles,"
         "VolBlocks,VolBytes,VolABytes,VolHoleBytes,VolHoles,VolMounts,"
         "VolErrors,VolWrites,MaxVolBytes,VolCapacityBytes,"
         "MediaType,VolStatus,PoolId,VolRetention,VolUseDuration,MaxVolJobs,"
         "MaxVolFiles,Recycle,Slot,FirstWritten,LastWritten,InChanger,"
         "EndFile,EndBlock,VolType,VolParts,VolCloudParts,LastPartBytes,"
         "LabelType,LabelDate,StorageId,"
         "Enabled,LocationId,RecycleCount,InitialWrite,"
         "ScratchPoolId,RecyclePoolId,VolReadTime,VolWriteTime,ActionOnPurge,CacheRetention "
         "FROM Media WHERE MediaId=%s",
         edit_int64(mr->MediaId, ed1));
   } else {                           /* find by name */
      bdb_escape_string(jcr, esc, mr->VolumeName, strlen(mr->VolumeName));
      Mmsg(cmd, "SELECT MediaId,VolumeName,VolJobs,VolFiles,"
         "VolBlocks,VolBytes,VolABytes,VolHoleBytes,VolHoles,VolMounts,"
         "VolErrors,VolWrites,MaxVolBytes,VolCapacityBytes,"
         "MediaType,VolStatus,PoolId,VolRetention,VolUseDuration,MaxVolJobs,"
         "MaxVolFiles,Recycle,Slot,FirstWritten,LastWritten,InChanger,"
         "EndFile,EndBlock,VolType,VolParts,VolCloudParts,LastPartBytes,"
         "LabelType,LabelDate,StorageId,"
         "Enabled,LocationId,RecycleCount,InitialWrite,"
         "ScratchPoolId,RecyclePoolId,VolReadTime,VolWriteTime,ActionOnPurge,CacheRetention "
         "FROM Media WHERE VolumeName='%s'", esc);
   }

   if (QueryDB(jcr, cmd)) {
      char ed1[50];
      if (sql_num_rows() > 1) {
         Mmsg1(errmsg, _("More than one Volume!: %s\n"),
            edit_uint64(sql_num_rows(), ed1));
         Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      } else if (sql_num_rows() == 1) {
         if ((row = sql_fetch_row()) == NULL) {
            Mmsg1(errmsg, _("error fetching row: %s\n"), sql_strerror());
            Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
         } else {
            mr->MediaId = str_to_int64(row[0]);
            bstrncpy(mr->VolumeName, row[1]!=NULL?row[1]:"", sizeof(mr->VolumeName));
            mr->VolJobs = str_to_int64(row[2]);
            mr->VolFiles = str_to_int64(row[3]);
            mr->VolBlocks = str_to_int64(row[4]);
            mr->VolBytes = str_to_uint64(row[5]);
            mr->VolABytes = str_to_uint64(row[6]);
            mr->VolHoleBytes = str_to_uint64(row[7]);
            mr->VolHoles = str_to_int64(row[8]);
            mr->VolMounts = str_to_int64(row[9]);
            mr->VolErrors = str_to_int64(row[10]);
            mr->VolWrites = str_to_int64(row[11]);
            mr->MaxVolBytes = str_to_uint64(row[12]);
            mr->VolCapacityBytes = str_to_uint64(row[13]);
            bstrncpy(mr->MediaType, row[14]!=NULL?row[14]:"", sizeof(mr->MediaType));
            bstrncpy(mr->VolStatus, row[15]!=NULL?row[15]:"", sizeof(mr->VolStatus));
            mr->PoolId = str_to_int64(row[16]);
            mr->VolRetention = str_to_uint64(row[17]);
            mr->VolUseDuration = str_to_uint64(row[18]);
            mr->MaxVolJobs = str_to_int64(row[19]);
            mr->MaxVolFiles = str_to_int64(row[20]);
            mr->Recycle = str_to_int64(row[21]);
            mr->Slot = str_to_int64(row[22]);
            bstrncpy(mr->cFirstWritten, row[23]!=NULL?row[23]:"", sizeof(mr->cFirstWritten));
            mr->FirstWritten = (time_t)str_to_utime(mr->cFirstWritten);
            bstrncpy(mr->cLastWritten, row[24]!=NULL?row[24]:"", sizeof(mr->cLastWritten));
            mr->LastWritten = (time_t)str_to_utime(mr->cLastWritten);
            mr->InChanger = str_to_uint64(row[25]);
            mr->EndFile = str_to_uint64(row[26]);
            mr->EndBlock = str_to_uint64(row[27]);
            mr->VolType = str_to_int64(row[28]);
            mr->VolParts = str_to_int64(row[29]);
            mr->VolCloudParts = str_to_int64(row[30]);
            mr->LastPartBytes = str_to_uint64(row[31]);
            mr->LabelType = str_to_int64(row[32]);
            bstrncpy(mr->cLabelDate, row[33]!=NULL?row[33]:"", sizeof(mr->cLabelDate));
            mr->LabelDate = (time_t)str_to_utime(mr->cLabelDate);
            mr->StorageId = str_to_int64(row[34]);
            mr->Enabled = str_to_int64(row[35]);
            mr->LocationId = str_to_int64(row[36]);
            mr->RecycleCount = str_to_int64(row[37]);
            bstrncpy(mr->cInitialWrite, row[38]!=NULL?row[38]:"", sizeof(mr->cInitialWrite));
            mr->InitialWrite = (time_t)str_to_utime(mr->cInitialWrite);
            mr->ScratchPoolId = str_to_int64(row[39]);
            mr->RecyclePoolId = str_to_int64(row[40]);
            mr->VolReadTime = str_to_int64(row[41]);
            mr->VolWriteTime = str_to_int64(row[42]);
            mr->ActionOnPurge = str_to_int32(row[43]);
            mr->CacheRetention = str_to_int64(row[44]);

            ok = true;
         }
      } else {
         if (mr->MediaId != 0) {
            Mmsg1(errmsg, _("Media record with MediaId=%s not found.\n"),
               edit_int64(mr->MediaId, ed1));
         } else {
            Mmsg1(errmsg, _("Media record for Volume name \"%s\" not found.\n"),
                  mr->VolumeName);
         }
      }
      sql_free_result();
   } else {
      if (mr->MediaId != 0) {
         Mmsg(errmsg, _("Media record for MediaId=%u not found in Catalog.\n"),
            mr->MediaId);
       } else {
         Mmsg(errmsg, _("Media record for Volume Name \"%s\" not found in Catalog.\n"),
            mr->VolumeName);
   }   }
   bdb_unlock();
   return ok;
}

/* Remove all MD5 from a query (can save lot of memory with many files) */
static void strip_md5(char *q)
{
   char *p = q;
   while ((p = strstr(p, ", MD5"))) {
      memset(p, ' ', 5 * sizeof(char));
   }
}

/**
 * Find the last "accurate" backup state (that can take deleted files in
 * account)
 * 1) Get all files with jobid in list (F subquery)
 *    Get all files in BaseFiles with jobid in list
 * 2) Take only the last version of each file (Temp subquery) => accurate list
 *    is ok
 * 3) Join the result to file table to get fileindex, jobid and lstat information
 *
 * TODO: See if we can do the SORT only if needed (as an argument)
 */
bool BDB::bdb_get_file_list(JCR *jcr, char *jobids, int opts,
                      DB_RESULT_HANDLER *result_handler, void *ctx)
{
   const char *type;

   if (opts & DBL_ALL_FILES) {
      type = ""; 
   } else {
      /* Only non-deleted files */
      type = "WHERE FileIndex > 0";
   }
   if (opts & DBL_DELETED) {
      type = "WHERE FileIndex <= 0";
   }
   if (!*jobids) {
      bdb_lock();
      Mmsg(errmsg, _("ERR=JobIds are empty\n"));
      bdb_unlock();
      return false;
   }
   POOL_MEM buf(PM_MESSAGE);
   POOL_MEM buf2(PM_MESSAGE);
   if (opts & DBL_USE_DELTA) {
      Mmsg(buf2, select_recent_version_with_basejob_and_delta[bdb_get_type_index()],
           jobids, jobids, jobids, jobids);

   } else {
      Mmsg(buf2, select_recent_version_with_basejob[bdb_get_type_index()],
           jobids, jobids, jobids, jobids);
   }

   /* bsr code is optimized for JobId sorted, with Delta, we need to get
    * them ordered by date. JobTDate and JobId can be mixed if using Copy
    * or Migration
    */
   Mmsg(buf,
"SELECT Path.Path, Filename.Name, T1.FileIndex, T1.JobId, LStat, DeltaSeq, MD5 "
 "FROM ( %s ) AS T1 "
 "JOIN Filename ON (Filename.FilenameId = T1.FilenameId) "
 "JOIN Path ON (Path.PathId = T1.PathId) %s "
"ORDER BY T1.JobTDate, FileIndex ASC",/* Return sorted by JobTDate */
                                      /* FileIndex for restore code */
        buf2.c_str(), type);

   if (!(opts & DBL_USE_MD5)) {
      strip_md5(buf.c_str());
   }

   Dmsg1(100, "q=%s\n", buf.c_str());

   return bdb_big_sql_query(buf.c_str(), result_handler, ctx);
}

/**
 * This procedure gets the base jobid list used by jobids,
 */
bool BDB::bdb_get_used_base_jobids(JCR *jcr,
                             POOLMEM *jobids, db_list_ctx *result)
{
   POOL_MEM buf;

   Mmsg(buf,
 "SELECT DISTINCT BaseJobId "
 "  FROM Job JOIN BaseFiles USING (JobId) "
 " WHERE Job.HasBase = 1 "
 "   AND Job.JobId IN (%s) ", jobids);
   return bdb_sql_query(buf.c_str(), db_list_handler, result);
}

/* Mutex used to have global counter on btemp table */
static pthread_mutex_t btemp_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t btemp_cur = 1;

/**
 * The decision do change an incr/diff was done before
 * Full : do nothing
 * Differential : get the last full id
 * Incremental : get the last full + last diff + last incr(s) ids
 *
 * If you specify jr->StartTime, it will be used to limit the search
 * in the time. (usually now)
 *
 * TODO: look and merge from ua_restore.c
 */
bool BDB::bdb_get_accurate_jobids(JCR *jcr,
                            JOB_DBR *jr, db_list_ctx *jobids)
{
   bool ret=false;
   char clientid[50], jobid[50], filesetid[50];
   char date[MAX_TIME_LENGTH];
   char esc[MAX_ESCAPE_NAME_LENGTH];
   POOL_MEM query(PM_MESSAGE), name(PM_FNAME);

   /* Take the current time as upper limit if nothing else specified */
   utime_t StartTime = (jr->StartTime)?jr->StartTime:time(NULL);

   bstrutime(date, sizeof(date),  StartTime + 1);
   jobids->reset();

   /* If we are comming from bconsole, we must ensure that we
    * have a unique name.
    */
   if (jcr->JobId == 0) {
      P(btemp_mutex);
      bsnprintf(jobid, sizeof(jobid), "0%u", btemp_cur++);
      V(btemp_mutex);
   } else {
      edit_uint64(jcr->JobId, jobid);
   }

   if (jr->Name[0] != 0) {
      bdb_escape_string(jcr, esc, jr->Name, strlen(jr->Name));
      Mmsg(name, " AND Name = '%s' ", esc);
   }

   /* First, find the last good Full backup for this job/client/fileset */
   Mmsg(query, create_temp_accurate_jobids[bdb_get_type_index()],
        jobid,
        edit_uint64(jr->ClientId, clientid),
        date,
        edit_uint64(jr->FileSetId, filesetid),
        name.c_str()
      );

   if (!bdb_sql_query(query.c_str(), NULL, NULL)) {
      goto bail_out;
   }

   if (jr->JobLevel == L_INCREMENTAL || jr->JobLevel == L_VIRTUAL_FULL) {
      /* Now, find the last differential backup after the last full */
      Mmsg(query,
"INSERT INTO btemp3%s (JobId, StartTime, EndTime, JobTDate, PurgedFiles) "
 "SELECT JobId, StartTime, EndTime, JobTDate, PurgedFiles "
   "FROM Job JOIN FileSet USING (FileSetId) "
  "WHERE ClientId = %s "
    "AND Level='D' AND JobStatus IN ('T','W') AND Type='B' "
    "AND StartTime > (SELECT EndTime FROM btemp3%s ORDER BY EndTime DESC LIMIT 1) "
    "AND StartTime < '%s' "
    "AND FileSet.FileSet= (SELECT FileSet FROM FileSet WHERE FileSetId = %s) "
    " %s "                      /* Optional name */
  "ORDER BY Job.JobTDate DESC LIMIT 1 ",
           jobid,
           clientid,
           jobid,
           date,
           filesetid,
           name.c_str()
         );

      if (!bdb_sql_query(query.c_str(), NULL, NULL)) {
         goto bail_out;
      }

      /* We just have to take all incremental after the last Full/Diff */
      Mmsg(query,
"INSERT INTO btemp3%s (JobId, StartTime, EndTime, JobTDate, PurgedFiles) "
 "SELECT JobId, StartTime, EndTime, JobTDate, PurgedFiles "
   "FROM Job JOIN FileSet USING (FileSetId) "
  "WHERE ClientId = %s "
    "AND Level='I' AND JobStatus IN ('T','W') AND Type='B' "
    "AND StartTime > (SELECT EndTime FROM btemp3%s ORDER BY EndTime DESC LIMIT 1) "
    "AND StartTime < '%s' "
    "AND FileSet.FileSet= (SELECT FileSet FROM FileSet WHERE FileSetId = %s) "
    " %s "
  "ORDER BY Job.JobTDate DESC ",
           jobid,
           clientid,
           jobid,
           date,
           filesetid,
           name.c_str()
         );
      if (!bdb_sql_query(query.c_str(), NULL, NULL)) {
         goto bail_out;
      }
   }

   /* build a jobid list ie: 1,2,3,4 */
   Mmsg(query, "SELECT JobId FROM btemp3%s ORDER by JobTDate", jobid);
   bdb_sql_query(query.c_str(), db_list_handler, jobids);
   Dmsg1(1, "db_get_accurate_jobids=%s\n", jobids->list);
   ret = true;

bail_out:
   Mmsg(query, "DROP TABLE btemp3%s", jobid);
   bdb_sql_query(query.c_str(), NULL, NULL);

   return ret;
}

bool BDB::bdb_get_base_file_list(JCR *jcr, bool use_md5,
                           DB_RESULT_HANDLER *result_handler, void *ctx)
{
   POOL_MEM buf(PM_MESSAGE);

   Mmsg(buf,
 "SELECT Path, Name, FileIndex, JobId, LStat, 0 As DeltaSeq, MD5 "
   "FROM new_basefile%lld ORDER BY JobId, FileIndex ASC",
        (uint64_t) jcr->JobId);

   if (!use_md5) {
      strip_md5(buf.c_str());
   }
   return bdb_sql_query(buf.c_str(), result_handler, ctx);
}

bool BDB::bdb_get_base_jobid(JCR *jcr, JOB_DBR *jr, JobId_t *jobid)
{
   POOL_MEM query(PM_FNAME);
   utime_t StartTime;
   db_int64_ctx lctx;
   char date[MAX_TIME_LENGTH];
   char esc[MAX_ESCAPE_NAME_LENGTH];
   bool ret = false;

   *jobid = 0;
   lctx.count = 0;
   lctx.value = 0;

   StartTime = (jr->StartTime)?jr->StartTime:time(NULL);
   bstrutime(date, sizeof(date),  StartTime + 1);
   bdb_escape_string(jcr, esc, jr->Name, strlen(jr->Name));

   /* we can take also client name, fileset, etc... */

   Mmsg(query,
 "SELECT JobId, Job, StartTime, EndTime, JobTDate, PurgedFiles "
   "FROM Job "
// "JOIN FileSet USING (FileSetId) JOIN Client USING (ClientId) "
  "WHERE Job.Name = '%s' "
    "AND Level='B' AND JobStatus IN ('T','W') AND Type='B' "
//    "AND FileSet.FileSet= '%s' "
//    "AND Client.Name = '%s' "
    "AND StartTime<'%s' "
  "ORDER BY Job.JobTDate DESC LIMIT 1",
        esc,
//      edit_uint64(jr->ClientId, clientid),
//      edit_uint64(jr->FileSetId, filesetid));
        date);

   Dmsg1(10, "db_get_base_jobid q=%s\n", query.c_str());
   if (!bdb_sql_query(query.c_str(), db_int64_handler, &lctx)) {
      goto bail_out;
   }
   *jobid = (JobId_t) lctx.value;

   Dmsg1(10, "db_get_base_jobid=%lld\n", *jobid);
   ret = true;

bail_out:
   return ret;
}

/* Get JobIds associated with a volume */
bool BDB::bdb_get_volume_jobids(JCR *jcr,
                         MEDIA_DBR *mr, db_list_ctx *lst)
{
   char ed1[50];
   bool ret = false;

   bdb_lock();
   Mmsg(cmd, "SELECT DISTINCT JobId FROM JobMedia WHERE MediaId=%s",
        edit_int64(mr->MediaId, ed1));
   ret = bdb_sql_query(cmd, db_list_handler, lst);
   bdb_unlock();
   return ret;
}

/* Get JobIds associated with a client */
bool BDB::bdb_get_client_jobids(JCR *jcr,
                                CLIENT_DBR *cr, db_list_ctx *lst)
{
   char ed1[50];
   bool ret = false;

   bdb_lock();
   Mmsg(cmd, "SELECT JobId FROM Job WHERE ClientId=%s",
        edit_int64(cr->ClientId, ed1));
   ret = bdb_sql_query(cmd, db_list_handler, lst);
   bdb_unlock();
   return ret;
}

/**
 * Get Snapshot Record
 *
 * Returns: false: on failure
 *          true:  on success
 */
bool BDB::bdb_get_snapshot_record(JCR *jcr, SNAPSHOT_DBR *sr)
{
   SQL_ROW row;
   char ed1[50];
   bool ok = false;
   char esc[MAX_ESCAPE_NAME_LENGTH];
   POOL_MEM filter1, filter2;

   if (sr->SnapshotId == 0 && (sr->Name[0] == 0 || sr->Device[0] == 0)) {
      Dmsg0(10, "No SnapshotId or Name/Device provided\n");
      return false;
   }

   bdb_lock();

   if (sr->SnapshotId != 0) {               /* find by id */
      Mmsg(filter1, "Snapshot.SnapshotId=%d", sr->SnapshotId);

   } else if (*sr->Name && *sr->Device) { /* find by name */
      bdb_escape_string(jcr, esc, sr->Name, strlen(sr->Name));
      Mmsg(filter1, "Snapshot.Name='%s'", esc);
      bdb_escape_string(jcr, esc, sr->Device, strlen(sr->Device));
      Mmsg(filter2, "AND Snapshot.Device='%s'", esc);

   } else {
      Dmsg0(10, "No SnapshotId or Name and Device\n");
      return false;
   }

   Mmsg(cmd, "SELECT SnapshotId, Snapshot.Name, JobId, Snapshot.FileSetId, "
        "FileSet.FileSet, CreateTDate, CreateDate, "
        "Client.Name AS Client, Snapshot.ClientId, Volume, Device, Type, Retention, "
        "Comment FROM Snapshot JOIN Client USING (ClientId) LEFT JOIN FileSet USING (FileSetId) WHERE %s %s",
        filter1.c_str(), filter2.c_str());

   if (QueryDB(jcr, cmd)) {
      char ed1[50];
      if (sql_num_rows() > 1) {
         Mmsg1(errmsg, _("More than one Snapshot!: %s\n"),
            edit_uint64(sql_num_rows(), ed1));
         Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
      } else if (sql_num_rows() == 1) {
         if ((row = sql_fetch_row()) == NULL) {
            Mmsg1(errmsg, _("error fetching row: %s\n"), sql_strerror());
            Jmsg(jcr, M_ERROR, 0, "%s", errmsg);
         } else {
            /* return values */
            sr->reset();
            sr->need_to_free = true;
            sr->SnapshotId = str_to_int64(row[0]);
            bstrncpy(sr->Name, row[1], sizeof(sr->Name));
            sr->JobId = str_to_int64(row[2]);
            sr->FileSetId = str_to_int64(row[3]);
            bstrncpy(sr->FileSet, row[4], sizeof(sr->FileSet));
            sr->CreateTDate = str_to_uint64(row[5]);
            bstrncpy(sr->CreateDate, row[6], sizeof(sr->CreateDate));
            bstrncpy(sr->Client, row[7], sizeof(sr->Client));
            sr->ClientId = str_to_int64(row[8]);
            sr->Volume = bstrdup(row[9]);
            sr->Device = bstrdup(row[10]);
            bstrncpy(sr->Type, row[11], sizeof(sr->Type));
            sr->Retention = str_to_int64(row[12]);
            bstrncpy(sr->Comment, NPRTB(row[13]), sizeof(sr->Comment));
            ok = true;
         }
      } else {
         if (sr->SnapshotId != 0) {
            Mmsg1(errmsg, _("Snapshot record with SnapshotId=%s not found.\n"),
               edit_int64(sr->SnapshotId, ed1));
         } else {
            Mmsg1(errmsg, _("Snapshot record for Snapshot name \"%s\" not found.\n"),
                  sr->Name);
         }
      }
      sql_free_result();
   } else {
      if (sr->SnapshotId != 0) {
         Mmsg1(errmsg, _("Snapshot record with SnapshotId=%s not found.\n"),
               edit_int64(sr->SnapshotId, ed1));
      } else {
         Mmsg1(errmsg, _("Snapshot record for Snapshot name \"%s\" not found.\n"),
                  sr->Name);
      }
   }
   bdb_unlock();
   return ok;
}

/* Job, Level */
static void build_estimate_query(BDB *db, POOL_MEM &query, const char *mode,
                                 char *job_esc, char level)
{
   POOL_MEM filter, tmp;
   char ed1[50];


   if (level == 0) {
      level = 'F';
   }
   /* MySQL doesn't have statistic functions */
   if (db->bdb_get_type_index() == SQL_TYPE_POSTGRESQL) {
      /* postgresql have functions that permit to handle lineal regression
       * in y=ax + b
       * REGR_SLOPE(Y,X) = get x
       * REGR_INTERCEPT(Y,X) = get b
       * and we need y when x=now()
       * CORR gives the correlation
       * (TODO: display progress bar only if CORR > 0.8)
       */
      btime_t now = time(NULL);
      Mmsg(query,
           "SELECT temp.jobname AS jobname, "
           "COALESCE(CORR(value,JobTDate),0) AS corr, "
           "(%s*REGR_SLOPE(value,JobTDate) "
           " + REGR_INTERCEPT(value,JobTDate)) AS value, "
           "AVG(value) AS avg_value, "
           " COUNT(1) AS nb ", edit_int64(now, ed1));
   } else {
      Mmsg(query,
           "SELECT jobname AS jobname, "
           "0.1 AS corr, AVG(value) AS value, AVG(value) AS avg_value, "
           "COUNT(1) AS nb ");
    }

    /* if it's a differential, we need to compare since the last full
     * 
     *   F D D D F D D D      F I I I I D I I I
     * | #     # #     #    | #         #
     * | #   # # #   # #    | #         #
     * | # # # # # # # #    | # # # # # # # # #
     * +-----------------   +-------------------
     */
   if (level == L_DIFFERENTIAL) {
      Mmsg(filter,
           " AND Job.StartTime > ( "
             " SELECT StartTime "
              " FROM Job "
             " WHERE Job.Name = '%s' " 
             " AND Job.Level = 'F' "
             " AND Job.JobStatus IN ('T', 'W') "
           " ORDER BY Job.StartTime DESC LIMIT 1) ",
           job_esc);
   }
   Mmsg(tmp,
        " FROM ( "
         " SELECT Job.Name AS jobname, "
         " %s AS value, "
         " JobTDate AS jobtdate "
          " FROM Job INNER JOIN Client USING (ClientId) "
         " WHERE Job.Name = '%s' "
          " AND Job.Level = '%c' "
          " AND Job.JobStatus IN ('T', 'W') "
        "%s "
        "ORDER BY StartTime DESC "
        "LIMIT 4"
        ") AS temp GROUP BY temp.jobname",
        mode, job_esc, level, filter.c_str()
      );
   pm_strcat(query, tmp.c_str());
}

bool BDB::bdb_get_job_statistics(JCR *jcr, JOB_DBR *jr)
{
   SQL_ROW row;
   POOL_MEM queryB, queryF, query;
   char job_esc[MAX_ESCAPE_NAME_LENGTH];
   bool ok = false;

   bdb_lock();
   bdb_escape_string(jcr, job_esc, jr->Name, strlen(jr->Name));
   build_estimate_query(this, queryB, "JobBytes", job_esc, jr->JobLevel);
   build_estimate_query(this, queryF, "JobFiles", job_esc, jr->JobLevel);
   Mmsg(query,
        "SELECT  bytes.corr * 100 AS corr_jobbytes, " /* 0 */
                "bytes.value AS jobbytes, "           /* 1 */
                "bytes.avg_value AS avg_jobbytes, "   /* 2 */
                "bytes.nb AS nb_jobbytes, "           /* 3 */
                "files.corr * 100 AS corr_jobfiles, " /* 4 */
                "files.value AS jobfiles, "           /* 5 */
                "files.avg_value AS avg_jobfiles, "   /* 6 */
                "files.nb AS nb_jobfiles "            /* 7 */
        "FROM (%s) AS bytes LEFT JOIN (%s) AS files USING (jobname)",
        queryB.c_str(), queryF.c_str());
   Dmsg1(100, "query=%s\n", query.c_str());

   if (QueryDB(jcr, query.c_str())) {
      char ed1[50];
      if (sql_num_rows() > 1) {
         Mmsg1(errmsg, _("More than one Result!: %s\n"),
            edit_uint64(sql_num_rows(), ed1));
         goto bail_out;
      }
      ok = true;

      if ((row = sql_fetch_row()) == NULL) {
         Mmsg1(errmsg, _("error fetching row: %s\n"), sql_strerror());
      } else {
         jr->CorrJobBytes = str_to_int64(row[0]);
         jr->JobBytes = str_to_int64(row[1]);

         /* lineal expression with only one job doesn't return a correct value */
         if (str_to_int64(row[3]) == 1) {
            jr->JobBytes = str_to_int64(row[2]); /* Take the AVG value */
         }
         jr->CorrNbJob = str_to_int64(row[3]); /* Number of jobs used in this sample */
         jr->CorrJobFiles = str_to_int64(row[4]);
         jr->JobFiles = str_to_int64(row[5]);

         if (str_to_int64(row[7]) == 1) {
            jr->JobFiles = str_to_int64(row[6]); /* Take the AVG value */
         }
      }
      sql_free_result();
   }
bail_out:
   bdb_unlock();
   return ok;
}

/*
 * List Client/Pool associations. Return a list of Client/Pool in a alist.
 * [0] Client
 * [1] Pool
 * [2] Client
 * [3] Pool
 * ...
 */
bool BDB::bdb_get_client_pool(JCR *jcr, alist *results)
{
   SQL_ROW row;
   bool ret=false;
   POOLMEM *where  = get_pool_memory(PM_MESSAGE);
   POOLMEM *tmp    = get_pool_memory(PM_MESSAGE);
   bdb_lock();

   /* Build the WHERE part with the current ACLs if any */
   pm_strcpy(where, get_acls(DB_ACL_BIT(DB_ACL_CLIENT)  |
                             DB_ACL_BIT(DB_ACL_JOB)     |
                             DB_ACL_BIT(DB_ACL_POOL),
                             true));

   Mmsg(cmd, "SELECT DISTINCT Client.Name, Pool.Name "
        "FROM Job JOIN Client USING (ClientId) JOIN Pool USING (PoolId) %s",
        where);

   Dmsg1(100, "sql=%s\n", cmd);
   if (QueryDB(jcr, cmd)) {
      ret = true;

      while ((row = sql_fetch_row()) != NULL) {
         results->append(bstrdup(row[0])); /* append client */
         results->append(bstrdup(row[1])); /* append pool */
      }
      sql_free_result();
   }

   bdb_unlock();
   free_pool_memory(where);
   free_pool_memory(tmp);
   return ret;
}

#endif /* HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL */
