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
 * Bacula Catalog Database routines specific to MySQL 
 *   These are MySQL specific routines -- hopefully all 
 *    other files are generic. 
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
 
#ifdef HAVE_MYSQL 
 
#include "cats.h" 
#include <mysql.h> 
#define  __BDB_MYSQL_H_ 1 
#include "bdb_mysql.h" 
 
/* ----------------------------------------------------------------------- 
 * 
 *   MySQL dependent defines and subroutines 
 * 
 * ----------------------------------------------------------------------- 
 */ 
 
/* List of open databases */ 
static dlist *db_list = NULL; 
 
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 
 
BDB_MYSQL::BDB_MYSQL(): BDB()
{ 
   BDB_MYSQL *mdb = this; 
 
   if (db_list == NULL) { 
      db_list = New(dlist(this, &this->m_link)); 
   } 
   mdb->m_db_driver_type = SQL_DRIVER_TYPE_MYSQL; 
   mdb->m_db_type = SQL_TYPE_MYSQL; 
   mdb->m_db_driver = bstrdup("MySQL"); 
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
   mdb->esc_obj = get_pool_memory(PM_FNAME); 
   mdb->m_use_fatal_jmsg = true; 
 
   /* Initialize the private members. */ 
   mdb->m_db_handle = NULL; 
   mdb->m_result = NULL; 
 
   db_list->append(this); 
} 
 
BDB_MYSQL::~BDB_MYSQL()
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
   BDB_MYSQL *mdb = NULL; 
 
   if (!db_user) { 
      Jmsg(jcr, M_FATAL, 0, _("A user name for MySQL must be supplied.\n")); 
      return NULL; 
   } 
   P(mutex);                          /* lock DB queue */ 
 
   /* 
    * Look to see if DB already open 
    */ 
   if (db_list && !mult_db_connections) { 
      foreach_dlist(mdb, db_list) { 
         if (mdb->bdb_match_database(db_driver, db_name, db_address, db_port)) { 
            Dmsg1(100, "DB REopen %s\n", db_name); 
            mdb->increment_refcount(); 
            goto get_out; 
         } 
      } 
   } 
   Dmsg0(100, "db_init_database first time\n"); 
   mdb = New(BDB_MYSQL()); 
   if (!mdb) goto get_out; 
 
   /* 
    * Initialize the parent class members. 
    */ 
   mdb->m_db_name = bstrdup(db_name); 
   mdb->m_db_user = bstrdup(db_user); 
   if (db_password) { 
      mdb->m_db_password = bstrdup(db_password); 
   } 
   if (db_address) { 
      mdb->m_db_address = bstrdup(db_address); 
   } 
   if (db_socket) {
      mdb->m_db_socket = bstrdup(db_socket); 
   } 
   if (db_ssl_mode) {
      mdb->m_db_ssl_mode = bstrdup(db_ssl_mode);
   } else {
      mdb->m_db_ssl_mode = bstrdup("preferred");
   }
   if (db_ssl_key) {
      mdb->m_db_ssl_key = bstrdup(db_ssl_key);
   }
   if (db_ssl_cert) {
      mdb->m_db_ssl_cert = bstrdup(db_ssl_cert);
   }
   if (db_ssl_ca) {
      mdb->m_db_ssl_ca = bstrdup(db_ssl_ca);
   }
   if (db_ssl_capath) {
      mdb->m_db_ssl_capath = bstrdup(db_ssl_capath);
   }
   if (db_ssl_cipher) {
      mdb->m_db_ssl_cipher = bstrdup(db_ssl_cipher);
   }
   mdb->m_db_port = db_port; 
 
   if (disable_batch_insert) { 
      mdb->m_disabled_batch_insert = true; 
      mdb->m_have_batch_insert = false; 
   } else { 
      mdb->m_disabled_batch_insert = false; 
#ifdef USE_BATCH_FILE_INSERT 
#ifdef HAVE_MYSQL_THREAD_SAFE 
      mdb->m_have_batch_insert = mysql_thread_safe(); 
#else 
      mdb->m_have_batch_insert = false; 
#endif /* HAVE_MYSQL_THREAD_SAFE */ 
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
 
get_out: 
   V(mutex); 
   return mdb; 
} 
 
 
/* 
 * Now actually open the database.  This can generate errors, 
 *  which are returned in the errmsg 
 * 
 * DO NOT close the database or delete mdb here !!!! 
 */ 
bool BDB_MYSQL::bdb_open_database(JCR *jcr) 
{ 
   BDB_MYSQL *mdb = this; 
   bool retval = false; 
   int errstat; 
   bool reconnect = true;
 
   P(mutex); 
   if (mdb->m_connected) { 
      retval = true; 
      goto get_out; 
   } 
 
   if ((errstat=rwl_init(&mdb->m_lock)) != 0) { 
      berrno be; 
      Mmsg1(&mdb->errmsg, _("Unable to initialize DB lock. ERR=%s\n"), 
            be.bstrerror(errstat)); 
      goto get_out; 
   } 
 
   /* 
    * Connect to the database 
    */ 
#ifdef xHAVE_EMBEDDED_MYSQL 
// mysql_server_init(0, NULL, NULL); 
#endif 
   mysql_init(&mdb->m_instance); 
 
   Dmsg0(50, "mysql_init done\n"); 

   /*
   * Sets the appropriate certificate options for
   * establishing secure connection using SSL to the database.
   */
   if (mdb->m_db_ssl_key) {
      mysql_ssl_set(&(mdb->m_instance),
                   mdb->m_db_ssl_key,
                   mdb->m_db_ssl_cert,
                   mdb->m_db_ssl_ca,
                   mdb->m_db_ssl_capath,
                   mdb->m_db_ssl_cipher);
   }

   /* 
    * If connection fails, try at 5 sec intervals for 30 seconds. 
    */ 
   for (int retry=0; retry < 6; retry++) { 
      mdb->m_db_handle = mysql_real_connect( 
           &(mdb->m_instance),      /* db */ 
           mdb->m_db_address,       /* default = localhost */ 
           mdb->m_db_user,          /* login name */ 
           mdb->m_db_password,      /* password */ 
           mdb->m_db_name,          /* database name */ 
           mdb->m_db_port,          /* default port */ 
           mdb->m_db_socket,        /* default = socket */ 
           CLIENT_FOUND_ROWS);      /* flags */ 
 
      /* 
       * If no connect, try once more in case it is a timing problem 
       */ 
      if (mdb->m_db_handle != NULL) { 
         break; 
      } 
      bmicrosleep(5,0); 
   } 
 
   mysql_options(&mdb->m_instance, MYSQL_OPT_RECONNECT, &reconnect); /* so connection does not timeout */ 
   Dmsg0(50, "mysql_real_connect done\n"); 
   Dmsg3(50, "db_user=%s db_name=%s db_password=%s\n", mdb->m_db_user, mdb->m_db_name, 
        (mdb->m_db_password == NULL) ? "(NULL)" : mdb->m_db_password); 

   if (mdb->m_db_handle == NULL) { 
      Mmsg2(&mdb->errmsg, _("Unable to connect to MySQL server.\n" 
"Database=%s User=%s\n" 
"MySQL connect failed either server not running or your authorization is incorrect.\n"), 
         mdb->m_db_name, mdb->m_db_user); 
#if MYSQL_VERSION_ID >= 40101 
      Dmsg3(50, "Error %u (%s): %s\n", 
            mysql_errno(&(mdb->m_instance)), mysql_sqlstate(&(mdb->m_instance)), 
            mysql_error(&(mdb->m_instance))); 
#else 
      Dmsg2(50, "Error %u: %s\n", 
            mysql_errno(&(mdb->m_instance)), mysql_error(&(mdb->m_instance))); 
#endif 
      goto get_out; 
   } 
 
   /* get the current cipher used for SSL connection */
   if (mdb->m_db_ssl_key) {
      const char *cipher;
      if (mdb->m_db_ssl_cipher) {
         free(mdb->m_db_ssl_cipher);
      }
      cipher = (const char *)mysql_get_ssl_cipher(&(mdb->m_instance));
      if (cipher) {
         mdb->m_db_ssl_cipher = bstrdup(cipher);
      }
      Dmsg1(50, "db_ssl_ciper=%s\n", (mdb->m_db_ssl_cipher == NULL) ? "(NULL)" : mdb->m_db_ssl_cipher);
   }

   mdb->m_connected = true; 
   if (!bdb_check_version(jcr)) { 
      goto get_out; 
   } 
 
   Dmsg3(100, "opendb ref=%d connected=%d db=%p\n", mdb->m_ref_count, mdb->m_connected, mdb->m_db_handle); 
 
   /* 
    * Set connection timeout to 8 days specialy for batch mode 
    */ 
   sql_query("SET wait_timeout=691200"); 
   sql_query("SET interactive_timeout=691200"); 
 
   retval = true; 
 
get_out: 
   V(mutex); 
   return retval; 
} 
 
void BDB_MYSQL::bdb_close_database(JCR *jcr) 
{ 
   BDB_MYSQL *mdb = this; 
 
   if (mdb->m_connected) { 
      bdb_end_transaction(jcr); 
   } 
   P(mutex); 
   mdb->m_ref_count--; 
   Dmsg3(100, "closedb ref=%d connected=%d db=%p\n", mdb->m_ref_count, mdb->m_connected, mdb->m_db_handle); 
   if (mdb->m_ref_count == 0) { 
      if (mdb->m_connected) { 
         sql_free_result(); 
      } 
      db_list->remove(mdb); 
      if (mdb->m_connected) { 
         Dmsg1(100, "close db=%p\n", mdb->m_db_handle); 
         mysql_close(&mdb->m_instance); 
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
      if (mdb->m_db_user) { 
         free(mdb->m_db_user); 
      } 
      if (mdb->m_db_password) { 
         free(mdb->m_db_password); 
      } 
      if (mdb->m_db_address) { 
         free(mdb->m_db_address); 
      } 
      if (mdb->m_db_socket) { 
         free(mdb->m_db_socket); 
      }
      if (mdb->m_db_ssl_mode) {
         free(mdb->m_db_ssl_mode);
      }
      if (mdb->m_db_ssl_key) {
         free(mdb->m_db_ssl_key);
      }
      if (mdb->m_db_ssl_cert) {
         free(mdb->m_db_ssl_cert);
      }
      if (mdb->m_db_ssl_ca) {
         free(mdb->m_db_ssl_ca);
      }
      if (mdb->m_db_ssl_capath) {
         free(mdb->m_db_ssl_capath);
      }
      if (mdb->m_db_ssl_cipher) {
         free(mdb->m_db_ssl_cipher);
      }
      delete mdb; 
      if (db_list->size() == 0) { 
         delete db_list; 
         db_list = NULL; 
      } 
   } 
   V(mutex); 
} 
 
/* 
 * This call is needed because the message channel thread 
 *  opens a database on behalf of a jcr that was created in 
 *  a different thread. MySQL then allocates thread specific 
 *  data, which is NOT freed when the original jcr thread 
 *  closes the database.  Thus the msgchan must call here 
 *  to cleanup any thread specific data that it created. 
 */ 
void BDB_MYSQL::bdb_thread_cleanup(void) 
{ 
#ifndef HAVE_WIN32 
   mysql_thread_end();       /* Cleanup thread specific data */ 
#endif 
} 
 
/* 
 * Escape strings so MySQL is happy 
 * 
 * len is the length of the old string. Your new 
 *   string must be long enough (max 2*old+1) to hold 
 *   the escaped output. 
 */ 
void BDB_MYSQL::bdb_escape_string(JCR *jcr, char *snew, char *old, int len) 
{ 
   BDB_MYSQL *mdb = this; 
   mysql_real_escape_string(mdb->m_db_handle, snew, old, len); 
} 
 
/* 
 * Escape binary object so that MySQL is happy 
 * Memory is stored in BDB struct, no need to free it 
 */ 
char *BDB_MYSQL::bdb_escape_object(JCR *jcr, char *old, int len) 
{ 
   BDB_MYSQL *mdb = this; 
   mdb->esc_obj = check_pool_memory_size(mdb->esc_obj, len*2+1); 
   mysql_real_escape_string(mdb->m_db_handle, mdb->esc_obj, old, len); 
   return mdb->esc_obj; 
} 
 
/* 
 * Unescape binary object so that MySQL is happy 
 */ 
void BDB_MYSQL::bdb_unescape_object(JCR *jcr, char *from, int32_t expected_len, 
                                    POOLMEM **dest, int32_t *dest_len) 
{ 
   if (!from) { 
      *dest[0] = 0; 
      *dest_len = 0; 
      return; 
   } 
   *dest = check_pool_memory_size(*dest, expected_len+1); 
   *dest_len = expected_len; 
   memcpy(*dest, from, expected_len); 
   (*dest)[expected_len]=0; 
} 
 
void BDB_MYSQL::bdb_start_transaction(JCR *jcr) 
{ 
   if (!jcr->attr) { 
      jcr->attr = get_pool_memory(PM_FNAME); 
   } 
   if (!jcr->ar) { 
      jcr->ar = (ATTR_DBR *)malloc(sizeof(ATTR_DBR)); 
      memset(jcr->ar, 0, sizeof(ATTR_DBR));
   } 
} 
 
void BDB_MYSQL::bdb_end_transaction(JCR *jcr) 
{ 
} 
 
/* 
 * Submit a general SQL command (cmd), and for each row returned, 
 * the result_handler is called with the ctx. 
 */ 
bool BDB_MYSQL::bdb_sql_query(const char *query, DB_RESULT_HANDLER *result_handler, void *ctx) 
{ 
   int ret; 
   SQL_ROW row; 
   bool send = true; 
   bool retval = false; 
   BDB_MYSQL *mdb = this; 
 
   Dmsg1(500, "db_sql_query starts with %s\n", query); 
 
   bdb_lock(); 
   errmsg[0] = 0; 
   ret = mysql_query(m_db_handle, query); 
   if (ret != 0) { 
      Mmsg(mdb->errmsg, _("Query failed: %s: ERR=%s\n"), query, sql_strerror()); 
      Dmsg0(500, "db_sql_query failed\n"); 
      goto get_out; 
   } 
 
   Dmsg0(500, "db_sql_query succeeded. checking handler\n"); 
 
   if (result_handler) { 
      if ((mdb->m_result = mysql_use_result(mdb->m_db_handle)) != NULL) { 
         mdb->m_num_fields = mysql_num_fields(mdb->m_result); 
 
         /* 
          * We *must* fetch all rows 
          */ 
         while ((row = mysql_fetch_row(m_result))) { 
            if (send) { 
               /* the result handler returns 1 when it has 
                *  seen all the data it wants.  However, we 
                *  loop to the end of the data. 
                */ 
               if (result_handler(ctx, mdb->m_num_fields, row)) { 
                  send = false; 
               } 
            } 
         } 
         sql_free_result(); 
      } 
   } 
 
   Dmsg0(500, "db_sql_query finished\n"); 
   retval = true; 
 
get_out: 
   bdb_unlock(); 
   return retval; 
} 
 
bool BDB_MYSQL::sql_query(const char *query, int flags) 
{ 
   int ret; 
   bool retval = true; 
   BDB_MYSQL *mdb = this; 
 
   Dmsg1(500, "sql_query starts with '%s'\n", query); 
   /* 
    * We are starting a new query. reset everything. 
    */ 
   mdb->m_num_rows     = -1; 
   mdb->m_row_number   = -1; 
   mdb->m_field_number = -1; 
 
   if (mdb->m_result) { 
      mysql_free_result(mdb->m_result); 
      mdb->m_result = NULL; 
   } 
 
   ret = mysql_query(mdb->m_db_handle, query); 
   if (ret == 0) { 
      Dmsg0(500, "we have a result\n"); 
      if (flags & QF_STORE_RESULT) { 
         mdb->m_result = mysql_store_result(mdb->m_db_handle); 
         if (mdb->m_result != NULL) { 
            mdb->m_num_fields = mysql_num_fields(mdb->m_result); 
            Dmsg1(500, "we have %d fields\n", mdb->m_num_fields); 
            mdb->m_num_rows = mysql_num_rows(mdb->m_result); 
            Dmsg1(500, "we have %d rows\n", mdb->m_num_rows); 
         } else { 
            mdb->m_num_fields = 0; 
            mdb->m_num_rows = mysql_affected_rows(mdb->m_db_handle); 
            Dmsg1(500, "we have %d rows\n", mdb->m_num_rows); 
         } 
      } else { 
         mdb->m_num_fields = 0; 
         mdb->m_num_rows = mysql_affected_rows(mdb->m_db_handle); 
         Dmsg1(500, "we have %d rows\n", mdb->m_num_rows); 
      } 
   } else { 
      Dmsg0(500, "we failed\n"); 
      mdb->m_status = 1;                   /* failed */ 
      retval = false; 
   } 
   return retval; 
} 
 
void BDB_MYSQL::sql_free_result(void) 
{ 
   BDB_MYSQL *mdb = this; 
   bdb_lock(); 
   if (mdb->m_result) { 
      mysql_free_result(mdb->m_result); 
      mdb->m_result = NULL; 
   } 
   if (mdb->m_fields) { 
      free(mdb->m_fields); 
      mdb->m_fields = NULL; 
   } 
   mdb->m_num_rows = mdb->m_num_fields = 0; 
   bdb_unlock(); 
} 
 
SQL_ROW BDB_MYSQL::sql_fetch_row(void) 
{ 
   BDB_MYSQL *mdb = this; 
   if (!mdb->m_result) { 
      return NULL; 
   } else { 
      return mysql_fetch_row(mdb->m_result); 
   } 
} 
 
const char *BDB_MYSQL::sql_strerror(void) 
{ 
   BDB_MYSQL *mdb = this; 
   return mysql_error(mdb->m_db_handle); 
} 
 
void BDB_MYSQL::sql_data_seek(int row) 
{ 
   BDB_MYSQL *mdb = this; 
   return mysql_data_seek(mdb->m_result, row); 
} 
 
int BDB_MYSQL::sql_affected_rows(void) 
{ 
   BDB_MYSQL *mdb = this; 
   return mysql_affected_rows(mdb->m_db_handle); 
} 
 
uint64_t BDB_MYSQL::sql_insert_autokey_record(const char *query, const char *table_name) 
{ 
   BDB_MYSQL *mdb = this; 
   /* 
    * First execute the insert query and then retrieve the currval. 
    */ 
   if (mysql_query(mdb->m_db_handle, query) != 0) { 
      return 0; 
   } 
 
   mdb->m_num_rows = mysql_affected_rows(mdb->m_db_handle); 
   if (mdb->m_num_rows != 1) { 
      return 0; 
   } 
 
   mdb->changes++; 
 
   return mysql_insert_id(mdb->m_db_handle); 
} 
 
SQL_FIELD *BDB_MYSQL::sql_fetch_field(void) 
{ 
   int i; 
   MYSQL_FIELD *field; 
   BDB_MYSQL *mdb = this; 
 
   if (!mdb->m_fields || mdb->m_fields_size < mdb->m_num_fields) { 
      if (mdb->m_fields) { 
         free(mdb->m_fields); 
         mdb->m_fields = NULL; 
      } 
      Dmsg1(500, "allocating space for %d fields\n", mdb->m_num_fields); 
      mdb->m_fields = (SQL_FIELD *)malloc(sizeof(SQL_FIELD) * mdb->m_num_fields); 
      mdb->m_fields_size = mdb->m_num_fields; 
 
      for (i = 0; i < mdb->m_num_fields; i++) { 
         Dmsg1(500, "filling field %d\n", i); 
         if ((field = mysql_fetch_field(mdb->m_result)) != NULL) { 
            mdb->m_fields[i].name = field->name; 
            mdb->m_fields[i].max_length = field->max_length; 
            mdb->m_fields[i].type = field->type; 
            mdb->m_fields[i].flags = field->flags; 
 
            Dmsg4(500, "sql_fetch_field finds field '%s' has length='%d' type='%d' and IsNull=%d\n", 
                  mdb->m_fields[i].name, mdb->m_fields[i].max_length, mdb->m_fields[i].type, mdb->m_fields[i].flags); 
         } 
      } 
   } 
 
   /* 
    * Increment field number for the next time around 
    */ 
   return &mdb->m_fields[mdb->m_field_number++]; 
} 
 
bool BDB_MYSQL::sql_field_is_not_null(int field_type) 
{ 
   return IS_NOT_NULL(field_type); 
} 
 
bool BDB_MYSQL::sql_field_is_numeric(int field_type) 
{ 
   return IS_NUM(field_type); 
} 
 
/* 
 * Returns true  if OK 
 *         false if failed 
 */ 
bool BDB_MYSQL::sql_batch_start(JCR *jcr) 
{ 
   BDB_MYSQL *mdb = this; 
   bool retval; 
 
   bdb_lock(); 
   retval = sql_query("CREATE TEMPORARY TABLE batch (" 
                      "FileIndex integer," 
                      "JobId integer," 
                      "Path blob," 
                      "Name blob," 
                      "LStat tinyblob," 
                      "MD5 tinyblob," 
                      "DeltaSeq integer)"); 
   bdb_unlock(); 
 
   /* 
    * Keep track of the number of changes in batch mode. 
    */ 
   mdb->changes = 0; 
 
   return retval; 
} 
 
/* set error to something to abort operation */ 
/* 
 * Returns true  if OK 
 *         false if failed 
 */ 
bool BDB_MYSQL::sql_batch_end(JCR *jcr, const char *error) 
{ 
   BDB_MYSQL *mdb = this; 
 
   mdb->m_status = 0; 
 
   /* 
    * Flush any pending inserts. 
    */ 
   if (mdb->changes) { 
      return sql_query(mdb->cmd); 
   } 
 
   return true; 
} 
 
/* 
 * Returns true  if OK 
 *         false if failed 
 */ 
bool BDB_MYSQL::sql_batch_insert(JCR *jcr, ATTR_DBR *ar) 
{ 
   BDB_MYSQL *mdb = this; 
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
 
   /* 
    * Try to batch up multiple inserts using multi-row inserts. 
    */ 
   if (mdb->changes == 0) { 
      Mmsg(cmd, "INSERT INTO batch VALUES " 
           "(%d,%s,'%s','%s','%s','%s',%u)", 
           ar->FileIndex, edit_int64(ar->JobId,ed1), mdb->esc_path, 
           mdb->esc_name, ar->attr, digest, ar->DeltaSeq); 
      mdb->changes++; 
   } else { 
      /* 
       * We use the esc_obj for temporary storage otherwise 
       * we keep on copying data. 
       */ 
      Mmsg(mdb->esc_obj, ",(%d,%s,'%s','%s','%s','%s',%u)", 
           ar->FileIndex, edit_int64(ar->JobId,ed1), mdb->esc_path, 
           mdb->esc_name, ar->attr, digest, ar->DeltaSeq); 
      pm_strcat(mdb->cmd, mdb->esc_obj); 
      mdb->changes++; 
   } 
 
   /* 
    * See if we need to flush the query buffer filled 
    * with multi-row inserts. 
    */ 
   if ((mdb->changes % MYSQL_CHANGES_PER_BATCH_INSERT) == 0) { 
      if (!sql_query(mdb->cmd)) { 
         mdb->changes = 0; 
         return false; 
      } else { 
         mdb->changes = 0; 
      } 
   } 
   return true; 
} 
 
 
#endif /* HAVE_MYSQL */ 
