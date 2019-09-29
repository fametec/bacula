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

/* Postgresql driver specific definitions */

#ifdef __BDB_POSTGRESQL_H_

class BDB_POSTGRESQL: public BDB {
private:
   PGconn *m_db_handle;
   PGresult *m_result;
   POOLMEM *m_buf;                /* Buffer to manipulate queries */

public:
   BDB_POSTGRESQL();
   ~BDB_POSTGRESQL();

   /* Functions that we override */
   bool bdb_open_database(JCR *jcr);
   void bdb_close_database(JCR *jcr);
   void bdb_thread_cleanup(void);
   void bdb_escape_string(JCR *jcr, char *snew, char *old, int len);
   char *bdb_escape_object(JCR *jcr, char *old, int len);
   void bdb_unescape_object(JCR *jcr, char *from, int32_t expected_len,
                           POOLMEM **dest, int32_t *len);
   void bdb_start_transaction(JCR *jcr);
   void bdb_end_transaction(JCR *jcr);
   bool bdb_sql_query(const char *query, DB_RESULT_HANDLER *result_handler, void *ctx);
   bool bdb_big_sql_query(const char *query, DB_RESULT_HANDLER *result_handler, void *ctx);
   void sql_free_result(void);
   SQL_ROW sql_fetch_row(void);
   bool sql_query(const char *query, int flags=0);
   const char *sql_strerror(void);
   int sql_num_rows(void);
   void sql_data_seek(int row);
   int sql_affected_rows(void);
   uint64_t sql_insert_autokey_record(const char *query, const char *table_name);
   void sql_field_seek(int field);
   SQL_FIELD *sql_fetch_field(void);
   int sql_num_fields(void);
   bool sql_field_is_not_null(int field_type);
   bool sql_field_is_numeric(int field_type);
   bool sql_batch_start(JCR *jcr);
   bool sql_batch_end(JCR *jcr, const char *error);
   bool sql_batch_insert(JCR *jcr, ATTR_DBR *ar);
};

#endif /* __BDB_POSTGRESQL_H_ */
