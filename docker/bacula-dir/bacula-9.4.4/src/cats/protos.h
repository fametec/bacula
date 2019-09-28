/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

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
 *
 *  Database routines that are exported by the cats library for
 *    use elsewhere in Bacula (mainly the Director).
 *
 * Note: the interface that is used by the core Bacula code outside
 *  of the cats directory has names that are:
 *  db_xxx(x, db, y, ...) 
 *  usually with a database pointer such as db as an argument.  
 *  This simplifies the vast bulk of the code and makes it easier to read.
 *  These are translated into class calls on the db pointer by a #define
 *  in this file.
 *
 *  The actual class code is named bdb_xxx(x, y, ...) and is called with
 *  the class pointer such as db->bdb_xxx(x, y, ...)  The code in this
 *  cats directory can use the db_xxx() calls or the db->bdb_xxx() calls.
 *  In the Bacula core code we prefer using only the db_xxx() calls.
 *
 *    Written by Kern Sibbald, MM
 */

#ifndef __SQL_PROTOS_H
#define __SQL_PROTOS_H

#include "cats.h"

BDB *db_init_database(JCR *jcr, const char *db_driver, const char *db_name,
        const char *db_user, const char *db_password,
        const char *db_address, int db_port,
        const char *db_socket,
        const char *db_ssl_mode, const char *db_ssl_key,
        const char *db_ssl_cert, const char *db_ssl_ca,
        const char *db_ssl_capath, const char *db_ssl_cipher,
        bool mult_db_connections, bool disable_batch_insert);

/* Database prototypes and defines */

/* Misc */
#define db_lock(mdb) \
   mdb->bdb_lock(__FILE__, __LINE__)
#define db_unlock(mdb) \
   mdb->bdb_unlock()


/* Virtual methods */
#define db_escape_string(jcr, mdb, snew, old, len) \
           mdb->bdb_escape_string(jcr, snew, old, len)
#define db_escape_object(jcr, mdb, old, len) \
           mdb->bdb_escape_object(jcr, old, len)
#define db_unescape_object(jcr, mdb, from, expected_len, dest, len) \
           mdb->bdb_unescape_object(jcr, from, expected_len, dest, len)
#define db_open_database(jcr, mdb) \
           mdb->bdb_open_database(jcr)
#define db_close_database(jcr, mdb) \
           mdb->bdb_close_database(jcr)
#define db_close_database(jcr, mdb) \
           mdb->bdb_close_database(jcr)
#define db_start_transaction(jcr, mdb) \
           mdb->bdb_start_transaction(jcr)
#define db_end_transaction(jcr, mdb) \
           if (mdb) mdb->bdb_end_transaction(jcr)
#define db_sql_query(mdb, query, result_handler, ctx) \
           mdb->bdb_sql_query(query, result_handler, ctx)
#define db_thread_cleanup(mdb) \
           if (mdb) mdb->bdb_thread_cleanup()

/* sql.c */
int db_int64_handler(void *ctx, int num_fields, char **row);
int db_strtime_handler(void *ctx, int num_fields, char **row);
int db_list_handler(void *ctx, int num_fields, char **row);
int db_string_list_handler(void *ctx, int num_fields, char **row);
int db_int_handler(void *ctx, int num_fields, char **row);
void bdb_debug_print(JCR *jcr, FILE *fp);
void db_free_restoreobject_record(JCR *jcr, ROBJECT_DBR *rr);

#define db_open_batch_connexion(jcr, mdb) \
           mdb->bdb_open_batch_connexion(jcr)
#define db_strerror(mdb) \
           mdb->bdb_strerror()
#define db_debug_print(jcr, fp) \
           bdb_debug_print(jcr, fp)
#define db_check_max_connections(jcr, mdb, maxc) \
           mdb->bdb_check_max_connections(jcr, maxc)

/* sql_create.c */
bool bdb_write_batch_file_records(JCR *jcr);
void bdb_disable_batch_insert(bool disable);

/* sql_get.c */
void bdb_free_restoreobject_record(JCR *jcr, ROBJECT_DBR *rr);
#define db_get_client_pool(jcr, mdb, results)    \
   mdb->bdb_get_client_pool(jcr, results)

/* sql_create.c */
#define db_create_path_record(jcr, mdb, ar) \
           mdb->bdb_create_path_record(jcr, ar)
#define db_create_file_attributes_record(jcr, mdb, ar) \
           mdb->bdb_create_file_attributes_record(jcr, ar)
#define db_create_job_record(jcr, mdb, jr) \
           mdb->bdb_create_job_record(jcr, jr)
#define db_create_media_record(jcr, mdb, media_dbr) \
           mdb->bdb_create_media_record(jcr, media_dbr)
#define db_create_client_record(jcr, mdb, cr) \
           mdb->bdb_create_client_record(jcr, cr)
#define db_create_fileset_record(jcr, mdb, fsr) \
           mdb->bdb_create_fileset_record(jcr, fsr)
#define db_create_pool_record(jcr, mdb, pool_dbr) \
           mdb->bdb_create_pool_record(jcr, pool_dbr)
#define db_create_jobmedia_record(jcr, mdb, jr) \
           mdb->bdb_create_jobmedia_record(jcr, jr)
#define db_create_counter_record(jcr, mdb, cr) \
           mdb->bdb_create_counter_record(jcr, cr)
#define db_create_device_record(jcr, mdb, dr) \
           mdb->bdb_create_device_record(jcr, dr)
#define db_create_storage_record(jcr, mdb, sr) \
           mdb->bdb_create_storage_record(jcr, sr)
#define db_create_mediatype_record(jcr, mdb, mr) \
           mdb->bdb_create_mediatype_record(jcr, mr)
#define db_write_batch_file_records(jcr) \
           bdb_write_batch_file_records(jcr)
#define db_create_attributes_record(jcr, mdb, ar) \
           mdb->bdb_create_attributes_record(jcr, ar)
#define db_create_restore_object_record(jcr, mdb, ar) \
           mdb->bdb_create_restore_object_record(jcr, ar)
#define db_create_base_file_attributes_record(jcr, mdb, ar) \
           mdb->bdb_create_base_file_attributes_record(jcr, ar)
#define db_commit_base_file_attributes_record(jcr, mdb) \
           mdb->bdb_commit_base_file_attributes_record(jcr)
#define db_create_base_file_list(jcr, mdb, jobids) \
           mdb->bdb_create_base_file_list(jcr, jobids)
#define db_disable_batch_insert(disable) \
           bdb_disable_batch_insert(disable)
#define db_create_snapshot_record(jcr, mdb, sr) \
           mdb->bdb_create_snapshot_record(jcr, sr)
#define db_get_job_statistics(jcr, mdb, jr)      \
           mdb->bdb_get_job_statistics(jcr, jr)

/* sql_delete.c */
#define db_delete_pool_record(jcr, mdb, pool_dbr) \
           mdb->bdb_delete_pool_record(jcr, pool_dbr)
#define db_delete_media_record(jcr, mdb, mr) \
           mdb->bdb_delete_media_record(jcr, mr)
#define db_purge_media_record(jcr, mdb, mr) \
           mdb->bdb_purge_media_record(jcr, mr)
#define db_delete_snapshot_record(jcr, mdb, sr) \
           mdb->bdb_delete_snapshot_record(jcr, sr)
#define db_delete_client_record(jcr, mdb, cr)         \
           mdb->bdb_delete_client_record(jcr, cr)


/* sql_find.c */
#define db_find_last_job_end_time(jcr, mdb, jr, etime, job) \
           mdb->bdb_find_last_job_end_time(jcr, jr, etime, job)
#define db_find_last_job_start_time(jcr, mdb, jr, stime, job, JobLevel) \
           mdb->bdb_find_last_job_start_time(jcr, jr, stime, job, JobLevel)
#define db_find_job_start_time(jcr, mdb, jr, stime, job) \
           mdb->bdb_find_job_start_time(jcr, jr, stime, job)
#define db_find_last_jobid(jcr, mdb, Name, jr) \
           mdb->bdb_find_last_jobid(jcr, Name, jr)
#define db_find_next_volume(jcr, mdb, index, InChanger, mr) \
           mdb->bdb_find_next_volume(jcr, index, InChanger, mr)
#define db_find_failed_job_since(jcr, mdb, jr, stime, JobLevel) \
           mdb->bdb_find_failed_job_since(jcr, jr, stime, JobLevel)

/* sql_get.c */
#define db_get_volume_jobids(jcr, mdb, mr, lst) \
           mdb->bdb_get_volume_jobids(jcr, mr, lst)
#define db_get_client_jobids(jcr, mdb, cr, lst) \
           mdb->bdb_get_client_jobids(jcr, cr, lst)
#define db_get_base_file_list(jcr, mdb, use_md5, result_handler, ctx) \
           mdb->bdb_get_base_file_list(jcr, use_md5, result_handler, ctx)
#define db_get_path_record(jcr, mdb) \
           mdb->bdb_get_path_record(jcr)
#define db_get_pool_record(jcr, mdb, pdbr) \
           mdb->bdb_get_pool_record(jcr, pdbr)
#define db_get_pool_numvols(jcr, mdb, pdbr) \
           mdb->bdb_get_pool_numvols(jcr, pdbr)
#define db_get_client_record(jcr, mdb, cr) \
           mdb->bdb_get_client_record(jcr, cr)
#define db_get_jobmedia_record(jcr, mdb, jmr)   \
           mdb->bdb_get_jobmedia_record(jcr, jmr)
#define db_get_job_record(jcr, mdb, jr) \
           mdb->bdb_get_job_record(jcr, jr)
#define db_get_job_volume_names(jcr, mdb, JobId, VolumeNames) \
           mdb->bdb_get_job_volume_names(jcr, JobId, VolumeNames)
#define db_get_file_attributes_record(jcr, mdb, fname, jr, fdbr) \
           mdb->bdb_get_file_attributes_record(jcr, fname, jr, fdbr)
#define db_get_fileset_record(jcr, mdb, fsr) \
           mdb->bdb_get_fileset_record(jcr, fsr)
#define db_get_media_record(jcr, mdb, mr) \
           mdb->bdb_get_media_record(jcr, mr)
#define db_get_num_media_records(jcr, mdb) \
           mdb->bdb_get_num_media_records(jcr)
#define db_get_num_pool_records(jcr, mdb) \
           mdb->bdb_get_num_pool_records(jcr)
#define db_get_pool_ids(jcr, mdb, num_ids, ids) \
           mdb->bdb_get_pool_ids(jcr, num_ids, ids)
#define db_get_client_ids(jcr, mdb, num_ids, ids) \
           mdb->bdb_get_client_ids(jcr, num_ids, ids)
#define db_get_media_ids(jcr, mdb, mr, num_ids, ids) \
           mdb->bdb_get_media_ids(jcr, mr, num_ids, ids)
#define db_get_job_volume_parameters(jcr, mdb, JobId, VolParams) \
           mdb->bdb_get_job_volume_parameters(jcr, JobId, VolParams)
#define db_get_counter_record(jcr, mdb, cr) \
           mdb->bdb_get_counter_record(jcr, cr)
#define db_get_query_dbids(jcr, mdb, query, ids) \
           mdb->bdb_get_query_dbids(jcr, query, ids)
#define db_get_file_list(jcr, mdb, jobids, opts, result_handler, ctx) \
           mdb->bdb_get_file_list(jcr, jobids, opts, result_handler, ctx)
#define db_get_base_jobid(jcr, mdb, jr, jobid) \
           mdb->bdb_get_base_jobid(jcr, jr, jobid)
#define db_get_accurate_jobids(jcr, mdb, jr, jobids) \
           mdb->bdb_get_accurate_jobids(jcr, jr, jobids)
#define db_get_used_base_jobids(jcr, mdb, jobids, result) \
           mdb->bdb_get_used_base_jobids(jcr, jobids, result)
#define db_get_restoreobject_record(jcr, mdb, rr) \
           mdb->bdb_get_restoreobject_record(jcr, rr)
#define db_get_type_index(mdb) \
           mdb->bdb_get_type_index()
#define db_get_engine_name(mdb) \
           mdb->bdb_get_engine_name()
#define db_get_snapshot_record(jcr, mdb, sr) \
           mdb->bdb_get_snapshot_record(jcr, sr)

/* sql_list.c */
#define db_list_pool_records(jcr, mdb, pr, sendit, ctx, type) \
           mdb->bdb_list_pool_records(jcr, pr, sendit, ctx, type)
#define db_list_job_records(jcr, mdb, jr, sendit, ctx, type) \
           mdb->bdb_list_job_records(jcr, jr, sendit, ctx, type)
#define db_list_job_totals(jcr, mdb, jr, sendit, ctx) \
           mdb->bdb_list_job_totals(jcr, jr, sendit, ctx)
#define db_list_files_for_job(jcr, mdb, jobid, deleted, sendit, ctx)     \
           mdb->bdb_list_files_for_job(jcr, jobid, deleted, sendit, ctx)
#define db_list_media_records(jcr, mdb, mdbr, sendit, ctx, type) \
           mdb->bdb_list_media_records(jcr, mdbr, sendit, ctx, type)
#define db_list_jobmedia_records(jcr, mdb, JobId, sendit, ctx, type) \
           mdb->bdb_list_jobmedia_records(jcr, JobId, sendit, ctx, type)
#define db_list_joblog_records(jcr, mdb, JobId, sendit, ctx, type) \
           mdb->bdb_list_joblog_records(jcr, JobId, sendit, ctx, type)
#define db_list_sql_query(jcr, mdb, query, sendit, ctx, verbose, type) \
           mdb->bdb_list_sql_query(jcr, query, sendit, ctx, verbose, type)
#define db_list_client_records(jcr, mdb, sendit, ctx, type) \
           mdb->bdb_list_client_records(jcr, sendit, ctx, type)
#define db_list_copies_records(jcr, mdb, limit, jobids, sendit, ctx, type) \
           mdb->bdb_list_copies_records(jcr, limit, jobids, sendit, ctx, type)
#define db_list_base_files_for_job(jcr, mdb, jobid, sendit, ctx) \
           mdb->bdb_list_base_files_for_job(jcr, jobid, sendit, ctx)
#define db_list_restore_objects(jcr, mdb, rr, sendit, ctx, type) \
           mdb->bdb_list_restore_objects(jcr, rr, sendit, ctx, type)
#define db_list_snapshot_records(jcr, mdb, snapdbr, sendit, ua, llist) \
           mdb->bdb_list_snapshot_records(jcr, snapdbr, sendit, ua, llist)



/* sql_update.c */
#define db_update_job_start_record(jcr, mdb, jr) \
           mdb->bdb_update_job_start_record(jcr, jr)
#define db_update_job_end_record(jcr, mdb, jr) \
           mdb->bdb_update_job_end_record(jcr, jr)
#define db_update_client_record(jcr, mdb, cr) \
           mdb->bdb_update_client_record(jcr, cr)
#define db_update_pool_record(jcr, mdb, pr) \
           mdb->bdb_update_pool_record(jcr, pr)
#define db_update_storage_record(jcr, mdb, sr) \
           mdb->bdb_update_storage_record(jcr, sr)
#define db_update_media_record(jcr, mdb, mr) \
           mdb->bdb_update_media_record(jcr, mr)
#define db_update_media_defaults(jcr, mdb, mr) \
           mdb->bdb_update_media_defaults(jcr, mr)
#define db_update_counter_record(jcr, mdb, cr) \
           mdb->bdb_update_counter_record(jcr, cr)
#define db_add_digest_to_file_record(jcr, mdb, FileId, digest, type) \
           mdb->bdb_add_digest_to_file_record(jcr, FileId, digest, type)
#define db_mark_file_record(jcr, mdb, FileId, JobId) \
           mdb->bdb_mark_file_record(jcr, FileId, JobId)
#define db_make_inchanger_unique(jcr, mdb, mr) \
           mdb->bdb_make_inchanger_unique(jcr, mr)
#define db_update_stats(jcr, mdb, age) \
           mdb->bdb_update_stats(jcr, age)
#define db_update_snapshot_record(jcr, mdb, sr) \
           mdb->bdb_update_snapshot_record(jcr, sr)


#endif /* __SQL_PROTOS_H */
