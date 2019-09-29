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
 * Bacula Catalog Database interface routines 
 * 
 *     Almost generic set of SQL database interface routines 
 *      (with a little more work) 
 *     SQL engine specific routines are in mysql.c, postgresql.c, 
 *       sqlite.c, ... 
 * 
 *    Written by Kern Sibbald, March 2000 
 * 
 * Note: at one point, this file was changed to class based by a certain   
 *  programmer, and other than "wrapping" in a class, which is a trivial  
 *  change for a C++ programmer, nothing substantial was done, yet all the  
 *  code was recommitted under this programmer's name.  Consequently, we  
 *  undo those changes here.  
 */ 
 
#include "bacula.h" 
 
#if HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL 
 
#include "cats.h" 
 
/* Forward referenced subroutines */ 
void print_dashes(BDB *mdb); 
void print_result(BDB *mdb); 
 
dbid_list::dbid_list() 
{ 
   memset(this, 0, sizeof(dbid_list)); 
   max_ids = 1000; 
   DBId = (DBId_t *)malloc(max_ids * sizeof(DBId_t)); 
   num_ids = num_seen = tot_ids = 0; 
   PurgedFiles = NULL; 
} 
 
dbid_list::~dbid_list() 
{ 
   free(DBId); 
} 
 
/* 
 * Called here to retrieve an string list from the database 
 */ 
int db_string_list_handler(void *ctx, int num_fields, char **row) 
{ 
   alist **val = (alist **)ctx; 
 
   if (row[0]) { 
      (*val)->append(bstrdup(row[0])); 
   } 
 
   return 0; 
} 
 
/* 
 * Called here to retrieve an integer from the database 
 */ 
int db_int_handler(void *ctx, int num_fields, char **row) 
{ 
   uint32_t *val = (uint32_t *)ctx; 
 
   Dmsg1(800, "int_handler starts with row pointing at %x\n", row); 
 
   if (row[0]) { 
      Dmsg1(800, "int_handler finds '%s'\n", row[0]); 
      *val = str_to_int64(row[0]); 
   } else { 
      Dmsg0(800, "int_handler finds zero\n"); 
      *val = 0; 
   } 
   Dmsg0(800, "int_handler finishes\n"); 
   return 0; 
} 
 
/* 
 * Called here to retrieve a 32/64 bit integer from the database. 
 *   The returned integer will be extended to 64 bit. 
 */ 
int db_int64_handler(void *ctx, int num_fields, char **row) 
{ 
   db_int64_ctx *lctx = (db_int64_ctx *)ctx; 
 
   if (row[0]) { 
      lctx->value = str_to_int64(row[0]); 
      lctx->count++; 
   } 
   return 0; 
} 
 
/* 
 * Called here to retrieve a btime from the database. 
 *   The returned integer will be extended to 64 bit. 
 */ 
int db_strtime_handler(void *ctx, int num_fields, char **row) 
{ 
   db_int64_ctx *lctx = (db_int64_ctx *)ctx; 
 
   if (row[0]) { 
      lctx->value = str_to_utime(row[0]); 
      lctx->count++; 
   } 
   return 0; 
} 
 
/* 
 * Use to build a comma separated list of values from a query. "10,20,30" 
 */ 
int db_list_handler(void *ctx, int num_fields, char **row) 
{ 
   db_list_ctx *lctx = (db_list_ctx *)ctx; 
   if (num_fields == 1 && row[0]) { 
      lctx->add(row[0]); 
   } 
   return 0; 
} 
 
/* 
 * specific context passed from bdb_check_max_connections to  
 * db_max_connections_handler. 
 */ 
struct max_connections_context { 
   BDB *db; 
   uint32_t nr_connections; 
}; 
 
/* 
 * Called here to retrieve max_connections from db 
 */ 
static int db_max_connections_handler(void *ctx, int num_fields, char **row) 
{ 
   struct max_connections_context *context; 
   uint32_t index; 
 
   context = (struct max_connections_context *)ctx; 
   switch (context->db->bdb_get_type_index()) { 
   case SQL_TYPE_MYSQL: 
      index = 1; 
   default: 
      index = 0; 
   } 
 
   if (row[index]) { 
      context->nr_connections = str_to_int64(row[index]); 
   } else { 
      Dmsg0(800, "int_handler finds zero\n"); 
      context->nr_connections = 0; 
   } 
   return 0; 
} 

BDB::BDB()
{
   init_acl();
   acl_join = get_pool_memory(PM_MESSAGE);
   acl_where = get_pool_memory(PM_MESSAGE);
}

BDB::~BDB()
{
   free_acl();
   free_pool_memory(acl_join);
   free_pool_memory(acl_where);
}

/* Get the WHERE section of a query that permits to respect
 * the console ACLs.
 *
 *  get_acls(DB_ACL_BIT(DB_ACL_JOB) | DB_ACL_BIT(DB_ACL_CLIENT), true)
 *     -> WHERE Job.Name IN ('a', 'b', 'c') AND Client.Name IN ('d', 'e')
 *
 *  get_acls(DB_ACL_BIT(DB_ACL_JOB) | DB_ACL_BIT(DB_ACL_CLIENT), false)
 *     -> AND Job.Name IN ('a', 'b', 'c') AND Client.Name IN ('d', 'e')
 */
char *BDB::get_acls(int tables, bool where /* use WHERE or AND */)
{
   POOL_MEM tmp;
   pm_strcpy(acl_where, "");

   for (int i=0 ;  i < DB_ACL_LAST; i++) {
      if (tables & DB_ACL_BIT(i)) {
         pm_strcat(acl_where, get_acl((DB_ACL_t)i, where));
         where = acl_where[0] == 0 && where;
      }
   }
   return acl_where;
}

/* Create the JOIN string that will help to filter queries results */
char *BDB::get_acl_join_filter(int tables)
{
   POOL_MEM tmp;
   pm_strcpy(acl_join, "");

   if (tables & DB_ACL_BIT(DB_ACL_JOB)) {
      Mmsg(tmp, " JOIN Job USING (JobId) ");
      pm_strcat(acl_join, tmp);
   }
   if (tables & (DB_ACL_BIT(DB_ACL_CLIENT) | DB_ACL_BIT(DB_ACL_RCLIENT) | DB_ACL_BIT(DB_ACL_BCLIENT))) {
      Mmsg(tmp, " JOIN Client USING (ClientId) ");
      pm_strcat(acl_join, tmp);
   }
   if (tables & DB_ACL_BIT(DB_ACL_POOL)) {
      Mmsg(tmp, " JOIN Pool USING (PoolId) ");
      pm_strcat(acl_join, tmp);
   }
   if (tables & DB_ACL_BIT(DB_ACL_PATH)) {
      Mmsg(tmp, " JOIN Path USING (PathId) ");
      pm_strcat(acl_join, tmp);
   }
   if (tables & DB_ACL_BIT(DB_ACL_LOG)) {
      Mmsg(tmp, " JOIN Log USING (JobId) ");
      pm_strcat(acl_join, tmp);
   }
   if (tables & DB_ACL_BIT(DB_ACL_FILESET)) {
      Mmsg(tmp, " LEFT JOIN FileSet USING (FileSetId) ");
      pm_strcat(acl_join, tmp);
   }
   return acl_join;
}

/* Intialize the ACL list */
void BDB::init_acl()
{
   for(int i=0; i < DB_ACL_LAST; i++) {
      acls[i] = NULL;
   }
}

/* Free ACL list */
void BDB::free_acl()
{
   for(int i=0; i < DB_ACL_LAST; i++) {
      free_and_null_pool_memory(acls[i]);
   }
}

/* Get ACL for a given type */
const char *BDB::get_acl(DB_ACL_t type, bool where /* display WHERE or AND */)
{
   if (!acls[type]) {
      return "";
   }
   strcpy(acls[type], where?" WHERE ":"   AND ");
   acls[type][7] = ' ' ;        /* replace \0 by ' ' */
   return acls[type];
}

/* Keep UAContext ACLs in our structure for further SQL queries */
void BDB::set_acl(JCR *jcr, DB_ACL_t type, alist *list, alist *list2)
{
   /* If the list is present, but we authorize everything */
   if (list && list->size() == 1 && strcasecmp((char*)list->get(0), "*all*") == 0) {
      return;
   }

   /* If the list is present, but we authorize everything */
   if (list2 && list2->size() == 1 && strcasecmp((char*)list2->get(0), "*all*") == 0) {
      return;
   }

   POOLMEM *tmp = get_pool_memory(PM_FNAME);
   POOLMEM *where = get_pool_memory(PM_FNAME);

   *where = 0;
   *tmp = 0;

   /* For clients, we can have up to 2 lists */
   escape_acl_list(jcr, &tmp, list);
   escape_acl_list(jcr, &tmp, list2);

   switch(type) {
   case DB_ACL_JOB:
      Mmsg(where, "   AND  Job.Name IN (%s) ", tmp);
      break;
   case DB_ACL_CLIENT:
      Mmsg(where, "   AND  Client.Name IN (%s) ", tmp);
      break;
   case DB_ACL_BCLIENT:
      Mmsg(where, "   AND  Client.Name IN (%s) ", tmp);
      break;
   case DB_ACL_RCLIENT:
      Mmsg(where, "   AND  Client.Name IN (%s) ", tmp);
      break;
   case  DB_ACL_FILESET:
      Mmsg(where, "   AND  (FileSetId = 0 OR FileSet.FileSet IN (%s)) ", tmp);
      break;
   case DB_ACL_POOL:
      Mmsg(where, "   AND  (PoolId = 0 OR Pool.Name IN (%s)) ", tmp);
      break;
   default:
      break;
   }
   acls[type] = where;
   free_pool_memory(tmp);
}

/* Convert a ACL list to a SQL IN() list */
char *BDB::escape_acl_list(JCR *jcr, POOLMEM **escaped_list, alist *lst)
{
   char *elt;
   int len;
   POOL_MEM tmp;

   if (!lst) {
      return *escaped_list;     /* TODO: check how we handle the empty list */

   /* List is empty, reject everything */
   } else if (lst->size() == 0) {
      Mmsg(escaped_list, "''");
      return *escaped_list;
   }

   foreach_alist(elt, lst) {
      if (elt && *elt) {
         len = strlen(elt);
         /* Escape + ' ' */
         tmp.check_size(2 * len + 2 + 2);

         pm_strcpy(tmp, "'");
         bdb_lock();
         bdb_escape_string(jcr, tmp.c_str() + 1 , elt, len);
         bdb_unlock();
         pm_strcat(tmp, "'");

         if (*escaped_list[0]) {
            pm_strcat(escaped_list, ",");
         }

         pm_strcat(escaped_list, tmp.c_str());
      }
   }
   return *escaped_list;
}

/* 
 * Check catalog max_connections setting 
 */ 
bool BDB::bdb_check_max_connections(JCR *jcr, uint32_t max_concurrent_jobs) 
{ 
   struct max_connections_context context; 
   
   /* Without Batch insert, no need to verify max_connections */ 
   if (!batch_insert_available()) 
      return true; 
 
   context.db = this; 
   context.nr_connections = 0; 
 
   /* Check max_connections setting */ 
   if (!bdb_sql_query(sql_get_max_connections[bdb_get_type_index()], 
                     db_max_connections_handler, &context)) { 
      Jmsg(jcr, M_ERROR, 0, "Can't verify max_connections settings %s", errmsg); 
      return false; 
   } 
   if (context.nr_connections && max_concurrent_jobs && max_concurrent_jobs > context.nr_connections) { 
      Mmsg(errmsg, 
           _("Potential performance problem:\n" 
             "max_connections=%d set for %s database \"%s\" should be larger than Director's " 
             "MaxConcurrentJobs=%d\n"), 
           context.nr_connections, bdb_get_engine_name(), get_db_name(), max_concurrent_jobs); 
      Jmsg(jcr, M_WARNING, 0, "%s", errmsg); 
      return false; 
   } 
 
   return true; 
} 
 
/* NOTE!!! The following routines expect that the 
 *  calling subroutine sets and clears the mutex 
 */ 
 
/* Check that the tables correspond to the version we want */ 
bool BDB::bdb_check_version(JCR *jcr) 
{ 
   uint32_t bacula_db_version = 0; 
   const char *query = "SELECT VersionId FROM Version"; 
 
   bacula_db_version = 0; 
   if (!bdb_sql_query(query, db_int_handler, (void *)&bacula_db_version)) { 
      Jmsg(jcr, M_FATAL, 0, "%s", errmsg); 
      return false; 
   } 
   if (bacula_db_version != BDB_VERSION) { 
      Mmsg(errmsg, "Version error for database \"%s\". Wanted %d, got %d\n", 
          get_db_name(), BDB_VERSION, bacula_db_version); 
      Jmsg(jcr, M_FATAL, 0, "%s", errmsg); 
      return false; 
   } 
   return true; 
} 
 
/* 
 * Utility routine for queries. The database MUST be locked before calling here. 
 * Returns: 0 on failure 
 *          1 on success 
 */ 
bool BDB::QueryDB(JCR *jcr, char *cmd, const char *file, int line) 
{ 
   sql_free_result(); 
   if (!sql_query(cmd, QF_STORE_RESULT)) { 
      m_msg(file, line, &errmsg, _("query %s failed:\n%s\n"), cmd, sql_strerror()); 
      if (use_fatal_jmsg()) { 
         j_msg(file, line, jcr, M_FATAL, 0, "%s", errmsg); 
      } 
      if (verbose) { 
         j_msg(file, line, jcr, M_INFO, 0, "%s\n", cmd); 
      } 
      return false; 
   } 
 
   return true; 
} 
 
/* 
 * Utility routine to do inserts 
 * Returns: 0 on failure 
 *          1 on success 
 */ 
bool BDB::InsertDB(JCR *jcr, char *cmd, const char *file, int line) 
{ 
   if (!sql_query(cmd)) { 
      m_msg(file, line, &errmsg,  _("insert %s failed:\n%s\n"), cmd, sql_strerror()); 
      if (use_fatal_jmsg()) { 
         j_msg(file, line, jcr, M_FATAL, 0, "%s", errmsg); 
      } 
      if (verbose) { 
         j_msg(file, line, jcr, M_INFO, 0, "%s\n", cmd); 
      } 
      return false; 
   } 
   int num_rows = sql_affected_rows(); 
   if (num_rows != 1) { 
      char ed1[30]; 
      m_msg(file, line, &errmsg, _("Insertion problem: affected_rows=%s\n"), 
         edit_uint64(num_rows, ed1)); 
      if (verbose) { 
         j_msg(file, line, jcr, M_INFO, 0, "%s\n", cmd); 
      } 
      return false; 
   } 
   changes++; 
   return true; 
} 
 
/* Utility routine for updates. 
 *  Returns: false on failure 
 *           true  on success 
 *
 * Some UPDATE queries must update record(s), other queries might not update
 * anything.
 */ 
bool BDB::UpdateDB(JCR *jcr, char *cmd, bool can_be_empty,
                   const char *file, int line) 
{ 
   if (!sql_query(cmd)) { 
      m_msg(file, line, &errmsg, _("update %s failed:\n%s\n"), cmd, sql_strerror()); 
      j_msg(file, line, jcr, M_ERROR, 0, "%s", errmsg); 
      if (verbose) { 
         j_msg(file, line, jcr, M_INFO, 0, "%s\n", cmd); 
      } 
      return false; 
   } 
   int num_rows = sql_affected_rows(); 
   if ((num_rows == 0 && !can_be_empty) || num_rows < 0) { 
      char ed1[30]; 
      m_msg(file, line, &errmsg, _("Update failed: affected_rows=%s for %s\n"), 
         edit_uint64(num_rows, ed1), cmd); 
      if (verbose) { 
//       j_msg(file, line, jcr, M_INFO, 0, "%s\n", cmd); 
      } 
      return false; 
   } 
   changes++; 
   return true; 
} 
 
/* Utility routine for deletes 
 * 
 * Returns: -1 on error 
 *           n number of rows affected 
 */ 
int BDB::DeleteDB(JCR *jcr, char *cmd, const char *file, int line) 
{ 
 
   if (!sql_query(cmd)) { 
      m_msg(file, line, &errmsg, _("delete %s failed:\n%s\n"), cmd, sql_strerror()); 
      j_msg(file, line, jcr, M_ERROR, 0, "%s", errmsg); 
      if (verbose) { 
         j_msg(file, line, jcr, M_INFO, 0, "%s\n", cmd); 
      } 
      return -1; 
   } 
   changes++; 
   return sql_affected_rows(); 
} 
 
 
/* 
 * Get record max. Query is already in mdb->cmd 
 *  No locking done 
 * 
 * Returns: -1 on failure 
 *          count on success 
 */ 
int get_sql_record_max(JCR *jcr, BDB *mdb) 
{ 
   SQL_ROW row; 
   int stat = 0; 
 
   if (mdb->QueryDB(jcr, mdb->cmd)) { 
      if ((row = mdb->sql_fetch_row()) == NULL) { 
         Mmsg1(&mdb->errmsg, _("error fetching row: %s\n"), mdb->sql_strerror()); 
         stat = -1; 
      } else { 
         stat = str_to_int64(row[0]); 
      } 
      mdb->sql_free_result(); 
   } else { 
      Mmsg1(&mdb->errmsg, _("error fetching row: %s\n"), mdb->sql_strerror()); 
      stat = -1; 
   } 
   return stat; 
} 
 
/* 
 * Given a full filename, split it into its path 
 *  and filename parts. They are returned in pool memory 
 *  in the mdb structure. 
 */ 
void split_path_and_file(JCR *jcr, BDB *mdb, const char *afname) 
{ 
   const char *p, *f; 
 
   /* Find path without the filename. 
    * I.e. everything after the last / is a "filename". 
    * OK, maybe it is a directory name, but we treat it like 
    * a filename. If we don't find a / then the whole name 
    * must be a path name (e.g. c:). 
    */ 
   for (p=f=afname; *p; p++) { 
      if (IsPathSeparator(*p)) { 
         f = p;                       /* set pos of last slash */ 
      } 
   } 
   if (IsPathSeparator(*f)) {                   /* did we find a slash? */ 
      f++;                            /* yes, point to filename */ 
   } else {                           /* no, whole thing must be path name */ 
      f = p; 
   } 
 
   /* If filename doesn't exist (i.e. root directory), we 
    * simply create a blank name consisting of a single 
    * space. This makes handling zero length filenames 
    * easier. 
    */ 
   mdb->fnl = p - f; 
   if (mdb->fnl > 0) { 
      mdb->fname = check_pool_memory_size(mdb->fname, mdb->fnl+1); 
      memcpy(mdb->fname, f, mdb->fnl);    /* copy filename */ 
      mdb->fname[mdb->fnl] = 0; 
   } else { 
      mdb->fname[0] = 0; 
      mdb->fnl = 0; 
   } 
 
   mdb->pnl = f - afname; 
   if (mdb->pnl > 0) { 
      mdb->path = check_pool_memory_size(mdb->path, mdb->pnl+1); 
      memcpy(mdb->path, afname, mdb->pnl); 
      mdb->path[mdb->pnl] = 0; 
   } else { 
      Mmsg1(&mdb->errmsg, _("Path length is zero. File=%s\n"), afname); 
      Jmsg(jcr, M_FATAL, 0, "%s", mdb->errmsg); 
      mdb->path[0] = 0; 
      mdb->pnl = 0; 
   } 
 
   Dmsg3(500, "split fname=%s: path=%s file=%s\n", afname, mdb->path, mdb->fname); 
} 
 
/* 
 * Set maximum field length to something reasonable 
 */ 
static int max_length(int max_length) 
{ 
   int max_len = max_length; 
   /* Sanity check */ 
   if (max_len < 0) { 
      max_len = 2; 
   } else if (max_len > 100) { 
      max_len = 100; 
   } 
   return max_len; 
} 
 
/* 
 * List dashes as part of header for listing SQL results in a table 
 */ 
void 
list_dashes(BDB *mdb, DB_LIST_HANDLER *send, void *ctx) 
{ 
   SQL_FIELD  *field; 
   int i, j; 
   int len; 
 
   mdb->sql_field_seek(0); 
   send(ctx, "+"); 
   for (i = 0; i < mdb->sql_num_fields(); i++) { 
      field = mdb->sql_fetch_field(); 
      if (!field) { 
         break; 
      } 
      len = max_length(field->max_length + 2); 
      for (j = 0; j < len; j++) { 
         send(ctx, "-"); 
      } 
      send(ctx, "+"); 
   } 
   send(ctx, "\n"); 
} 
 
/* Small handler to print the last line of a list xxx command */ 
static void last_line_handler(void *vctx, const char *str) 
{ 
   LIST_CTX *ctx = (LIST_CTX *)vctx; 
   bstrncat(ctx->line, str, sizeof(ctx->line)); 
} 
 
int list_result(void *vctx, int nb_col, char **row) 
{ 
   SQL_FIELD *field; 
   int i, col_len, max_len = 0; 
   char buf[2000], ewc[30]; 
 
   LIST_CTX *pctx = (LIST_CTX *)vctx; 
   DB_LIST_HANDLER *send = pctx->send; 
   e_list_type type = pctx->type; 
   BDB *mdb = pctx->mdb; 
   void *ctx = pctx->ctx; 
   JCR *jcr = pctx->jcr; 
 
   if (!pctx->once) { 
      pctx->once = true; 
 
      Dmsg1(800, "list_result starts looking at %d fields\n", mdb->sql_num_fields()); 
      /* determine column display widths */ 
      mdb->sql_field_seek(0); 
      for (i = 0; i < mdb->sql_num_fields(); i++) { 
         Dmsg1(800, "list_result processing field %d\n", i); 
         field = mdb->sql_fetch_field(); 
         if (!field) { 
            break; 
         } 
         col_len = cstrlen(field->name); 
         if (type == VERT_LIST) { 
            if (col_len > max_len) { 
               max_len = col_len; 
            } 
         } else { 
            if (mdb->sql_field_is_numeric(field->type) && (int)field->max_length > 0) { /* fixup for commas */ 
               field->max_length += (field->max_length - 1) / 3; 
            } 
            if (col_len < (int)field->max_length) { 
               col_len = field->max_length; 
            } 
            if (col_len < 4 && !mdb->sql_field_is_not_null(field->flags)) { 
               col_len = 4;                 /* 4 = length of the word "NULL" */ 
            } 
            field->max_length = col_len;    /* reset column info */ 
         } 
      } 
 
      pctx->num_rows++; 
 
      Dmsg0(800, "list_result finished first loop\n"); 
      if (type == VERT_LIST) { 
         goto vertical_list; 
      } 
      if (type == ARG_LIST) { 
         goto arg_list; 
      } 
 
      Dmsg1(800, "list_result starts second loop looking at %d fields\n",  
            mdb->sql_num_fields()); 
 
      /* Keep the result to display the same line at the end of the table */ 
      list_dashes(mdb, last_line_handler, pctx); 
      send(ctx, pctx->line); 
 
      send(ctx, "|"); 
      mdb->sql_field_seek(0); 
      for (i = 0; i < mdb->sql_num_fields(); i++) { 
         Dmsg1(800, "list_result looking at field %d\n", i); 
         field = mdb->sql_fetch_field(); 
         if (!field) { 
            break; 
         } 
         max_len = max_length(field->max_length); 
         bsnprintf(buf, sizeof(buf), " %-*s |", max_len, field->name); 
         send(ctx, buf); 
      } 
      send(ctx, "\n"); 
      list_dashes(mdb, send, ctx); 
   } 
   Dmsg1(800, "list_result starts third loop looking at %d fields\n",  
         mdb->sql_num_fields()); 
   mdb->sql_field_seek(0); 
   send(ctx, "|"); 
   for (i = 0; i < mdb->sql_num_fields(); i++) { 
      field = mdb->sql_fetch_field(); 
      if (!field) { 
         break; 
      } 
      max_len = max_length(field->max_length); 
      if (row[i] == NULL) { 
         bsnprintf(buf, sizeof(buf), " %-*s |", max_len, "NULL"); 
      } else if (mdb->sql_field_is_numeric(field->type) && !jcr->gui && is_an_integer(row[i])) { 
         bsnprintf(buf, sizeof(buf), " %*s |", max_len, 
                   add_commas(row[i], ewc)); 
      } else { 
         bsnprintf(buf, sizeof(buf), " %-*s |", max_len, row[i]); 
      } 
      send(ctx, buf); 
   } 
   send(ctx, "\n"); 
   return 0; 
 
vertical_list: 
 
   Dmsg1(800, "list_result starts vertical list at %d fields\n", mdb->sql_num_fields()); 
   mdb->sql_field_seek(0); 
   for (i = 0; i < mdb->sql_num_fields(); i++) { 
      field = mdb->sql_fetch_field(); 
      if (!field) { 
         break; 
      } 
      if (row[i] == NULL) { 
         bsnprintf(buf, sizeof(buf), " %*s: %s\n", max_len, field->name, "NULL"); 
      } else if (mdb->sql_field_is_numeric(field->type) && !jcr->gui && is_an_integer(row[i])) { 
         bsnprintf(buf, sizeof(buf), " %*s: %s\n", max_len, field->name, 
                   add_commas(row[i], ewc)); 
      } else { 
         bsnprintf(buf, sizeof(buf), " %*s: %s\n", max_len, field->name, row[i]); 
      } 
      send(ctx, buf); 
   } 
   send(ctx, "\n"); 
   return 0; 
 
arg_list: 
   Dmsg1(800, "list_result starts simple list at %d fields\n", mdb->sql_num_fields()); 
   mdb->sql_field_seek(0); 
   for (i = 0; i < mdb->sql_num_fields(); i++) { 
      field = mdb->sql_fetch_field(); 
      if (!field) { 
         break; 
      } 
      if (row[i] == NULL) { 
         bsnprintf(buf, sizeof(buf), "%s%s=", (i>0?" ":""), field->name); 
      } else { 
         bash_spaces(row[i]); 
         bsnprintf(buf, sizeof(buf), "%s%s=%s ", (i>0?" ":""), field->name, row[i]); 
      } 
      send(ctx, buf); 
   } 
   send(ctx, "\n"); 
   return 0; 
 
} 
 
/* 
 * If full_list is set, we list vertically, otherwise, we 
 *  list on one line horizontally. 
 * Return number of rows 
 */ 
int 
list_result(JCR *jcr, BDB *mdb, DB_LIST_HANDLER *send, void *ctx, e_list_type type) 
{ 
   SQL_FIELD *field; 
   SQL_ROW row; 
   int i, col_len, max_len = 0; 
   char buf[2000], ewc[30]; 
 
   Dmsg0(800, "list_result starts\n"); 
   if (mdb->sql_num_rows() == 0) { 
      send(ctx, _("No results to list.\n")); 
      return mdb->sql_num_rows(); 
   } 
 
   Dmsg1(800, "list_result starts looking at %d fields\n", mdb->sql_num_fields()); 
   /* determine column display widths */ 
   mdb->sql_field_seek(0); 
   for (i = 0; i < mdb->sql_num_fields(); i++) { 
      Dmsg1(800, "list_result processing field %d\n", i); 
      field = mdb->sql_fetch_field(); 
      if (!field) { 
         break; 
      } 
      col_len = cstrlen(field->name); 
      if (type == VERT_LIST) { 
         if (col_len > max_len) { 
            max_len = col_len; 
         } 
      } else { 
         if (mdb->sql_field_is_numeric(field->type) && (int)field->max_length > 0) { /* fixup for commas */ 
            field->max_length += (field->max_length - 1) / 3; 
         } 
         if (col_len < (int)field->max_length) { 
            col_len = field->max_length; 
         } 
         if (col_len < 4 && !mdb->sql_field_is_not_null(field->flags)) { 
            col_len = 4;                 /* 4 = length of the word "NULL" */ 
         } 
         field->max_length = col_len;    /* reset column info */ 
      } 
   } 
 
   Dmsg0(800, "list_result finished first loop\n"); 
   if (type == VERT_LIST) { 
      goto vertical_list; 
   } 
   if (type == ARG_LIST) { 
      goto arg_list; 
   } 
 
   Dmsg1(800, "list_result starts second loop looking at %d fields\n", mdb->sql_num_fields()); 
   list_dashes(mdb, send, ctx); 
   send(ctx, "|"); 
   mdb->sql_field_seek(0); 
   for (i = 0; i < mdb->sql_num_fields(); i++) { 
      Dmsg1(800, "list_result looking at field %d\n", i); 
      field = mdb->sql_fetch_field(); 
      if (!field) { 
         break; 
      } 
      max_len = max_length(field->max_length); 
      bsnprintf(buf, sizeof(buf), " %-*s |", max_len, field->name); 
      send(ctx, buf); 
   } 
   send(ctx, "\n"); 
   list_dashes(mdb, send, ctx); 
 
   Dmsg1(800, "list_result starts third loop looking at %d fields\n", mdb->sql_num_fields()); 
   while ((row = mdb->sql_fetch_row()) != NULL) { 
      mdb->sql_field_seek(0); 
      send(ctx, "|"); 
      for (i = 0; i < mdb->sql_num_fields(); i++) { 
         field = mdb->sql_fetch_field(); 
         if (!field) { 
            break; 
         } 
         max_len = max_length(field->max_length); 
         if (row[i] == NULL) { 
            bsnprintf(buf, sizeof(buf), " %-*s |", max_len, "NULL"); 
         } else if (mdb->sql_field_is_numeric(field->type) && !jcr->gui && is_an_integer(row[i])) { 
            bsnprintf(buf, sizeof(buf), " %*s |", max_len, 
                      add_commas(row[i], ewc)); 
         } else {
            strip_trailing_junk(row[i]);
            bsnprintf(buf, sizeof(buf), " %-*s |", max_len, row[i]); 
         } 
         send(ctx, buf); 
      } 
      send(ctx, "\n"); 
   } 
   list_dashes(mdb, send, ctx); 
   return mdb->sql_num_rows(); 
 
vertical_list: 
 
   Dmsg1(800, "list_result starts vertical list at %d fields\n", mdb->sql_num_fields()); 
   while ((row = mdb->sql_fetch_row()) != NULL) { 
      mdb->sql_field_seek(0); 
      for (i = 0; i < mdb->sql_num_fields(); i++) { 
         field = mdb->sql_fetch_field(); 
         if (!field) { 
            break; 
         } 
         if (row[i] == NULL) { 
            bsnprintf(buf, sizeof(buf), " %*s: %s\n", max_len, field->name, "NULL"); 
         } else if (mdb->sql_field_is_numeric(field->type) && !jcr->gui && is_an_integer(row[i])) { 
            bsnprintf(buf, sizeof(buf), " %*s: %s\n", max_len, field->name, 
                add_commas(row[i], ewc)); 
         } else {
            strip_trailing_junk(row[i]);
            bsnprintf(buf, sizeof(buf), " %*s: %s\n", max_len, field->name, row[i]); 
         } 
         send(ctx, buf); 
      } 
      send(ctx, "\n"); 
   } 
 
arg_list: 
 
   Dmsg1(800, "list_result starts arg list at %d fields\n", mdb->sql_num_fields()); 
   while ((row = mdb->sql_fetch_row()) != NULL) { 
      mdb->sql_field_seek(0); 
      for (i = 0; i < mdb->sql_num_fields(); i++) { 
         field = mdb->sql_fetch_field(); 
         if (!field) { 
            break; 
         } 
         if (row[i] == NULL) { 
            bsnprintf(buf, sizeof(buf), "%s%s=", (i>0?" ":""), field->name); 
         } else { 
            bash_spaces(row[i]); 
            bsnprintf(buf, sizeof(buf), "%s%s=%s", (i>0?" ":""), field->name, row[i]); 
         } 
         send(ctx, buf); 
      } 
      send(ctx, "\n"); 
   } 
   return mdb->sql_num_rows(); 
} 
 
/* 
 * Open a new connexion to mdb catalog. This function is used 
 * by batch and accurate mode. 
 */ 
bool BDB::bdb_open_batch_connexion(JCR *jcr) 
{ 
   bool multi_db; 
 
   multi_db = batch_insert_available(); 
 
   if (!jcr->db_batch) { 
      jcr->db_batch = bdb_clone_database_connection(jcr, multi_db); 
      if (!jcr->db_batch) { 
         Mmsg0(&errmsg, _("Could not init database batch connection\n")); 
         Jmsg(jcr, M_FATAL, 0, "%s", errmsg); 
         return false; 
      } 
 
      if (!jcr->db_batch->bdb_open_database(jcr)) { 
         Mmsg2(&errmsg,  _("Could not open database \"%s\": ERR=%s\n"), 
              jcr->db_batch->get_db_name(), jcr->db_batch->bdb_strerror()); 
         Jmsg(jcr, M_FATAL, 0, "%s", errmsg); 
         return false; 
      } 
   } 
   return true; 
} 
 
/* 
 * !!! WARNING !!! Use this function only when bacula is stopped. 
 * ie, after a fatal signal and before exiting the program 
 * Print information about a BDB object. 
 */ 
void bdb_debug_print(JCR *jcr, FILE *fp) 
{ 
   BDB *mdb = jcr->db; 
 
   if (!mdb) { 
      return; 
   } 
 
   fprintf(fp, "BDB=%p db_name=%s db_user=%s connected=%s\n", 
           mdb, NPRTB(mdb->get_db_name()), NPRTB(mdb->get_db_user()), mdb->is_connected() ? "true" : "false"); 
   fprintf(fp, "\tcmd=\"%s\" changes=%i\n", NPRTB(mdb->cmd), mdb->changes); 
   mdb->print_lock_info(fp); 
} 
 
bool BDB::bdb_check_settings(JCR *jcr, int64_t *starttime, int val, int64_t val2) 
{ 
   return true; 
} 
 
#endif /* HAVE_SQLITE3 || HAVE_MYSQL || HAVE_POSTGRESQL */ 
