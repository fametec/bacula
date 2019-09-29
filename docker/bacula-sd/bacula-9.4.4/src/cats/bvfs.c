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

#include "bacula.h"
#include "cats.h" 
#if HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL
#include "lib/htable.h"
#include "bvfs.h"

#define can_access(x) (true)

/* from libbacfind */
extern int decode_stat(char *buf, struct stat *statp, int stat_size, int32_t *LinkFI);

#define dbglevel      DT_BVFS|10
#define dbglevel_sql  DT_SQL|15

static int result_handler(void *ctx, int fields, char **row)
{
   if (fields == 4) {
      Pmsg4(0, "%s\t%s\t%s\t%s\n",
            row[0], row[1], row[2], row[3]);
   } else if (fields == 5) {
      Pmsg5(0, "%s\t%s\t%s\t%s\t%s\n",
            row[0], row[1], row[2], row[3], row[4]);
   } else if (fields == 6) {
      Pmsg6(0, "%s\t%s\t%s\t%s\t%s\t%s\n",
            row[0], row[1], row[2], row[3], row[4], row[5]);
   } else if (fields == 7) {
      Pmsg7(0, "%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
            row[0], row[1], row[2], row[3], row[4], row[5], row[6]);
   }
   return 0;
}

Bvfs::Bvfs(JCR *j, BDB *mdb)
{
   jcr = j;
   jcr->inc_use_count();
   db = mdb;                 /* need to inc ref count */
   jobids = get_pool_memory(PM_NAME);
   prev_dir = get_pool_memory(PM_NAME);
   pattern = get_pool_memory(PM_NAME);
   filename = get_pool_memory(PM_NAME);
   tmp = get_pool_memory(PM_NAME);
   escaped_list = get_pool_memory(PM_NAME);
   *filename = *jobids = *prev_dir = *pattern = 0;
   pwd_id = offset = 0;
   see_copies = see_all_versions = false;
   compute_delta = true;
   limit = 1000;
   attr = new_attr(jcr);
   list_entries = result_handler;
   user_data = this;
   username = NULL;
   job_acl = client_acl = pool_acl = fileset_acl = NULL;
   last_dir_acl = NULL;
   dir_acl = NULL;
   use_acl = false;
   dir_filenameid = 0;       /* special FilenameId where Name='' */
}

Bvfs::~Bvfs() {
   free_pool_memory(jobids);
   free_pool_memory(pattern);
   free_pool_memory(prev_dir);
   free_pool_memory(filename);
   free_pool_memory(tmp);
   free_pool_memory(escaped_list);
   if (username) {
      free(username);
   }
   free_attr(attr);
   jcr->dec_use_count();
   if (dir_acl) {
      delete dir_acl;
   }
}

char *Bvfs::escape_list(alist *lst)
{
   char *elt;
   int len;

   /* List is empty, reject everything */
   if (!lst || lst->size() == 0) {
      Mmsg(escaped_list, "''");
      return escaped_list;
   }

   *tmp = 0;
   *escaped_list = 0;

   foreach_alist(elt, lst) {
      if (elt && *elt) {
         len = strlen(elt);
         /* Escape + ' ' */
         tmp = check_pool_memory_size(tmp, 2 * len + 2 + 2);

         tmp[0] = '\'';
         db->bdb_escape_string(jcr, tmp + 1 , elt, len);
         pm_strcat(tmp, "'");

         if (*escaped_list) {
            pm_strcat(escaped_list, ",");
         }

         pm_strcat(escaped_list, tmp);
      }
   }
   return escaped_list;
}

/* Returns the number of jobids in the result */
int Bvfs::filter_jobid()
{
   POOL_MEM query;
   POOL_MEM sub_where;
   POOL_MEM sub_join;

   /* No ACL, no username, no check */
   if (!job_acl && !fileset_acl && !client_acl && !pool_acl && !username) {
      Dmsg0(dbglevel_sql, "No ACL\n");
      /* Just count the number of items in the list */
      int nb = (*jobids != 0) ? 1 : 0;
      for (char *p=jobids; *p ; p++) {
         if (*p == ',') {
            nb++;
         }
      }
      return nb;
   }

   if (job_acl) {
      Mmsg(sub_where, " AND Job.Name IN (%s) ", escape_list(job_acl));
   }

   if (fileset_acl) {
      Mmsg(query, " AND FileSet.FileSet IN (%s) ", escape_list(fileset_acl));
      pm_strcat(sub_where, query.c_str());
      pm_strcat(sub_join, " JOIN FileSet USING (FileSetId) ");
   }

   if (client_acl) {
      Mmsg(query, " AND Client.Name IN (%s) ", escape_list(client_acl));
      pm_strcat(sub_where, query.c_str());
   }

   if (pool_acl) {
      Mmsg(query, " AND Pool.Name IN (%s) ", escape_list(pool_acl));
      pm_strcat(sub_where, query.c_str());
      pm_strcat(sub_join, " JOIN Pool USING (PoolId) ");
   }

   if (username) {
      /* Query used by Bweb to filter clients, activated when using
       * set_username()
       */
      Mmsg(query,
      "SELECT DISTINCT JobId FROM Job JOIN Client USING (ClientId) %s "
        "JOIN (SELECT ClientId FROM client_group_member "
        "JOIN client_group USING (client_group_id) "
        "JOIN bweb_client_group_acl USING (client_group_id) "
        "JOIN bweb_user USING (userid) "
       "WHERE bweb_user.username = '%s' "
      ") AS filter USING (ClientId) "
        " WHERE JobId IN (%s) %s",
           sub_join.c_str(), username, jobids, sub_where.c_str());

   } else {
      Mmsg(query,
      "SELECT DISTINCT JobId FROM Job JOIN Client USING (ClientId) %s "
      " WHERE JobId IN (%s) %s",
           sub_join.c_str(), jobids, sub_where.c_str());
   }

   db_list_ctx ctx;
   Dmsg1(dbglevel_sql, "q=%s\n", query.c_str());
   db->bdb_sql_query(query.c_str(), db_list_handler, &ctx);
   pm_strcpy(jobids, ctx.list);
   return ctx.count;
}

/* Return the number of jobids after the filter */
int Bvfs::set_jobid(JobId_t id)
{
   Mmsg(jobids, "%lld", (uint64_t)id);
   return filter_jobid();
}

/* Return the number of jobids after the filter */
int Bvfs::set_jobids(char *ids)
{
   pm_strcpy(jobids, ids);
   return filter_jobid();
}

/*
 * TODO: Find a way to let the user choose how he wants to display
 * files and directories
 */


/*
 * Working Object to store PathId already seen (avoid
 * database queries), equivalent to %cache_ppathid in perl
 */

#define NITEMS 50000
class pathid_cache {
private:
   hlink *nodes;
   int nb_node;
   int max_node;

   alist *table_node;

   htable *cache_ppathid;

public:
   pathid_cache() {
      hlink link;
      cache_ppathid = (htable *)malloc(sizeof(htable));
      cache_ppathid->init(&link, &link, NITEMS);
      max_node = NITEMS;
      nodes = (hlink *) malloc(max_node * sizeof (hlink));
      nb_node = 0;
      table_node = New(alist(5, owned_by_alist));
      table_node->append(nodes);
   };

   hlink *get_hlink() {
      if (++nb_node >= max_node) {
         nb_node = 0;
         nodes = (hlink *)malloc(max_node * sizeof(hlink));
         table_node->append(nodes);
      }
      return nodes + nb_node;
   };

   bool lookup(char *pathid) {
      bool ret = cache_ppathid->lookup(pathid) != NULL;
      return ret;
   };

   void insert(char *pathid) {
      hlink *h = get_hlink();
      cache_ppathid->insert(pathid, h);
   };

   ~pathid_cache() {
      cache_ppathid->destroy();
      free(cache_ppathid);
      delete table_node;
   };
private:
   pathid_cache(const pathid_cache &); /* prohibit pass by value */
   pathid_cache &operator= (const pathid_cache &);/* prohibit class assignment*/
} ;

/* Return the parent_dir with the trailing /  (update the given string)
 * TODO: see in the rest of bacula if we don't have already this function
 * dir=/tmp/toto/
 * dir=/tmp/
 * dir=/
 * dir=
 */
char *bvfs_parent_dir(char *path)
{
   char *p = path;
   int len = strlen(path) - 1;

   /* windows directory / */
   if (len == 2 && B_ISALPHA(path[0])
                && path[1] == ':'
                && path[2] == '/')
   {
      len = 0;
      path[0] = '\0';
   }

   if (len >= 0 && path[len] == '/') {      /* if directory, skip last / */
      path[len] = '\0';
   }

   if (len > 0) {
      p += len;
      while (p > path && !IsPathSeparator(*p)) {
         p--;
      }
      p[1] = '\0';
   }
   return path;
}

/* Return the basename of the with the trailing /
 * TODO: see in the rest of bacula if we don't have
 * this function already
 */
char *bvfs_basename_dir(char *path)
{
   char *p = path;
   int len = strlen(path) - 1;

   if (path[len] == '/') {      /* if directory, skip last / */
      len -= 1;
   }

   if (len > 0) {
      p += len;
      while (p > path && !IsPathSeparator(*p)) {
         p--;
      }
      if (*p == '/') {
         p++;                  /* skip first / */
      }
   }
   return p;
}

static void build_path_hierarchy(JCR *jcr, BDB *mdb,
                                 pathid_cache &ppathid_cache,
                                 char *org_pathid, char *path)
{
   Dmsg1(dbglevel, "build_path_hierarchy(%s)\n", path);
   char pathid[50];
   ATTR_DBR parent;
   char *bkp = mdb->path;
   strncpy(pathid, org_pathid, sizeof(pathid));

   /* Does the ppathid exist for this ? we use a memory cache...  In order to
    * avoid the full loop, we consider that if a dir is allready in the
    * PathHierarchy table, then there is no need to calculate all the
    * hierarchy
    */
   while (path && *path)
   {
      if (!ppathid_cache.lookup(pathid))
      {
         Mmsg(mdb->cmd,
              "SELECT PPathId FROM PathHierarchy WHERE PathId = %s",
              pathid);

         if (!mdb->QueryDB(jcr, mdb->cmd)) {
            goto bail_out;      /* Query failed, just leave */
         }

         /* Do we have a result ? */
         if (mdb->sql_num_rows() > 0) {
            ppathid_cache.insert(pathid);
            /* This dir was in the db ...
             * It means we can leave, the tree has allready been built for
             * this dir
             */
            goto bail_out;
         } else {
            /* search or create parent PathId in Path table */
            mdb->path = bvfs_parent_dir(path);
            mdb->pnl = strlen(mdb->path);
            if (!mdb->bdb_create_path_record(jcr, &parent)) {
               goto bail_out;
            }
            ppathid_cache.insert(pathid);

            Mmsg(mdb->cmd,
                 "INSERT INTO PathHierarchy (PathId, PPathId) "
                 "VALUES (%s,%lld)",
                 pathid, (uint64_t) parent.PathId);

            if (!mdb->InsertDB(jcr, mdb->cmd)) {
               goto bail_out;   /* Can't insert the record, just leave */
            }

            edit_uint64(parent.PathId, pathid);
            path = mdb->path;   /* already done */
         }
      } else {
         /* It's already in the cache.  We can leave, no time to waste here,
          * all the parent dirs have allready been done
          */
         goto bail_out;
      }
   }

bail_out:
   mdb->path = bkp;
   mdb->fnl = 0;
}

/*
 * Internal function to update path_hierarchy cache with a shared pathid cache
 * return Error 0
 *        OK    1
 */
static int update_path_hierarchy_cache(JCR *jcr,
                                        BDB *mdb,
                                        pathid_cache &ppathid_cache,
                                        JobId_t JobId)
{
   Dmsg0(dbglevel, "update_path_hierarchy_cache()\n");
   uint32_t ret=0;
   uint32_t num;
   char jobid[50];
   edit_uint64(JobId, jobid);

   mdb->bdb_lock();

   /* We don't really want to harm users with spurious messages,
    * everything is handled by transaction
    */
   mdb->set_use_fatal_jmsg(false);

   mdb->bdb_start_transaction(jcr);

   Mmsg(mdb->cmd, "SELECT 1 FROM Job WHERE JobId = %s AND HasCache=1", jobid);

   if (!mdb->QueryDB(jcr, mdb->cmd) || mdb->sql_num_rows() > 0) {
      Dmsg1(dbglevel, "already computed %d\n", (uint32_t)JobId );
      ret = 1;
      goto bail_out;
   }

   /* Inserting path records for JobId */
   Mmsg(mdb->cmd, "INSERT INTO PathVisibility (PathId, JobId) "
                   "SELECT DISTINCT PathId, JobId "
                     "FROM (SELECT PathId, JobId FROM File WHERE JobId = %s AND FileIndex > 0 "
                           "UNION "
                           "SELECT PathId, BaseFiles.JobId "
                             "FROM BaseFiles JOIN File AS F USING (FileId) "
                            "WHERE BaseFiles.JobId = %s) AS B",
        jobid, jobid);

   if (!mdb->QueryDB(jcr, mdb->cmd)) {
      Dmsg1(dbglevel, "Can't fill PathVisibility %d\n", (uint32_t)JobId );
      goto bail_out;
   }

   /* Now we have to do the directory recursion stuff to determine missing
    * visibility We try to avoid recursion, to be as fast as possible We also
    * only work on not allready hierarchised directories...
    */
   Mmsg(mdb->cmd,
     "SELECT PathVisibility.PathId, Path "
       "FROM PathVisibility "
            "JOIN Path ON( PathVisibility.PathId = Path.PathId) "
            "LEFT JOIN PathHierarchy "
         "ON (PathVisibility.PathId = PathHierarchy.PathId) "
      "WHERE PathVisibility.JobId = %s "
        "AND PathHierarchy.PathId IS NULL "
      "ORDER BY Path", jobid);
   Dmsg1(dbglevel_sql, "q=%s\n", mdb->cmd);

   if (!mdb->QueryDB(jcr, mdb->cmd)) {
      Dmsg1(dbglevel, "Can't get new Path %d\n", (uint32_t)JobId );
      goto bail_out;
   }

   /* TODO: I need to reuse the DB connection without emptying the result
    * So, now i'm copying the result in memory to be able to query the
    * catalog descriptor again.
    */
   num = mdb->sql_num_rows();
   if (num > 0) {
      char **result = (char **)malloc (num * 2 * sizeof(char *));

      SQL_ROW row;
      int i=0;
      while((row = mdb->sql_fetch_row())) {
         result[i++] = bstrdup(row[0]);
         result[i++] = bstrdup(row[1]);
      }

      i=0;
      while (num > 0) {
         build_path_hierarchy(jcr, mdb, ppathid_cache, result[i], result[i+1]);
         free(result[i++]);
         free(result[i++]);
         num--;
      }
      free(result);
   }

   if (mdb->bdb_get_type_index() == SQL_TYPE_SQLITE3) {
      Mmsg(mdb->cmd,
 "INSERT INTO PathVisibility (PathId, JobId) "
   "SELECT DISTINCT h.PPathId AS PathId, %s "
     "FROM PathHierarchy AS h "
    "WHERE h.PathId IN (SELECT PathId FROM PathVisibility WHERE JobId=%s) "
      "AND h.PPathId NOT IN (SELECT PathId FROM PathVisibility WHERE JobId=%s)",
           jobid, jobid, jobid );

   } else if (mdb->bdb_get_type_index() == SQL_TYPE_MYSQL) {
      Mmsg(mdb->cmd,
  "INSERT INTO PathVisibility (PathId, JobId)  "
   "SELECT a.PathId,%s "
   "FROM ( "
     "SELECT DISTINCT h.PPathId AS PathId "
       "FROM PathHierarchy AS h "
       "JOIN  PathVisibility AS p ON (h.PathId=p.PathId) "
      "WHERE p.JobId=%s) AS a "
      "LEFT JOIN PathVisibility AS b ON (b.JobId=%s and a.PathId = b.PathId) "
      "WHERE b.PathId IS NULL",  jobid, jobid, jobid);

   } else {
      Mmsg(mdb->cmd,
  "INSERT INTO PathVisibility (PathId, JobId)  "
   "SELECT a.PathId,%s "
   "FROM ( "
     "SELECT DISTINCT h.PPathId AS PathId "
       "FROM PathHierarchy AS h "
       "JOIN  PathVisibility AS p ON (h.PathId=p.PathId) "
      "WHERE p.JobId=%s) AS a LEFT JOIN "
       "(SELECT PathId "
          "FROM PathVisibility "
         "WHERE JobId=%s) AS b ON (a.PathId = b.PathId) "
   "WHERE b.PathId IS NULL",  jobid, jobid, jobid);
   }

   do {
      ret = mdb->QueryDB(jcr, mdb->cmd);
   } while (ret && mdb->sql_affected_rows() > 0);

   Mmsg(mdb->cmd, "UPDATE Job SET HasCache=1 WHERE JobId=%s", jobid);
   ret = mdb->UpdateDB(jcr, mdb->cmd, false);

bail_out:
   mdb->bdb_end_transaction(jcr);

   if (!ret) {
      Mmsg(mdb->cmd, "SELECT HasCache FROM Job WHERE JobId=%s", jobid);
      mdb->bdb_sql_query(mdb->cmd, db_int_handler, &ret);
   }

   /* Enable back the FATAL message if something is wrong */
   mdb->set_use_fatal_jmsg(true);

   mdb->bdb_unlock();
   return ret;
}

/* Compute the cache for the bfileview compoment */
void Bvfs::fv_update_cache()
{
   int64_t pathid;
   int64_t size=0, count=0;

   Dmsg0(dbglevel, "fv_update_cache()\n");

   if (!*jobids) {
      return;                   /* Nothing to build */
   }

   db->bdb_lock();
   /* We don't really want to harm users with spurious messages,
    * everything is handled by transaction
    */
   db->set_use_fatal_jmsg(false);

   db->bdb_start_transaction(jcr);

   pathid = get_root();

   fv_compute_size_and_count(pathid, &size, &count);

   db->bdb_end_transaction(jcr);

   /* Enable back the FATAL message if something is wrong */
   db->set_use_fatal_jmsg(true);

   db->bdb_unlock();
}

/* 
 * Find an store the filename descriptor for empty directories Filename.Name=''
 */
DBId_t Bvfs::get_dir_filenameid()
{
   uint32_t id;
   if (dir_filenameid) {
      return dir_filenameid;
   }
   Mmsg(db->cmd, "SELECT FilenameId FROM Filename WHERE Name = ''");
   db_sql_query(db, db->cmd, db_int_handler, &id);
   dir_filenameid = id;
   return dir_filenameid;
}

/* Not yet working */
void Bvfs::fv_get_big_files(int64_t pathid, int64_t min_size, int32_t limit)
{
   Mmsg(db->cmd, 
        "SELECT FilenameId AS filenameid, Name AS name, size "
          "FROM ( "
         "SELECT FilenameId, base64_decode_lstat(8,LStat) AS size "
           "FROM File "
          "WHERE PathId  = %lld "
            "AND JobId = %s "
        ") AS S INNER JOIN Filename USING (FilenameId) "
     "WHERE S.size > %lld "
     "ORDER BY S.size DESC "
     "LIMIT %d ", pathid, jobids, min_size, limit);
}


/* Get the current path size and files count */
void Bvfs::fv_get_current_size_and_count(int64_t pathid, int64_t *size, int64_t *count)
{
   SQL_ROW row;

   *size = *count = 0;

   Mmsg(db->cmd,
 "SELECT Size AS size, Files AS files "
  " FROM PathVisibility "
 " WHERE PathId = %lld "
   " AND JobId = %s ", pathid, jobids);

   if (!db->QueryDB(jcr, db->cmd)) {
      return;
   }

   if ((row = db->sql_fetch_row())) {
      *size = str_to_int64(row[0]);
      *count =  str_to_int64(row[1]);
   }
}

/* Compute for the current path the size and files count */
void Bvfs::fv_get_size_and_count(int64_t pathid, int64_t *size, int64_t *count)
{
   SQL_ROW row;

   *size = *count = 0;

   Mmsg(db->cmd,
 "SELECT sum(base64_decode_lstat(8,LStat)) AS size, count(1) AS files "
  " FROM File "
 " WHERE PathId = %lld "
   " AND JobId = %s ", pathid, jobids);

   if (!db->QueryDB(jcr, db->cmd)) {
      return;
   }

   if ((row = db->sql_fetch_row())) {
      *size = str_to_int64(row[0]);
      *count =  str_to_int64(row[1]);
   }
}

void Bvfs::fv_compute_size_and_count(int64_t pathid, int64_t *size, int64_t *count)
{
   Dmsg1(dbglevel, "fv_compute_size_and_count(%lld)\n", pathid);

   fv_get_current_size_and_count(pathid, size, count);
   if (*size > 0) {
      return;
   }

   /* Update stats for the current directory */
   fv_get_size_and_count(pathid, size, count);

   /* Update stats for all sub directories */
   Mmsg(db->cmd,
        " SELECT PathId "
          " FROM PathVisibility "
               " INNER JOIN PathHierarchy USING (PathId) "
         " WHERE PPathId  = %lld "
           " AND JobId = %s ", pathid, jobids);

   db->QueryDB(jcr, db->cmd);
   int num = db->sql_num_rows();

   if (num > 0) {
      int64_t *result = (int64_t *)malloc (num * sizeof(int64_t));
      SQL_ROW row;
      int i=0;

      while((row = db->sql_fetch_row())) {
         result[i++] = str_to_int64(row[0]); /* PathId */
      }

      i=0;
      while (num > 0) {
         int64_t c=0, s=0;
         fv_compute_size_and_count(result[i], &s, &c);
         *size += s;
         *count += c;

         i++;
         num--;
      }
      free(result);
   }

   fv_update_size_and_count(pathid, *size, *count);
}

void Bvfs::fv_update_size_and_count(int64_t pathid, int64_t size, int64_t count)
{
   Mmsg(db->cmd,
        "UPDATE PathVisibility SET Files = %lld, Size = %lld "
        " WHERE JobId = %s "
        " AND PathId = %lld ", count, size, jobids, pathid);

   db->UpdateDB(jcr, db->cmd, false);
}

void bvfs_update_cache(JCR *jcr, BDB *mdb)
{
   uint32_t nb=0;
   db_list_ctx jobids_list;

   mdb->bdb_lock();

#ifdef xxx
   /* TODO: Remove this code when updating make_bacula_table script */
   Mmsg(mdb->cmd, "SELECT 1 FROM Job WHERE HasCache<>2 LIMIT 1");
   if (!mdb->QueryDB(jcr, mdb->cmd)) {
      Dmsg0(dbglevel, "Creating cache table\n");
      Mmsg(mdb->cmd, "ALTER TABLE Job ADD HasCache int DEFAULT 0");
      mdb->QueryDB(jcr, mdb->cmd);

      Mmsg(mdb->cmd,
           "CREATE TABLE PathHierarchy ( "
           "PathId integer NOT NULL, "
           "PPathId integer NOT NULL, "
           "CONSTRAINT pathhierarchy_pkey "
           "PRIMARY KEY (PathId))");
      mdb->QueryDB(jcr, mdb->cmd);

      Mmsg(mdb->cmd,
           "CREATE INDEX pathhierarchy_ppathid "
           "ON PathHierarchy (PPathId)");
      mdb->QueryDB(jcr, mdb->cmd);

      Mmsg(mdb->cmd,
           "CREATE TABLE PathVisibility ("
           "PathId integer NOT NULL, "
           "JobId integer NOT NULL, "
           "Size int8 DEFAULT 0, "
           "Files int4 DEFAULT 0, "
           "CONSTRAINT pathvisibility_pkey "
           "PRIMARY KEY (JobId, PathId))");
      mdb->QueryDB(jcr, mdb->cmd);

      Mmsg(mdb->cmd,
           "CREATE INDEX pathvisibility_jobid "
           "ON PathVisibility (JobId)");
      mdb->QueryDB(jcr, mdb->cmd);

   }
#endif

   Mmsg(mdb->cmd,
 "SELECT JobId from Job "
  "WHERE HasCache = 0 "
    "AND Type IN ('B') AND JobStatus IN ('T', 'f', 'A') "
  "ORDER BY JobId");

   mdb->bdb_sql_query(mdb->cmd, db_list_handler, &jobids_list);

   bvfs_update_path_hierarchy_cache(jcr, mdb, jobids_list.list);

   mdb->bdb_start_transaction(jcr);
   Dmsg0(dbglevel, "Cleaning pathvisibility\n");
   Mmsg(mdb->cmd,
        "DELETE FROM PathVisibility "
         "WHERE NOT EXISTS "
        "(SELECT 1 FROM Job WHERE JobId=PathVisibility.JobId)");
   nb = mdb->DeleteDB(jcr, mdb->cmd);
   Dmsg1(dbglevel, "Affected row(s) = %d\n", nb);

   mdb->bdb_end_transaction(jcr);
   mdb->bdb_unlock();
}

/*
 * Update the bvfs cache for given jobids (1,2,3,4)
 */
int
bvfs_update_path_hierarchy_cache(JCR *jcr, BDB *mdb, char *jobids)
{
   pathid_cache ppathid_cache;
   JobId_t JobId;
   char *p;
   int ret=1;

   for (p=jobids; ; ) {
      int stat = get_next_jobid_from_list(&p, &JobId);
      if (stat < 0) {
         ret = 0;
         break;
      }
      if (stat == 0) {
         break;
      }
      Dmsg1(dbglevel, "Updating cache for %lld\n", (uint64_t)JobId);
      if (!update_path_hierarchy_cache(jcr, mdb, ppathid_cache, JobId)) {
         ret = 0;
      }
   }
   return ret;
}

/*
 * Update the bvfs fileview for given jobids
 */
void
bvfs_update_fv_cache(JCR *jcr, BDB *mdb, char *jobids)
{
   char *p;
   JobId_t JobId;
   Bvfs bvfs(jcr, mdb);

   for (p=jobids; ; ) {
      int stat = get_next_jobid_from_list(&p, &JobId);
      if (stat < 0) {
         return;
      }
      if (stat == 0) {
         break;
      }

      Dmsg1(dbglevel, "Trying to create cache for %lld\n", (int64_t)JobId);

      bvfs.set_jobid(JobId);
      bvfs.fv_update_cache();
   }
}

/*
 * Update the bvfs cache for current jobids
 */
void Bvfs::update_cache()
{
   bvfs_update_path_hierarchy_cache(jcr, db, jobids);
}


bool Bvfs::ch_dir(DBId_t pathid)
{
   reset_offset();

   pwd_id = pathid;
   return pwd_id != 0;
}


/* Change the current directory, returns true if the path exists */
bool Bvfs::ch_dir(const char *path)
{
   db->bdb_lock();
   pm_strcpy(db->path, path);
   db->pnl = strlen(db->path);
   ch_dir(db->bdb_get_path_record(jcr));
   db->bdb_unlock();
   return pwd_id != 0;
}

/*
 * Get all file versions for a specified list of clients
 * TODO: Handle basejobs using different client
 */
void Bvfs::get_all_file_versions(DBId_t pathid, FileId_t fnid, alist *clients)
{
   char ed1[50], ed2[50], *eclients;
   POOL_MEM q, query;

   if (see_copies) {
      Mmsg(q, " AND Job.Type IN ('C', 'B') ");
   } else {
      Mmsg(q, " AND Job.Type = 'B' ");
   }

   eclients = escape_list(clients);

   Dmsg3(dbglevel, "get_all_file_versions(%lld, %lld, %s)\n", (uint64_t)pathid,
         (uint64_t)fnid, eclients);

   Mmsg(query,//    1           2           3      4 
"SELECT 'V', File.PathId, File.FilenameId,  0, File.JobId, "
//            5           6          7
        "File.LStat, File.FileId, File.Md5, "
//         8                    9
        "Media.VolumeName, Media.InChanger "
"FROM File, Job, Client, JobMedia, Media "
"WHERE File.FilenameId = %s "
  "AND File.PathId=%s "
  "AND File.JobId = Job.JobId "
  "AND Job.JobId = JobMedia.JobId "
  "AND File.FileIndex >= JobMedia.FirstIndex "
  "AND File.FileIndex <= JobMedia.LastIndex "
  "AND JobMedia.MediaId = Media.MediaId "
  "AND Job.ClientId = Client.ClientId "
  "AND Client.Name IN (%s) "
  "%s ORDER BY FileId LIMIT %d OFFSET %d"
        ,edit_uint64(fnid, ed1), edit_uint64(pathid, ed2), eclients, q.c_str(),
        limit, offset);
   Dmsg1(dbglevel_sql, "q=%s\n", query.c_str());
   db->bdb_sql_query(query.c_str(), list_entries, user_data);
}

/*
 * Get all file versions for a specified client
 * TODO: Handle basejobs using different client
 */
bool Bvfs::get_delta(FileId_t fileid)
{
   Dmsg1(dbglevel, "get_delta(%lld)\n", (uint64_t)fileid);
   char ed1[50];
   int32_t num;
   SQL_ROW row;
   POOL_MEM q;
   POOL_MEM query;
   char *fn = NULL;
   bool ret = false;
   db->bdb_lock();

   /* Check if some FileId have DeltaSeq > 0
    * Foreach of them we need to get the accurate_job list, and compute
    * what are dependencies
    */
   Mmsg(query,
        "SELECT F.JobId, FN.Name, F.PathId, F.DeltaSeq "
        "FROM File AS F, Filename AS FN WHERE FileId = %lld "
        "AND FN.FilenameId = F.FilenameId AND DeltaSeq > 0", fileid);

   if (!db->QueryDB(jcr, query.c_str())) {
      Dmsg1(dbglevel_sql, "Can't execute query=%s\n", query.c_str());
      goto bail_out;
   }

   /* TODO: Use an other DB connection can avoid to copy the result of the
    * previous query into a temporary buffer
    */
   num = db->sql_num_rows();
   Dmsg2(dbglevel, "Found %d Delta parts q=%s\n",
         num, query.c_str());

   if (num > 0 && (row = db->sql_fetch_row())) {
      JOB_DBR jr, jr2;
      db_list_ctx lst;
      memset(&jr, 0, sizeof(jr));
      memset(&jr2, 0, sizeof(jr2));

      fn = bstrdup(row[1]);               /* Filename */
      int64_t jid = str_to_int64(row[0]);     /* JobId */
      int64_t pid = str_to_int64(row[2]);     /* PathId */

      /* Need to limit the query to StartTime, Client/Fileset */
      jr2.JobId = jid;
      if (!db->bdb_get_job_record(jcr, &jr2)) {
         Dmsg1(0, "Unable to get job record for jobid %d\n", jid);
         goto bail_out;
      }

      jr.JobId = jid;
      jr.ClientId = jr2.ClientId;
      jr.FileSetId = jr2.FileSetId;
      jr.JobLevel = L_INCREMENTAL;
      jr.StartTime = jr2.StartTime;

      /* Get accurate jobid list */
      if (!db->bdb_get_accurate_jobids(jcr, &jr, &lst)) {
         Dmsg1(0, "Unable to get Accurate list for jobid %d\n", jid);
         goto bail_out;
      }

      /* Escape filename */
      db->fnl = strlen(fn);
      db->esc_name = check_pool_memory_size(db->esc_name, 2*db->fnl+2);
      db->bdb_escape_string(jcr, db->esc_name, fn, db->fnl);

      edit_int64(pid, ed1);     /* pathid */

      int id=db->bdb_get_type_index();
      Mmsg(query, bvfs_select_delta_version_with_basejob_and_delta[id],
           lst.list, db->esc_name, ed1,
           lst.list, db->esc_name, ed1,
           lst.list, lst.list);

      Mmsg(db->cmd,
           //       0     1     2   3      4      5      6            7
           "SELECT 'd', PathId, 0, JobId, LStat, FileId, DeltaSeq, JobTDate"
           " FROM (%s) AS F1 "
           "ORDER BY DeltaSeq ASC",
           query.c_str());

      Dmsg1(dbglevel_sql, "q=%s\n", db->cmd);

      if (!db->bdb_sql_query(db->cmd, list_entries, user_data)) {
         Dmsg1(dbglevel_sql, "Can't exec q=%s\n", db->cmd);
         goto bail_out;
      }
   }
   ret = true;
bail_out:
   if (fn) {
      free(fn);
   }
   db->bdb_unlock();
   return ret;
}

/*
 * Get all volumes for a specific file
 */
void Bvfs::get_volumes(FileId_t fileid)
{
   Dmsg1(dbglevel, "get_volumes(%lld)\n", (uint64_t)fileid);

   char ed1[50];
   POOL_MEM query;

   Mmsg(query,
//                                   7                8
"SELECT DISTINCT 'L',0,0,0,0,0,0, Media.VolumeName, Media.InChanger "
"FROM File JOIN JobMedia USING (JobId) JOIN Media USING (MediaId) "
"WHERE File.FileId = %s "
  "AND File.FileIndex >= JobMedia.FirstIndex "
  "AND File.FileIndex <= JobMedia.LastIndex "
  " LIMIT %d OFFSET %d"
        ,edit_uint64(fileid, ed1), limit, offset);
   Dmsg1(dbglevel_sql, "q=%s\n", query.c_str());
   db->bdb_sql_query(query.c_str(), list_entries, user_data);
}

DBId_t Bvfs::get_root()
{
   int p;
   *db->path = 0;
   db->bdb_lock();
   p = db->bdb_get_path_record(jcr);
   db->bdb_unlock();
   return p;
}

static int path_handler(void *ctx, int fields, char **row)
{
   Bvfs *fs = (Bvfs *) ctx;
   return fs->_handle_path(ctx, fields, row);
}

int Bvfs::_handle_path(void *ctx, int fields, char **row)
{
   if (bvfs_is_dir(row)) {
      /* can have the same path 2 times */
      if (strcmp(row[BVFS_PathId], prev_dir)) {
         pm_strcpy(prev_dir, row[BVFS_PathId]);
         if (strcmp(NPRTB(row[BVFS_FileIndex]), "0") == 0 &&
             strcmp(NPRTB(row[BVFS_FileId]), "0") != 0)
         {
            /* The directory was probably deleted */
            return 0;
         }
         return list_entries(user_data, fields, row);
      }
   }
   return 0;
}

/*
 * Retrieve . and .. information
 */
void Bvfs::ls_special_dirs()
{
   Dmsg1(dbglevel, "ls_special_dirs(%lld)\n", (uint64_t)pwd_id);
   char ed1[50], ed2[50];
   if (*jobids == 0) {
      return;
   }
   if (!dir_filenameid) {
      get_dir_filenameid();
   }

   /* Will fetch directories  */
   *prev_dir = 0;

   POOL_MEM query;
   Mmsg(query,
"(SELECT PathHierarchy.PPathId AS PathId, '..' AS Path "
    "FROM  PathHierarchy JOIN PathVisibility USING (PathId) "
   "WHERE  PathHierarchy.PathId = %s "
   "AND PathVisibility.JobId IN (%s) "
"UNION "
 "SELECT %s AS PathId, '.' AS Path)",
        edit_uint64(pwd_id, ed1), jobids, ed1);

   POOL_MEM query2;
   Mmsg(query2,// 1      2     3        4     5       6
"SELECT 'D', tmp.PathId, 0, tmp.Path, JobId, LStat, FileId, FileIndex "
  "FROM %s AS tmp  LEFT JOIN ( " // get attributes if any
       "SELECT File1.PathId AS PathId, File1.JobId AS JobId, "
              "File1.LStat AS LStat, File1.FileId AS FileId, "
              "File1.FileIndex AS FileIndex, "
              "Job1.JobTDate AS JobTDate "
      "FROM File AS File1 JOIN Job AS Job1 USING (JobId)"
       "WHERE File1.FilenameId = %s "
       "AND File1.JobId IN (%s)) AS listfile1 "
  "ON (tmp.PathId = listfile1.PathId) "
  "ORDER BY tmp.Path, JobTDate DESC ",
        query.c_str(), edit_uint64(dir_filenameid, ed2), jobids);

   Dmsg1(dbglevel_sql, "q=%s\n", query2.c_str());
   db->bdb_sql_query(query2.c_str(), path_handler, this);
}

/* Returns true if we have dirs to read */
bool Bvfs::ls_dirs()
{
   Dmsg1(dbglevel, "ls_dirs(%lld)\n", (uint64_t)pwd_id);
   char ed1[50], ed2[50];
   if (*jobids == 0) {
      return false;
   }

   POOL_MEM query;
   POOL_MEM filter;
   if (*pattern) {
      Mmsg(filter, " AND Path2.Path %s '%s' ",
           match_query[db->bdb_get_type_index()], pattern);

   }

   if (!dir_filenameid) {
      get_dir_filenameid();
   }

   /* the sql query displays same directory multiple time, take the first one */
   *prev_dir = 0;

   /* Let's retrieve the list of the visible dirs in this dir ...
    * First, I need the empty filenameid to locate efficiently
    * the dirs in the file table
    * my $dir_filenameid = $self->get_dir_filenameid();
    */
   /* Then we get all the dir entries from File ... */
   Mmsg(query,
//       0     1      2      3      4     5       6
"SELECT 'D', PathId,  0,    Path, JobId, LStat, FileId, FileIndex FROM ( "
    "SELECT Path1.PathId AS PathId, Path1.Path AS Path, "
           "lower(Path1.Path) AS lpath, "
           "listfile1.JobId AS JobId, listfile1.LStat AS LStat, "
           "listfile1.FileId AS FileId, "
           "listfile1.JobTDate AS JobTDate, "
           "listfile1.FileIndex AS FileIndex "
    "FROM ( "
      "SELECT DISTINCT PathHierarchy1.PathId AS PathId "
      "FROM PathHierarchy AS PathHierarchy1 "
      "JOIN Path AS Path2 "
        "ON (PathHierarchy1.PathId = Path2.PathId) "
      "JOIN PathVisibility AS PathVisibility1 "
        "ON (PathHierarchy1.PathId = PathVisibility1.PathId) "
      "WHERE PathHierarchy1.PPathId = %s "
      "AND PathVisibility1.JobId IN (%s) "
           "%s "
     ") AS listpath1 "
   "JOIN Path AS Path1 ON (listpath1.PathId = Path1.PathId) "

   "LEFT JOIN ( " /* get attributes if any */
       "SELECT File1.PathId AS PathId, File1.JobId AS JobId, "
              "File1.LStat AS LStat, File1.FileId AS FileId, "
              "File1.FileIndex, Job1.JobTDate AS JobTDate "
     "FROM File AS File1 JOIN Job AS Job1 USING (JobId) "
       "WHERE File1.FilenameId = %s "
       "AND File1.JobId IN (%s)) AS listfile1 "
       "ON (listpath1.PathId = listfile1.PathId) "
    ") AS A ORDER BY Path,JobTDate DESC LIMIT %d OFFSET %d",
        edit_uint64(pwd_id, ed1),
        jobids,
        filter.c_str(),
        edit_uint64(dir_filenameid, ed2),
        jobids,
        limit, offset);

   Dmsg1(dbglevel_sql, "q=%s\n", query.c_str());

   db->bdb_lock();
   db->bdb_sql_query(query.c_str(), path_handler, this);
   nb_record = db->sql_num_rows();
   db->bdb_unlock();

   return nb_record == limit;
}

void build_ls_files_query(BDB *db, POOL_MEM &query,
                          const char *JobId, const char *PathId,
                          const char *filter, int64_t limit, int64_t offset)
{
   if (db->bdb_get_type_index() == SQL_TYPE_POSTGRESQL) {
      Mmsg(query, sql_bvfs_list_files[db->bdb_get_type_index()],
           JobId, PathId, JobId, PathId,
           filter, limit, offset);
   } else {
      Mmsg(query, sql_bvfs_list_files[db->bdb_get_type_index()],
           JobId, PathId, JobId, PathId,
           limit, offset, filter, JobId, JobId);
   }
}

/* Returns true if we have files to read */
bool Bvfs::ls_files()
{
   POOL_MEM query;
   POOL_MEM filter;
   char pathid[50];

   Dmsg1(dbglevel, "ls_files(%lld)\n", (uint64_t)pwd_id);
   if (*jobids == 0) {
      return false;
   }

   if (!pwd_id) {
      ch_dir(get_root());
   }

   edit_uint64(pwd_id, pathid);
   if (*pattern) {
      Mmsg(filter, " AND Filename.Name %s '%s' ", 
           match_query[db_get_type_index(db)], pattern);

   } else if (*filename) {
      Mmsg(filter, " AND Filename.Name = '%s' ", filename);
   }

   build_ls_files_query(db, query,
                        jobids, pathid, filter.c_str(),
                        limit, offset);

   Dmsg1(dbglevel_sql, "q=%s\n", query.c_str());

   db->bdb_lock();
   db->bdb_sql_query(query.c_str(), list_entries, user_data);
   nb_record = db->sql_num_rows();
   db->bdb_unlock();

   return nb_record == limit;
}


/*
 * Return next Id from comma separated list
 *
 * Returns:
 *   1 if next Id returned
 *   0 if no more Ids are in list
 *  -1 there is an error
 * TODO: merge with get_next_jobid_from_list() and get_next_dbid_from_list()
 */
static int get_next_id_from_list(char **p, int64_t *Id)
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
   *Id = str_to_int64(id);
   return 1;
}

static int get_path_handler(void *ctx, int fields, char **row)
{
   POOL_MEM *buf = (POOL_MEM *) ctx;
   pm_strcpy(*buf, row[0]);
   return 0;
}

static bool check_temp(char *output_table)
{
   if (output_table[0] == 'b' &&
       output_table[1] == '2' &&
       is_an_integer(output_table + 2))
   {
      return true;
   }
   return false;
}

void Bvfs::clear_cache()
{
   db->bdb_sql_query("BEGIN",                     NULL, NULL);
   db->bdb_sql_query("UPDATE Job SET HasCache=0", NULL, NULL);
   db->bdb_sql_query("TRUNCATE PathHierarchy",    NULL, NULL);
   db->bdb_sql_query("TRUNCATE PathVisibility",   NULL, NULL);
   db->bdb_sql_query("COMMIT",                    NULL, NULL);
}

bool Bvfs::drop_restore_list(char *output_table)
{
   POOL_MEM query;
   if (check_temp(output_table)) {
      Mmsg(query, "DROP TABLE %s", output_table);
      db->bdb_sql_query(query.c_str(), NULL, NULL);
      return true;
   }
   return false;
}

bool Bvfs::compute_restore_list(char *fileid, char *dirid, char *hardlink,
                                char *output_table)
{
   POOL_MEM query;
   POOL_MEM tmp, tmp2;
   int64_t id, jobid, prev_jobid;
   int num;
   bool init=false;
   bool ret=false;
   /* check args */
   if ((*fileid   && !is_a_number_list(fileid))  ||
       (*dirid    && !is_a_number_list(dirid))   ||
       (*hardlink && !is_a_number_list(hardlink))||
       (!*hardlink && !*fileid && !*dirid && !*hardlink))
   {
      Dmsg0(dbglevel, "ERROR: One or more of FileId, DirId or HardLink is not given or not a number.\n");
      return false;
   }
   if (!check_temp(output_table)) {
      Dmsg0(dbglevel, "ERROR: Wrong format for table name (in path field).\n");
      return false;
   }

   db->bdb_lock();

   /* Cleanup old tables first */
   Mmsg(query, "DROP TABLE btemp%s", output_table);
   db->bdb_sql_query(query.c_str());

   Mmsg(query, "DROP TABLE %s", output_table);
   db->bdb_sql_query(query.c_str());

   Mmsg(query, "CREATE TABLE btemp%s AS ", output_table);

   if (*fileid) {               /* Select files with their direct id */
      init=true;
      Mmsg(tmp,"SELECT Job.JobId, JobTDate, FileIndex, FilenameId, "
                      "PathId, FileId "
                 "FROM File,Job WHERE Job.JobId=File.Jobid "
                 "AND FileId IN (%s)",
           fileid);
      pm_strcat(query, tmp.c_str());
   }

   /* Add a directory content */
   while (get_next_id_from_list(&dirid, &id) == 1) {
      Mmsg(tmp, "SELECT Path FROM Path WHERE PathId=%lld", id);

      if (!db->bdb_sql_query(tmp.c_str(), get_path_handler, (void *)&tmp2)) {
         Dmsg3(dbglevel, "ERROR: Path not found %lld q=%s s=%s\n",
               id, tmp.c_str(), tmp2.c_str());
         /* print error */
         goto bail_out;
      }
      if (!strcmp(tmp2.c_str(), "")) { /* path not found */
         Dmsg3(dbglevel, "ERROR: Path not found %lld q=%s s=%s\n",
               id, tmp.c_str(), tmp2.c_str());
         break;
      }
      /* escape % and _ for LIKE search */
      tmp.check_size((strlen(tmp2.c_str())+1) * 2);
      char *p = tmp.c_str();
      for (char *s = tmp2.c_str(); *s ; s++) {
         if (*s == '%' || *s == '_' || *s == '\\') {
            *p = '\\';
            p++;
         }
         *p = *s;
         p++;
      }
      *p = '\0';
      tmp.strcat("%");

      size_t len = strlen(tmp.c_str());
      tmp2.check_size((len+1) * 2);
      db->bdb_escape_string(jcr, tmp2.c_str(), tmp.c_str(), len);

      if (init) {
         query.strcat(" UNION ");
      }

      Mmsg(tmp, "SELECT Job.JobId, JobTDate, File.FileIndex, File.FilenameId, "
                        "File.PathId, FileId "
                   "FROM Path JOIN File USING (PathId) JOIN Job USING (JobId) "
                  "WHERE Path.Path LIKE '%s' ESCAPE '%s' AND File.JobId IN (%s) ",
           tmp2.c_str(), escape_char_value[db->bdb_get_type_index()], jobids);
      query.strcat(tmp.c_str());
      init = true;

      query.strcat(" UNION ");

      /* A directory can have files from a BaseJob */
      Mmsg(tmp, "SELECT File.JobId, JobTDate, BaseFiles.FileIndex, "
                        "File.FilenameId, File.PathId, BaseFiles.FileId "
                   "FROM BaseFiles "
                        "JOIN File USING (FileId) "
                        "JOIN Job ON (BaseFiles.JobId = Job.JobId) "
                        "JOIN Path USING (PathId) "
                  "WHERE Path.Path LIKE '%s' AND BaseFiles.JobId IN (%s) ",
           tmp2.c_str(), jobids);
      query.strcat(tmp.c_str());
   }

   /* expect jobid,fileindex */
   prev_jobid=0;
   while (get_next_id_from_list(&hardlink, &jobid) == 1) {
      if (get_next_id_from_list(&hardlink, &id) != 1) {
         Dmsg0(dbglevel, "ERROR: hardlink should be two by two\n");
         goto bail_out;
      }
      if (jobid != prev_jobid) { /* new job */
         if (prev_jobid == 0) {  /* first jobid */
            if (init) {
               query.strcat(" UNION ");
            }
         } else {               /* end last job, start new one */
            tmp.strcat(") UNION ");
            query.strcat(tmp.c_str());
         }
         Mmsg(tmp,   "SELECT Job.JobId, JobTDate, FileIndex, FilenameId, "
                            "PathId, FileId "
                       "FROM File JOIN Job USING (JobId) WHERE JobId = %lld "
                        "AND FileIndex IN (%lld", jobid, id);
         prev_jobid = jobid;

      } else {                  /* same job, add new findex */
         Mmsg(tmp2, ", %lld", id);
         tmp.strcat(tmp2.c_str());
      }
   }

   if (prev_jobid != 0) {       /* end last job */
      tmp.strcat(") ");
      query.strcat(tmp.c_str());
      init = true;
   }

   Dmsg1(dbglevel_sql, "query=%s\n", query.c_str());

   if (!db->bdb_sql_query(query.c_str(), NULL, NULL)) {
      Dmsg1(dbglevel, "ERROR executing query=%s\n", query.c_str());
      goto bail_out;
   }

   Mmsg(query, sql_bvfs_select[db->bdb_get_type_index()],
        output_table, output_table, output_table);

   /* TODO: handle jobid filter */
   Dmsg1(dbglevel_sql, "query=%s\n", query.c_str());
   if (!db->bdb_sql_query(query.c_str(), NULL, NULL)) {
      Dmsg1(dbglevel, "ERROR executing query=%s\n", query.c_str());
      goto bail_out;
   }

   /* MySQL needs the index */
   if (db->bdb_get_type_index() == SQL_TYPE_MYSQL) {
      Mmsg(query, "CREATE INDEX idx_%s ON %s (JobId)",
           output_table, output_table);
      Dmsg1(dbglevel_sql, "query=%s\n", query.c_str());
      if (!db->bdb_sql_query(query.c_str(), NULL, NULL)) {
         Dmsg1(dbglevel, "ERROR executing query=%s\n", query.c_str());
         goto bail_out; 
      } 
   }

   /* Check if some FileId have DeltaSeq > 0
    * Foreach of them we need to get the accurate_job list, and compute
    * what are dependencies
    */
   Mmsg(query, 
        "SELECT F.FileId, F.JobId, F.FilenameId, F.PathId, F.DeltaSeq "
          "FROM File AS F JOIN Job USING (JobId) JOIN %s USING (FileId) "
         "WHERE DeltaSeq > 0", output_table);

   if (!db->QueryDB(jcr, query.c_str())) {
      Dmsg1(dbglevel_sql, "Can't execute query=%s\n", query.c_str());
   }

   /* TODO: Use an other DB connection can avoid to copy the result of the
    * previous query into a temporary buffer
    */
   num = db->sql_num_rows();
   Dmsg2(dbglevel, "Found %d Delta parts in restore selection q=%s\n", num, query.c_str());

   if (num > 0) {
      int64_t *result = (int64_t *)malloc (num * 4 * sizeof(int64_t));
      SQL_ROW row;
      int i=0;

      while((row = db->sql_fetch_row())) {
         result[i++] = str_to_int64(row[0]); /* FileId */
         result[i++] = str_to_int64(row[1]); /* JobId */
         result[i++] = str_to_int64(row[2]); /* FilenameId */
         result[i++] = str_to_int64(row[3]); /* PathId */
      } 

      i=0;
      while (num > 0) {
         insert_missing_delta(output_table, result + i);
         i += 4;
         num--;
      }
      free(result);
   }

   ret = true;

bail_out:
   Mmsg(query, "DROP TABLE btemp%s", output_table);
   db->bdb_sql_query(query.c_str(), NULL, NULL);
   db->bdb_unlock();
   return ret;
}

void Bvfs::insert_missing_delta(char *output_table, int64_t *res)
{
   char ed1[50];
   db_list_ctx lst;
   POOL_MEM query;
   JOB_DBR jr, jr2;
   memset(&jr, 0, sizeof(jr));
   memset(&jr2, 0, sizeof(jr2));

   /* Need to limit the query to StartTime, Client/Fileset */
   jr2.JobId = res[1];
   db->bdb_get_job_record(jcr, &jr2);

   jr.JobId = res[1];
   jr.ClientId = jr2.ClientId;
   jr.FileSetId = jr2.FileSetId;
   jr.JobLevel = L_INCREMENTAL;
   jr.StartTime = jr2.StartTime;

   /* Get accurate jobid list */
   db->bdb_get_accurate_jobids(jcr, &jr, &lst);

   Dmsg2(dbglevel_sql, "JobId list for %lld is %s\n", res[0], lst.list);

   /* The list contains already the last DeltaSeq element, so
    * we don't need to select it in the next query
    */
   for (int l = strlen(lst.list); l > 0; l--) {
      if (lst.list[l] == ',') {
         lst.list[l] = '\0';
         break;
      }
   }

   Dmsg1(dbglevel_sql, "JobId list after strip is %s\n", lst.list);

   /* Escape filename */
   db->fnl = strlen((char *)res[2]);
   db->esc_name = check_pool_memory_size(db->esc_name, 2*db->fnl+2);
   db->bdb_escape_string(jcr, db->esc_name, (char *)res[2], db->fnl);

   edit_int64(res[3], ed1);     /* pathid */

   int id=db->bdb_get_type_index();
   Mmsg(query, bvfs_select_delta_version_with_basejob_and_delta[id],
        lst.list, db->esc_name, ed1,
        lst.list, db->esc_name, ed1,
        lst.list, lst.list);

   Mmsg(db->cmd, "INSERT INTO %s "
                   "SELECT JobId, FileIndex, FileId FROM (%s) AS F1",
        output_table, query.c_str());

   if (!db->bdb_sql_query(db->cmd, NULL, NULL)) {
      Dmsg1(dbglevel_sql, "Can't exec q=%s\n", db->cmd);
   }
}

#endif /* HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL */
