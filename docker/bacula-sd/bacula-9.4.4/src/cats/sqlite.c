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
 * Bacula Catalog Database routines specific to SQLite 
 * 
 *    Written by Kern Sibbald, January 2002 
 * 
 * Note: at one point, this file was changed to class based by a certain  
 *  programmer, and other than "wrapping" in a class, which is a trivial 
 *  change for a C++ programmer, nothing substantial was done, yet all the 
 *  code was recommitted under this programmer's name.  Consequently, we 
 *  undo those changes here. 
 */ 
 
#include "bacula.h" 
 
#if HAVE_SQLITE3  
 
#include "cats.h" 
#include <sqlite3.h> 
#define __BDB_SQLITE_H_ 1 
#include "bdb_sqlite.h" 
 
/* ----------------------------------------------------------------------- 
 * 
 *    SQLite dependent defines and subroutines 
 * 
 * ----------------------------------------------------------------------- 
 */ 
 
/* List of open databases */ 
static dlist *db_list = NULL; 
 
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 
 
/* 
 * When using mult_db_connections 
 *   sqlite can be BUSY. We just need sleep a little in this case. 
 */ 
static int my_sqlite_busy_handler(void *arg, int calls) 
{  
   bmicrosleep(0, 500); 
   return 1; 
}  
 
BDB_SQLITE::BDB_SQLITE() 
{  
   BDB_SQLITE *mdb = this; 
 
   if (db_list == NULL) { 
      db_list = New(dlist(mdb, &mdb->m_link)); 
   } 
   mdb->m_db_driver_type = SQL_DRIVER_TYPE_SQLITE3; 
   mdb->m_db_type = SQL_TYPE_SQLITE3; 
   mdb->m_db_driver = bstrdup("SQLite3"); 
 
   mdb->errmsg = get_pool_memory(PM_EMSG); /* get error message buffer */ 
   mdb->errmsg[0] = 0; 
   mdb->cmd = get_pool_memory(PM_EMSG);    /* get command buffer */ 
   mdb->cached_path = get_pool_memory(PM_FNAME); 
   mdb->cached_path_id = 0; 
   mdb->m_ref_count = 1; 
   mdb->fname = get_pool_memory(PM_FNAME); 
   mdb->path = get_pool_memory(PM_FNAME); 
   mdb->esc_name = get_pool_memory(PM_FNAME); 
   mdb->esc_path = get_pool_memory(PM_FNAME); 
   mdb->esc_obj  = get_pool_memory(PM_FNAME); 
   mdb->m_use_fatal_jmsg = true; 
 
   /* Initialize the private members. */ 
   mdb->m_db_handle = NULL; 
   mdb->m_result = NULL; 
   mdb->m_sqlite_errmsg = NULL; 
 
   db_list->append(this); 
}  
 
BDB_SQLITE::~BDB_SQLITE() 
{  
}  
 
/* 
 * Initialize database data structure. In principal this should 
 * never have errors, or it is really fatal. 
 */ 
BDB *db_init_database(JCR *jcr, const char *db_driver, const char *db_name, const char *db_user, 
                       const char *db_password, const char *db_address, int db_port, const char *db_socket, 
                       const char *db_ssl_mode, const char *db_ssl_key, 
                       const char *db_ssl_cert, const char *db_ssl_ca,
                       const char *db_ssl_capath, const char *db_ssl_cipher,
                       bool mult_db_connections, bool disable_batch_insert) 
{  
   BDB_SQLITE *mdb = NULL; 
 
   P(mutex);                          /* lock DB queue */ 
   /* 
    * Look to see if DB already open 
    */ 
   if (db_list && !mult_db_connections) { 
      foreach_dlist(mdb, db_list) { 
         if (mdb->bdb_match_database(db_driver, db_name, db_address, db_port)) { 
            Dmsg1(300, "DB REopen %s\n", db_name); 
            mdb->increment_refcount(); 
            goto bail_out; 
         } 
      } 
   } 
   Dmsg0(300, "db_init_database first time\n"); 
   mdb = New(BDB_SQLITE()); 
 
   mdb->m_db_name = bstrdup(db_name); 
   if (disable_batch_insert) { 
      mdb->m_disabled_batch_insert = true; 
      mdb->m_have_batch_insert = false; 
   } else { 
      mdb->m_disabled_batch_insert = false; 
#ifdef USE_BATCH_FILE_INSERT 
#ifdef HAVE_SQLITE3_THREADSAFE 
      mdb->m_have_batch_insert = sqlite3_threadsafe(); 
#else 
      mdb->m_have_batch_insert = false; 
#endif /* HAVE_SQLITE3_THREADSAFE */ 
#else 
      mdb->m_have_batch_insert = false; 
#endif /* USE_BATCH_FILE_INSERT */ 
   } 
   mdb->m_allow_transactions = mult_db_connections; 
 
   /* At this time, when mult_db_connections == true, this is for 
    * specific console command such as bvfs or batch mode, and we don't 
    * want to share a batch mode or bvfs. In the future, we can change 
    * the creation function to add this parameter. 
    */ 
   mdb->m_dedicated = mult_db_connections; 
 
bail_out: 
   V(mutex); 
   return mdb; 
}  
 
 
/* 
 * Now actually open the database.  This can generate errors, 
 * which are returned in the errmsg 
 * 
 * DO NOT close the database or delete mdb here !!!! 
 */ 
bool BDB_SQLITE::bdb_open_database(JCR *jcr) 
{  
   bool retval = false; 
   char *db_file; 
   int len; 
   struct stat statbuf; 
   int ret; 
   int errstat; 
   int retry = 0; 
   BDB_SQLITE *mdb = this; 
 
   P(mutex); 
   if (mdb->m_connected) { 
      retval = true; 
      goto bail_out; 
   } 
 
   if ((errstat=rwl_init(&mdb->m_lock)) != 0) { 
      berrno be; 
      Mmsg1(&mdb->errmsg, _("Unable to initialize DB lock. ERR=%s\n"), 
            be.bstrerror(errstat)); 
      goto bail_out; 
   } 
 
   /* 
    * Open the database 
    */ 
   len = strlen(working_directory) + strlen(mdb->m_db_name) + 5; 
   db_file = (char *)malloc(len); 
   strcpy(db_file, working_directory); 
   strcat(db_file, "/"); 
   strcat(db_file, m_db_name); 
   strcat(db_file, ".db"); 
   if (stat(db_file, &statbuf) != 0) { 
      Mmsg1(&mdb->errmsg, _("Database %s does not exist, please create it.\n"), 
         db_file); 
      free(db_file); 
      goto bail_out; 
   } 
 
   for (mdb->m_db_handle = NULL; !mdb->m_db_handle && retry++ < 10; ) { 
      ret = sqlite3_open(db_file, &mdb->m_db_handle); 
      if (ret != SQLITE_OK) { 
         mdb->m_sqlite_errmsg = (char *)sqlite3_errmsg(mdb->m_db_handle); 
         sqlite3_close(mdb->m_db_handle); 
         mdb->m_db_handle = NULL; 
      } else { 
         mdb->m_sqlite_errmsg = NULL; 
      } 
 
      Dmsg0(300, "sqlite_open\n"); 
      if (!mdb->m_db_handle) { 
         bmicrosleep(1, 0); 
      } 
   } 
   if (mdb->m_db_handle == NULL) { 
      Mmsg2(&mdb->errmsg, _("Unable to open Database=%s. ERR=%s\n"), 
         db_file, mdb->m_sqlite_errmsg ? mdb->m_sqlite_errmsg : _("unknown")); 
      free(db_file); 
      goto bail_out; 
   } 
   mdb->m_connected = true; 
   free(db_file); 
 
   /* 
    * Set busy handler to wait when we use mult_db_connections = true 
    */ 
   sqlite3_busy_handler(mdb->m_db_handle, my_sqlite_busy_handler, NULL); 
 
#if defined(SQLITE3_INIT_QUERY) 
   sql_query(SQLITE3_INIT_QUERY); 
#endif 
 
   if (!bdb_check_version(jcr)) { 
      goto bail_out; 
   } 
 
   retval = true; 
 
bail_out: 
   V(mutex); 
   return retval; 
}  
 
void BDB_SQLITE::bdb_close_database(JCR *jcr) 
{  
   BDB_SQLITE *mdb = this; 
 
   if (mdb->m_connected) { 
      bdb_end_transaction(jcr); 
   } 
   P(mutex); 
   mdb->m_ref_count--; 
   if (mdb->m_ref_count == 0) { 
      if (mdb->m_connected) { 
         sql_free_result(); 
      } 
      db_list->remove(mdb); 
      if (mdb->m_connected && mdb->m_db_handle) { 
         sqlite3_close(mdb->m_db_handle); 
      } 
      if (is_rwl_valid(&mdb->m_lock)) { 
         rwl_destroy(&mdb->m_lock); 
      } 
      free_pool_memory(mdb->errmsg); 
      free_pool_memory(mdb->cmd); 
      free_pool_memory(mdb->cached_path); 
      free_pool_memory(mdb->fname); 
      free_pool_memory(mdb->path); 
      free_pool_memory(mdb->esc_name); 
      free_pool_memory(mdb->esc_path); 
      free_pool_memory(mdb->esc_obj); 
      if (mdb->m_db_driver) { 
         free(mdb->m_db_driver); 
      } 
      if (mdb->m_db_name) { 
         free(mdb->m_db_name); 
      } 
      delete this; 
      if (db_list->size() == 0) { 
         delete db_list; 
         db_list = NULL; 
      } 
   } 
   V(mutex); 
}  
 
void BDB_SQLITE::bdb_thread_cleanup(void) 
{  
   sqlite3_thread_cleanup(); 
}  
 
/* 
 * Escape strings so SQLite is happy 
 * 
 * len is the length of the old string. Your new 
 *   string must be long enough (max 2*old+1) to hold 
 *   the escaped output. 
 */ 
void BDB_SQLITE::bdb_escape_string(JCR *jcr, char *snew, char *sold, int len) 
{  
   char *n, *o; 
 
   n = snew; 
   o = sold; 
   while (len--) { 
      switch (*o) { 
      case '\'': 
         *n++ = '\''; 
         *n++ = '\''; 
         o++; 
         break; 
      case 0: 
         *n++ = '\\'; 
         *n++ = 0; 
         o++; 
         break; 
      default: 
         *n++ = *o++; 
         break; 
      } 
   } 
   *n = 0; 
}  
 
/* 
 * Escape binary object so that SQLite is happy 
 * Memory is stored in BDB struct, no need to free it 
 * 
 * TODO: this should be implemented  (escape \0) 
 */ 
char *BDB_SQLITE::bdb_escape_object(JCR *jcr, char *old, int len) 
{  
   int l; 
   int max = len*2;           /* TODO: too big, should be *4/3 */ 
 
   esc_obj = check_pool_memory_size(esc_obj, max); 
   l = bin_to_base64(esc_obj, max, old, len, true); 
   esc_obj[l] = 0; 
   ASSERT(l < max);    /* TODO: add check for l */ 
 
   return esc_obj; 
}  
 
/* 
 * Unescape binary object so that SQLIte is happy 
 * 
 * TODO: need to be implemented (escape \0) 
 */ 
 
void BDB_SQLITE::bdb_unescape_object(JCR *jcr, char *from, int32_t expected_len, 
                                     POOLMEM **dest, int32_t *dest_len) 
{  
   if (!from) { 
      *dest[0] = 0; 
      *dest_len = 0; 
      return; 
   } 
   *dest = check_pool_memory_size(*dest, expected_len+1); 
   base64_to_bin(*dest, expected_len+1, from, strlen(from)); 
   *dest_len = expected_len; 
   (*dest)[expected_len] = 0; 
}  
 
/* 
 * Start a transaction. This groups inserts and makes things 
 *  more efficient. Usually started when inserting file attributes. 
 */ 
void BDB_SQLITE::bdb_start_transaction(JCR *jcr) 
{  
   BDB_SQLITE *mdb = this; 
 
   if (!jcr->attr) { 
      jcr->attr = get_pool_memory(PM_FNAME); 
   } 
   if (!jcr->ar) { 
      jcr->ar = (ATTR_DBR *)malloc(sizeof(ATTR_DBR)); 
      memset(jcr->ar, 0, sizeof(ATTR_DBR));
   } 
 
   if (!mdb->m_allow_transactions) { 
      return; 
   } 
 
   bdb_lock(); 
   /* 
    * Allow only 10,000 changes per transaction 
    */ 
   if (mdb->m_transaction && mdb->changes > 10000) { 
      bdb_end_transaction(jcr); 
   } 
   if (!mdb->m_transaction) { 
      sql_query("BEGIN");                  /* begin transaction */ 
      Dmsg0(400, "Start SQLite transaction\n"); 
      mdb->m_transaction = true;
   } 
   bdb_unlock(); 
}  
 
void BDB_SQLITE::bdb_end_transaction(JCR *jcr) 
{  
   BDB_SQLITE *mdb = this; 
   if (!mdb->m_allow_transactions) { 
      return; 
   } 
 
   bdb_lock(); 
   if (mdb->m_transaction) { 
      sql_query("COMMIT"); /* end transaction */ 
      mdb->m_transaction = false; 
      Dmsg1(400, "End SQLite transaction changes=%d\n", changes); 
   } 
   mdb->changes = 0; 
   bdb_unlock(); 
}  
 
struct rh_data { 
   BDB_SQLITE *mdb; 
   DB_RESULT_HANDLER *result_handler; 
   void *ctx; 
   bool initialized; 
}; 
 
/* 
 * Convert SQLite's callback into Bacula DB callback 
 */ 
static int sqlite_result_handler(void *arh_data, int num_fields, char **rows, char **col_names) 
{  
   struct rh_data *rh_data = (struct rh_data *)arh_data; 
 
   /* The db_sql_query doesn't have access to m_results, so if we wan't to get 
    * fields information, we need to use col_names 
    */ 
   if (!rh_data->initialized) { 
      rh_data->mdb->set_column_names(col_names, num_fields); 
      rh_data->initialized = true; 
   } 
   if (rh_data->result_handler) { 
      (*(rh_data->result_handler))(rh_data->ctx, num_fields, rows); 
   } 
 
   return 0; 
}  
 
/* 
 * Submit a general SQL command (cmd), and for each row returned, 
 *  the result_handler is called with the ctx. 
 */ 
bool BDB_SQLITE::bdb_sql_query(const char *query, DB_RESULT_HANDLER *result_handler, void *ctx) 
{  
   BDB_SQLITE *mdb = this; 
   bool retval = false; 
   int stat; 
   struct rh_data rh_data; 
 
   Dmsg1(500, "db_sql_query starts with '%s'\n", query); 
 
   bdb_lock(); 
   mdb->errmsg[0] = 0; 
   if (mdb->m_sqlite_errmsg) { 
      sqlite3_free(mdb->m_sqlite_errmsg); 
      mdb->m_sqlite_errmsg = NULL; 
   } 
   sql_free_result(); 
 
   rh_data.ctx = ctx; 
   rh_data.mdb = this; 
   rh_data.initialized = false; 
   rh_data.result_handler = result_handler; 
 
   stat = sqlite3_exec(m_db_handle, query, sqlite_result_handler, 
                       (void *)&rh_data, &m_sqlite_errmsg); 
 
   if (stat != SQLITE_OK) { 
      Mmsg(mdb->errmsg, _("Query failed: %s: ERR=%s\n"), query, sql_strerror()); 
      Dmsg0(500, "db_sql_query finished\n"); 
      goto bail_out; 
   } 
   Dmsg0(500, "db_sql_query finished\n"); 
   sql_free_result(); 
   retval = true; 
 
bail_out: 
   bdb_unlock(); 
   return retval; 
}  
 
/* 
 * Submit a sqlite query and retrieve all the data 
 */ 
bool BDB_SQLITE::sql_query(const char *query, int flags) 
{  
   int stat; 
   bool retval = false; 
   BDB_SQLITE *mdb = this; 
 
   Dmsg1(500, "sql_query starts with '%s'\n", query); 
 
   sql_free_result(); 
   if (mdb->m_sqlite_errmsg) { 
      sqlite3_free(mdb->m_sqlite_errmsg); 
      mdb->m_sqlite_errmsg = NULL; 
   } 
 
   stat = sqlite3_get_table(m_db_handle, (char *)query, &m_result, 
                            &m_num_rows, &m_num_fields, &m_sqlite_errmsg); 
 
   mdb->m_row_number = 0;               /* no row fetched */ 
   if (stat != 0) {                     /* something went wrong */ 
      mdb->m_num_rows = mdb->m_num_fields = 0; 
      Dmsg0(500, "sql_query finished\n"); 
   } else { 
      Dmsg0(500, "sql_query finished\n"); 
      retval = true; 
   } 
   return retval; 
}  
 
void BDB_SQLITE::sql_free_result(void) 
{  
   BDB_SQLITE *mdb = this; 
 
   bdb_lock(); 
   if (mdb->m_fields) { 
      free(mdb->m_fields); 
      mdb->m_fields = NULL; 
   } 
   if (mdb->m_result) { 
      sqlite3_free_table(mdb->m_result); 
      mdb->m_result = NULL; 
   } 
   mdb->m_col_names = NULL; 
   mdb->m_num_rows = mdb->m_num_fields = 0; 
   bdb_unlock(); 
}  
 
/* 
 * Fetch one row at a time 
 */ 
SQL_ROW BDB_SQLITE::sql_fetch_row(void) 
{  
   BDB_SQLITE *mdb = this; 
   if (!mdb->m_result || (mdb->m_row_number >= mdb->m_num_rows)) { 
      return NULL; 
   } 
   mdb->m_row_number++; 
   return &mdb->m_result[mdb->m_num_fields * mdb->m_row_number]; 
}  
 
const char *BDB_SQLITE::sql_strerror(void) 
{  
   BDB_SQLITE *mdb = this; 
   return mdb->m_sqlite_errmsg ? mdb->m_sqlite_errmsg : "unknown"; 
}  
 
void BDB_SQLITE::sql_data_seek(int row) 
{  
   BDB_SQLITE *mdb = this; 
   /* Set the row number to be returned on the next call to sql_fetch_row  */ 
   mdb->m_row_number = row; 
}  
 
int BDB_SQLITE::sql_affected_rows(void) 
{  
   BDB_SQLITE *mdb = this; 
   return sqlite3_changes(mdb->m_db_handle); 
}  
 
uint64_t BDB_SQLITE::sql_insert_autokey_record(const char *query, const char *table_name) 
{  
   BDB_SQLITE *mdb = this; 
   /* First execute the insert query and then retrieve the currval.  */ 
   if (!sql_query(query)) { 
      return 0; 
   } 
 
   mdb->m_num_rows = sql_affected_rows(); 
   if (mdb->m_num_rows != 1) { 
      return 0; 
   } 
 
   mdb->changes++; 
 
   return sqlite3_last_insert_rowid(mdb->m_db_handle); 
}  
 
SQL_FIELD *BDB_SQLITE::sql_fetch_field(void) 
{  
   BDB_SQLITE *mdb = this; 
   int i, j, len; 
 
   /* We are in the middle of a db_sql_query and we want to get fields info */ 
   if (mdb->m_col_names != NULL) { 
      if (mdb->m_num_fields > mdb->m_field_number) { 
         mdb->m_sql_field.name = mdb->m_col_names[mdb->m_field_number]; 
         /* We don't have the maximum field length, so we can use 80 as 
          * estimation. 
          */ 
         len = MAX(cstrlen(mdb->m_sql_field.name), 80/mdb->m_num_fields); 
         mdb->m_sql_field.max_length = len; 
 
         mdb->m_field_number++; 
         mdb->m_sql_field.type = 0;  /* not numeric */ 
         mdb->m_sql_field.flags = 1; /* not null */ 
         return &mdb->m_sql_field; 
      } else {                  /* too much fetch_field() */ 
         return NULL; 
      } 
   } 
 
   /* We are after a sql_query() that stores the result in m_results */ 
   if (!mdb->m_fields || mdb->m_fields_size < mdb->m_num_fields) { 
      if (mdb->m_fields) { 
         free(mdb->m_fields); 
         mdb->m_fields = NULL; 
      } 
      Dmsg1(500, "allocating space for %d fields\n", m_num_fields); 
      mdb->m_fields = (SQL_FIELD *)malloc(sizeof(SQL_FIELD) * mdb->m_num_fields); 
      mdb->m_fields_size = mdb->m_num_fields; 
 
      for (i = 0; i < mdb->m_num_fields; i++) { 
         Dmsg1(500, "filling field %d\n", i); 
         mdb->m_fields[i].name = mdb->m_result[i]; 
         mdb->m_fields[i].max_length = cstrlen(mdb->m_fields[i].name); 
         for (j = 1; j <= mdb->m_num_rows; j++) { 
            if (mdb->m_result[i + mdb->m_num_fields * j]) { 
               len = (uint32_t)cstrlen(mdb->m_result[i + mdb->m_num_fields * j]); 
            } else { 
               len = 0; 
            } 
            if (len > mdb->m_fields[i].max_length) { 
               mdb->m_fields[i].max_length = len; 
            } 
         } 
         mdb->m_fields[i].type = 0; 
         mdb->m_fields[i].flags = 1;        /* not null */ 
 
         Dmsg4(500, "sql_fetch_field finds field '%s' has length='%d' type='%d' and IsNull=%d\n", 
               mdb->m_fields[i].name, mdb->m_fields[i].max_length, mdb->m_fields[i].type, mdb->m_fields[i].flags); 
      } 
   } 
 
   /* Increment field number for the next time around */ 
   return &mdb->m_fields[mdb->m_field_number++]; 
}  
 
bool BDB_SQLITE::sql_field_is_not_null(int field_type) 
{  
   if (field_type == 1) { 
      return true; 
   } 
   return false; 
}  
 
bool BDB_SQLITE::sql_field_is_numeric(int field_type) 
{  
   if (field_type == 1) { 
      return true; 
   } 
   return false; 
}  
 
/* 
 * Returns true  if OK 
 *         false if failed 
 */ 
bool BDB_SQLITE::sql_batch_start(JCR *jcr) 
{  
   bool ret; 
 
   bdb_lock(); 
   ret = sql_query("CREATE TEMPORARY TABLE batch ("
                   "FileIndex integer,"
                   "JobId integer,"
                   "Path blob,"
                   "Name blob,"
                   "LStat tinyblob,"
                   "MD5 tinyblob,"
                   "DeltaSeq integer)"); 
   bdb_unlock(); 
 
   return ret; 
}  
 
/* Set error to something to abort operation */ 
/* 
 * Returns true  if OK 
 *         false if failed 
 */ 
bool BDB_SQLITE::sql_batch_end(JCR *jcr, const char *error) 
{  
   m_status = 0; 
   return true; 
}  
 
/* 
 * Returns true  if OK 
 *         false if failed 
 */ 
bool BDB_SQLITE::sql_batch_insert(JCR *jcr, ATTR_DBR *ar) 
{  
   BDB_SQLITE *mdb = this; 
   const char *digest; 
   char ed1[50]; 
 
   mdb->esc_name = check_pool_memory_size(mdb->esc_name, mdb->fnl*2+1); 
   bdb_escape_string(jcr, mdb->esc_name, mdb->fname, mdb->fnl); 
 
   mdb->esc_path = check_pool_memory_size(mdb->esc_path, mdb->pnl*2+1); 
   bdb_escape_string(jcr, mdb->esc_path, mdb->path, mdb->pnl); 
 
   if (ar->Digest == NULL || ar->Digest[0] == 0) { 
      digest = "0"; 
   } else { 
      digest = ar->Digest; 
   } 
 
   Mmsg(mdb->cmd, "INSERT INTO batch VALUES " 
        "(%d,%s,'%s','%s','%s','%s',%u)", 
        ar->FileIndex, edit_int64(ar->JobId,ed1), mdb->esc_path, 
        mdb->esc_name, ar->attr, digest, ar->DeltaSeq); 
 
   return sql_query(mdb->cmd); 
}  
 
 
#endif /* HAVE_SQLITE3 */ 
