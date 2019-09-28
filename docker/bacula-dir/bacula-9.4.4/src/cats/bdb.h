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
 *  Catalog DB Interface class
 *
 *  Written by Kern E. Sibbald
 */

#ifndef __BDB_H_
#define __BDB_H_ 1

/*
 * These enums can be used to build queries that respects
 * Bacula Restricted Consoles.
 */
typedef enum
{
   DB_ACL_JOB      = 1,
   DB_ACL_CLIENT,
   DB_ACL_STORAGE,
   DB_ACL_POOL,
   DB_ACL_FILESET,
   DB_ACL_RCLIENT,
   DB_ACL_BCLIENT,
   DB_ACL_PATH,
   DB_ACL_LOG,
   DB_ACL_LAST                  /* Keep last */
} DB_ACL_t;

/*
 * Bits for the opts argument of db_get_job_list()
 * If neither DBL_ALL_FILES nor DBL_DELETED, return non-deleted files
 */
#define DBL_NONE             0     /* no options */
#define DBL_USE_DELTA    (1<<0)    /* Use delta indexes */
#define DBL_ALL_FILES    (1<<1)    /* Return all files including deleted ones */
#define DBL_DELETED      (1<<2)    /* Return only deleted files */
#define DBL_USE_MD5      (1<<3)    /* Include md5 */

/* Turn the num to a bit field */
#define DB_ACL_BIT(x) (1<<x)

class BDB: public SMARTALLOC {
public:
   dlink m_link;                      /* queue control */
   brwlock_t m_lock;                  /* transaction lock */
   SQL_DRIVER m_db_driver_type;       /* driver type */
   SQL_DBTYPE m_db_type;              /* database type */
   char *m_db_name;                   /* database name */
   char *m_db_user;                   /* database user */
   char *m_db_address;                /* host name address */
   char *m_db_socket;                 /* socket for local access */
   char *m_db_password;               /* database password */
   char *m_db_driver;                 /* database driver */
   char *m_db_driverdir;              /* database driver dir */
   int m_ref_count;                   /* reference count */
   int m_db_port;                     /* port for host name address */
   char *m_db_ssl_mode;               /* security mode of the connection to the server */
   char *m_db_ssl_key;                /* path name to the key file */
   char *m_db_ssl_cert;               /* path name to the certificate file */
   char *m_db_ssl_ca;                 /* path name to the certificate authority file */
   char *m_db_ssl_capath;             /* path name to a directory that contains trusted SSL CA certificates in PEM format */
   char *m_db_ssl_cipher;             /* a list of permissible ciphers to use for SSL encryption */
   bool m_disabled_batch_insert;      /* explicitly disabled batch insert mode ? */
   bool m_dedicated;                  /* is this connection dedicated? */
   bool m_use_fatal_jmsg;             /* use Jmsg(M_FATAL) after bad queries? */
   bool m_connected;                  /* connection made to db */
   bool m_have_batch_insert;          /* have batch insert support ? */

   /* Cats Internal */
   int m_status;                      /* status */
   int m_num_rows;                    /* number of rows returned by last query */
   int m_num_fields;                  /* number of fields returned by last query */
   int m_rows_size;                   /* size of malloced rows */
   int m_fields_size;                 /* size of malloced fields */
   int m_row_number;                  /* row number from xx_data_seek */
   int m_field_number;                /* field number from sql_field_seek */
   SQL_ROW m_rows;                    /* defined rows */
   SQL_FIELD *m_fields;               /* defined fields */
   bool m_allow_transactions;         /* transactions allowed */
   bool m_transaction;                /* transaction started */

   POOLMEM *cached_path;              /* cached path name */
   POOLMEM *cmd;                      /* SQL command string */
   POOLMEM *errmsg;                   /* nicely edited error message */
   POOLMEM *esc_name;                 /* Escaped file name */
   POOLMEM *esc_obj;                  /* Escaped restore object */
   POOLMEM *esc_path;                 /* Escaped path name */
   POOLMEM *fname;                    /* Filename only */
   POOLMEM *path;                     /* Path only */
   POOLMEM *acl_where;                /* Buffer for the ACL where part */
   POOLMEM *acl_join;                 /* Buffer for the ACL join part */
   uint32_t cached_path_id;           /* cached path id */
   int cached_path_len;               /* length of cached path */
   int changes;                       /* changes during transaction */
   int fnl;                           /* file name length */
   int pnl;                           /* path name length */

   POOLMEM *acls[DB_ACL_LAST];        /* ACLs */

   /* methods */
   BDB();
   virtual ~BDB();
   const char *get_db_name(void) { return m_db_name; };
   const char *get_db_user(void) { return m_db_user; };
   const bool is_connected(void) { return m_connected; };
   const bool is_dedicated(void) { return m_dedicated; };
   bool use_fatal_jmsg(void) { return m_use_fatal_jmsg; };
   const bool batch_insert_available(void) { return m_have_batch_insert; };
   void set_use_fatal_jmsg(bool val) { m_use_fatal_jmsg = val; };
   void increment_refcount(void) { m_ref_count++; };
   const int bdb_get_type_index(void) { return m_db_type; };
   const char *bdb_get_engine_name(void);

   BDB *bdb_clone_database_connection(JCR *jcr, bool mult_db_connections);
   bool bdb_match_database(const char *db_driver, const char *db_name,
            const char *bdb_address, int db_port);
   bool bdb_sql_query(const char *query, int flags=0);
   void bdb_lock(const char *file=__FILE__, int line=__LINE__);
   void bdb_unlock(const char *file=__FILE__, int line=__LINE__);
   void print_lock_info(FILE *fp);

   /* sql.c */
   bool UpdateDB(JCR *jcr, char *cmd, bool can_be_empty, const char *file=__FILE__, int line=__LINE__);
   bool InsertDB(JCR *jcr, char *cmd, const char *file=__FILE__, int line=__LINE__);
   bool QueryDB(JCR *jcr, char *cmd, const char *file=__FILE__, int line=__LINE__);
   int  DeleteDB(JCR *jcr, char *cmd, const char *file=__FILE__, int line=__LINE__);
   char *bdb_strerror() { return errmsg; };
   bool bdb_check_version(JCR *jcr);
   bool bdb_check_settings(JCR *jcr, int64_t *starttime, int val1, int64_t val2);
   bool bdb_open_batch_connexion(JCR *jcr);
   bool bdb_check_max_connections(JCR *jcr, uint32_t max_concurrent_jobs);

   /* Acl parts for various SQL commands */
   void  free_acl();             /* Used internally, free acls tab */
   void  init_acl();             /* Used internally, initialize acls tab */
   /* Take a alist of strings and turn it to an escaped sql IN () list  */
   char *escape_acl_list(JCR *jcr, POOLMEM **escape_list, alist *lst);

   /* Used during the initialization, the UA code can call this function
    * foreach kind of ACL
    */
   void  set_acl(JCR *jcr, DB_ACL_t type, alist *lst, alist *lst2=NULL); 

   /* Get the SQL string that corresponds to the Console ACL for Pool, Job,
    * Client, ... 
    */
   const char *get_acl(DB_ACL_t type, bool where);

   /* Get the SQL string that corresponds to multiple ACLs (with DB_ACL_BIT) */
   char *get_acls(int type, bool where);

   /* Get the JOIN SQL string for various tables (with DB_ACL_BIT) */
   char *get_acl_join_filter(int tables);

   /* sql_delete.c */
   int bdb_delete_pool_record(JCR *jcr, POOL_DBR *pool_dbr);
   int bdb_delete_media_record(JCR *jcr, MEDIA_DBR *mr);
   int bdb_purge_media_record(JCR *jcr, MEDIA_DBR *mr);
   int bdb_delete_snapshot_record(JCR *jcr, SNAPSHOT_DBR *sr);
   int bdb_delete_client_record(JCR *jcr, CLIENT_DBR *cr);

   /* sql_find.c */
   bool bdb_find_last_job_end_time(JCR *jcr, JOB_DBR *jr, POOLMEM **etime, char *job);
   bool bdb_find_last_job_start_time(JCR *jcr, JOB_DBR *jr, POOLMEM **stime, char *job, int JobLevel);
   bool bdb_find_job_start_time(JCR *jcr, JOB_DBR *jr, POOLMEM **stime, char *job);
   bool bdb_find_last_jobid(JCR *jcr, const char *Name, JOB_DBR *jr);
   int bdb_find_next_volume(JCR *jcr, int index, bool InChanger, MEDIA_DBR *mr);
   bool bdb_find_failed_job_since(JCR *jcr, JOB_DBR *jr, POOLMEM *stime, int &JobLevel);
   
   /* sql_create.c */
   int bdb_create_path_record(JCR *jcr, ATTR_DBR *ar);
   bool bdb_create_file_attributes_record(JCR *jcr, ATTR_DBR *ar);
   bool bdb_create_job_record(JCR *jcr, JOB_DBR *jr);
   int bdb_create_media_record(JCR *jcr, MEDIA_DBR *media_dbr);
   int bdb_create_client_record(JCR *jcr, CLIENT_DBR *cr);
   bool bdb_create_fileset_record(JCR *jcr, FILESET_DBR *fsr);
   bool bdb_create_pool_record(JCR *jcr, POOL_DBR *pool_dbr);
   bool bdb_create_jobmedia_record(JCR *jcr, JOBMEDIA_DBR *jr);
   int bdb_create_counter_record(JCR *jcr, COUNTER_DBR *cr);
   bool bdb_create_device_record(JCR *jcr, DEVICE_DBR *dr);
   bool bdb_create_storage_record(JCR *jcr, STORAGE_DBR *sr);
   bool bdb_create_mediatype_record(JCR *jcr, MEDIATYPE_DBR *mr);
   bool bdb_create_attributes_record(JCR *jcr, ATTR_DBR *ar);
   bool bdb_create_restore_object_record(JCR *jcr, ROBJECT_DBR *ar);
   bool bdb_create_base_file_attributes_record(JCR *jcr, ATTR_DBR *ar);
   bool bdb_commit_base_file_attributes_record(JCR *jcr);
   bool bdb_create_base_file_list(JCR *jcr, char *jobids);
   bool bdb_create_snapshot_record(JCR *jcr, SNAPSHOT_DBR *snap);
   int bdb_create_file_record(JCR *jcr, ATTR_DBR *ar);
   int bdb_create_filename_record(JCR *jcr, ATTR_DBR *ar);
   bool bdb_create_batch_file_attributes_record(JCR *jcr, ATTR_DBR *ar);

   /* sql_get.c */
   bool bdb_get_file_record(JCR *jcr, JOB_DBR *jr, FILE_DBR *fdbr);
   bool bdb_get_snapshot_record(JCR *jcr, SNAPSHOT_DBR *snap);
   bool bdb_get_volume_jobids(JCR *jcr,
            MEDIA_DBR *mr, db_list_ctx *lst);
   bool bdb_get_client_jobids(JCR *jcr,
            CLIENT_DBR *cr, db_list_ctx *lst);
   bool bdb_get_base_file_list(JCR *jcr, bool use_md5,
            DB_RESULT_HANDLER *result_handler,void *ctx);
   int bdb_get_filename_record(JCR *jcr);
   int bdb_get_path_record(JCR *jcr);
   bool bdb_get_pool_record(JCR *jcr, POOL_DBR *pdbr);
   bool bdb_get_pool_numvols(JCR *jcr, POOL_DBR *pdbr);
   int bdb_get_client_record(JCR *jcr, CLIENT_DBR *cr);
   bool bdb_get_jobmedia_record(JCR *jcr, JOBMEDIA_DBR *jmr);
   bool bdb_get_job_record(JCR *jcr, JOB_DBR *jr);
   int bdb_get_job_volume_names(JCR *jcr, JobId_t JobId, POOLMEM **VolumeNames);
   bool bdb_get_file_attributes_record(JCR *jcr, char *fname, JOB_DBR *jr, FILE_DBR *fdbr);
   int bdb_get_fileset_record(JCR *jcr, FILESET_DBR *fsr);
   bool bdb_get_media_record(JCR *jcr, MEDIA_DBR *mr);
   int bdb_get_num_media_records(JCR *jcr);
   int bdb_get_num_pool_records(JCR *jcr);
   int bdb_get_pool_ids(JCR *jcr, int *num_ids, DBId_t **ids);
   int bdb_get_client_ids(JCR *jcr, int *num_ids, DBId_t **ids);
   bool bdb_get_media_ids(JCR *jcr, MEDIA_DBR *mr, int *num_ids, uint32_t **ids);
   int  bdb_get_job_volume_parameters(JCR *jcr, JobId_t JobId, VOL_PARAMS **VolParams);
   bool bdb_get_counter_record(JCR *jcr, COUNTER_DBR *cr);
   bool bdb_get_query_dbids(JCR *jcr, POOL_MEM &query, dbid_list &ids);
   bool bdb_get_file_list(JCR *jcr, char *jobids,
            int opts,
            DB_RESULT_HANDLER *result_handler, void *ctx);
   bool bdb_get_base_jobid(JCR *jcr, JOB_DBR *jr, JobId_t *jobid);
   bool bdb_get_accurate_jobids(JCR *jcr, JOB_DBR *jr, db_list_ctx *jobids);
   bool bdb_get_used_base_jobids(JCR *jcr, POOLMEM *jobids, db_list_ctx *result);
   bool bdb_get_restoreobject_record(JCR *jcr, ROBJECT_DBR *rr);
   int  bdb_get_num_restoreobject_records(JCR *jcr, ROBJECT_DBR *rr);
   bool bdb_get_job_statistics(JCR *jcr, JOB_DBR *jr);
   bool bdb_get_client_pool(JCR *jcr, alist *results);

/* sql_list.c */
   void bdb_list_pool_records(JCR *jcr, POOL_DBR *pr, DB_LIST_HANDLER sendit, void *ctx, e_list_type type);
   alist *bdb_list_job_records(JCR *jcr, JOB_DBR *jr, DB_LIST_HANDLER sendit, void *ctx, e_list_type type);
   void bdb_list_job_totals(JCR *jcr, JOB_DBR *jr, DB_LIST_HANDLER sendit, void *ctx);
   void bdb_list_files_for_job(JCR *jcr, uint32_t jobid, int deleted, DB_LIST_HANDLER sendit, void *ctx);
   void bdb_list_media_records(JCR *jcr, MEDIA_DBR *mdbr, DB_LIST_HANDLER *sendit, void *ctx, e_list_type type);
   void bdb_list_jobmedia_records(JCR *jcr, JobId_t JobId, DB_LIST_HANDLER *sendit, void *ctx, e_list_type type);
   void bdb_list_joblog_records(JCR *jcr, JobId_t JobId, DB_LIST_HANDLER *sendit, void *ctx, e_list_type type);
   int  bdb_list_sql_query(JCR *jcr, const char *query, DB_LIST_HANDLER *sendit, void *ctx, int verbose, e_list_type type);
   void bdb_list_client_records(JCR *jcr, DB_LIST_HANDLER *sendit, void *ctx, e_list_type type);
   void bdb_list_copies_records(JCR *jcr, uint32_t limit, char *jobids, DB_LIST_HANDLER *sendit, void *ctx, e_list_type type);
   void bdb_list_base_files_for_job(JCR *jcr, JobId_t jobid, DB_LIST_HANDLER *sendit, void *ctx);
   void bdb_list_restore_objects(JCR *jcr, ROBJECT_DBR *rr, DB_LIST_HANDLER *sendit, void *ctx, e_list_type type);
   void bdb_list_snapshot_records(JCR *jcr, SNAPSHOT_DBR *sdbr,
              DB_LIST_HANDLER *sendit, void *ctx, e_list_type type);
   void bdb_list_files(JCR *jcr, FILE_DBR *fr, DB_RESULT_HANDLER *sendit, void *ctx);


   /* sql_update.c */
   bool bdb_update_job_start_record(JCR *jcr, JOB_DBR *jr);
   int  bdb_update_job_end_record(JCR *jcr, JOB_DBR *jr);
   int  bdb_update_client_record(JCR *jcr, CLIENT_DBR *cr);
   int  bdb_update_pool_record(JCR *jcr, POOL_DBR *pr);
   bool bdb_update_storage_record(JCR *jcr, STORAGE_DBR *sr);
   int  bdb_update_media_record(JCR *jcr, MEDIA_DBR *mr);
   int  bdb_update_media_defaults(JCR *jcr, MEDIA_DBR *mr);
   int  bdb_update_counter_record(JCR *jcr, COUNTER_DBR *cr);
   int  bdb_add_digest_to_file_record(JCR *jcr, FileId_t FileId, char *digest, int type);
   int  bdb_mark_file_record(JCR *jcr, FileId_t FileId, JobId_t JobId);
   void bdb_make_inchanger_unique(JCR *jcr, MEDIA_DBR *mr);
   int  bdb_update_stats(JCR *jcr, utime_t age);
   bool bdb_update_snapshot_record(JCR *jcr, SNAPSHOT_DBR *sr);

   /* Pure virtual low level methods */
   virtual void bdb_escape_string(JCR *jcr, char *snew, char *old, int len) = 0;
   virtual char *bdb_escape_object(JCR *jcr, char *old, int len) = 0;
   virtual void bdb_unescape_object(JCR *jcr, char *from, int32_t expected_len,
                   POOLMEM **dest, int32_t *len) = 0;
   virtual bool bdb_open_database(JCR *jcr) = 0;
   virtual void bdb_close_database(JCR *jcr) = 0;
   virtual void bdb_start_transaction(JCR *jcr) = 0;
   virtual void bdb_end_transaction(JCR *jcr) = 0;
   virtual bool bdb_sql_query(const char *query, DB_RESULT_HANDLER *result_handler, void *ctx) = 0;
   virtual void bdb_thread_cleanup(void) = 0;

   /* By default, we use bdb_sql_query */
   virtual bool bdb_big_sql_query(const char *query,
                   DB_RESULT_HANDLER *result_handler, void *ctx) {
      return bdb_sql_query(query, result_handler, ctx);
   };

   /* Cats Internal */
#ifdef CATS_PRIVATE_DBI
   int sql_num_rows(void) { return m_num_rows; };
   void sql_field_seek(int field) { m_field_number = field; };
   int sql_num_fields(void) { return m_num_fields; };
   virtual void sql_free_result(void) = 0;
   virtual SQL_ROW sql_fetch_row(void) = 0;
   virtual bool sql_query(const char *query, int flags=0) = 0;
   virtual const char *sql_strerror(void) = 0;
   virtual void sql_data_seek(int row) = 0;
   virtual int sql_affected_rows(void) = 0;
   virtual uint64_t sql_insert_autokey_record(const char *query, const char *table_name) = 0;
   virtual SQL_FIELD *sql_fetch_field(void) = 0;
   virtual bool sql_field_is_not_null(int field_type) = 0;
   virtual bool sql_field_is_numeric(int field_type) = 0;
   virtual bool sql_batch_start(JCR *jcr) = 0;
   virtual bool sql_batch_end(JCR *jcr, const char *error) = 0;
   virtual bool sql_batch_insert(JCR *jcr, ATTR_DBR *ar) = 0;
#endif
};

#endif /* __Bbdb_H_ */
