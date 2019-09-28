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
 * Prototypes for lib directory of Bacula
 *
 */

#ifndef __LIBPROTOS_H
#define __LIBPROTOS_H

class JCR;

/* address_conf.c */
void remove_duplicate_addresses(dlist *addr_list);

/* attr.c */
ATTR     *new_attr(JCR *jcr);
void      free_attr(ATTR *attr);
int       unpack_attributes_record(JCR *jcr, int32_t stream, char *rec, int32_t reclen, ATTR *attr);
void      build_attr_output_fnames(JCR *jcr, ATTR *attr);
void      print_ls_output(JCR *jcr, ATTR *attr, int message_type=M_RESTORED);

/* base64.c */
void      base64_init            (void);
int       to_base64              (int64_t value, char *where);
int       from_base64            (int64_t *value, char *where);
int       bin_to_base64          (char *buf, int buflen, char *bin, int binlen,
                                  int compatible);
int       base64_to_bin(char *dest, int destlen, char *src, int srclen);

/* bjson.c */
void strip_long_opts(char *out, const char *in);
void edit_alist(HPKT &hpkt);
void edit_msg_types(HPKT &hpkt, DEST *dest);
void display_msgs(HPKT &hpkt);
void display_alist(HPKT &hpkt);
bool display_alist_str(HPKT &hpkt);
bool display_alist_res(HPKT &hpkt);
void display_res(HPKT &hpkt);
void display_string_pair(HPKT &hpkt);
void display_int32_pair(HPKT &hpkt);
void display_int64_pair(HPKT &hpkt);
void display_bool_pair(HPKT &hpkt);
void display_bit_pair(HPKT &hpkt);
bool byte_is_set(char *byte, int num);
void display_bit_array(char *array, int num);
void display_last(HPKT &hpkt);
void init_hpkt(HPKT &hpkt);
void term_hpkt(HPKT &hpkt);
bool display_global_item(HPKT &hpkt);

/* bsys.c */
char *ucfirst(char *dest, const char *src, int len);
typedef enum {
   WAIT_READ  = 1,
   WAIT_WRITE = 2
} fd_wait_mode;
int fd_wait_data(int fd, fd_wait_mode mode, int sec, int msec);
FILE *bfopen(const char *path, const char *mode);
int baccept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int copyfile(const char *src, const char *dst);
void setup_env(char *envp[]);
POOLMEM  *quote_string           (POOLMEM *snew, const char *old);
POOLMEM  *quote_where            (POOLMEM *snew, const char *old);
char     *bstrncpy               (char *dest, const char *src, int maxlen);
char     *bstrncpy               (char *dest, POOL_MEM &src, int maxlen);
char     *bstrncat               (char *dest, const char *src, int maxlen);
char     *bstrncat               (char *dest, POOL_MEM &src, int maxlen);
bool      bstrcmp                (const char *s1, const char *s2);
bool      bstrcasecmp            (const char *s1, const char *s2);
int       cstrlen                (const char *str);
void     *b_malloc               (const char *file, int line, size_t size);
#ifndef bmalloc
void     *bmalloc                (size_t size);
#endif
void      bfree                  (void *buf);
void     *brealloc               (void *buf, size_t size);
void     *bcalloc                (size_t size1, size_t size2);
int       bsnprintf              (char *str, int32_t size, const char *format, ...);
int       bvsnprintf             (char *str, int32_t size, const char *format, va_list ap);
int       pool_sprintf           (char *pool_buf, const char *fmt, ...);
int       create_lock_file       (char *fname, const char *progname, const char *filetype, POOLMEM **errmsg, int *fd);
void      create_pid_file        (char *dir, const char *progname, int port);
int       delete_pid_file        (char *dir, const char *progname, int port);
void      drop                   (char *uid, char *gid, bool keep_readall_caps);
int reset_job_user();
int change_job_user(char *uname, char *gname, char *errmsg, int errlen);

int       bmicrosleep            (int32_t sec, int32_t usec);
char     *bfgets                 (char *s, int size, FILE *fd);
char     *bfgets                 (POOLMEM *&s, FILE *fd);
void      make_unique_filename   (POOLMEM **name, int Id, char *what);
#ifndef HAVE_STRTOLL
long long int strtoll            (const char *ptr, char **endptr, int base);
#endif
void      read_state_file(char *dir, const char *progname, int port);
int       b_strerror(int errnum, char *buf, size_t bufsiz);
char     *escape_filename(const char *file_path);
int       Zdeflate(char *in, int in_len, char *out, int &out_len);
int       Zinflate(char *in, int in_len, char *out, int &out_len);
void      stack_trace();
int       safer_unlink(const char *pathname, const char *regex);
int fs_get_free_space(const char *path, int64_t *freeval, int64_t *totalval);

/* bnet.c */
bool       bnet_tls_server       (TLS_CONTEXT *ctx, BSOCK *bsock,
                                  alist *verify_list);
bool       bnet_tls_client       (TLS_CONTEXT *ctx, BSOCK *bsock,
                                  alist *verify_list);
BSOCK *    init_bsock            (JCR *jcr, int sockfd, const char *who, const char *ip,
                                  int port, struct sockaddr *client_addr);
#ifdef HAVE_WIN32
#ifndef socklen_t
#define socklen_t int
#endif
#endif
int        bnet_get_peer           (BSOCK *bs, char *buf, socklen_t buflen);
BSOCK *    dup_bsock             (BSOCK *bsock);
void       term_bsock            (BSOCK *bsock);
const char *bnet_strerror         (BSOCK *bsock);
const char *bnet_sig_to_ascii     (int32_t msglen);
dlist *bnet_host2ipaddrs(const char *host, int family, const char **errstr);
void       bnet_restore_blocking (BSOCK *sock, int flags);
int        set_socket_errno(int sockstat);

/* bget_msg.c */
int      bget_msg(BSOCK *sock);

/* bpipe.c */
BPIPE *          open_bpipe(char *prog, int wait, const char *mode, char *envp[]=NULL);
int              close_wpipe(BPIPE *bpipe);
int              close_epipe(BPIPE *bpipe);
int              close_bpipe(BPIPE *bpipe);

/* cram-md5.c */
bool cram_md5_respond(BSOCK *bs, const char *password, int *tls_remote_need, int *compatible);
bool cram_md5_challenge(BSOCK *bs, const char *password, int tls_local_need, int compatible);
void hmac_md5(uint8_t* text, int text_len, uint8_t* key, int key_len, uint8_t *hmac);

/* crc32.c */

uint32_t bcrc32(unsigned char *buf, int len);


/* crypto.c */
int                init_crypto                 (void);
int                cleanup_crypto              (void);
DIGEST *           crypto_digest_new           (JCR *jcr, crypto_digest_t type);
bool               crypto_digest_update        (DIGEST *digest, const uint8_t *data, uint32_t length);
bool               crypto_digest_finalize      (DIGEST *digest, uint8_t *dest, uint32_t *length);
void               crypto_digest_free          (DIGEST *digest);
SIGNATURE *        crypto_sign_new             (JCR *jcr);
crypto_error_t     crypto_sign_get_digest      (SIGNATURE *sig, X509_KEYPAIR *keypair,
                                                crypto_digest_t &algorithm, DIGEST **digest);
crypto_error_t     crypto_sign_verify          (SIGNATURE *sig, X509_KEYPAIR *keypair, DIGEST *digest);
int                crypto_sign_add_signer      (SIGNATURE *sig, DIGEST *digest, X509_KEYPAIR *keypair);
int                crypto_sign_encode          (SIGNATURE *sig, uint8_t *dest, uint32_t *length);
SIGNATURE *        crypto_sign_decode          (JCR *jcr, const uint8_t *sigData, uint32_t length);
void               crypto_sign_free            (SIGNATURE *sig);
CRYPTO_SESSION *   crypto_session_new          (crypto_cipher_t cipher, alist *pubkeys);
void               crypto_session_free         (CRYPTO_SESSION *cs);
bool               crypto_session_encode       (CRYPTO_SESSION *cs, uint8_t *dest, uint32_t *length);
crypto_error_t     crypto_session_decode       (const uint8_t *data, uint32_t length, alist *keypairs, CRYPTO_SESSION **session);
CRYPTO_SESSION *   crypto_session_decode       (const uint8_t *data, uint32_t length);
CIPHER_CONTEXT *   crypto_cipher_new           (CRYPTO_SESSION *cs, bool encrypt, uint32_t *blocksize);
bool               crypto_cipher_update        (CIPHER_CONTEXT *cipher_ctx, const uint8_t *data, uint32_t length, const uint8_t *dest, uint32_t *written);
bool               crypto_cipher_finalize      (CIPHER_CONTEXT *cipher_ctx, uint8_t *dest, uint32_t *written);
void               crypto_cipher_free          (CIPHER_CONTEXT *cipher_ctx);
X509_KEYPAIR *     crypto_keypair_new          (void);
X509_KEYPAIR *     crypto_keypair_dup          (X509_KEYPAIR *keypair);
int                crypto_keypair_load_cert    (X509_KEYPAIR *keypair, const char *file);
bool               crypto_keypair_has_key      (const char *file);
int                crypto_keypair_load_key     (X509_KEYPAIR *keypair, const char *file, CRYPTO_PEM_PASSWD_CB *pem_callback, const void *pem_userdata);
void               crypto_keypair_free         (X509_KEYPAIR *keypair);
int                crypto_default_pem_callback (char *buf, int size, const void *userdata);
const char *       crypto_digest_name          (DIGEST *digest);
crypto_digest_t    crypto_digest_stream_type   (int stream);
const char *       crypto_strerror             (crypto_error_t error);

/* daemon.c */
void     daemon_start            ();

/* edit.c */
uint64_t         str_to_uint64(char *str);
int64_t          str_to_int64(char *str);
#define str_to_int32(str) ((int32_t)str_to_int64(str))
char *           edit_uint64_with_commas   (uint64_t val, char *buf);
char *           edit_uint64_with_suffix   (uint64_t val, char *buf);
char *           add_commas              (char *val, char *buf);
char *           edit_uint64             (uint64_t val, char *buf);
char *           edit_int64              (int64_t val, char *buf);
char *           edit_int64_with_commas  (int64_t val, char *buf);
bool             duration_to_utime       (char *str, utime_t *value);
bool             size_to_uint64(char *str, int str_len, uint64_t *rtn_value);
bool             speed_to_uint64(char *str, int str_len, uint64_t *rtn_value);
char             *edit_utime             (utime_t val, char *buf, int buf_len);
bool             is_a_number             (const char *num);
bool             is_a_number_list        (const char *n);
bool             is_an_integer           (const char *n);
bool             is_name_valid           (const char *name, POOLMEM **msg);

/* jcr.c (most definitions are in src/jcr.h) */
void     init_last_jobs_list();
void     term_last_jobs_list();
void     lock_last_jobs_list();
void     unlock_last_jobs_list();
bool     read_last_jobs_list(int fd, uint64_t addr);
uint64_t write_last_jobs_list(int fd, uint64_t addr);
void     write_state_file(char *dir, const char *progname, int port);
void     job_end_push(JCR *jcr, void job_end_cb(JCR *jcr,void *), void *ctx);
void     lock_jobs();
void     unlock_jobs();
JCR     *jcr_walk_start();
JCR     *jcr_walk_next(JCR *prev_jcr);
void     jcr_walk_end(JCR *jcr);
int      job_count();
JCR     *get_jcr_from_tsd();
void     set_jcr_in_tsd(JCR *jcr);
void     remove_jcr_from_tsd(JCR *jcr);
uint32_t get_jobid_from_tsd();
uint32_t get_jobid_from_tid(pthread_t tid);


/* lex.c */
LEX *     lex_close_file         (LEX *lf);
LEX *     lex_open_file          (LEX *lf, const char *fname, LEX_ERROR_HANDLER *scan_error);
LEX *     lex_open_buf           (LEX *lf, const char *buf, LEX_ERROR_HANDLER *scan_error);
int       lex_get_char           (LEX *lf);
void      lex_unget_char         (LEX *lf);
const char *  lex_tok_to_str     (int token);
int       lex_get_token          (LEX *lf, int expect);
void      lex_set_default_error_handler (LEX *lf);
int       lex_set_error_handler_error_type (LEX *lf, int err_type);
bool      lex_check_eol          (LEX *lf);

/* Required typedef, not in a C file */
extern "C" {
   typedef char *(*job_code_callback_t)(JCR *, const char *, char *, int);
}

/* message.c */
void       my_name_is            (int argc, char *argv[], const char *name);
void       init_msg              (JCR *jcr, MSGS *msg, job_code_callback_t job_code_callback = NULL);
void       term_msg              (void);
void       close_msg             (JCR *jcr);
void       add_msg_dest          (MSGS *msg, int dest, int type, char *where, char *dest_code);
void       rem_msg_dest          (MSGS *msg, int dest, int type, char *where);
void       Jmsg                  (JCR *jcr, int type, utime_t mtime, const char *fmt, ...);
void       dispatch_message      (JCR *jcr, int type, utime_t mtime, char *buf);
void       init_console_msg      (const char *wd);
void       free_msgs_res         (MSGS *msgs);
void       dequeue_messages      (JCR *jcr);
void       dequeue_daemon_messages (JCR *jcr);
void       set_db_engine_name    (const char *name);
void       set_trace             (int trace_flag);
bool       get_trace             (void);
void       set_hangup            (int hangup_value);
void       set_blowup            (int blowup_value);
int        get_hangup            (void);
int        get_blowup            (void);
bool       handle_hangup_blowup  (JCR *jcr, uint32_t file_count, uint64_t byte_count);
void       set_assert_msg        (const char *file, int line, const char *msg);
void       register_message_callback(void msg_callback(int type, char *msg));
void       setup_daemon_message_queue();
void       free_daemon_message_queue();

/* bnet_server.c */
void       bnet_thread_server(dlist *addr_list, int max_clients,
              workq_t *client_wq, void *handle_client_request(void *bsock));
void       bnet_stop_thread_server(pthread_t tid);
void             bnet_server             (int port, void handle_client_request(BSOCK *bsock));
int              net_connect             (int port);
BSOCK *          bnet_bind               (int port);
BSOCK *          bnet_accept             (BSOCK *bsock, char *who);

/* message.c */
typedef int (EVENT_HANDLER)(JCR *jcr, const char *event);
int generate_daemon_event(JCR *jcr, const char *event);

/* signal.c */
void             init_signals             (void terminate(int sig));
void             init_stack_dump          (void);

/* Used to display specific job information after a fatal signal */
typedef void (dbg_hook_t)(FILE *fp);
void dbg_add_hook(dbg_hook_t *fct);

/* scan.c */
char *next_name(char **s);
void             strip_leading_space     (char *str);
char            *strip_trailing_junk     (char *str);
char            *strip_trailing_newline  (char *str);
char            *strip_trailing_slashes  (char *dir);
bool             skip_spaces             (char **msg);
bool             skip_nonspaces          (char **msg);
int              fstrsch                 (const char *a, const char *b);
char            *next_arg(char **s);
int              parse_args(POOLMEM *cmd, POOLMEM **args, int *argc,
                        char **argk, char **argv, int max_args);
int              parse_args_only(POOLMEM *cmd, POOLMEM **args, int *argc,
                        char **argk, char **argv, int max_args);
void            split_path_and_filename(const char *fname, POOLMEM **path,
                        int *pnl, POOLMEM **file, int *fnl);
int             bsscanf(const char *buf, const char *fmt, ...);


/* tls.c */
TLS_CONTEXT      *new_tls_context        (const char *ca_certfile,
                                          const char *ca_certdir,
                                          const char *certfile,
                                          const char *keyfile,
                                          CRYPTO_PEM_PASSWD_CB *pem_callback,
                                          const void *pem_userdata,
                                          const char *dhfile,
                                          bool verify_peer);
void             free_tls_context        (TLS_CONTEXT *ctx);
#ifdef HAVE_TLS
bool             tls_postconnect_verify_host(JCR *jcr, TLS_CONNECTION *tls,
                                               const char *host);
bool             tls_postconnect_verify_cn(JCR *jcr, TLS_CONNECTION *tls,
                                               alist *verify_list);
TLS_CONNECTION   *new_tls_connection     (TLS_CONTEXT *ctx, int fd);
bool             tls_bsock_accept        (BSOCK *bsock);
int              tls_bsock_writen        (BSOCK *bsock, char *ptr, int32_t nbytes);
int              tls_bsock_readn         (BSOCK *bsock, char *ptr, int32_t nbytes);
bool             tls_bsock_probe         (BSOCKCORE *bsock);
#endif /* HAVE_TLS */
bool             tls_bsock_connect       (BSOCK *bsock);
void             tls_bsock_shutdown      (BSOCKCORE *bsock);
void             free_tls_connection     (TLS_CONNECTION *tls);
bool             get_tls_require         (TLS_CONTEXT *ctx);
bool             get_tls_enable          (TLS_CONTEXT *ctx);


/* util.c */
void             bmemzero                (void *buf, size_t size);
bool             is_null                 (const void *ptr);
bool             is_buf_zero             (const char *buf, int len);
void             lcase                   (char *str);
void             bash_spaces             (char *str);
void             bash_spaces             (POOL_MEM &pm);
void             unbash_spaces           (char *str);
void             unbash_spaces           (POOL_MEM &pm);
char *           encode_time             (utime_t time, char *buf);
char *           encode_mode             (mode_t mode, char *buf);
char *           hexdump(const char *data, int len, char *buf, int capacity, bool add_spaces=true);
char *           asciidump(const char *data, int len, char *buf, int capacity);
char *           smartdump(const char *data, int len, char *buf, int capacity, bool *is_ascii=NULL);
int              is_power_of_two         (uint64_t x);
int              do_shell_expansion      (char *name, int name_len);
void             jobstatus_to_ascii      (int JobStatus, char *msg, int maxlen);
void             jobstatus_to_ascii_gui  (int JobStatus, char *msg, int maxlen);
int              run_program             (char *prog, int wait, POOLMEM *&results);
int              run_program_full_output (char *prog, int wait, POOLMEM *&results, char *env[]=NULL);
char *           action_on_purge_to_string(int aop, POOL_MEM &ret);
const char *     job_type_to_str         (int type);
const char *     job_status_to_str       (int stat, int errors);
const char *     job_level_to_str        (int level);
const char *     volume_status_to_str    (const char *status);
void             make_session_key        (char *key, char *seed, int mode);
void             encode_session_key      (char *encode, char *session, char *key, int maxlen);
void             decode_session_key      (char *decode, char *session, char *key, int maxlen);
POOLMEM *        edit_job_codes          (JCR *jcr, char *omsg, char *imsg, const char *to, job_code_callback_t job_code_callback = NULL);
void             set_working_directory   (char *wd);
const char *     last_path_separator     (const char *str);

/* watchdog.c */
int start_watchdog(void);
int stop_watchdog(void);
watchdog_t *new_watchdog(void);
bool register_watchdog(watchdog_t *wd);
bool unregister_watchdog(watchdog_t *wd);
bool is_watchdog();

/* timers.c */
btimer_t *start_child_timer(JCR *jcr, pid_t pid, uint32_t wait);
void stop_child_timer(btimer_t *wid);
btimer_t *start_thread_timer(JCR *jcr, pthread_t tid, uint32_t wait);
void stop_thread_timer(btimer_t *wid);
btimer_t *start_bsock_timer(BSOCK *bs, uint32_t wait);
void stop_bsock_timer(btimer_t *wid);

#endif /* __LIBPROTOS_H */
