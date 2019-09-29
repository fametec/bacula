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
 * Bacula Catalog Database routines specific to PostgreSQL
 *   These are PostgreSQL specific routines
 *
 *    Dan Langille, December 2003
 *    based upon work done by Kern Sibbald, March 2000
 *
 * Note: at one point, this file was changed to class based by a certain 
 *  programmer, and other than "wrapping" in a class, which is a trivial
 *  change for a C++ programmer, nothing substantial was done, yet all the
 *  code was recommitted under this programmer's name.  Consequently, we
 *  undo those changes here.  Unfortunately, it is too difficult to put
 *  back the original author's name (Dan Langille) on the parts he wrote.
 */

#include "bacula.h"

#ifdef HAVE_POSTGRESQL

#include  "cats.h"

/* Note in this file, we want these for Postgresql not Bacula */
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include  "libpq-fe.h"
#include  "postgres_ext.h"       /* needed for NAMEDATALEN */
#include  "pg_config_manual.h"   /* get NAMEDATALEN on version 8.3 or later */
#include  "pg_config.h"          /* for PG_VERSION_NUM */
#define __BDB_POSTGRESQL_H_ 1
#include  "bdb_postgresql.h"

#define dbglvl_dbg   DT_SQL|100
#define dbglvl_info  DT_SQL|50
#define dbglvl_err   DT_SQL|10

/* -----------------------------------------------------------------------
 *
 *   PostgreSQL dependent defines and subroutines
 *
 * -----------------------------------------------------------------------
 */

/* List of open databases */
static dlist *db_list = NULL; 

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 

BDB_POSTGRESQL::BDB_POSTGRESQL(): BDB()
{
   BDB_POSTGRESQL *mdb = this;

   if (db_list == NULL) {
      db_list = New(dlist(mdb, &mdb->m_link));
   }
   mdb->m_db_driver_type = SQL_DRIVER_TYPE_POSTGRESQL;
   mdb->m_db_type = SQL_TYPE_POSTGRESQL;
   mdb->m_db_driver = bstrdup("PostgreSQL");

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
   mdb->m_buf =  get_pool_memory(PM_FNAME);

   db_list->append(this);
}

BDB_POSTGRESQL::~BDB_POSTGRESQL()
{
}

/*
 * Initialize database data structure. In principal this should
 * never have errors, or it is really fatal.
 */
BDB *db_init_database(JCR *jcr, const char *db_driver, const char *db_name, const char *db_user, 
                       const char *db_password, const char *db_address, int db_port, const char *db_socket, 
                       const char *db_ssl_mode, const char *db_ssl_key, const char *db_ssl_cert,
                       const char *db_ssl_ca, const char *db_ssl_capath, const char *db_ssl_cipher,
                       bool mult_db_connections, bool disable_batch_insert) 
{
   BDB_POSTGRESQL *mdb = NULL;

   if (!db_user) {
      Jmsg(jcr, M_FATAL, 0, _("A user name for PostgreSQL must be supplied.\n"));
      return NULL;
   }
   P(mutex);                          /* lock DB queue */
   if (db_list && !mult_db_connections) {
      /*
       * Look to see if DB already open
       */
      foreach_dlist(mdb, db_list) {
         if (mdb->bdb_match_database(db_driver, db_name, db_address, db_port)) {
            Dmsg1(dbglvl_info, "DB REopen %s\n", db_name);
            mdb->increment_refcount();
            goto get_out;
         }
      }
   }
   Dmsg0(dbglvl_info, "db_init_database first time\n");
   /* Create the global Bacula db context */
   mdb = New(BDB_POSTGRESQL());
   if (!mdb) goto get_out;

   /* Initialize the parent class members. */
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
      mdb->m_db_ssl_mode = bstrdup("prefer");
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
   mdb->m_db_port = db_port;

   if (disable_batch_insert) { 
      mdb->m_disabled_batch_insert = true;
      mdb->m_have_batch_insert = false;
   } else { 
      mdb->m_disabled_batch_insert = false;
#ifdef USE_BATCH_FILE_INSERT
#if defined(HAVE_POSTGRESQL_BATCH_FILE_INSERT) || defined(HAVE_PQISTHREADSAFE) 
#ifdef HAVE_PQISTHREADSAFE 
      mdb->m_have_batch_insert = PQisthreadsafe();
#else 
      mdb->m_have_batch_insert = true;
#endif /* HAVE_PQISTHREADSAFE */ 
#else 
      mdb->m_have_batch_insert = true;
#endif /* HAVE_POSTGRESQL_BATCH_FILE_INSERT || HAVE_PQISTHREADSAFE */ 
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
  
 
/* Check that the database corresponds to the encoding we want  */
static bool pgsql_check_database_encoding(JCR *jcr, BDB_POSTGRESQL *mdb)
{
   SQL_ROW row;
   int ret = false; 

   if (!mdb->sql_query("SELECT getdatabaseencoding()", QF_STORE_RESULT)) { 
      Jmsg(jcr, M_ERROR, 0, "%s", mdb->errmsg);
      return false; 
   }

   if ((row = mdb->sql_fetch_row()) == NULL) { 
      Mmsg1(mdb->errmsg, _("error fetching row: %s\n"), mdb->sql_strerror()); 
      Jmsg(jcr, M_ERROR, 0, "Can't check database encoding %s", mdb->errmsg);
   } else { 
      ret = bstrcmp(row[0], "SQL_ASCII");

      if (ret) {
         /* If we are in SQL_ASCII, we can force the client_encoding to SQL_ASCII too */
         mdb->sql_query("SET client_encoding TO 'SQL_ASCII'"); 

      } else { 
         /* Something is wrong with database encoding */
         Mmsg(mdb->errmsg,
              _("Encoding error for database \"%s\". Wanted SQL_ASCII, got %s\n"),
              mdb->get_db_name(), row[0]); 
         Jmsg(jcr, M_WARNING, 0, "%s", mdb->errmsg);
         Dmsg1(dbglvl_err, "%s", mdb->errmsg);
      }
   }
   return ret;
}

/*
 * Now actually open the database.  This can generate errors,
 *   which are returned in the errmsg
 *
 *  DO NOT close the database or delete mdb here !!!!
 */
bool BDB_POSTGRESQL::bdb_open_database(JCR *jcr)
{
   bool retval = false; 
   int errstat;
   char buf[10], *port;
   BDB_POSTGRESQL *mdb = this;

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

   if (mdb->m_db_port) {
      bsnprintf(buf, sizeof(buf), "%d", mdb->m_db_port);
      port = buf;
   } else {
      port = NULL;
   }

   /* Tells libpq that the SSL library has already been initialized */
   PQinitSSL(0);

   /* If connection fails, try at 5 sec intervals for 30 seconds. */
   for (int retry=0; retry < 6; retry++) {
      /* connect to the database */

#if PG_VERSION_NUM < 90000

      /* Old "depreciated" connection call */
      mdb->m_db_handle = PQsetdbLogin(
           mdb->m_db_address,         /* default = localhost */
           port,                      /* default port */
           NULL,                      /* pg options */
           NULL,                      /* tty, ignored */
           mdb->m_db_name,            /* database name */
           mdb->m_db_user,            /* login name */
           mdb->m_db_password);       /* password */
#else
      /* Code for Postgresql 9.0 and greater */
      const char *keywords[10] = {"host", "port",
                                  "dbname", "user",
                                  "password", "sslmode",
                                  "sslkey", "sslcert",
                                  "sslrootcert", NULL };
      const char *values[10] = {mdb->m_db_address, /* default localhost */
                                port, /* default port */
                                mdb->m_db_name,
                                mdb->m_db_user,
                                mdb->m_db_password,
                                mdb->m_db_ssl_mode,
                                mdb->m_db_ssl_key,
                                mdb->m_db_ssl_cert,
                                mdb->m_db_ssl_ca,
                                NULL };
      mdb->m_db_handle = PQconnectdbParams(keywords, values, 0);
#endif

      /* If no connect, try once more in case it is a timing problem */
      if (PQstatus(mdb->m_db_handle) == CONNECTION_OK) {
         break;
      }
      bmicrosleep(5, 0);
   }

   Dmsg0(dbglvl_info, "pg_real_connect done\n");
   Dmsg3(dbglvl_info, "db_user=%s db_name=%s db_password=%s\n", mdb->m_db_user, mdb->m_db_name,
        mdb->m_db_password==NULL?"(NULL)":mdb->m_db_password);

#ifdef HAVE_OPENSSL
   #define USE_OPENSSL 1
   SSL *ssl;
   if (PQgetssl(mdb->m_db_handle) != NULL) {
      Dmsg0(dbglvl_info, "SSL in use\n");
      ssl = (SSL *)PQgetssl(mdb->m_db_handle);
      Dmsg2(dbglvl_info, "Version:%s Cipher:%s\n", SSL_get_version(ssl), SSL_get_cipher(ssl)); 
   } else {
      Dmsg0(dbglvl_info, "SSL not in use\n");
   }
#endif

   if (PQstatus(mdb->m_db_handle) != CONNECTION_OK) {
      Mmsg2(&mdb->errmsg, _("Unable to connect to PostgreSQL server. Database=%s User=%s\n"
         "Possible causes: SQL server not running; password incorrect; max_connections exceeded.\n"),
         mdb->m_db_name, mdb->m_db_user);
      goto get_out;
   }

   mdb->m_connected = true;
   if (!bdb_check_version(jcr)) {
      goto get_out;
   }

   sql_query("SET datestyle TO 'ISO, YMD'"); 
   sql_query("SET cursor_tuple_fraction=1");

   /* 
    * Tell PostgreSQL we are using standard conforming strings and avoid warnings such as:
    *   WARNING:  nonstandard use of \\ in a string literal
    */ 
   sql_query("SET standard_conforming_strings=on"); 
 
   /* Check that encoding is SQL_ASCII */
   pgsql_check_database_encoding(jcr, mdb);
 
   retval = true; 

get_out:
   V(mutex);
   return retval; 
} 

void BDB_POSTGRESQL::bdb_close_database(JCR *jcr)
{
   BDB_POSTGRESQL *mdb = this;

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
         PQfinish(mdb->m_db_handle);
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
      free_pool_memory(mdb->m_buf);
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
      delete mdb;
      if (db_list->size() == 0) {
         delete db_list;
         db_list = NULL;
      }
   }
   V(mutex);
}

void BDB_POSTGRESQL::bdb_thread_cleanup(void)
{
}

/*
 * Escape strings so PostgreSQL is happy
 *
 *  len is the length of the old string. Your new
 *    string must be long enough (max 2*old+1) to hold
 *    the escaped output.
 */
void BDB_POSTGRESQL::bdb_escape_string(JCR *jcr, char *snew, char *old, int len)
{
   BDB_POSTGRESQL *mdb = this;
   int failed; 

   PQescapeStringConn(mdb->m_db_handle, snew, old, len, &failed);
   if (failed) {
      Jmsg(jcr, M_FATAL, 0, _("PQescapeStringConn returned non-zero.\n")); 
      /* failed on encoding, probably invalid multibyte encoding in the source string
         see PQescapeStringConn documentation for details. */
      Dmsg0(dbglvl_err, "PQescapeStringConn failed\n");
   } 
}

/*
 * Escape binary so that PostgreSQL is happy
 *
 */
char *BDB_POSTGRESQL::bdb_escape_object(JCR *jcr, char *old, int len)
{
   size_t new_len;
   unsigned char *obj; 
   BDB_POSTGRESQL *mdb = this;

   mdb->esc_obj[0] = 0;
   obj = PQescapeByteaConn(mdb->m_db_handle, (unsigned const char *)old, len, &new_len);
   if (!obj) { 
      Jmsg(jcr, M_FATAL, 0, _("PQescapeByteaConn returned NULL.\n"));
   } else {
      mdb->esc_obj = check_pool_memory_size(mdb->esc_obj, new_len+1);
      memcpy(mdb->esc_obj, obj, new_len);
      mdb->esc_obj[new_len] = 0;
      PQfreemem(obj);
   }
   return (char *)mdb->esc_obj;
}

/*
 * Unescape binary object so that PostgreSQL is happy
 *
 */
void BDB_POSTGRESQL::bdb_unescape_object(JCR *jcr, char *from, int32_t expected_len,
                       POOLMEM **dest, int32_t *dest_len)
{
   size_t new_len;
   unsigned char *obj;

   if (!from) {
      *dest[0] = 0;
      *dest_len = 0;
      return;
   }

   obj = PQunescapeBytea((unsigned const char *)from, &new_len);

   if (!obj) {
      Jmsg(jcr, M_FATAL, 0, _("PQunescapeByteaConn returned NULL.\n"));
   }

   *dest_len = new_len;
   *dest = check_pool_memory_size(*dest, new_len+1);
   memcpy(*dest, obj, new_len);
   (*dest)[new_len]=0;

   PQfreemem(obj);

   Dmsg1(dbglvl_info, "obj size: %d\n", *dest_len);
}

/*
 * Start a transaction. This groups inserts and makes things more efficient.
 *  Usually started when inserting file attributes.
 */
void BDB_POSTGRESQL::bdb_start_transaction(JCR *jcr)
{
   BDB_POSTGRESQL *mdb = this;

   if (!jcr->attr) { 
      jcr->attr = get_pool_memory(PM_FNAME); 
   }
   if (!jcr->ar) { 
      jcr->ar = (ATTR_DBR *)malloc(sizeof(ATTR_DBR)); 
      memset(jcr->ar, 0, sizeof(ATTR_DBR));
   }

   /* 
    * This is turned off because transactions break if
    *  multiple simultaneous jobs are run.
    */ 
   if (!mdb->m_allow_transactions) {
      return; 
   }

   bdb_lock();
   /* Allow only 25,000 changes per transaction */
   if (mdb->m_transaction && changes > 25000) {
      bdb_end_transaction(jcr);
   } 
   if (!mdb->m_transaction) {
      sql_query("BEGIN");             /* begin transaction */
      Dmsg0(dbglvl_info, "Start PosgreSQL transaction\n");
      mdb->m_transaction = true;
   } 
   bdb_unlock();
}

void BDB_POSTGRESQL::bdb_end_transaction(JCR *jcr)
{
   BDB_POSTGRESQL *mdb = this;

   if (!mdb->m_allow_transactions) {
      return; 
   }

   bdb_lock();
   if (mdb->m_transaction) {
      sql_query("COMMIT");            /* end transaction */
      mdb->m_transaction = false;
      Dmsg1(dbglvl_info, "End PostgreSQL transaction changes=%d\n", changes);
   }
   changes = 0; 
   bdb_unlock();
}


/*
 * Submit a general SQL command, and for each row returned,
 *  the result_handler is called with the ctx.
 */
bool BDB_POSTGRESQL::bdb_big_sql_query(const char *query,
                                       DB_RESULT_HANDLER *result_handler,
                                       void *ctx)
{
   BDB_POSTGRESQL *mdb = this;
   SQL_ROW row; 
   bool retval = false; 
   bool in_transaction = mdb->m_transaction;

   Dmsg1(dbglvl_info, "db_sql_query starts with '%s'\n", query);

   mdb->errmsg[0] = 0;
   /* This code handles only SELECT queries */
   if (strncasecmp(query, "SELECT", 6) != 0) {
      return bdb_sql_query(query, result_handler, ctx);
   }

   if (!result_handler) {       /* no need of big_query without handler */
      return false; 
   }

   bdb_lock();

   if (!in_transaction) {       /* CURSOR needs transaction */
      sql_query("BEGIN");
   }

   Mmsg(m_buf, "DECLARE _bac_cursor CURSOR FOR %s", query);

   if (!sql_query(mdb->m_buf)) {
      Mmsg(mdb->errmsg, _("Query failed: %s: ERR=%s\n"), mdb->m_buf, sql_strerror());
      Dmsg1(dbglvl_err, "%s\n", mdb->errmsg);
      goto get_out;
   }

   do {
      if (!sql_query("FETCH 100 FROM _bac_cursor")) {
         Mmsg(mdb->errmsg, _("Fetch failed: ERR=%s\n"), sql_strerror());
         Dmsg1(dbglvl_err, "%s\n", mdb->errmsg);
         goto get_out;
      }
      while ((row = sql_fetch_row()) != NULL) {
         Dmsg1(dbglvl_info, "Fetching %d rows\n", mdb->m_num_rows);
         if (result_handler(ctx, mdb->m_num_fields, row))
            break;
      }
      PQclear(mdb->m_result);
      m_result = NULL;

   } while (m_num_rows > 0);    /* TODO: Can probably test against 100 */

   sql_query("CLOSE _bac_cursor");

   Dmsg0(dbglvl_info, "db_big_sql_query finished\n");
   sql_free_result();
   retval = true;

get_out:
   if (!in_transaction) {
      sql_query("COMMIT");  /* end transaction */
   }

   bdb_unlock();
   return retval;
}

/* 
 * Submit a general SQL command, and for each row returned,
 *  the result_handler is called with the ctx.
 */ 
bool BDB_POSTGRESQL::bdb_sql_query(const char *query, DB_RESULT_HANDLER *result_handler, void *ctx)
{
   SQL_ROW row; 
   bool retval = true; 
   BDB_POSTGRESQL *mdb = this;

   Dmsg1(dbglvl_info, "db_sql_query starts with '%s'\n", query);

   bdb_lock();
   mdb->errmsg[0] = 0;
   if (!sql_query(query, QF_STORE_RESULT)) { 
      Mmsg(mdb->errmsg, _("Query failed: %s: ERR=%s\n"), query, sql_strerror());
      Dmsg0(dbglvl_err, "db_sql_query failed\n");
      retval = false; 
      goto get_out;
   } 

   Dmsg0(dbglvl_info, "db_sql_query succeeded. checking handler\n");

   if (result_handler) {
      Dmsg0(dbglvl_dbg, "db_sql_query invoking handler\n");
      while ((row = sql_fetch_row())) {
         Dmsg0(dbglvl_dbg, "db_sql_query sql_fetch_row worked\n");
         if (result_handler(ctx, mdb->m_num_fields, row))
            break; 
      } 
      sql_free_result(); 
   } 

   Dmsg0(dbglvl_info, "db_sql_query finished\n");

get_out:
   bdb_unlock();
   return retval; 
}

/*
 * If this routine returns false (failure), Bacula expects
 *   that no result has been stored.
 * This is where QueryDB calls to with Postgresql.
 *
 *  Returns: true  on success
 *           false on failure
 *
 */
bool BDB_POSTGRESQL::sql_query(const char *query, int flags)
{
   int i; 
   bool retval = false; 
   BDB_POSTGRESQL *mdb = this;

   Dmsg1(dbglvl_info, "sql_query starts with '%s'\n", query);

   /* We are starting a new query. reset everything. */
   mdb->m_num_rows     = -1;
   mdb->m_row_number   = -1;
   mdb->m_field_number = -1;

   if (mdb->m_result) {
      PQclear(mdb->m_result);  /* hmm, someone forgot to free?? */
      mdb->m_result = NULL;
   } 

   for (i = 0; i < 10; i++) { 
      mdb->m_result = PQexec(mdb->m_db_handle, query);
      if (mdb->m_result) {
         break;
      }
      bmicrosleep(5, 0);
   }
   if (!mdb->m_result) {
      Dmsg1(dbglvl_err, "Query failed: %s\n", query);
      goto get_out;
   }

   mdb->m_status = PQresultStatus(mdb->m_result);
   if (mdb->m_status == PGRES_TUPLES_OK || mdb->m_status == PGRES_COMMAND_OK) {
      Dmsg0(dbglvl_dbg, "we have a result\n");

      /* How many fields in the set? */
      mdb->m_num_fields = (int)PQnfields(mdb->m_result);
      Dmsg1(dbglvl_dbg, "we have %d fields\n", mdb->m_num_fields);

      mdb->m_num_rows = PQntuples(mdb->m_result);
      Dmsg1(dbglvl_dbg, "we have %d rows\n", mdb->m_num_rows);

      mdb->m_row_number = 0;      /* we can start to fetch something */
      mdb->m_status = 0;          /* succeed */
      retval = true; 
   } else {
      Dmsg1(dbglvl_err, "Result status failed: %s\n", query);
      goto get_out;
   }

   Dmsg0(dbglvl_info, "sql_query finishing\n");
   goto ok_out; 

get_out:
   Dmsg0(dbglvl_err, "we failed\n");
   PQclear(mdb->m_result);
   mdb->m_result = NULL;
   mdb->m_status = 1;                   /* failed */
 
ok_out: 
   return retval; 
}  
 
void BDB_POSTGRESQL::sql_free_result(void)
{
   BDB_POSTGRESQL *mdb = this;

   bdb_lock();
   if (mdb->m_result) {
      PQclear(mdb->m_result);
      mdb->m_result = NULL;
   }
   if (mdb->m_rows) {
      free(mdb->m_rows);
      mdb->m_rows = NULL;
   }
   if (mdb->m_fields) {
      free(mdb->m_fields);
      mdb->m_fields = NULL;
   } 
   mdb->m_num_rows = mdb->m_num_fields = 0;
   bdb_unlock();
} 

SQL_ROW BDB_POSTGRESQL::sql_fetch_row(void)
{ 
   SQL_ROW row = NULL;            /* by default, return NULL */
   BDB_POSTGRESQL *mdb = this;
 
   Dmsg0(dbglvl_info, "sql_fetch_row start\n");
 
   if (mdb->m_num_fields == 0) {     /* No field, no row */
      Dmsg0(dbglvl_err, "sql_fetch_row finishes returning NULL, no fields\n");
      return NULL;
   }

   if (!mdb->m_rows || mdb->m_rows_size < mdb->m_num_fields) {
      if (mdb->m_rows) {
         Dmsg0(dbglvl_dbg, "sql_fetch_row freeing space\n");
         free(mdb->m_rows);
      } 
      Dmsg1(dbglvl_dbg, "we need space for %d bytes\n", sizeof(char *) * mdb->m_num_fields);
      mdb->m_rows = (SQL_ROW)malloc(sizeof(char *) * mdb->m_num_fields);
      mdb->m_rows_size = mdb->m_num_fields;
 
      /* Now reset the row_number now that we have the space allocated */
      mdb->m_row_number = 0;
   }

   /* If still within the result set */
   if (mdb->m_row_number >= 0 && mdb->m_row_number < mdb->m_num_rows) {
      Dmsg2(dbglvl_dbg, "sql_fetch_row row number '%d' is acceptable (0..%d)\n", mdb->m_row_number, m_num_rows);

      /* Get each value from this row */
      for (int j = 0; j < mdb->m_num_fields; j++) {
         mdb->m_rows[j] = PQgetvalue(mdb->m_result, mdb->m_row_number, j);
         Dmsg2(dbglvl_dbg, "sql_fetch_row field '%d' has value '%s'\n", j, mdb->m_rows[j]);
      } 
      mdb->m_row_number++;  /* Increment the row number for the next call */
      row = mdb->m_rows;
   } else { 
      Dmsg2(dbglvl_dbg, "sql_fetch_row row number '%d' is NOT acceptable (0..%d)\n", mdb->m_row_number, m_num_rows);
   }
 
   Dmsg1(dbglvl_info, "sql_fetch_row finishes returning %p\n", row);
 
   return row; 
} 

const char *BDB_POSTGRESQL::sql_strerror(void)
{   
   BDB_POSTGRESQL *mdb = this;
   return PQerrorMessage(mdb->m_db_handle);
} 

void BDB_POSTGRESQL::sql_data_seek(int row)
{ 
   BDB_POSTGRESQL *mdb = this;
   /* Set the row number to be returned on the next call to sql_fetch_row */
   mdb->m_row_number = row;
} 

int BDB_POSTGRESQL::sql_affected_rows(void)
{ 
   BDB_POSTGRESQL *mdb = this;
   return (unsigned)str_to_int32(PQcmdTuples(mdb->m_result));
} 
 
uint64_t BDB_POSTGRESQL::sql_insert_autokey_record(const char *query, const char *table_name)
{ 
   uint64_t id = 0; 
   char sequence[NAMEDATALEN-1]; 
   char getkeyval_query[NAMEDATALEN+50]; 
   PGresult *p_result;
   BDB_POSTGRESQL *mdb = this;

   /* First execute the insert query and then retrieve the currval. */
   if (!sql_query(query)) { 
      return 0; 
   } 

   mdb->m_num_rows = sql_affected_rows();
   if (mdb->m_num_rows != 1) {
      return 0; 
   } 
   mdb->changes++;
   /* 
    *  Obtain the current value of the sequence that
    *  provides the serial value for primary key of the table.
    * 
    *  currval is local to our session.  It is not affected by
    *  other transactions.
    * 
    *  Determine the name of the sequence.
    *  PostgreSQL automatically creates a sequence using
    *   <table>_<column>_seq.
    *  At the time of writing, all tables used this format for
    *   for their primary key: <table>id
    *  Except for basefiles which has a primary key on baseid.
    *  Therefore, we need to special case that one table.
    * 
    *  everything else can use the PostgreSQL formula.
    */ 
   if (strcasecmp(table_name, "basefiles") == 0) {
      bstrncpy(sequence, "basefiles_baseid", sizeof(sequence));
   } else {
      bstrncpy(sequence, table_name, sizeof(sequence));
      bstrncat(sequence, "_",        sizeof(sequence));
      bstrncat(sequence, table_name, sizeof(sequence));
      bstrncat(sequence, "id",       sizeof(sequence));
   }

   bstrncat(sequence, "_seq", sizeof(sequence));
   bsnprintf(getkeyval_query, sizeof(getkeyval_query), "SELECT currval('%s')", sequence); 

   Dmsg1(dbglvl_info, "sql_insert_autokey_record executing query '%s'\n", getkeyval_query);
   for (int i = 0; i < 10; i++) { 
      p_result = PQexec(mdb->m_db_handle, getkeyval_query);
      if (p_result) {
         break;
      }
      bmicrosleep(5, 0);
   }
   if (!p_result) {
      Dmsg1(dbglvl_err, "Query failed: %s\n", getkeyval_query);
      goto get_out;
   }

   Dmsg0(dbglvl_dbg, "exec done");

   if (PQresultStatus(p_result) == PGRES_TUPLES_OK) {
      Dmsg0(dbglvl_dbg, "getting value");
      id = str_to_uint64(PQgetvalue(p_result, 0, 0));
      Dmsg2(dbglvl_dbg, "got value '%s' which became %d\n", PQgetvalue(p_result, 0, 0), id);
   } else {
      Dmsg1(dbglvl_err, "Result status failed: %s\n", getkeyval_query);
      Mmsg1(&mdb->errmsg, _("error fetching currval: %s\n"), PQerrorMessage(mdb->m_db_handle));
   }

get_out:
   PQclear(p_result);
   return id;
} 

SQL_FIELD *BDB_POSTGRESQL::sql_fetch_field(void)
{ 
   int max_len; 
   int this_len; 
   BDB_POSTGRESQL *mdb = this;
 
   Dmsg0(dbglvl_dbg, "sql_fetch_field starts\n");
 
   if (!mdb->m_fields || mdb->m_fields_size < mdb->m_num_fields) {
      if (mdb->m_fields) {
         free(mdb->m_fields);
         mdb->m_fields = NULL;
      } 
      Dmsg1(dbglvl_dbg, "allocating space for %d fields\n", mdb->m_num_fields);
      mdb->m_fields = (SQL_FIELD *)malloc(sizeof(SQL_FIELD) * mdb->m_num_fields);
      mdb->m_fields_size = mdb->m_num_fields;
 
      for (int i = 0; i < mdb->m_num_fields; i++) {
         Dmsg1(dbglvl_dbg, "filling field %d\n", i);
         mdb->m_fields[i].name = PQfname(mdb->m_result, i);
         mdb->m_fields[i].type = PQftype(mdb->m_result, i);
         mdb->m_fields[i].flags = 0;
 
         /* For a given column, find the max length. */
         max_len = 0; 
         for (int j = 0; j < mdb->m_num_rows; j++) {
            if (PQgetisnull(mdb->m_result, j, i)) {
               this_len = 4;         /* "NULL" */
            } else { 
               this_len = cstrlen(PQgetvalue(mdb->m_result, j, i));
            } 

            if (max_len < this_len) { 
               max_len = this_len; 
            } 
         } 
         mdb->m_fields[i].max_length = max_len;
  
         Dmsg4(dbglvl_dbg, "sql_fetch_field finds field '%s' has length='%d' type='%d' and IsNull=%d\n",
               mdb->m_fields[i].name, mdb->m_fields[i].max_length, mdb->m_fields[i].type, mdb->m_fields[i].flags);
      } 
   } 
 
   /* Increment field number for the next time around */
   return &mdb->m_fields[mdb->m_field_number++];
}  
 
bool BDB_POSTGRESQL::sql_field_is_not_null(int field_type)
{  
   if (field_type == 1) {
      return true; 
   } 
   return false; 
} 
 
bool BDB_POSTGRESQL::sql_field_is_numeric(int field_type)
{ 
    /*
     * TEMP: the following is taken from select OID, typname from pg_type;
     */
    switch (field_type) {
    case 20:
    case 21:
    case 23:
    case 700:
    case 701:
       return true; 
    default:
       return false; 
    }
} 
 
/* 
 * Escape strings so PostgreSQL is happy on COPY
 * 
 * len is the length of the old string. Your new
 *   string must be long enough (max 2*old+1) to hold
 *   the escaped output.
 */ 
static char *pgsql_copy_escape(char *dest, char *src, size_t len) 
{ 
    /* we have to escape \t, \n, \r, \ */
    char c = '\0' ;
 
    while (len > 0 && *src) {
       switch (*src) {
       case '\n':
          c = 'n';
          break;
       case '\\':
          c = '\\';
          break;
       case '\t':
          c = 't';
          break;
       case '\r':
          c = 'r';
          break;
       default:
          c = '\0' ;
       }

       if (c) {
          *dest = '\\';
          dest++;
          *dest = c;
       } else {
          *dest = *src;
       }

       len--;
       src++;
       dest++;
    }

    *dest = '\0';
    return dest;
} 
 
bool BDB_POSTGRESQL::sql_batch_start(JCR *jcr)
{
   BDB_POSTGRESQL *mdb = this;
   const char *query = "COPY batch FROM STDIN";
 
   Dmsg0(dbglvl_info, "sql_batch_start started\n");
 
   if  (!sql_query("CREATE TEMPORARY TABLE batch ("
                          "FileIndex int,"
                          "JobId int,"
                          "Path varchar,"
                          "Name varchar,"
                          "LStat varchar,"
                          "Md5 varchar,"
                          "DeltaSeq smallint)")) {
      Dmsg0(dbglvl_err, "sql_batch_start failed\n");
      return false; 
   }

   /* We are starting a new query.  reset everything. */
   mdb->m_num_rows     = -1;
   mdb->m_row_number   = -1;
   mdb->m_field_number = -1;

   sql_free_result(); 

   for (int i=0; i < 10; i++) {
      mdb->m_result = PQexec(mdb->m_db_handle, query);
      if (mdb->m_result) {
         break;
      }
      bmicrosleep(5, 0);
   }
   if (!mdb->m_result) {
      Dmsg1(dbglvl_err, "Query failed: %s\n", query);
      goto get_out;
   }

   mdb->m_status = PQresultStatus(mdb->m_result);
   if (mdb->m_status == PGRES_COPY_IN) {
      /* How many fields in the set? */
      mdb->m_num_fields = (int) PQnfields(mdb->m_result);
      mdb->m_num_rows = 0;
      mdb->m_status = 1;
   } else {
      Dmsg1(dbglvl_err, "Result status failed: %s\n", query);
      goto get_out;
   }

   Dmsg0(dbglvl_info, "sql_batch_start finishing\n");

   return true; 

get_out:
   Mmsg1(&mdb->errmsg, _("error starting batch mode: %s"), PQerrorMessage(mdb->m_db_handle));
   mdb->m_status = 0;
   PQclear(mdb->m_result);
   mdb->m_result = NULL;
   return false; 
}

/* 
 * Set error to something to abort the operation
 */ 
bool BDB_POSTGRESQL::sql_batch_end(JCR *jcr, const char *error)
{
   int res;
   int count=30;
   PGresult *p_result;
   BDB_POSTGRESQL *mdb = this;

   Dmsg0(dbglvl_info, "sql_batch_end started\n");

   do {
      res = PQputCopyEnd(mdb->m_db_handle, error);
   } while (res == 0 && --count > 0);

   if (res == 1) {
      Dmsg0(dbglvl_dbg, "ok\n");
      mdb->m_status = 0;
   }

   if (res <= 0) {
      mdb->m_status = 1;
      Mmsg1(&mdb->errmsg, _("error ending batch mode: %s"), PQerrorMessage(mdb->m_db_handle));
      Dmsg1(dbglvl_err, "failure %s\n", errmsg);
   }

   /* Check command status and return to normal libpq state */
   p_result = PQgetResult(mdb->m_db_handle);
   if (PQresultStatus(p_result) != PGRES_COMMAND_OK) {
      Mmsg1(&mdb->errmsg, _("error ending batch mode: %s"), PQerrorMessage(mdb->m_db_handle));
      mdb->m_status = 1;
   }

   /* Get some statistics to compute the best plan */
   sql_query("ANALYZE batch");

   PQclear(p_result);

   Dmsg0(dbglvl_info, "sql_batch_end finishing\n");
   return true; 
}

bool BDB_POSTGRESQL::sql_batch_insert(JCR *jcr, ATTR_DBR *ar)
{
   int res;
   int count=30;
   size_t len;
   const char *digest;
   char ed1[50];
   BDB_POSTGRESQL *mdb = this;

   mdb->esc_name = check_pool_memory_size(mdb->esc_name, fnl*2+1);
   pgsql_copy_escape(mdb->esc_name, fname, fnl);

   mdb->esc_path = check_pool_memory_size(mdb->esc_path, pnl*2+1);
   pgsql_copy_escape(mdb->esc_path, path, pnl);

   if (ar->Digest == NULL || ar->Digest[0] == 0) {
      digest = "0";
   } else {
      digest = ar->Digest;
   }

   len = Mmsg(mdb->cmd, "%d\t%s\t%s\t%s\t%s\t%s\t%u\n",
              ar->FileIndex, edit_int64(ar->JobId, ed1), mdb->esc_path,
              mdb->esc_name, ar->attr, digest, ar->DeltaSeq);

   do {
      res = PQputCopyData(mdb->m_db_handle, mdb->cmd, len);
   } while (res == 0 && --count > 0);

   if (res == 1) {
      Dmsg0(dbglvl_dbg, "ok\n");
      mdb->changes++;
      mdb->m_status = 1;
   }

   if (res <= 0) {
      mdb->m_status = 0;
      Mmsg1(&mdb->errmsg, _("error copying in batch mode: %s"), PQerrorMessage(mdb->m_db_handle));
      Dmsg1(dbglvl_err, "failure %s\n", mdb->errmsg);
   }

   Dmsg0(dbglvl_info, "sql_batch_insert finishing\n");

   return true; 
}


#endif /* HAVE_POSTGRESQL */
