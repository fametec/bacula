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
 * Written by Kern Sibbald, MM
 */

extern bool blast_data_to_storage_daemon(JCR *jcr, char *addr);
extern void do_verify_volume(JCR *jcr);
extern void do_restore(JCR *jcr);
extern int make_estimate(JCR *jcr);

/* From restore.c */
bool decompress_data(JCR *jcr, int32_t stream, char **data, uint32_t *length);

/* From authenticate.c */
bool authenticate_director(JCR *jcr);
bool authenticate_storagedaemon(JCR *jcr);

/* From hello.c */
bool validate_dir_hello(JCR *jcr);
bool send_hello_ok(BSOCK *bs);
bool send_sorry(BSOCK *bs);
bool send_hello_sd(JCR *jcr, char *Job);
void *handle_storage_connection(BSOCK *sd);
BSOCK *connect_director(JCR *jcr, CONSRES *dir);

/* From verify.c */
int digest_file(JCR *jcr, FF_PKT *ff_pkt, DIGEST *digest);
void do_verify(JCR *jcr);

/* From heartbeat.c */
void start_heartbeat_monitor(JCR *jcr);
void stop_heartbeat_monitor(JCR *jcr);
void start_dir_heartbeat(JCR *jcr);
void stop_dir_heartbeat(JCR *jcr);

/* from accurate.c */
bool accurate_finish(JCR *jcr);
bool accurate_check_file(JCR *jcr, FF_PKT *ff_pkt);
bool accurate_mark_file_as_seen(JCR *jcr, char *fname);
void accurate_free(JCR *jcr);
bool accurate_check_file(JCR *jcr, ATTR *attr, char *digest);

/* from backup.c */
void strip_path(FF_PKT *ff_pkt);
void unstrip_path(FF_PKT *ff_pkt);

/* from job.c */
findINCEXE *new_exclude(JCR *jcr);
findINCEXE *new_preinclude(JCR *jcr);
findINCEXE *get_incexe(JCR *jcr);
void set_incexe(JCR *jcr, findINCEXE *incexe);
void new_options(JCR *jcr, findINCEXE *incexe);
void add_file_to_fileset(JCR *jcr, const char *fname, bool is_file);
int add_options_to_fileset(JCR *jcr, const char *item);
int add_wild_to_fileset(JCR *jcr, const char *item, int type);
int add_regex_to_fileset(JCR *jcr, const char *item, int type);
findINCEXE *new_include(JCR *jcr);

/* from snapshot.c */
int snapshot_cmd(JCR *jcr);

#ifdef HAVE_WIN32
void VSSCleanup(VSSClient *c);
VSSClient *VSSInit();
#endif

/* Definition for encyption cipher/digest type  */
void store_cipher_type(LEX *lc, RES_ITEM *item, int index, int pass);
void store_digest_type(LEX *lc, RES_ITEM *item, int index, int pass);
