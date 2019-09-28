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
 * Bacula Catalog Database Delete record interface routines
 *
 *    Written by Kern Sibbald, December 2000-2014
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
 * Delete Pool record, must also delete all associated
 *  Media records.
 *
 *  Returns: 0 on error
 *           1 on success
 *           PoolId = number of Pools deleted (should be 1)
 *           NumVols = number of Media records deleted
 */
int BDB::bdb_delete_pool_record(JCR *jcr, POOL_DBR *pr)
{
   SQL_ROW row;
   char esc[MAX_ESCAPE_NAME_LENGTH];

   bdb_lock();
   bdb_escape_string(jcr, esc, pr->Name, strlen(pr->Name));
   Mmsg(cmd, "SELECT PoolId FROM Pool WHERE Name='%s'", esc);
   Dmsg1(10, "selectpool: %s\n", cmd);

   pr->PoolId = pr->NumVols = 0;

   if (QueryDB(jcr, cmd)) {
      int nrows = sql_num_rows();
      if (nrows == 0) {
         Mmsg(errmsg, _("No pool record %s exists\n"), pr->Name);
         sql_free_result();
         bdb_unlock();
         return 0;
      } else if (nrows != 1) {
         Mmsg(errmsg, _("Expecting one pool record, got %d\n"), nrows);
         sql_free_result();
         bdb_unlock();
         return 0;
      }
      if ((row = sql_fetch_row()) == NULL) {
         Mmsg1(&errmsg, _("Error fetching row %s\n"), sql_strerror());
         bdb_unlock();
         return 0;
      }
      pr->PoolId = str_to_int64(row[0]);
      sql_free_result();
   }

   /* Delete Media owned by this pool */
   Mmsg(cmd,
"DELETE FROM Media WHERE Media.PoolId = %d", pr->PoolId);

   pr->NumVols = DeleteDB(jcr, cmd);
   Dmsg1(200, "Deleted %d Media records\n", pr->NumVols);

   /* Delete Pool */
   Mmsg(cmd,
"DELETE FROM Pool WHERE Pool.PoolId = %d", pr->PoolId);
   pr->PoolId = DeleteDB(jcr, cmd);
   Dmsg1(200, "Deleted %d Pool records\n", pr->PoolId);

   bdb_unlock();
   return 1;
}

#define MAX_DEL_LIST_LEN 1000000

struct s_del_ctx {
   JobId_t *JobId;
   int num_ids;                       /* ids stored */
   int max_ids;                       /* size of array */
   int num_del;                       /* number deleted */
   int tot_ids;                       /* total to process */
};

/*
 * Called here to make in memory list of JobIds to be
 *  deleted. The in memory list will then be transversed
 *  to issue the SQL DELETE commands.  Note, the list
 *  is allowed to get to MAX_DEL_LIST_LEN to limit the
 *  maximum malloc'ed memory.
 */
static int delete_handler(void *ctx, int num_fields, char **row)
{
   struct s_del_ctx *del = (struct s_del_ctx *)ctx;

   if (del->num_ids == MAX_DEL_LIST_LEN) {
      return 1;
   }
   if (del->num_ids == del->max_ids) {
      del->max_ids = (del->max_ids * 3) / 2;
      del->JobId = (JobId_t *)brealloc(del->JobId, sizeof(JobId_t) *
         del->max_ids);
   }
   del->JobId[del->num_ids++] = (JobId_t)str_to_int64(row[0]);
   return 0;
}


/*
 * This routine will purge (delete) all records
 * associated with a particular Volume. It will
 * not delete the media record itself.
 * TODO: This function is broken and it doesn't purge
 *       File, BaseFiles, Log, ...
 *       We call it from relabel and delete volume=, both ensure
 *       that the volume is properly purged.
 */
static int do_media_purge(BDB *mdb, MEDIA_DBR *mr)
{
   POOLMEM *query = get_pool_memory(PM_MESSAGE);
   struct s_del_ctx del;
   char ed1[50];
   int i;

   del.num_ids = 0;
   del.tot_ids = 0;
   del.num_del = 0;
   del.max_ids = 0;
   Mmsg(mdb->cmd, "SELECT JobId from JobMedia WHERE MediaId=%lu", mr->MediaId);
   del.max_ids = mr->VolJobs;
   if (del.max_ids < 100) {
      del.max_ids = 100;
   } else if (del.max_ids > MAX_DEL_LIST_LEN) {
      del.max_ids = MAX_DEL_LIST_LEN;
   }
   del.JobId = (JobId_t *)malloc(sizeof(JobId_t) * del.max_ids);
   mdb->bdb_sql_query(mdb->cmd, delete_handler, (void *)&del);

   for (i=0; i < del.num_ids; i++) {
      Dmsg1(400, "Delete JobId=%d\n", del.JobId[i]);
      Mmsg(query, "DELETE FROM Job WHERE JobId=%s", edit_int64(del.JobId[i], ed1));
      mdb->bdb_sql_query(query, NULL, (void *)NULL);
      Mmsg(query, "DELETE FROM File WHERE JobId=%s", edit_int64(del.JobId[i], ed1));
      mdb->bdb_sql_query(query, NULL, (void *)NULL);
      Mmsg(query, "DELETE FROM JobMedia WHERE JobId=%s", edit_int64(del.JobId[i], ed1));
      mdb->bdb_sql_query(query, NULL, (void *)NULL);
   }
   free(del.JobId);
   free_pool_memory(query);
   return 1;
}

/* Delete Media record and all records that
 * are associated with it.
 */
int BDB::bdb_delete_media_record(JCR *jcr, MEDIA_DBR *mr)
{
   bdb_lock();
   if (mr->MediaId == 0 && !bdb_get_media_record(jcr, mr)) {
      bdb_unlock();
      return 0;
   }
   /* Do purge if not already purged */
   if (strcmp(mr->VolStatus, "Purged") != 0) {
      /* Delete associated records */
      do_media_purge(this, mr);
   }

   Mmsg(cmd, "DELETE FROM Media WHERE MediaId=%lu", mr->MediaId);
   bdb_sql_query(cmd, NULL, (void *)NULL);
   bdb_unlock();
   return 1;
}

/*
 * Purge all records associated with a
 * media record. This does not delete the
 * media record itself. But the media status
 * is changed to "Purged".
 */
int BDB::bdb_purge_media_record(JCR *jcr, MEDIA_DBR *mr)
{
   bdb_lock();
   if (mr->MediaId == 0 && !bdb_get_media_record(jcr, mr)) {
      bdb_unlock();
      return 0;
   }
   /* Delete associated records */
   do_media_purge(this, mr);           /* Note, always purge */

   /* Mark Volume as purged */
   strcpy(mr->VolStatus, "Purged");
   if (!bdb_update_media_record(jcr, mr)) {
      bdb_unlock();
      return 0;
   }

   bdb_unlock();
   return 1;
}

/* Delete Snapshot record */
int BDB::bdb_delete_snapshot_record(JCR *jcr, SNAPSHOT_DBR *sr)
{
   bdb_lock();
   if (sr->SnapshotId == 0 && !bdb_get_snapshot_record(jcr, sr)) {
      bdb_unlock();
      return 0;
   }

   Mmsg(cmd, "DELETE FROM Snapshot WHERE SnapshotId=%d", sr->SnapshotId);
   bdb_sql_query(cmd, NULL, (void *)NULL);
   bdb_unlock();
   return 1;
}

/* Delete Client record */
int BDB::bdb_delete_client_record(JCR *jcr, CLIENT_DBR *cr)
{
   bdb_lock();
   if (cr->ClientId == 0 && !bdb_get_client_record(jcr, cr)) {
      bdb_unlock();
      return 0;
   }

   Mmsg(cmd, "DELETE FROM Client WHERE ClientId=%d", cr->ClientId);
   bdb_sql_query(cmd, NULL, (void *)NULL);
   bdb_unlock();
   return 1;
}

#endif /* HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL */
