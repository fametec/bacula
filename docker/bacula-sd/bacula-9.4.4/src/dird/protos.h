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
 * Director external function prototypes
 */

/* admin.c */
extern bool do_admin_init(JCR *jcr);
extern bool do_admin(JCR *jcr);
extern void admin_cleanup(JCR *jcr, int TermCode);


/* authenticate.c */
extern bool authenticate_storage_daemon(JCR *jcr, STORE *store);
extern int authenticate_file_daemon(JCR *jcr);
extern int authenticate_user_agent(UAContext *ua);

/* autoprune.c */
extern void do_autoprune(JCR *jcr);
extern void prune_volumes(JCR *jcr, bool InChanger, MEDIA_DBR *mr,
              STORE *store);

/* autorecycle.c */
extern bool recycle_oldest_purged_volume(JCR *jcr, bool InChanger,
              MEDIA_DBR *mr, STORE *store);

extern int recycle_volume(JCR *jcr, MEDIA_DBR *mr);
extern bool find_recycled_volume(JCR *jcr, bool InChanger,
                MEDIA_DBR *mr, STORE *store);

/* backup.c */
extern int wait_for_job_termination(JCR *jcr, int timeout=0);
extern bool do_backup_init(JCR *jcr);
extern bool do_backup(JCR *jcr);
extern void backup_cleanup(JCR *jcr, int TermCode);
extern void update_bootstrap_file(JCR *jcr);
extern bool send_accurate_current_files(JCR *jcr);
extern char *get_storage_address(CLIENT *cli, STORE *store);
extern bool run_storage_and_start_message_thread(JCR *jcr, BSOCK *sd);
extern bool send_client_addr_to_sd(JCR *jcr);
extern bool send_store_addr_to_fd(JCR *jcr, STORE *store,
               char *store_address, uint32_t store_port);

/* vbackup.c */
extern bool do_vbackup_init(JCR *jcr);
extern bool do_vbackup(JCR *jcr);
extern void vbackup_cleanup(JCR *jcr, int TermCode);


/* bsr.c */
RBSR *new_bsr();
rblist *create_bsr_list(uint32_t JobId, int findex, int findex2);
void free_bsr(rblist *bsr_list);
bool complete_bsr(UAContext *ua, rblist *bsr_list);
uint32_t write_bsr_file(UAContext *ua, RESTORE_CTX &rx);
void display_bsr_info(UAContext *ua, RESTORE_CTX &rx);
void add_findex(rblist *bsr_list, uint32_t JobId, int32_t findex);
void add_findex_all(rblist *bsr_list, uint32_t JobId, const char *fileregex);
RBSR_FINDEX *new_findex();
void make_unique_restore_filename(UAContext *ua, POOLMEM **fname);
void print_bsr(UAContext *ua, RESTORE_CTX &rx);


/* catreq.c */
extern void catalog_request(JCR *jcr, BSOCK *bs);
extern void catalog_update(JCR *jcr, BSOCK *bs);
extern bool despool_attributes_from_file(JCR *jcr, const char *file);
extern void remove_dummy_jobmedia_records(JCR *jcr);

/* dird_conf.c */
extern char *level_to_str(char *buf, int len, int level);
extern "C" char *job_code_callback_director(JCR *jcr, const char*, char *, int);

/* expand.c */
int variable_expansion(JCR *jcr, char *inp, POOLMEM **exp);


/* fd_cmds.c */
extern int connect_to_file_daemon(JCR *jcr, int retry_interval,
                                  int max_retry_time, int verbose);
extern bool send_ls_fileset(JCR *jcr, const char *path);
extern bool send_ls_plugin_fileset(JCR *jcr, const char *plugin, const char *path);
extern bool send_include_list(JCR *jcr);
extern bool send_exclude_list(JCR *jcr);
extern bool send_level_command(JCR *jcr);
extern bool send_bwlimit(JCR *jcr, const char *Job);
extern int get_attributes_and_put_in_catalog(JCR *jcr);
extern void get_attributes_and_compare_to_catalog(JCR *jcr, JobId_t JobId);
extern int put_file_into_catalog(JCR *jcr, long file_index, char *fname,
                          char *link, char *attr, int stream);
extern void get_level_since_time(JCR *jcr, char *since, int since_len);
extern int send_runscripts_commands(JCR *jcr);
extern bool send_restore_objects(JCR *jcr);
extern bool send_component_info(JCR *jcr);

/* getmsg.c */
enum e_prtmsg {
   DISPLAY_ERROR,
   NO_DISPLAY
};
extern bool response(JCR *jcr, BSOCK *fd, char *resp, const char *cmd, e_prtmsg prtmsg);

/* job.c */
extern bool allow_duplicate_job(JCR *jcr);
extern void set_jcr_defaults(JCR *jcr, JOB *job);
extern void create_unique_job_name(JCR *jcr, const char *base_name);
extern void update_job_end_record(JCR *jcr);
extern bool get_or_create_client_record(JCR *jcr);
extern bool get_or_create_fileset_record(JCR *jcr);
extern DBId_t get_or_create_pool_record(JCR *jcr, char *pool_name);
extern void apply_pool_overrides(JCR *jcr);
extern bool apply_wstorage_overrides(JCR *jcr, POOL *original_pool);
extern JobId_t run_job(JCR *jcr);
extern JobId_t resume_job(JCR *jcr, JOB_DBR *jr);
extern bool cancel_job(UAContext *ua, JCR *jcr, int wait, bool cancel=true);
extern int cancel_inactive_job(UAContext *ua);
extern void get_job_storage(USTORE *store, JOB *job, RUN *run);
extern void init_jcr_job_record(JCR *jcr);
extern void update_job_end(JCR *jcr, int TermCode);
extern void copy_rwstorage(JCR *jcr, alist *storage, const char *where);
extern void set_rwstorage(JCR *jcr, USTORE *store);
extern void free_rwstorage(JCR *jcr);
extern void copy_wstorage(JCR *jcr, alist *storage, const char *where);
extern void set_wstorage(JCR *jcr, USTORE *store);
extern void free_wstorage(JCR *jcr);
extern void copy_rstorage(JCR *jcr, alist *storage, const char *where);
extern void set_rstorage(JCR *jcr, USTORE *store);
extern void free_rstorage(JCR *jcr);
extern bool setup_job(JCR *jcr);
extern void create_clones(JCR *jcr);
extern int create_restore_bootstrap_file(JCR *jcr);
extern int create_restore_bootstrap_file(JCR *jcr, JobId_t jobid, int findex1, int findex2);
extern void dird_free_jcr(JCR *jcr);
extern void dird_free_jcr_pointers(JCR *jcr);
extern void cancel_storage_daemon_job(JCR *jcr);
extern bool run_console_command(JCR *jcr, const char *cmd);
extern void sd_msg_thread_send_signal(JCR *jcr, int sig);
void terminate_sd_msg_chan_thread(JCR *jcr);
bool flush_file_records(JCR *jcr);

/* jobq.c */
extern bool inc_read_store(JCR *jcr);
extern void dec_read_store(JCR *jcr);

/* mac.c */
extern bool do_mac(JCR *jcr);
extern bool do_mac_init(JCR *jcr);
extern void mac_cleanup(JCR *jcr, int TermCode, int writeTermCode);
extern bool set_mac_wstorage(UAContext *ua, JCR *jcr, POOL *pool,
               POOL *next_pool, const char *source);


/* mountreq.c */
extern void mount_request(JCR *jcr, BSOCK *bs, char *buf);

/* msgchan.c */
extern BSOCK *open_sd_bsock(UAContext *ua);
extern void close_sd_bsock(UAContext *ua);
extern bool connect_to_storage_daemon(JCR *jcr, int retry_interval,
                              int max_retry_time, int verbose);
extern bool start_storage_daemon_job(JCR *jcr, alist *rstore, alist *wstore,
              bool send_bsr=false);
extern bool start_storage_daemon_message_thread(JCR *jcr);
extern int bget_dirmsg(BSOCK *bs);
extern void wait_for_storage_daemon_termination(JCR *jcr);
extern bool send_bootstrap_file(JCR *jcr, BSOCK *sd);

/* next_vol.c */
int find_next_volume_for_append(JCR *jcr, MEDIA_DBR *mr, int index,
                                bool create, bool purge, POOL_MEM &errmsg);
void set_storageid_in_mr(STORE *store, MEDIA_DBR *mr);
bool has_volume_expired(JCR *jcr, MEDIA_DBR *mr);
void check_if_volume_valid_or_recyclable(JCR *jcr, MEDIA_DBR *mr, const char **reason);
bool get_scratch_volume(JCR *jcr, bool InChanger, MEDIA_DBR *mr,
        STORE *store);

/* newvol.c */
bool newVolume(JCR *jcr, MEDIA_DBR *mr, STORE *store, POOL_MEM &errmsg);

/* restore.c */
extern bool do_restore(JCR *jcr);
extern bool do_restore_init(JCR *jcr);
extern void restore_cleanup(JCR *jcr, int TermCode);


/* ua_acl.c */
bool acl_access_ok(UAContext *ua, int acl, const char *item);
bool acl_access_ok(UAContext *ua, int acl, const char *item, int len);
bool have_restricted_acl(UAContext *ua, int acl);
bool acl_access_client_ok(UAContext *ua, const char *name, int32_t jobtype);

/* ua_cmds.c */
bool get_uid_gid_from_acl(UAContext *ua, alist **uid, alist **gid, alist **dir);
bool do_a_command(UAContext *ua);
bool do_a_dot_command(UAContext *ua);
int qmessagescmd(UAContext *ua, const char *cmd);
bool open_new_client_db(UAContext *ua);
bool open_client_db(UAContext *ua);
bool open_db(UAContext *ua);
void close_db(UAContext *ua);
int cloud_volumes_cmd(UAContext *ua, const char *cmd, const char *mode);
enum e_pool_op {
   POOL_OP_UPDATE,
   POOL_OP_CREATE
};
int create_pool(JCR *jcr, BDB *db, POOL *pool, e_pool_op op);
void set_pool_dbr_defaults_in_media_dbr(MEDIA_DBR *mr, POOL_DBR *pr);
bool set_pooldbr_references(JCR *jcr, BDB *db, POOL_DBR *pr, POOL *pool);
void set_pooldbr_from_poolres(POOL_DBR *pr, POOL *pool, e_pool_op op);
int update_pool_references(JCR *jcr, BDB *db, POOL *pool);

/* ua_input.c */
bool get_cmd(UAContext *ua, const char *prompt, bool subprompt=false);
bool get_selection_list(UAContext *ua, sellist &sl, const char *prompt, bool subprompt=false);
bool get_pint(UAContext *ua, const char *prompt);
bool get_yesno(UAContext *ua, const char *prompt);
bool is_yesno(char *val, int *ret);
int get_enabled(UAContext *ua, const char *val);
void parse_ua_args(UAContext *ua);
bool is_comment_legal(UAContext *ua, const char *name);

/* ua_label.c */
bool is_volume_name_legal(UAContext *ua, const char *name);
int get_num_drives_from_SD(UAContext *ua);
void update_slots(UAContext *ua);

/* ua_update.c */
void update_vol_pool(UAContext *ua, char *val, MEDIA_DBR *mr, POOL_DBR *opr);

/* ua_output.c */
void prtit(void *ctx, const char *msg);
bool complete_jcr_for_job(JCR *jcr, JOB *job, POOL *pool);
RUN *find_next_run(RUN *run, JOB *job, utime_t &runtime, int ndays);
bool acl_access_jobid_ok(UAContext *ua, const char *jobids);

/* ua_restore.c */
void find_storage_resource(UAContext *ua, RESTORE_CTX &rx, char *Storage, char *MediaType);
bool insert_table_into_findex_list(UAContext *ua, RESTORE_CTX *rx, char *table);
void new_rx(RESTORE_CTX *rx);
void free_rx(RESTORE_CTX *rx);

/* ua_server.c */
void bsendmsg(void *ua_ctx, const char *fmt, ...);
void berrormsg(void *ua_ctx, const char *fmt, ...);
void bwarningmsg(void *ua_ctx, const char *fmt, ...);
void binfomsg(void *ua_ctx, const char *fmt, ...);
UAContext *new_ua_context(JCR *jcr);
JCR *new_control_jcr(const char *base_name, int job_type);
void free_ua_context(UAContext *ua);

/* ua_select.c */
STORE   *select_storage_resource(UAContext *ua, bool unique=false);
JOB     *select_job_resource(UAContext *ua);
JOB     *select_enable_disable_job_resource(UAContext *ua, bool enable);
JOB     *select_restore_job_resource(UAContext *ua);
CLIENT  *select_enable_disable_client_resource(UAContext *ua, bool enable);
CLIENT  *select_client_resource(UAContext *ua, int32_t jobtype);
FILESET *select_fileset_resource(UAContext *ua);
SCHED   *select_enable_disable_schedule_resource(UAContext *ua, bool enable);
int     select_pool_and_media_dbr(UAContext *ua, POOL_DBR *pr, MEDIA_DBR *mr);
int     select_media_dbr(UAContext *ua, MEDIA_DBR *mr);
bool    select_pool_dbr(UAContext *ua, POOL_DBR *pr, const char *argk="pool");
bool    select_client_dbr(UAContext *ua, CLIENT_DBR *cr, int32_t jobtype);

void    start_prompt(UAContext *ua, const char *msg);
void    add_prompt(UAContext *ua, const char *prompt, char *unique=NULL);
int     do_prompt(UAContext *ua, const char *automsg, const char *msg, char *prompt, int max_prompt);
int     do_alist_prompt(UAContext *ua, const char *automsg, const char *msg,
              alist *selected);
CAT    *get_catalog_resource(UAContext *ua);
STORE  *get_storage_resource(UAContext *ua, bool use_default, bool unique=false);
int     get_storage_drive(UAContext *ua, STORE *store);
int     get_storage_slot(UAContext *ua, STORE *store);
int     get_media_type(UAContext *ua, char *MediaType, int max_media);
bool    get_pool_dbr(UAContext *ua, POOL_DBR *pr, const char *argk="pool");
bool    get_client_dbr(UAContext *ua, CLIENT_DBR *cr, int32_t jobtype);
POOL   *get_pool_resource(UAContext *ua);
JOB    *get_restore_job(UAContext *ua);
POOL   *select_pool_resource(UAContext *ua);
int  select_running_jobs(UAContext *ua, alist *jcrs, const char *reason);
CLIENT *get_client_resource(UAContext *ua, int32_t jobtype);
int     get_job_dbr(UAContext *ua, JOB_DBR *jr);

int find_arg_keyword(UAContext *ua, const char **list);
int find_arg(UAContext *ua, const char *keyword);
int find_arg_with_value(UAContext *ua, const char *keyword);
int do_keyword_prompt(UAContext *ua, const char *msg, const char **list);
int confirm_retention(UAContext *ua, utime_t *ret, const char *msg);
int confirm_retention_yesno(UAContext *ua, utime_t ret, const char *msg);
bool get_level_from_name(JCR *jcr, const char *level_name);
int get_level_code_from_name(const char *level_name);
int scan_storage_cmd(UAContext *ua, const char *cmd,
                     bool allfrompool,
                     int *drive, MEDIA_DBR *mr, POOL_DBR *pr,
                     const char **action, char *storage, int *nb, uint32_t **results);


/* ua_status.c */
void list_dir_status_header(UAContext *ua);

/* ua_tree.c */
bool user_select_files_from_tree(TREE_CTX *tree);
int insert_tree_handler(void *ctx, int num_fields, char **row);
bool check_directory_acl(char **last_dir, alist *dir_acl, const char *path);

/* ua_prune.c */
int prune_files(UAContext *ua, CLIENT *client, POOL *pool);
int prune_jobs(UAContext *ua, CLIENT *client, POOL *pool, int JobType);
int prune_stats(UAContext *ua, utime_t retention);
bool prune_volume(UAContext *ua, MEDIA_DBR *mr);
int job_delete_handler(void *ctx, int num_fields, char **row);
int del_count_handler(void *ctx, int num_fields, char **row);
int file_delete_handler(void *ctx, int num_fields, char **row);
int get_prune_list_for_volume(UAContext *ua, MEDIA_DBR *mr, del_ctx *del);
int exclude_running_jobs_from_list(del_ctx *prune_list);

/* ua_purge.c */
bool is_volume_purged(UAContext *ua, MEDIA_DBR *mr, bool force=false);
bool mark_media_purged(UAContext *ua, MEDIA_DBR *mr);
void purge_files_from_volume(UAContext *ua, MEDIA_DBR *mr);
bool purge_jobs_from_volume(UAContext *ua, MEDIA_DBR *mr, bool force=false);
void purge_files_from_jobs(UAContext *ua, char *jobs);
void purge_jobs_from_catalog(UAContext *ua, char *jobs);
void purge_job_list_from_catalog(UAContext *ua, del_ctx &del);
void purge_files_from_job_list(UAContext *ua, del_ctx &del);


/* ua_run.c */
extern int run_cmd(UAContext *ua, const char *cmd);
extern int restart_cmd(UAContext *ua, const char *cmd);
extern bool check_pool(int32_t JobType, int32_t JobLevel, POOL *pool, POOL *nextpool, const char **name);

/* verify.c */
extern bool do_verify(JCR *jcr);
extern bool do_verify_init(JCR *jcr);
extern void verify_cleanup(JCR *jcr, int TermCode);

/* snapshot.c */
int  select_snapshot_dbr(UAContext *ua, SNAPSHOT_DBR *sr);
void snapshot_list(UAContext *ua, int i, DB_LIST_HANDLER *sendit, e_list_type llist);
int  snapshot_cmd(UAContext *ua, const char *cmd);
int  snapshot_catreq(JCR *jcr, BSOCK *bs);
int  delete_snapshot(UAContext *ua);
bool update_snapshot(UAContext *ua);
int prune_snapshot(UAContext *ua);
bool send_snapshot_retention(JCR *jcr, utime_t val);
