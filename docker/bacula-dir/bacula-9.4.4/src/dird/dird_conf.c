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
 *   Main configuration file parser for Bacula Directors,
 *    some parts may be split into separate files such as
 *    the schedule configuration (run_config.c).
 *
 *   Note, the configuration file parser consists of three parts
 *
 *   1. The generic lexical scanner in lib/lex.c and lib/lex.h
 *
 *   2. The generic config  scanner in lib/parse_config.c and
 *      lib/parse_config.h.
 *      These files contain the parser code, some utility
 *      routines, and the common store routines (name, int,
 *      string).
 *
 *   3. The daemon specific file, which contains the Resource
 *      definitions as well as any specific store routines
 *      for the resource records.
 *
 *     Kern Sibbald, January MM
 */


#include "bacula.h"
#include "dird.h"

/* Define the first and last resource ID record
 * types. Note, these should be unique for each
 * daemon though not a requirement.
 */
int32_t r_first = R_FIRST;
int32_t r_last  = R_LAST;
RES_HEAD **res_head;

static pthread_mutex_t globals_mutex = PTHREAD_MUTEX_INITIALIZER;
dlist client_globals;
dlist job_globals;
dlist store_globals;
dlist sched_globals;


/* Imported subroutines */
extern void store_run(LEX *lc, RES_ITEM *item, int index, int pass);
extern void store_finc(LEX *lc, RES_ITEM *item, int index, int pass);
extern void store_inc(LEX *lc, RES_ITEM *item, int index, int pass);


/* Forward referenced subroutines */

void store_jobtype(LEX *lc, RES_ITEM *item, int index, int pass);
void store_level(LEX *lc, RES_ITEM *item, int index, int pass);
void store_replace(LEX *lc, RES_ITEM *item, int index, int pass);
void store_acl(LEX *lc, RES_ITEM *item, int index, int pass);
void store_migtype(LEX *lc, RES_ITEM *item, int index, int pass);
void store_ac_res(LEX *lc, RES_ITEM *item, int index, int pass);
void store_device(LEX *lc, RES_ITEM *item, int index, int pass);
void store_actiononpurge(LEX *lc, RES_ITEM *item, int index, int pass);
static void store_runscript_when(LEX *lc, RES_ITEM *item, int index, int pass);
static void store_runscript_cmd(LEX *lc, RES_ITEM *item, int index, int pass);
static void store_short_runscript(LEX *lc, RES_ITEM *item, int index, int pass);

/* We build the current resource here as we are
 * scanning the resource configuration definition,
 * then move it to allocated memory when the resource
 * scan is complete.
 */
#if defined(_MSC_VER)
extern "C" { // work around visual compiler mangling variables
   URES res_all;
}
#else
URES res_all;
#endif
int32_t res_all_size = sizeof(res_all);

/* Implementation of certain classes */

void CLIENT::create_client_globals()
{
   globals = (CLIENT_GLOBALS *)malloc(sizeof(CLIENT_GLOBALS));
   memset(globals, 0, sizeof(CLIENT_GLOBALS));
   globals->name = bstrdup(name());
   globals->enabled = -1;       /* Not set */
   client_globals.append(globals);
}

int32_t CLIENT::getNumConcurrentJobs()
{
   if (!globals) {
      return 0;
   }
   return globals->NumConcurrentJobs;
}

void CLIENT::setNumConcurrentJobs(int32_t num)
{
   P(globals_mutex);
   if (!globals) {
      create_client_globals();
   }
   globals->NumConcurrentJobs = num;
   V(globals_mutex);
   ASSERT(num >= 0);
   Dmsg2(200, "Set NumConcurrentJobs=%ld for Client %s\n",
      num, globals->name);
}

char *CLIENT::address(POOLMEM *&buf)
{
   P(globals_mutex);
   if (!globals || !globals->SetIPaddress) {
      pm_strcpy(buf, client_address);

   } else {
      pm_strcpy(buf, globals->SetIPaddress);
   }
   V(globals_mutex);
   return buf;
}

void CLIENT::setAddress(char *addr)
{
   P(globals_mutex);
   if (!globals) {
      create_client_globals();
   }
   if (globals->SetIPaddress) {
      free(globals->SetIPaddress);
   }
   globals->SetIPaddress = bstrdup(addr);
   V(globals_mutex);
}

bool CLIENT::is_enabled()
{
   if (!globals || globals->enabled < 0) {
      return Enabled;
   }
   return globals->enabled;
}

void CLIENT::setEnabled(bool val)
{
   P(globals_mutex);
   if (!globals) {
      create_client_globals();
   }
   /* TODO: We probably need to set -1 (not set) when we are back to the default value */
   globals->enabled = val? 1 : 0;
   V(globals_mutex);
   Dmsg2(200, "Set Enabled=%d for Client %s\n",
      val, globals->name);
}

void JOB::create_job_globals()
{
   globals = (JOB_GLOBALS *)malloc(sizeof(JOB_GLOBALS));
   memset(globals, 0, sizeof(JOB_GLOBALS));
   globals->name = bstrdup(name());
   globals->enabled = -1;       /* Not set */
   job_globals.append(globals);
}

int32_t JOB::getNumConcurrentJobs()
{
   if (!globals) {
      return 0;
   }
   return globals->NumConcurrentJobs;
}

void JOB::setNumConcurrentJobs(int32_t num)
{
   P(globals_mutex);
   if (!globals) {
      create_job_globals();
   }
   globals->NumConcurrentJobs = num;
   V(globals_mutex);
   ASSERT(num >= 0);
   Dmsg2(200, "Set NumConcurrentJobs=%ld for Job %s\n",
      num, globals->name);
}

bool JOB::is_enabled()
{
   if (!globals || globals->enabled < 0) {
      return Enabled;
   }
   return globals->enabled;
}

void JOB::setEnabled(bool val)
{
   P(globals_mutex);
   if (!globals) {
      create_job_globals();
   }
   globals->enabled = val ? 1 : 0;
   V(globals_mutex);
   Dmsg2(200, "Set Enabled=%d for Job %s\n",
      val, globals->name);
}

void STORE::create_store_globals()
{
   globals = (STORE_GLOBALS *)malloc(sizeof(STORE_GLOBALS));
   memset(globals, 0, sizeof(STORE_GLOBALS));
   globals->name = bstrdup(name());
   globals->enabled = -1;       /* Not set */
   store_globals.append(globals);
}

int32_t STORE::getNumConcurrentReadJobs()
{
   if (!globals) {
      return 0;
   }
   return globals->NumConcurrentReadJobs;
}

void STORE::setNumConcurrentReadJobs(int32_t num)
{
   P(globals_mutex);
   if (!globals) {
      create_store_globals();
   }
   globals->NumConcurrentReadJobs = num;
   V(globals_mutex);
   Dmsg2(200, "Set NumConcurrentReadJobs=%ld for Store %s\n",
      num, globals->name);
   ASSERT(num >= 0);
}

int32_t STORE::getNumConcurrentJobs()
{
   if (!globals) {
      return 0;
   }
   return globals->NumConcurrentJobs;
}

void STORE::setNumConcurrentJobs(int32_t num)
{
   P(globals_mutex);
   if (!globals) {
      create_store_globals();
   }
   globals->NumConcurrentJobs = num;
   V(globals_mutex);
   Dmsg2(200, "Set numconcurrentJobs=%ld for Store %s\n",
      num, globals->name);
   ASSERT(num >= 0);
}

bool STORE::is_enabled()
{
   if (!globals || globals->enabled < 0) {
      return Enabled;
   }
   return globals->enabled;
}

void STORE::setEnabled(bool val)
{
   P(globals_mutex);
   if (!globals) {
      create_store_globals();
   }
   globals->enabled = val ? 1 : 0;
   V(globals_mutex);
   Dmsg2(200, "Set Enabled=%d for Storage %s\n",
      val, globals->name);
}

void SCHED::create_sched_globals()
{
   globals = (SCHED_GLOBALS *)malloc(sizeof(CLIENT_GLOBALS));
   memset(globals, 0, sizeof(SCHED_GLOBALS));
   globals->name = bstrdup(name());
   globals->enabled = -1;       /* Not set */
   sched_globals.append(globals);
}

bool SCHED::is_enabled()
{
   if (!globals || globals->enabled < 0) {
      return Enabled;
   }
   return globals->enabled;
}

void SCHED::setEnabled(bool val)
{
   P(globals_mutex);
   if (!globals) {
      create_sched_globals();
   }
   globals->enabled = val ? 1 : 0;
   V(globals_mutex);
   Dmsg2(200, "Set Enabled=%d for Schedule %s\n",
      val, globals->name);
}

/*
 * Definition of records permitted within each
 * resource with the routine to process the record
 * information.  NOTE! quoted names must be in lower case.
 */
/*
 *    Director Resource
 *
 *   name          handler     value                 code flags    default_value
 */
static RES_ITEM dir_items[] = {
   {"Name",        store_name,     ITEM(res_dir.hdr.name), 0, ITEM_REQUIRED, 0},
   {"Description", store_str,      ITEM(res_dir.hdr.desc), 0, 0, 0},
   {"Messages",    store_res,      ITEM(res_dir.messages), R_MSGS, 0, 0},
   {"DirPort",     store_addresses_port,    ITEM(res_dir.DIRaddrs),  0, ITEM_DEFAULT, 9101},
   {"DirAddress",  store_addresses_address, ITEM(res_dir.DIRaddrs),  0, ITEM_DEFAULT, 9101},
   {"DirAddresses",store_addresses,         ITEM(res_dir.DIRaddrs),  0, ITEM_DEFAULT, 9101},
   {"DirSourceAddress",store_addresses_address, ITEM(res_dir.DIRsrc_addr),  0, ITEM_DEFAULT, 0},
   {"QueryFile",   store_dir,      ITEM(res_dir.query_file), 0, ITEM_REQUIRED, 0},
   {"WorkingDirectory", store_dir, ITEM(res_dir.working_directory), 0, ITEM_REQUIRED, 0},
   {"PluginDirectory",  store_dir, ITEM(res_dir.plugin_directory),  0, 0, 0},
   {"ScriptsDirectory", store_dir, ITEM(res_dir.scripts_directory), 0, 0, 0},
   {"PidDirectory",     store_dir, ITEM(res_dir.pid_directory),     0, ITEM_REQUIRED, 0},
   {"SubsysDirectory",  store_dir, ITEM(res_dir.subsys_directory),  0, 0, 0},
   {"MaximumConcurrentJobs", store_pint32, ITEM(res_dir.MaxConcurrentJobs), 0, ITEM_DEFAULT, 20},
   {"MaximumReloadRequests", store_pint32, ITEM(res_dir.MaxReload), 0, ITEM_DEFAULT, 32},
   {"MaximumConsoleConnections", store_pint32, ITEM(res_dir.MaxConsoleConnect), 0, ITEM_DEFAULT, 20},
   {"Password",    store_password, ITEM(res_dir.password), 0, ITEM_REQUIRED, 0},
   {"FdConnectTimeout", store_time,ITEM(res_dir.FDConnectTimeout), 0, ITEM_DEFAULT, 3 * 60},
   {"SdConnectTimeout", store_time,ITEM(res_dir.SDConnectTimeout), 0, ITEM_DEFAULT, 30 * 60},
   {"HeartbeatInterval", store_time, ITEM(res_dir.heartbeat_interval), 0, ITEM_DEFAULT, 5 * 60},
   {"TlsAuthenticate",      store_bool,      ITEM(res_dir.tls_authenticate), 0, 0, 0},
   {"TlsEnable",            store_bool,      ITEM(res_dir.tls_enable), 0, 0, 0},
   {"TlsRequire",           store_bool,      ITEM(res_dir.tls_require), 0, 0, 0},
   {"TlsVerifyPeer",        store_bool,      ITEM(res_dir.tls_verify_peer), 0, ITEM_DEFAULT, true},
   {"TlsCaCertificateFile", store_dir,       ITEM(res_dir.tls_ca_certfile), 0, 0, 0},
   {"TlsCaCertificateDir",  store_dir,       ITEM(res_dir.tls_ca_certdir), 0, 0, 0},
   {"TlsCertificate",       store_dir,       ITEM(res_dir.tls_certfile), 0, 0, 0},
   {"TlsKey",               store_dir,       ITEM(res_dir.tls_keyfile), 0, 0, 0},
   {"TlsDhFile",            store_dir,       ITEM(res_dir.tls_dhfile), 0, 0, 0},
   {"TlsAllowedCn",         store_alist_str, ITEM(res_dir.tls_allowed_cns), 0, 0, 0},
   {"StatisticsRetention",  store_time,      ITEM(res_dir.stats_retention),  0, ITEM_DEFAULT, 60*60*24*31*12*5},
   {"VerId",                store_str,       ITEM(res_dir.verid), 0, 0, 0},
   {"CommCompression",      store_bool,      ITEM(res_dir.comm_compression), 0, ITEM_DEFAULT, true},
   {NULL, NULL, {0}, 0, 0, 0}
};

/*
 *    Console Resource
 *
 *   name          handler     value                 code flags    default_value
 */
static RES_ITEM con_items[] = {
   {"Name",        store_name,     ITEM(res_con.hdr.name), 0, ITEM_REQUIRED, 0},
   {"Description", store_str,      ITEM(res_con.hdr.desc), 0, 0, 0},
   {"Password",    store_password, ITEM(res_con.password), 0, ITEM_REQUIRED, 0},
   {"JobAcl",      store_acl,      ITEM(res_con.ACL_lists), Job_ACL, 0, 0},
   {"ClientAcl",   store_acl,      ITEM(res_con.ACL_lists), Client_ACL, 0, 0},
   {"StorageAcl",  store_acl,      ITEM(res_con.ACL_lists), Storage_ACL, 0, 0},
   {"ScheduleAcl", store_acl,      ITEM(res_con.ACL_lists), Schedule_ACL, 0, 0},
   {"RunAcl",      store_acl,      ITEM(res_con.ACL_lists), Run_ACL, 0, 0},
   {"PoolAcl",     store_acl,      ITEM(res_con.ACL_lists), Pool_ACL, 0, 0},
   {"CommandAcl",  store_acl,      ITEM(res_con.ACL_lists), Command_ACL, 0, 0},
   {"FilesetAcl",  store_acl,      ITEM(res_con.ACL_lists), FileSet_ACL, 0, 0},
   {"CatalogAcl",  store_acl,      ITEM(res_con.ACL_lists), Catalog_ACL, 0, 0},
   {"WhereAcl",    store_acl,      ITEM(res_con.ACL_lists), Where_ACL, 0, 0},
   {"RestoreClientAcl", store_acl, ITEM(res_con.ACL_lists), RestoreClient_ACL, 0, 0},
   {"BackupClientAcl",  store_acl, ITEM(res_con.ACL_lists), BackupClient_ACL, 0, 0},
   {"PluginOptionsAcl", store_acl, ITEM(res_con.ACL_lists), PluginOptions_ACL, 0, 0},
   {"DirectoryAcl",     store_acl, ITEM(res_con.ACL_lists), Directory_ACL, 0, 0},
   {"TlsAuthenticate",      store_bool,      ITEM(res_con.tls_authenticate), 0, 0, 0},
   {"TlsEnable",            store_bool,      ITEM(res_con.tls_enable), 0, 0, 0},
   {"TlsRequire",           store_bool,      ITEM(res_con.tls_require), 0, 0, 0},
   {"TlsVerifyPeer",        store_bool,      ITEM(res_con.tls_verify_peer), 0, ITEM_DEFAULT, true},
   {"TlsCaCertificateFile", store_dir,       ITEM(res_con.tls_ca_certfile), 0, 0, 0},
   {"TlsCaCertificateDir",  store_dir,       ITEM(res_con.tls_ca_certdir), 0, 0, 0},
   {"TlsCertificate",       store_dir,       ITEM(res_con.tls_certfile), 0, 0, 0},
   {"TlsKey",               store_dir,       ITEM(res_con.tls_keyfile), 0, 0, 0},
   {"TlsDhFile",            store_dir,       ITEM(res_con.tls_dhfile), 0, 0, 0},
   {"TlsAllowedCn",         store_alist_str, ITEM(res_con.tls_allowed_cns), 0, 0, 0},
   {NULL, NULL, {0}, 0, 0, 0}
};


/*
 *    Client or File daemon resource
 *
 *   name          handler     value                 code flags    default_value
 */

static RES_ITEM cli_items[] = {
   {"Name",     store_name,       ITEM(res_client.hdr.name), 0, ITEM_REQUIRED, 0},
   {"Description", store_str,     ITEM(res_client.hdr.desc), 0, 0, 0},
   {"fdaddress",  store_str,      ITEM(res_client.client_address),  0, 0, 0},
   {"Address",  store_str,        ITEM(res_client.client_address),  0, ITEM_REQUIRED, 0},
   {"FdPort",   store_pint32,     ITEM(res_client.FDport),   0, ITEM_DEFAULT, 9102},
   {"fdpassword", store_password, ITEM(res_client.password), 0, 0, 0},
   {"Password", store_password,   ITEM(res_client.password), 0, ITEM_REQUIRED, 0},
   {"FdStorageAddress", store_str, ITEM(res_client.fd_storage_address), 0, 0, 0},
   {"Catalog",  store_res,        ITEM(res_client.catalog),  R_CATALOG, ITEM_REQUIRED, 0},
   {"FileRetention", store_time,  ITEM(res_client.FileRetention), 0, ITEM_DEFAULT, 60*60*24*60},
   {"JobRetention",  store_time,  ITEM(res_client.JobRetention),  0, ITEM_DEFAULT, 60*60*24*180},
   {"HeartbeatInterval",    store_time, ITEM(res_client.heartbeat_interval), 0, ITEM_DEFAULT, 5 * 60},
   {"AutoPrune",            store_bool, ITEM(res_client.AutoPrune), 0, ITEM_DEFAULT, true},
   {"SDCallsClient",        store_bool, ITEM(res_client.sd_calls_client), 0, ITEM_DEFAULT, false},
   {"SnapshotRetention",  store_time,  ITEM(res_client.SnapRetention),  0, ITEM_DEFAULT, 0},
   {"MaximumConcurrentJobs", store_pint32,   ITEM(res_client.MaxConcurrentJobs), 0, ITEM_DEFAULT, 1},
   {"TlsAuthenticate",      store_bool,      ITEM(res_client.tls_authenticate), 0, 0, 0},
   {"TlsEnable",            store_bool,      ITEM(res_client.tls_enable), 0, 0, 0},
   {"TlsRequire",           store_bool,      ITEM(res_client.tls_require), 0, 0, 0},
   {"TlsCaCertificateFile", store_dir,       ITEM(res_client.tls_ca_certfile), 0, 0, 0},
   {"TlsCaCertificateDir",  store_dir,       ITEM(res_client.tls_ca_certdir), 0, 0, 0},
   {"TlsCertificate",       store_dir,       ITEM(res_client.tls_certfile), 0, 0, 0},
   {"TlsKey",               store_dir,       ITEM(res_client.tls_keyfile), 0, 0, 0},
   {"TlsAllowedCn",         store_alist_str, ITEM(res_client.tls_allowed_cns), 0, 0, 0},
   {"MaximumBandwidthPerJob", store_speed, ITEM(res_client.max_bandwidth), 0, 0, 0},
   {"Enabled",     store_bool, ITEM(res_client.Enabled), 0, ITEM_DEFAULT, true},
   {NULL, NULL, {0}, 0, 0, 0}
};

/* Storage daemon resource
 *
 *   name          handler     value                 code flags    default_value
 */
static RES_ITEM store_items[] = {
   {"Name",        store_name,     ITEM(res_store.hdr.name),   0, ITEM_REQUIRED, 0},
   {"Description", store_str,      ITEM(res_store.hdr.desc),   0, 0, 0},
   {"SdPort",      store_pint32,   ITEM(res_store.SDport),     0, ITEM_DEFAULT, 9103},
   {"sdaddress",   store_str,      ITEM(res_store.address),    0, 0, 0},
   {"Address",     store_str,      ITEM(res_store.address),    0, ITEM_REQUIRED, 0},
   {"FdStorageAddress", store_str, ITEM(res_store.fd_storage_address), 0, 0, 0},
   {"sdpassword",  store_password, ITEM(res_store.password),   0, 0, 0},
   {"Password",    store_password, ITEM(res_store.password),   0, ITEM_REQUIRED, 0},
   {"Device",      store_device,   ITEM(res_store.device),     R_DEVICE, ITEM_REQUIRED, 0},
   {"MediaType",   store_strname,  ITEM(res_store.media_type), 0, ITEM_REQUIRED, 0},
   /*                   _bool,
    * Big kludge, these two autochanger definitions must be in
    * this order and together.
    */
   {"autochanger", store_ac_res,   ITEM(res_store.changer), 0, ITEM_DEFAULT, 0},
   {"autochanger", store_bool,     ITEM(res_store.autochanger), 0, ITEM_DEFAULT, false},
   {"SharedStorage", store_ac_res, ITEM(res_store.shared_storage), 1, ITEM_DEFAULT, 0},
   {"Enabled",     store_bool,     ITEM(res_store.Enabled),     0, ITEM_DEFAULT, true},
   {"AllowCompression",  store_bool, ITEM(res_store.AllowCompress), 0, ITEM_DEFAULT, true},
   {"HeartbeatInterval", store_time, ITEM(res_store.heartbeat_interval), 0, ITEM_DEFAULT, 5 * 60},
   {"MaximumConcurrentJobs", store_pint32, ITEM(res_store.MaxConcurrentJobs), 0, ITEM_DEFAULT, 1},
   {"MaximumConcurrentReadjobs", store_pint32, ITEM(res_store.MaxConcurrentReadJobs), 0, ITEM_DEFAULT, 0},
   {"sddport", store_pint32, ITEM(res_store.SDDport), 0, 0, 0}, /* deprecated */
   {"TlsAuthenticate",      store_bool,      ITEM(res_store.tls_authenticate), 0, 0, 0},
   {"TlsEnable",            store_bool,      ITEM(res_store.tls_enable), 0, 0, 0},
   {"TlsRequire",           store_bool,      ITEM(res_store.tls_require), 0, 0, 0},
   {"TlsCaCertificateFile", store_dir,       ITEM(res_store.tls_ca_certfile), 0, 0, 0},
   {"TlsCaCertificateDir",  store_dir,       ITEM(res_store.tls_ca_certdir), 0, 0, 0},
   {"TlsCertificate",       store_dir,       ITEM(res_store.tls_certfile), 0, 0, 0},
   {"TlsKey",               store_dir,       ITEM(res_store.tls_keyfile), 0, 0, 0},
   {NULL, NULL, {0}, 0, 0, 0}
};

/*
 *    Catalog Resource Directives
 *
 *   name          handler     value                 code flags    default_value
 */
static RES_ITEM cat_items[] = {
   {"Name",     store_name,     ITEM(res_cat.hdr.name),    0, ITEM_REQUIRED, 0},
   {"Description", store_str,   ITEM(res_cat.hdr.desc),    0, 0, 0},
   {"dbaddress", store_str,     ITEM(res_cat.db_address),  0, 0, 0},
   {"Address",  store_str,      ITEM(res_cat.db_address),  0, 0, 0},
   {"DbPort",   store_pint32,   ITEM(res_cat.db_port),      0, 0, 0},
   /* keep this password as store_str for the moment */
   {"dbpassword", store_str,    ITEM(res_cat.db_password), 0, 0, 0},
   {"Password", store_str,      ITEM(res_cat.db_password), 0, 0, 0},
   {"dbuser",   store_str,      ITEM(res_cat.db_user),     0, 0, 0},
   {"User",     store_str,      ITEM(res_cat.db_user),     0, 0, 0},
   {"DbName",   store_str,      ITEM(res_cat.db_name),     0, ITEM_REQUIRED, 0},
   {"dbdriver", store_str,      ITEM(res_cat.db_driver),   0, 0, 0},
   {"DbSocket", store_str,      ITEM(res_cat.db_socket),   0, 0, 0},
   {"dbsslmode", store_str,     ITEM(res_cat.db_ssl_mode),  0, 0, 0},
   {"dbsslkey", store_str,      ITEM(res_cat.db_ssl_key),  0, 0, 0},
   {"dbsslcert", store_str,     ITEM(res_cat.db_ssl_cert),  0, 0, 0},
   {"dbsslca", store_str,       ITEM(res_cat.db_ssl_ca),  0, 0, 0},
   {"dbsslcapath", store_str,   ITEM(res_cat.db_ssl_capath),  0, 0, 0},
   {"DbSocket", store_str,      ITEM(res_cat.db_socket),   0, 0, 0},
   /* Turned off for the moment */
   {"MultipleConnections", store_bit, ITEM(res_cat.mult_db_connections), 0, 0, 0},
   {"DisableBatchInsert", store_bool, ITEM(res_cat.disable_batch_insert), 0, ITEM_DEFAULT, false},
   {NULL, NULL, {0}, 0, 0, 0}
};

/*
 *    Job Resource Directives
 *
 *   name          handler     value                 code flags    default_value
 */
RES_ITEM job_items[] = {
   {"Name",      store_name,    ITEM(res_job.hdr.name),  0, ITEM_REQUIRED, 0},
   {"Description", store_str,   ITEM(res_job.hdr.desc),  0, 0, 0},
   {"Type",      store_jobtype, ITEM(res_job.JobType),   0, ITEM_REQUIRED, 0},
   {"Level",     store_level,   ITEM(res_job.JobLevel),    0, 0, 0},
   {"Messages",  store_res,     ITEM(res_job.messages),  R_MSGS, ITEM_REQUIRED, 0},
   {"Storage",   store_alist_res, ITEM(res_job.storage),  R_STORAGE, 0, 0},
   {"Pool",      store_res,     ITEM(res_job.pool),      R_POOL, ITEM_REQUIRED, 0},
   {"NextPool",  store_res,     ITEM(res_job.next_pool), R_POOL, 0, 0},
   {"FullBackupPool",  store_res, ITEM(res_job.full_pool),   R_POOL, 0, 0},
   {"VirtualFullBackupPool", store_res, ITEM(res_job.vfull_pool), R_POOL, 0, 0},
   {"IncrementalBackupPool",  store_res, ITEM(res_job.inc_pool), R_POOL, 0, 0},
   {"DifferentialBackupPool", store_res, ITEM(res_job.diff_pool), R_POOL, 0, 0},
   {"Client",    store_res,     ITEM(res_job.client),   R_CLIENT, ITEM_REQUIRED, 0},
   {"Fileset",   store_res,     ITEM(res_job.fileset),  R_FILESET, ITEM_REQUIRED, 0},
   {"Schedule",  store_res,     ITEM(res_job.schedule), R_SCHEDULE, 0, 0},
   {"VerifyJob", store_res,     ITEM(res_job.verify_job), R_JOB, 0, 0},
   {"JobToVerify", store_res,   ITEM(res_job.verify_job), R_JOB, 0, 0},
   {"JobDefs",   store_res,     ITEM(res_job.jobdefs),    R_JOBDEFS, 0, 0},
   {"Run",       store_alist_str, ITEM(res_job.run_cmds), 0, 0, 0},
   /* Root of where to restore files */
   {"Where",    store_dir,      ITEM(res_job.RestoreWhere), 0, 0, 0},
   {"RegexWhere",    store_str, ITEM(res_job.RegexWhere), 0, 0, 0},
   {"StripPrefix",   store_str, ITEM(res_job.strip_prefix), 0, 0, 0},
   {"AddPrefix",    store_str,  ITEM(res_job.add_prefix), 0, 0, 0},
   {"AddSuffix",    store_str,  ITEM(res_job.add_suffix), 0, 0, 0},
   /* Where to find bootstrap during restore */
   {"Bootstrap",store_dir,      ITEM(res_job.RestoreBootstrap), 0, 0, 0},
   {"RestoreClient", store_str,   ITEM(res_job.RestoreClient), 0, 0, 0},
   /* Where to write bootstrap file during backup */
   {"WriteBootstrap",store_dir, ITEM(res_job.WriteBootstrap), 0, 0, 0},
   {"WriteVerifyList",store_dir,ITEM(res_job.WriteVerifyList), 0, 0, 0},
   {"Replace",  store_replace,  ITEM(res_job.replace), 0, ITEM_DEFAULT, REPLACE_ALWAYS},
   {"MaximumBandwidth", store_speed, ITEM(res_job.max_bandwidth), 0, 0, 0},
   {"MaxRunSchedTime", store_time, ITEM(res_job.MaxRunSchedTime), 0, 0, 0},
   {"MaxRunTime",   store_time, ITEM(res_job.MaxRunTime), 0, 0, 0},
   /* xxxMaxWaitTime are deprecated */
   {"fullmaxwaittime",  store_time, ITEM(res_job.FullMaxRunTime), 0, 0, 0},
   {"incrementalmaxwaittime",  store_time, ITEM(res_job.IncMaxRunTime), 0, 0, 0},
   {"differentialmaxwaittime", store_time, ITEM(res_job.DiffMaxRunTime), 0, 0, 0},
   {"FullMaxRunTime",  store_time, ITEM(res_job.FullMaxRunTime), 0, 0, 0},
   {"IncrementalMaxRunTime",  store_time, ITEM(res_job.IncMaxRunTime), 0, 0, 0},
   {"DifferentialMaxRunTime", store_time, ITEM(res_job.DiffMaxRunTime), 0, 0, 0},
   {"MaxWaitTime",  store_time, ITEM(res_job.MaxWaitTime), 0, 0, 0},
   {"MaxStartDelay",store_time, ITEM(res_job.MaxStartDelay), 0, 0, 0},
   {"MaxFullInterval",  store_time, ITEM(res_job.MaxFullInterval), 0, 0, 0},
   {"MaxVirtualFullInterval",  store_time, ITEM(res_job.MaxVirtualFullInterval), 0, 0, 0},
   {"MaxDiffInterval",  store_time, ITEM(res_job.MaxDiffInterval), 0, 0, 0},
   {"PrefixLinks", store_bool, ITEM(res_job.PrefixLinks), 0, ITEM_DEFAULT, false},
   {"PruneJobs",   store_bool, ITEM(res_job.PruneJobs), 0, ITEM_DEFAULT, false},
   {"PruneFiles",  store_bool, ITEM(res_job.PruneFiles), 0, ITEM_DEFAULT, false},
   {"PruneVolumes",store_bool, ITEM(res_job.PruneVolumes), 0, ITEM_DEFAULT, false},
   {"PurgeMigrationJob",  store_bool, ITEM(res_job.PurgeMigrateJob), 0, ITEM_DEFAULT, false},
   {"Enabled",     store_bool, ITEM(res_job.Enabled), 0, ITEM_DEFAULT, true},
   {"SnapshotRetention",  store_time,  ITEM(res_job.SnapRetention),  0, ITEM_DEFAULT, 0},
   {"SpoolAttributes",store_bool, ITEM(res_job.SpoolAttributes), 0, ITEM_DEFAULT, true},
   {"SpoolData",   store_bool, ITEM(res_job.spool_data), 0, ITEM_DEFAULT, false},
   {"SpoolSize",   store_size64, ITEM(res_job.spool_size), 0, 0, 0},
   {"ReRunFailedLevels",   store_bool, ITEM(res_job.rerun_failed_levels), 0, ITEM_DEFAULT, false},
   {"PreferMountedVolumes", store_bool, ITEM(res_job.PreferMountedVolumes), 0, ITEM_DEFAULT, true},
   /*
    * JSON tools skip Directive in lowercase. They are deprecated or
    * are synonym with an other one that follows. Like User and dbuser.
    */
   {"runbeforejob", store_short_runscript,  ITEM(res_job.RunScripts),  0, 0, 0},
   {"runafterjob",  store_short_runscript,  ITEM(res_job.RunScripts),  0, 0, 0},
   {"runafterfailedjob",  store_short_runscript,  ITEM(res_job.RunScripts),  0, 0, 0},
   {"clientrunbeforejob", store_short_runscript,  ITEM(res_job.RunScripts),  0, 0, 0},
   {"clientrunafterjob",  store_short_runscript,  ITEM(res_job.RunScripts),  0, 0, 0},
   {"consolerunbeforejob", store_short_runscript,  ITEM(res_job.RunScripts),  0, 0, 0},
   {"consolerunafterjob",  store_short_runscript,  ITEM(res_job.RunScripts),  0, 0, 0},
   {"Runscript",          store_runscript, ITEM(res_job.RunScripts), 0, ITEM_NO_EQUALS, 0},
   {"MaximumConcurrentJobs", store_pint32, ITEM(res_job.MaxConcurrentJobs), 0, ITEM_DEFAULT, 1},
   {"MaximumSpawnedJobs", store_pint32, ITEM(res_job.MaxSpawnedJobs), 0, ITEM_DEFAULT, 600},
   {"RescheduleOnError", store_bool, ITEM(res_job.RescheduleOnError), 0, ITEM_DEFAULT, false},
   {"RescheduleIncompleteJobs", store_bool, ITEM(res_job.RescheduleIncompleteJobs), 0, ITEM_DEFAULT, true},
   {"RescheduleInterval", store_time, ITEM(res_job.RescheduleInterval), 0, ITEM_DEFAULT, 60 * 30},
   {"RescheduleTimes",    store_pint32, ITEM(res_job.RescheduleTimes), 0, 0, 0},
   {"Priority",           store_pint32, ITEM(res_job.Priority), 0, ITEM_DEFAULT, 10},
   {"BackupsToKeep",      store_pint32, ITEM(res_job.BackupsToKeep), 0, ITEM_DEFAULT, 0},
   {"AllowMixedPriority", store_bool, ITEM(res_job.allow_mixed_priority), 0, ITEM_DEFAULT, false},
   {"WritePartAfterJob",  store_bool, ITEM(res_job.write_part_after_job), 0, ITEM_DEFAULT, true},
   {"SelectionPattern",   store_str, ITEM(res_job.selection_pattern), 0, 0, 0},
   {"SelectionType",      store_migtype, ITEM(res_job.selection_type), 0, 0, 0},
   {"Accurate",           store_bool, ITEM(res_job.accurate), 0,0,0},
   {"AllowDuplicateJobs", store_bool, ITEM(res_job.AllowDuplicateJobs), 0, ITEM_DEFAULT, true},
   {"allowhigherduplicates",   store_bool, ITEM(res_job.AllowHigherDuplicates), 0, ITEM_DEFAULT, true},
   {"CancelLowerLevelDuplicates", store_bool, ITEM(res_job.CancelLowerLevelDuplicates), 0, ITEM_DEFAULT, false},
   {"CancelQueuedDuplicates",  store_bool, ITEM(res_job.CancelQueuedDuplicates), 0, ITEM_DEFAULT, false},
   {"CancelRunningDuplicates", store_bool, ITEM(res_job.CancelRunningDuplicates), 0, ITEM_DEFAULT, false},
   {"DeleteConsolidatedJobs",  store_bool, ITEM(res_job.DeleteConsolidatedJobs), 0, ITEM_DEFAULT, false},
   {"PluginOptions", store_str, ITEM(res_job.PluginOptions), 0, 0, 0},
   {"Base", store_alist_res, ITEM(res_job.base),  R_JOB, 0, 0},
   {NULL, NULL, {0}, 0, 0, 0}
};

/* Fileset resource
 *
 *   Name          handler     value                 code flags    default_value
 */
static RES_ITEM fs_items[] = {
   {"Name",        store_name, ITEM(res_fs.hdr.name), 0, ITEM_REQUIRED, 0},
   {"Description", store_str,  ITEM(res_fs.hdr.desc), 0, 0, 0},
   {"IgnoreFilesetChanges", store_bool, ITEM(res_fs.ignore_fs_changes), 0, ITEM_DEFAULT, false},
   {"EnableVss",     store_bool, ITEM(res_fs.enable_vss), 0, ITEM_DEFAULT, true},
   {"EnableSnapshot",store_bool, ITEM(res_fs.enable_snapshot), 0, ITEM_DEFAULT, false},
   {"Include",     store_inc,  {0},                   0, ITEM_NO_EQUALS, 0},
   {"Exclude",     store_inc,  {0},                   1, ITEM_NO_EQUALS, 0},
   {NULL,          NULL,       {0},                  0, 0, 0}
};

/* Schedule -- see run_conf.c */
/* Schedule
 *
 *   name          handler     value                 code flags    default_value
 */
static RES_ITEM sch_items[] = {
   {"Name",        store_name,  ITEM(res_sch.hdr.name), 0, ITEM_REQUIRED, 0},
   {"Description", store_str,   ITEM(res_sch.hdr.desc), 0, 0, 0},
   {"Run",         store_run,   ITEM(res_sch.run),      0, 0, 0},
   {"Enabled",     store_bool,  ITEM(res_sch.Enabled),  0, ITEM_DEFAULT, true},
   {NULL, NULL, {0}, 0, 0, 0}
};

/* Pool resource
 *
 *   name             handler     value                        code flags default_value
 */
static RES_ITEM pool_items[] = {
   {"Name",            store_name,    ITEM(res_pool.hdr.name),      0, ITEM_REQUIRED, 0},
   {"Description",     store_str,     ITEM(res_pool.hdr.desc),      0, 0,     0},
   {"PoolType",        store_strname, ITEM(res_pool.pool_type),     0, ITEM_REQUIRED, 0},
   {"LabelFormat",     store_strname, ITEM(res_pool.label_format),  0, 0,     0},
   {"LabelType",       store_label,   ITEM(res_pool.LabelType),     0, 0,     0},
   {"CleaningPrefix",  store_strname, ITEM(res_pool.cleaning_prefix), 0, 0,   0},
   {"UseCatalog",      store_bool,    ITEM(res_pool.use_catalog),    0, ITEM_DEFAULT, true},
   {"UseVolumeOnce",   store_bool,    ITEM(res_pool.use_volume_once), 0, 0,   0},
   {"PurgeOldestVolume", store_bool,  ITEM(res_pool.purge_oldest_volume), 0, 0, 0},
   {"ActionOnPurge",   store_actiononpurge, ITEM(res_pool.action_on_purge), 0, 0, 0},
   {"RecycleOldestVolume", store_bool,  ITEM(res_pool.recycle_oldest_volume), 0, 0, 0},
   {"RecycleCurrentVolume", store_bool, ITEM(res_pool.recycle_current_volume), 0, 0, 0},
   {"MaximumVolumes",  store_pint32,    ITEM(res_pool.max_volumes),   0, 0,        0},
   {"MaximumVolumeJobs", store_pint32,  ITEM(res_pool.MaxVolJobs),    0, 0,       0},
   {"MaximumVolumeFiles", store_pint32, ITEM(res_pool.MaxVolFiles),   0, 0,       0},
   {"MaximumVolumeBytes", store_size64, ITEM(res_pool.MaxVolBytes),   0, 0,       0},
   {"CatalogFiles",    store_bool,    ITEM(res_pool.catalog_files),  0, ITEM_DEFAULT, true},
   {"CacheRetention", store_time,    ITEM(res_pool.CacheRetention),   0, 0, 0},
   {"VolumeRetention", store_time,    ITEM(res_pool.VolRetention),   0, ITEM_DEFAULT, 60*60*24*365},
   {"VolumeUseDuration", store_time,  ITEM(res_pool.VolUseDuration), 0, 0, 0},
   {"MigrationTime",  store_time,     ITEM(res_pool.MigrationTime), 0, 0, 0},
   {"MigrationHighBytes", store_size64, ITEM(res_pool.MigrationHighBytes), 0, 0, 0},
   {"MigrationLowBytes", store_size64,  ITEM(res_pool.MigrationLowBytes), 0, 0, 0},
   {"NextPool",      store_res,       ITEM(res_pool.NextPool), R_POOL, 0, 0},
   {"Storage",       store_alist_res, ITEM(res_pool.storage),  R_STORAGE, 0, 0},
   {"AutoPrune",     store_bool,      ITEM(res_pool.AutoPrune), 0, ITEM_DEFAULT, true},
   {"Recycle",       store_bool,      ITEM(res_pool.Recycle),   0, ITEM_DEFAULT, true},
   {"RecyclePool",   store_res,       ITEM(res_pool.RecyclePool), R_POOL, 0, 0},
   {"ScratchPool",   store_res,       ITEM(res_pool.ScratchPool), R_POOL, 0, 0},
   {"CopyPool",      store_alist_res, ITEM(res_pool.CopyPool), R_POOL, 0, 0},
   {"Catalog",       store_res,       ITEM(res_pool.catalog), R_CATALOG, 0, 0},
   {"FileRetention", store_time,      ITEM(res_pool.FileRetention), 0, 0, 0},
   {"JobRetention",  store_time,      ITEM(res_pool.JobRetention),  0, 0, 0},

   {NULL, NULL, {0}, 0, 0, 0}
};

/*
 * Counter Resource
 *   name             handler     value                        code flags default_value
 */
static RES_ITEM counter_items[] = {
   {"Name",            store_name,    ITEM(res_counter.hdr.name),        0, ITEM_REQUIRED, 0},
   {"Description",     store_str,     ITEM(res_counter.hdr.desc),        0, 0,     0},
   {"Minimum",         store_int32,   ITEM(res_counter.MinValue),        0, ITEM_DEFAULT, 0},
   {"Maximum",         store_pint32,  ITEM(res_counter.MaxValue),        0, ITEM_DEFAULT, INT32_MAX},
   {"WrapCounter",     store_res,     ITEM(res_counter.WrapCounter),     R_COUNTER, 0, 0},
   {"Catalog",         store_res,     ITEM(res_counter.Catalog),         R_CATALOG, 0, 0},
   {NULL, NULL, {0}, 0, 0, 0}
};


/* Message resource */
extern RES_ITEM msgs_items[];

/*
 * This is the master resource definition.
 * It must have one item for each of the resources.
 *
 *  NOTE!!! keep it in the same order as the R_codes
 *    or eliminate all resources[rindex].name
 *
 *  name             items        rcode
 */
RES_TABLE resources[] = {
   {"Director",      dir_items,        R_DIRECTOR},
   {"Client",        cli_items,        R_CLIENT},
   {"Job",           job_items,        R_JOB},
   {"Storage",       store_items,      R_STORAGE},
   {"Catalog",       cat_items,        R_CATALOG},
   {"Schedule",      sch_items,        R_SCHEDULE},
   {"Fileset",       fs_items,         R_FILESET},
   {"Pool",          pool_items,       R_POOL},
   {"Messages",      msgs_items,       R_MSGS},
   {"Counter",       counter_items,    R_COUNTER},
   {"Console",       con_items,        R_CONSOLE},
   {"JobDefs",       job_items,        R_JOBDEFS},
   {"Device",        NULL,             R_DEVICE},  /* info obtained from SD */
   {"Autochanger",   store_items,      R_AUTOCHANGER},  /* alias for R_STORAGE */
   {NULL,            NULL,             0}
};


/* Keywords (RHS) permitted in Job Level records
 *
 *   level_name      level              job_type
 */
struct s_jl joblevels[] = {
   {"Full",          L_FULL,            JT_BACKUP},
   {"Base",          L_BASE,            JT_BACKUP},
   {"Incremental",   L_INCREMENTAL,     JT_BACKUP},
   {"Differential",  L_DIFFERENTIAL,    JT_BACKUP},
   {"Since",         L_SINCE,           JT_BACKUP},
   {"VirtualFull",   L_VIRTUAL_FULL,    JT_BACKUP},
   {"Catalog",       L_VERIFY_CATALOG,  JT_VERIFY},
   {"InitCatalog",   L_VERIFY_INIT,     JT_VERIFY},
   {"VolumeToCatalog", L_VERIFY_VOLUME_TO_CATALOG,   JT_VERIFY},
   {"DiskToCatalog", L_VERIFY_DISK_TO_CATALOG,   JT_VERIFY},
   {"Data",          L_VERIFY_DATA,     JT_VERIFY},
   {"Full",          L_FULL,            JT_COPY},
   {"Incremental",   L_INCREMENTAL,     JT_COPY},
   {"Differential",  L_DIFFERENTIAL,    JT_COPY},
   {"Full",          L_FULL,            JT_MIGRATE},
   {"Incremental",   L_INCREMENTAL,     JT_MIGRATE},
   {"Differential",  L_DIFFERENTIAL,    JT_MIGRATE},
   {" ",             L_NONE,            JT_ADMIN},
   {" ",             L_NONE,            JT_RESTORE},
   {NULL,            0,                          0}
};


/* Keywords (RHS) permitted in Job type records
 *
 *   type_name       job_type
 */
s_jt jobtypes[] = {
   {"Backup",        JT_BACKUP},
   {"Admin",         JT_ADMIN},
   {"Verify",        JT_VERIFY},
   {"Restore",       JT_RESTORE},
   {"Migrate",       JT_MIGRATE},
   {"Copy",          JT_COPY},
   {NULL,            0}
};


/* Keywords (RHS) permitted in Selection type records
 *
 *   type_name       job_type
 */
s_jt migtypes[] = {
   {"SmallestVolume",   MT_SMALLEST_VOL},
   {"OldestVolume",     MT_OLDEST_VOL},
   {"PoolOccupancy",    MT_POOL_OCCUPANCY},
   {"PoolTime",         MT_POOL_TIME},
   {"PoolUncopiedJobs", MT_POOL_UNCOPIED_JOBS},
   {"Client",           MT_CLIENT},
   {"Volume",           MT_VOLUME},
   {"Job",              MT_JOB},
   {"SqlQuery",         MT_SQLQUERY},
   {NULL,            0}
};



/* Options permitted in Restore replace= */
s_kw ReplaceOptions[] = {
   {"Always",         REPLACE_ALWAYS},
   {"IfNewer",        REPLACE_IFNEWER},
   {"IfOlder",        REPLACE_IFOLDER},
   {"Never",          REPLACE_NEVER},
   {NULL,               0}
};

char *CAT::display(POOLMEM *dst) {
   Mmsg(dst,"catalog=%s\ndb_name=%s\ndb_driver=%s\ndb_user=%s\n"
        "db_password=%s\ndb_address=%s\ndb_port=%i\n"
        "db_socket=%s\n",
        name(), NPRTB(db_name),
        NPRTB(db_driver), NPRTB(db_user), NPRTB(db_password),
        NPRTB(db_address), db_port, NPRTB(db_socket));
   return dst;
}

char *level_to_str(char *buf, int len, int level)
{
   int i;
   bsnprintf(buf, len, "%c (%d)", level, level);    /* default if not found */
   for (i=0; joblevels[i].level_name; i++) {
      if (level == (int)joblevels[i].level) {
         bstrncpy(buf, joblevels[i].level_name, len);
         break;
      }
   }
   return buf;
}

/* Dump contents of resource */
void dump_resource(int type, RES *ares, void sendit(void *sock, const char *fmt, ...), void *sock)
{
   RES *next;
   URES *res = (URES *)ares;
   bool recurse = true;
   char ed1[100], ed2[100], ed3[100], edl[50];
   DEVICE *dev;
   UAContext *ua = (UAContext *)sock;
   POOLMEM *buf;

   if (res == NULL) {
      sendit(sock, _("No %s resource defined\n"), res_to_str(type));
      return;
   }
   if (type < 0) {                    /* no recursion */
      type = -type;
      recurse = false;
   }
   switch (type) {
   case R_DIRECTOR:
      sendit(sock, _("Director: name=%s MaxJobs=%d FDtimeout=%s SDtimeout=%s\n"),
         ares->name, res->res_dir.MaxConcurrentJobs,
         edit_uint64(res->res_dir.FDConnectTimeout, ed1),
         edit_uint64(res->res_dir.SDConnectTimeout, ed2));
      if (res->res_dir.query_file) {
         sendit(sock, _("   query_file=%s\n"), res->res_dir.query_file);
      }
      if (res->res_dir.messages) {
         sendit(sock, _("  --> "));
         dump_resource(-R_MSGS, (RES *)res->res_dir.messages, sendit, sock);
      }
      break;
   case R_CONSOLE:
      sendit(sock, _("Console: name=%s SSL=%d\n"),
         res->res_con.hdr.name, res->res_con.tls_enable);
      break;
   case R_COUNTER:
      if (res->res_counter.WrapCounter) {
         sendit(sock, _("Counter: name=%s min=%d max=%d cur=%d wrapcntr=%s\n"),
            res->res_counter.hdr.name, res->res_counter.MinValue,
            res->res_counter.MaxValue, res->res_counter.CurrentValue,
            res->res_counter.WrapCounter->hdr.name);
      } else {
         sendit(sock, _("Counter: name=%s min=%d max=%d\n"),
            res->res_counter.hdr.name, res->res_counter.MinValue,
            res->res_counter.MaxValue);
      }
      if (res->res_counter.Catalog) {
         sendit(sock, _("  --> "));
         dump_resource(-R_CATALOG, (RES *)res->res_counter.Catalog, sendit, sock);
      }
      break;

   case R_CLIENT:
      if (!acl_access_ok(ua, Client_ACL, res->res_client.name())) {
         break;
      }
      buf = get_pool_memory(PM_FNAME);
      sendit(sock, _("Client: Name=%s Enabled=%d Address=%s FDport=%d MaxJobs=%u NumJobs=%u\n"),
         res->res_client.name(), res->res_client.is_enabled(),
         res->res_client.address(buf), res->res_client.FDport,
         res->res_client.MaxConcurrentJobs, res->res_client.getNumConcurrentJobs());
      free_pool_memory(buf);
      sendit(sock, _("      JobRetention=%s FileRetention=%s AutoPrune=%d\n"),
         edit_utime(res->res_client.JobRetention, ed1, sizeof(ed1)),
         edit_utime(res->res_client.FileRetention, ed2, sizeof(ed2)),
         res->res_client.AutoPrune);
      if (res->res_client.fd_storage_address) {
         sendit(sock, "      FDStorageAddress=%s\n", res->res_client.fd_storage_address);
      }
      if (res->res_client.max_bandwidth) {
         sendit(sock, _("     MaximumBandwidth=%lld\n"),
                res->res_client.max_bandwidth);
      }
      if (res->res_client.catalog) {
         sendit(sock, _("  --> "));
         dump_resource(-R_CATALOG, (RES *)res->res_client.catalog, sendit, sock);
      }
      break;

   case R_DEVICE:
      dev = &res->res_dev;
      char ed1[50];
      sendit(sock, _("Device: name=%s ok=%d num_writers=%d max_writers=%d\n"
"      reserved=%d open=%d append=%d read=%d labeled=%d offline=%d autochgr=%d\n"
"      poolid=%s volname=%s MediaType=%s\n"),
         dev->hdr.name, dev->found, dev->num_writers, dev->max_writers,
         dev->reserved, dev->open, dev->append, dev->read, dev->labeled,
         dev->offline, dev->autochanger,
         edit_uint64(dev->PoolId, ed1),
         dev->VolumeName, dev->MediaType);
      break;

   case R_AUTOCHANGER:
   case R_STORAGE:
      if (!acl_access_ok(ua, Storage_ACL, res->res_store.hdr.name)) {
         break;
      }
      sendit(sock, _("%s: name=%s address=%s SDport=%d MaxJobs=%u NumJobs=%u\n"
"      DeviceName=%s MediaType=%s StorageId=%s Autochanger=%d\n"),
         res->res_store.changer == &res->res_store ? "Autochanger" : "Storage",
         res->res_store.hdr.name, res->res_store.address, res->res_store.SDport,
         res->res_store.MaxConcurrentJobs,
         res->res_store.getNumConcurrentJobs(),
         res->res_store.dev_name(),
         res->res_store.media_type,
         edit_int64(res->res_store.StorageId, ed1),
         res->res_store.autochanger);
      if (res->res_store.fd_storage_address) {
         sendit(sock, "      FDStorageAddress=%s\n", res->res_store.fd_storage_address);
      }
      if (res->res_store.ac_group) {
         STORE *shstore = res->res_store.shared_storage;
         sendit(sock, "      AC group=%s ShareStore=%s\n", res->res_store.ac_group,
            shstore?shstore->name():"*none*");
      }
      if (res->res_store.changer && res->res_store.changer != &res->res_store) {
         sendit(sock, _("   Parent --> "));
         dump_resource(-R_STORAGE, (RES *)res->res_store.changer, sendit, sock);
      }
      break;

   case R_CATALOG:
      if (!acl_access_ok(ua, Catalog_ACL, res->res_cat.hdr.name)) {
         break;
      }
      sendit(sock, _("Catalog: name=%s address=%s DBport=%d db_name=%s\n"
"      db_driver=%s db_user=%s MutliDBConn=%d\n"),
         res->res_cat.hdr.name, NPRT(res->res_cat.db_address),
         res->res_cat.db_port, res->res_cat.db_name,
         NPRT(res->res_cat.db_driver), NPRT(res->res_cat.db_user),
         res->res_cat.mult_db_connections);
      break;

   case R_JOB:
   case R_JOBDEFS:
      if (!acl_access_ok(ua, Job_ACL, res->res_job.hdr.name)) {
         break;
      }
      sendit(sock, _("%s: name=%s JobType=%d level=%s Priority=%d Enabled=%d\n"),
         type == R_JOB ? _("Job") : _("JobDefs"),
         res->res_job.hdr.name, res->res_job.JobType,
         level_to_str(edl, sizeof(edl), res->res_job.JobLevel),
         res->res_job.Priority,
         res->res_job.is_enabled());
      sendit(sock, _("     MaxJobs=%u NumJobs=%u Resched=%d Times=%d Interval=%s Spool=%d WritePartAfterJob=%d\n"),
         res->res_job.MaxConcurrentJobs,
         res->res_job.getNumConcurrentJobs(),
         res->res_job.RescheduleOnError, res->res_job.RescheduleTimes,
         edit_uint64_with_commas(res->res_job.RescheduleInterval, ed1),
         res->res_job.spool_data, res->res_job.write_part_after_job);
      if (res->res_job.spool_size) {
         sendit(sock, _("     SpoolSize=%s\n"),        edit_uint64(res->res_job.spool_size, ed1));
      }
      if (res->res_job.JobType == JT_BACKUP) {
         sendit(sock, _("     Accurate=%d\n"), res->res_job.accurate);
      }
      if (res->res_job.max_bandwidth) {
         sendit(sock, _("     MaximumBandwidth=%lld\n"),
                res->res_job.max_bandwidth);
      }
      if (res->res_job.JobType == JT_MIGRATE || res->res_job.JobType == JT_COPY) {
         sendit(sock, _("     SelectionType=%d\n"), res->res_job.selection_type);
      }
      if (res->res_job.JobType == JT_RESTORE) {
         sendit(sock, _("     PrefixLinks=%d\n"), res->res_job.PrefixLinks);
      }
      if (res->res_job.client) {
         sendit(sock, _("  --> "));
         dump_resource(-R_CLIENT, (RES *)res->res_job.client, sendit, sock);
      }
      if (res->res_job.fileset) {
         sendit(sock, _("  --> "));
         dump_resource(-R_FILESET, (RES *)res->res_job.fileset, sendit, sock);
      }
      if (res->res_job.schedule) {
         sendit(sock, _("  --> "));
         dump_resource(-R_SCHEDULE, (RES *)res->res_job.schedule, sendit, sock);
      }
      if (res->res_job.RestoreClient) {
         sendit(sock, _("  --> RestoreClient=%s\n"), NPRT(res->res_job.RestoreClient));
      }
      if (res->res_job.RestoreWhere && !res->res_job.RegexWhere) {
           sendit(sock, _("  --> Where=%s\n"), NPRT(res->res_job.RestoreWhere));
      }
      if (res->res_job.RegexWhere) {
           sendit(sock, _("  --> RegexWhere=%s\n"), NPRT(res->res_job.RegexWhere));
      }
      if (res->res_job.RestoreBootstrap) {
         sendit(sock, _("  --> Bootstrap=%s\n"), NPRT(res->res_job.RestoreBootstrap));
      }
      if (res->res_job.WriteBootstrap) {
         sendit(sock, _("  --> WriteBootstrap=%s\n"), NPRT(res->res_job.WriteBootstrap));
      }
      if (res->res_job.PluginOptions) {
         sendit(sock, _("  --> PluginOptions=%s\n"), NPRT(res->res_job.PluginOptions));
      }
      if (res->res_job.MaxRunTime) {
         sendit(sock, _("  --> MaxRunTime=%u\n"), res->res_job.MaxRunTime);
      }
      if (res->res_job.MaxWaitTime) {
         sendit(sock, _("  --> MaxWaitTime=%u\n"), res->res_job.MaxWaitTime);
      }
      if (res->res_job.MaxStartDelay) {
         sendit(sock, _("  --> MaxStartDelay=%u\n"), res->res_job.MaxStartDelay);
      }
      if (res->res_job.MaxRunSchedTime) {
         sendit(sock, _("  --> MaxRunSchedTime=%u\n"), res->res_job.MaxRunSchedTime);
      }
      if (res->res_job.storage) {
         STORE *store;
         foreach_alist(store, res->res_job.storage) {
            sendit(sock, _("  --> "));
            dump_resource(-R_STORAGE, (RES *)store, sendit, sock);
         }
      }
      if (res->res_job.base) {
         JOB *job;
         foreach_alist(job, res->res_job.base) {
            sendit(sock, _("  --> Base %s\n"), job->name());
         }
      }
      if (res->res_job.RunScripts) {
        RUNSCRIPT *script;
        foreach_alist(script, res->res_job.RunScripts) {
           sendit(sock, _(" --> RunScript\n"));
           sendit(sock, _("  --> Command=%s\n"), NPRT(script->command));
           sendit(sock, _("  --> Target=%s\n"),  NPRT(script->target));
           sendit(sock, _("  --> RunOnSuccess=%u\n"),  script->on_success);
           sendit(sock, _("  --> RunOnFailure=%u\n"),  script->on_failure);
           sendit(sock, _("  --> FailJobOnError=%u\n"),  script->fail_on_error);
           sendit(sock, _("  --> RunWhen=%u\n"),  script->when);
        }
      }
      if (res->res_job.pool) {
         sendit(sock, _("  --> "));
         dump_resource(-R_POOL, (RES *)res->res_job.pool, sendit, sock);
      }
      if (res->res_job.vfull_pool) {
         sendit(sock, _("  --> VFullBackup"));
         dump_resource(-R_POOL, (RES *)res->res_job.vfull_pool, sendit, sock);
      }
      if (res->res_job.full_pool) {
         sendit(sock, _("  --> FullBackup"));
         dump_resource(-R_POOL, (RES *)res->res_job.full_pool, sendit, sock);
      }
      if (res->res_job.inc_pool) {
         sendit(sock, _("  --> IncrementalBackup"));
         dump_resource(-R_POOL, (RES *)res->res_job.inc_pool, sendit, sock);
      }
      if (res->res_job.diff_pool) {
         sendit(sock, _("  --> DifferentialBackup"));
         dump_resource(-R_POOL, (RES *)res->res_job.diff_pool, sendit, sock);
      }
      if (res->res_job.next_pool) {
         sendit(sock, _("  --> Next")); /* Pool will be added by dump_resource */
         dump_resource(-R_POOL, (RES *)res->res_job.next_pool, sendit, sock);
      }
      if (res->res_job.JobType == JT_VERIFY && res->res_job.verify_job) {
         sendit(sock, _("  --> JobToVerify %s"), (RES *)res->res_job.verify_job->name());
      }
      if (res->res_job.run_cmds) {
         char *runcmd;
         foreach_alist(runcmd, res->res_job.run_cmds) {
            sendit(sock, _("  --> Run=%s\n"), runcmd);
         }
      }
      if (res->res_job.selection_pattern) {
         sendit(sock, _("  --> SelectionPattern=%s\n"), NPRT(res->res_job.selection_pattern));
      }
      if (res->res_job.messages) {
         sendit(sock, _("  --> "));
         dump_resource(-R_MSGS, (RES *)res->res_job.messages, sendit, sock);
      }
      break;

   case R_FILESET:
   {
      int i, j, k;
      if (!acl_access_ok(ua, FileSet_ACL, res->res_fs.hdr.name)) {
         break;
      }
      sendit(sock, _("FileSet: name=%s IgnoreFileSetChanges=%d\n"), res->res_fs.hdr.name, res->res_fs.ignore_fs_changes);
      for (i=0; i<res->res_fs.num_includes; i++) {
         INCEXE *incexe = res->res_fs.include_items[i];
         for (j=0; j<incexe->num_opts; j++) {
            FOPTS *fo = incexe->opts_list[j];
            sendit(sock, "      O %s\n", fo->opts);

            bool enhanced_wild = false;
            for (k=0; fo->opts[k]!='\0'; k++) {
               if (fo->opts[k]=='W') {
                  enhanced_wild = true;
                  break;
               }
            }

            for (k=0; k<fo->regex.size(); k++) {
               sendit(sock, "      R %s\n", fo->regex.get(k));
            }
            for (k=0; k<fo->regexdir.size(); k++) {
               sendit(sock, "      RD %s\n", fo->regexdir.get(k));
            }
            for (k=0; k<fo->regexfile.size(); k++) {
               sendit(sock, "      RF %s\n", fo->regexfile.get(k));
            }
            for (k=0; k<fo->wild.size(); k++) {
               sendit(sock, "      W %s\n", fo->wild.get(k));
            }
            for (k=0; k<fo->wilddir.size(); k++) {
               sendit(sock, "      WD %s\n", fo->wilddir.get(k));
            }
            for (k=0; k<fo->wildfile.size(); k++) {
               sendit(sock, "      WF %s\n", fo->wildfile.get(k));
            }
            for (k=0; k<fo->wildbase.size(); k++) {
               sendit(sock, "      W%c %s\n", enhanced_wild ? 'B' : 'F', fo->wildbase.get(k));
            }
            for (k=0; k<fo->base.size(); k++) {
               sendit(sock, "      B %s\n", fo->base.get(k));
            }
            for (k=0; k<fo->fstype.size(); k++) {
               sendit(sock, "      X %s\n", fo->fstype.get(k));
            }
            for (k=0; k<fo->drivetype.size(); k++) {
               sendit(sock, "      XD %s\n", fo->drivetype.get(k));
            }
            if (fo->plugin) {
               sendit(sock, "      G %s\n", fo->plugin);
            }
            if (fo->reader) {
               sendit(sock, "      D %s\n", fo->reader);
            }
            if (fo->writer) {
               sendit(sock, "      T %s\n", fo->writer);
            }
            sendit(sock, "      N\n");
         }
         if (incexe->ignoredir) {
            sendit(sock, "      Z %s\n", incexe->ignoredir);
         }
         for (j=0; j<incexe->name_list.size(); j++) {
            sendit(sock, "      I %s\n", incexe->name_list.get(j));
         }
         if (incexe->name_list.size()) {
            sendit(sock, "      N\n");
         }
         for (j=0; j<incexe->plugin_list.size(); j++) {
            sendit(sock, "      P %s\n", incexe->plugin_list.get(j));
         }
         if (incexe->plugin_list.size()) {
            sendit(sock, "      N\n");
         }
      } /* end for over includes */

      for (i=0; i<res->res_fs.num_excludes; i++) {
         INCEXE *incexe = res->res_fs.exclude_items[i];
         for (j=0; j<incexe->name_list.size(); j++) {
            sendit(sock, "      E %s\n", incexe->name_list.get(j));
         }
         if (incexe->name_list.size()) {
            sendit(sock, "      N\n");
         }
      }
      break;
   } /* end case R_FILESET */

   case R_SCHEDULE:
      if (!acl_access_ok(ua, Schedule_ACL, res->res_sch.hdr.name)) {
         break;
      }

      if (res->res_sch.run) {
         int i;
         RUN *run = res->res_sch.run;
         char buf[1000], num[30];
         sendit(sock, _("Schedule: Name=%s Enabled=%d\n"), 
            res->res_sch.hdr.name, res->res_sch.is_enabled());
         if (!run) {
            break;
         }
next_run:
         sendit(sock, _("  --> Run Level=%s\n"),
                level_to_str(edl, sizeof(edl), run->level));
         if (run->MaxRunSchedTime) {
            sendit(sock, _("      MaxRunSchedTime=%u\n"), run->MaxRunSchedTime);
         }
         if (run->Priority) {
            sendit(sock, _("      Priority=%u\n"), run->Priority);
         }
         bstrncpy(buf, _("      hour="), sizeof(buf));
         for (i=0; i<24; i++) {
            if (bit_is_set(i, run->hour)) {
               bsnprintf(num, sizeof(num), "%d ", i);
               bstrncat(buf, num, sizeof(buf));
            }
         }
         bstrncat(buf, "\n", sizeof(buf));
         sendit(sock, buf);
         bstrncpy(buf, _("      mday="), sizeof(buf));
         for (i=0; i<32; i++) {
            if (bit_is_set(i, run->mday)) {
               bsnprintf(num, sizeof(num), "%d ", i);
               bstrncat(buf, num, sizeof(buf));
            }
         }
         bstrncat(buf, "\n", sizeof(buf));
         sendit(sock, buf);
         bstrncpy(buf, _("      month="), sizeof(buf));
         for (i=0; i<12; i++) {
            if (bit_is_set(i, run->month)) {
               bsnprintf(num, sizeof(num), "%d ", i);
               bstrncat(buf, num, sizeof(buf));
            }
         }
         bstrncat(buf, "\n", sizeof(buf));
         sendit(sock, buf);
         bstrncpy(buf, _("      wday="), sizeof(buf));
         for (i=0; i<7; i++) {
            if (bit_is_set(i, run->wday)) {
               bsnprintf(num, sizeof(num), "%d ", i);
               bstrncat(buf, num, sizeof(buf));
            }
         }
         bstrncat(buf, "\n", sizeof(buf));
         sendit(sock, buf);
         bstrncpy(buf, _("      wom="), sizeof(buf));
         for (i=0; i<6; i++) {
            if (bit_is_set(i, run->wom)) {
               bsnprintf(num, sizeof(num), "%d ", i);
               bstrncat(buf, num, sizeof(buf));
            }
         }
         bstrncat(buf, "\n", sizeof(buf));
         sendit(sock, buf);
         bstrncpy(buf, _("      woy="), sizeof(buf));
         for (i=0; i<54; i++) {
            if (bit_is_set(i, run->woy)) {
               bsnprintf(num, sizeof(num), "%d ", i);
               bstrncat(buf, num, sizeof(buf));
            }
         }
         bstrncat(buf, "\n", sizeof(buf));
         sendit(sock, buf);
         sendit(sock, _("      mins=%d\n"), run->minute);
         if (run->pool) {
            sendit(sock, _("     --> "));
            dump_resource(-R_POOL, (RES *)run->pool, sendit, sock);
         }
         if (run->next_pool) {
            sendit(sock, _("     --> Next")); /* Pool will be added by dump_resource */
            dump_resource(-R_POOL, (RES *)run->next_pool, sendit, sock);
         }
         if (run->storage) {
            sendit(sock, _("     --> "));
            dump_resource(-R_STORAGE, (RES *)run->storage, sendit, sock);
         }
         if (run->msgs) {
            sendit(sock, _("     --> "));
            dump_resource(-R_MSGS, (RES *)run->msgs, sendit, sock);
         }
         /* If another Run record is chained in, go print it */
         if (run->next) {
            run = run->next;
            goto next_run;
         }
      } else {
         sendit(sock, _("Schedule: name=%s\n"), res->res_sch.hdr.name);
      }
      break;

   case R_POOL:
      if (!acl_access_ok(ua, Pool_ACL, res->res_pool.hdr.name)) {
         break;
      }
      sendit(sock, _("Pool: name=%s PoolType=%s\n"), res->res_pool.hdr.name,
              res->res_pool.pool_type);
      sendit(sock, _("      use_cat=%d use_once=%d cat_files=%d\n"),
              res->res_pool.use_catalog, res->res_pool.use_volume_once,
              res->res_pool.catalog_files);
      sendit(sock, _("      max_vols=%d auto_prune=%d VolRetention=%s\n"),
              res->res_pool.max_volumes, res->res_pool.AutoPrune,
              edit_utime(res->res_pool.VolRetention, ed1, sizeof(ed1)));
      sendit(sock, _("      VolUse=%s recycle=%d LabelFormat=%s\n"),
              edit_utime(res->res_pool.VolUseDuration, ed1, sizeof(ed1)),
              res->res_pool.Recycle,
              NPRT(res->res_pool.label_format));
      sendit(sock, _("      CleaningPrefix=%s LabelType=%d\n"),
              NPRT(res->res_pool.cleaning_prefix), res->res_pool.LabelType);
      sendit(sock, _("      RecyleOldest=%d PurgeOldest=%d ActionOnPurge=%d\n"),
              res->res_pool.recycle_oldest_volume,
              res->res_pool.purge_oldest_volume,
              res->res_pool.action_on_purge);
      sendit(sock, _("      MaxVolJobs=%d MaxVolFiles=%d MaxVolBytes=%s\n"),
              res->res_pool.MaxVolJobs,
              res->res_pool.MaxVolFiles,
              edit_uint64(res->res_pool.MaxVolBytes, ed1));
      sendit(sock, _("      MigTime=%s MigHiBytes=%s MigLoBytes=%s\n"),
              edit_utime(res->res_pool.MigrationTime, ed1, sizeof(ed1)),
              edit_uint64(res->res_pool.MigrationHighBytes, ed2),
              edit_uint64(res->res_pool.MigrationLowBytes, ed3));
      sendit(sock, _("      CacheRetention=%s\n"),
             edit_utime(res->res_pool.CacheRetention, ed1, sizeof(ed1)));
      sendit(sock, _("      JobRetention=%s FileRetention=%s\n"),
         edit_utime(res->res_pool.JobRetention, ed1, sizeof(ed1)),
         edit_utime(res->res_pool.FileRetention, ed2, sizeof(ed2)));
      if (res->res_pool.NextPool) {
         sendit(sock, _("      NextPool=%s\n"), res->res_pool.NextPool->name());
      }
      if (res->res_pool.RecyclePool) {
         sendit(sock, _("      RecyclePool=%s\n"), res->res_pool.RecyclePool->name());
      }
      if (res->res_pool.ScratchPool) {
         sendit(sock, _("      ScratchPool=%s\n"), res->res_pool.ScratchPool->name());
      }
      if (res->res_pool.catalog) {
         sendit(sock, _("      Catalog=%s\n"), res->res_pool.catalog->name());
      }
      if (res->res_pool.storage) {
         STORE *store;
         foreach_alist(store, res->res_pool.storage) {
            sendit(sock, _("  --> "));
            dump_resource(-R_STORAGE, (RES *)store, sendit, sock);
         }
      }
      if (res->res_pool.CopyPool) {
         POOL *copy;
         foreach_alist(copy, res->res_pool.CopyPool) {
            sendit(sock, _("  --> "));
            dump_resource(-R_POOL, (RES *)copy, sendit, sock);
         }
      }

      break;

   case R_MSGS:
      sendit(sock, _("Messages: name=%s\n"), res->res_msgs.hdr.name);
      if (res->res_msgs.mail_cmd)
         sendit(sock, _("      mailcmd=%s\n"), res->res_msgs.mail_cmd);
      if (res->res_msgs.operator_cmd)
         sendit(sock, _("      opcmd=%s\n"), res->res_msgs.operator_cmd);
      break;

   default:
      sendit(sock, _("Unknown resource type %d in dump_resource.\n"), type);
      break;
   }
   if (recurse) {
      next = GetNextRes(0, (RES *)res);
      if (next) {
         dump_resource(type, next, sendit, sock);
      }
   }
}

/*
 * Free all the members of an INCEXE structure
 */
static void free_incexe(INCEXE *incexe)
{
   incexe->name_list.destroy();
   incexe->plugin_list.destroy();
   for (int i=0; i<incexe->num_opts; i++) {
      FOPTS *fopt = incexe->opts_list[i];
      fopt->regex.destroy();
      fopt->regexdir.destroy();
      fopt->regexfile.destroy();
      fopt->wild.destroy();
      fopt->wilddir.destroy();
      fopt->wildfile.destroy();
      fopt->wildbase.destroy();
      fopt->base.destroy();
      fopt->fstype.destroy();
      fopt->drivetype.destroy();
      if (fopt->plugin) {
         free(fopt->plugin);
      }
      if (fopt->reader) {
         free(fopt->reader);
      }
      if (fopt->writer) {
         free(fopt->writer);
      }
      free(fopt);
   }
   if (incexe->opts_list) {
      free(incexe->opts_list);
   }
   if (incexe->ignoredir) {
      free(incexe->ignoredir);
   }
   free(incexe);
}


/*
 * Free memory of resource -- called when daemon terminates.
 * NB, we don't need to worry about freeing any references
 * to other resources as they will be freed when that
 * resource chain is traversed.  Mainly we worry about freeing
 * allocated strings (names).
 */
void free_resource(RES *rres, int type)
{
   int num;
   URES *res = (URES *)rres;

   if (res == NULL) {
      return;
   }

   Dmsg3(200, "type=%d res=%p name=%s\n", type, res, res->res_dir.hdr.name);
   /* common stuff -- free the resource name and description */
   if (res->res_dir.hdr.name) {
      free(res->res_dir.hdr.name);
   }
   if (res->res_dir.hdr.desc) {
      free(res->res_dir.hdr.desc);
   }

   switch (type) {
   case R_DIRECTOR:
      if (res->res_dir.working_directory) {
         free(res->res_dir.working_directory);
      }
      if (res->res_dir.scripts_directory) {
         free((char *)res->res_dir.scripts_directory);
      }
      if (res->res_dir.plugin_directory) {
         free((char *)res->res_dir.plugin_directory);
      }
      if (res->res_dir.pid_directory) {
         free(res->res_dir.pid_directory);
      }
      if (res->res_dir.subsys_directory) {
         free(res->res_dir.subsys_directory);
      }
      if (res->res_dir.password) {
         free(res->res_dir.password);
      }
      if (res->res_dir.query_file) {
         free(res->res_dir.query_file);
      }
      if (res->res_dir.DIRaddrs) {
         free_addresses(res->res_dir.DIRaddrs);
      }
      if (res->res_dir.DIRsrc_addr) {
         free_addresses(res->res_dir.DIRsrc_addr);
      }
      if (res->res_dir.tls_ctx) {
         free_tls_context(res->res_dir.tls_ctx);
      }
      if (res->res_dir.tls_ca_certfile) {
         free(res->res_dir.tls_ca_certfile);
      }
      if (res->res_dir.tls_ca_certdir) {
         free(res->res_dir.tls_ca_certdir);
      }
      if (res->res_dir.tls_certfile) {
         free(res->res_dir.tls_certfile);
      }
      if (res->res_dir.tls_keyfile) {
         free(res->res_dir.tls_keyfile);
      }
      if (res->res_dir.tls_dhfile) {
         free(res->res_dir.tls_dhfile);
      }
      if (res->res_dir.tls_allowed_cns) {
         delete res->res_dir.tls_allowed_cns;
      }
      if (res->res_dir.verid) {
         free(res->res_dir.verid);
      }
      break;
   case R_DEVICE:
   case R_COUNTER:
       break;
   case R_CONSOLE:
      if (res->res_con.password) {
         free(res->res_con.password);
      }
      if (res->res_con.tls_ctx) {
         free_tls_context(res->res_con.tls_ctx);
      }
      if (res->res_con.tls_ca_certfile) {
         free(res->res_con.tls_ca_certfile);
      }
      if (res->res_con.tls_ca_certdir) {
         free(res->res_con.tls_ca_certdir);
      }
      if (res->res_con.tls_certfile) {
         free(res->res_con.tls_certfile);
      }
      if (res->res_con.tls_keyfile) {
         free(res->res_con.tls_keyfile);
      }
      if (res->res_con.tls_dhfile) {
         free(res->res_con.tls_dhfile);
      }
      if (res->res_con.tls_allowed_cns) {
         delete res->res_con.tls_allowed_cns;
      }
      for (int i=0; i<Num_ACL; i++) {
         if (res->res_con.ACL_lists[i]) {
            delete res->res_con.ACL_lists[i];
            res->res_con.ACL_lists[i] = NULL;
         }
      }
      break;
   case R_CLIENT:
      if (res->res_client.client_address) {
         free(res->res_client.client_address);
      }
      if (res->res_client.fd_storage_address) {
         free(res->res_client.fd_storage_address);
      }
      if (res->res_client.password) {
         free(res->res_client.password);
      }
      if (res->res_client.tls_ctx) {
         free_tls_context(res->res_client.tls_ctx);
      }
      if (res->res_client.tls_ca_certfile) {
         free(res->res_client.tls_ca_certfile);
      }
      if (res->res_client.tls_ca_certdir) {
         free(res->res_client.tls_ca_certdir);
      }
      if (res->res_client.tls_certfile) {
         free(res->res_client.tls_certfile);
      }
      if (res->res_client.tls_keyfile) {
         free(res->res_client.tls_keyfile);
      }
      if (res->res_client.tls_allowed_cns) {
         delete res->res_client.tls_allowed_cns;
      }
      break;
   case R_AUTOCHANGER:
   case R_STORAGE:
      if (res->res_store.address) {
         free(res->res_store.address);
      }
      if (res->res_store.fd_storage_address) {
         free(res->res_store.fd_storage_address);
      }
      if (res->res_store.password) {
         free(res->res_store.password);
      }
      if (res->res_store.media_type) {
         free(res->res_store.media_type);
      }
      if (res->res_store.ac_group) {
         free_pool_memory(res->res_store.ac_group);
      }
      if (res->res_store.device) {
         delete res->res_store.device;
      }
      if (res->res_store.tls_ctx) {
         free_tls_context(res->res_store.tls_ctx);
      }
      if (res->res_store.tls_ca_certfile) {
         free(res->res_store.tls_ca_certfile);
      }
      if (res->res_store.tls_ca_certdir) {
         free(res->res_store.tls_ca_certdir);
      }
      if (res->res_store.tls_certfile) {
         free(res->res_store.tls_certfile);
      }
      if (res->res_store.tls_keyfile) {
         free(res->res_store.tls_keyfile);
      }
      break;
   case R_CATALOG:
      if (res->res_cat.db_address) {
         free(res->res_cat.db_address);
      }
      if (res->res_cat.db_socket) {
         free(res->res_cat.db_socket);
      }
      if (res->res_cat.db_user) {
         free(res->res_cat.db_user);
      }
      if (res->res_cat.db_name) {
         free(res->res_cat.db_name);
      }
      if (res->res_cat.db_driver) {
         free(res->res_cat.db_driver);
      }
      if (res->res_cat.db_password) {
         free(res->res_cat.db_password);
      }
      if (res->res_cat.db_ssl_mode) {
         free(res->res_cat.db_ssl_mode);
      }
      if (res->res_cat.db_ssl_key) {
         free(res->res_cat.db_ssl_key);
      }
      if (res->res_cat.db_ssl_cert) {
         free(res->res_cat.db_ssl_cert);
      }
      if (res->res_cat.db_ssl_ca) {
         free(res->res_cat.db_ssl_ca);
      }
      if (res->res_cat.db_ssl_capath) {
         free(res->res_cat.db_ssl_capath);
      }
      if (res->res_cat.db_ssl_cipher) {
         free(res->res_cat.db_ssl_cipher);
      }
      break;
   case R_FILESET:
      if ((num=res->res_fs.num_includes)) {
         while (--num >= 0) {
            free_incexe(res->res_fs.include_items[num]);
         }
         free(res->res_fs.include_items);
      }
      res->res_fs.num_includes = 0;
      if ((num=res->res_fs.num_excludes)) {
         while (--num >= 0) {
            free_incexe(res->res_fs.exclude_items[num]);
         }
         free(res->res_fs.exclude_items);
      }
      res->res_fs.num_excludes = 0;
      break;
   case R_POOL:
      if (res->res_pool.pool_type) {
         free(res->res_pool.pool_type);
      }
      if (res->res_pool.label_format) {
         free(res->res_pool.label_format);
      }
      if (res->res_pool.cleaning_prefix) {
         free(res->res_pool.cleaning_prefix);
      }
      if (res->res_pool.storage) {
         delete res->res_pool.storage;
      }
      break;
   case R_SCHEDULE:
      if (res->res_sch.run) {
         RUN *nrun, *next;
         nrun = res->res_sch.run;
         while (nrun) {
            next = nrun->next;
            free(nrun);
            nrun = next;
         }
      }
      break;
   case R_JOB:
   case R_JOBDEFS:
      if (res->res_job.RestoreWhere) {
         free(res->res_job.RestoreWhere);
      }
      if (res->res_job.RegexWhere) {
         free(res->res_job.RegexWhere);
      }
      if (res->res_job.strip_prefix) {
         free(res->res_job.strip_prefix);
      }
      if (res->res_job.add_prefix) {
         free(res->res_job.add_prefix);
      }
      if (res->res_job.add_suffix) {
         free(res->res_job.add_suffix);
      }
      if (res->res_job.RestoreBootstrap) {
         free(res->res_job.RestoreBootstrap);
      }
      if (res->res_job.RestoreClient) {
         free(res->res_job.RestoreClient);
      }
      if (res->res_job.WriteBootstrap) {
         free(res->res_job.WriteBootstrap);
      }
      if (res->res_job.PluginOptions) {
         free(res->res_job.PluginOptions);
      }
      if (res->res_job.selection_pattern) {
         free(res->res_job.selection_pattern);
      }
      if (res->res_job.run_cmds) {
         delete res->res_job.run_cmds;
      }
      if (res->res_job.storage) {
         delete res->res_job.storage;
      }
      if (res->res_job.base) {
         delete res->res_job.base;
      }
      if (res->res_job.RunScripts) {
         free_runscripts(res->res_job.RunScripts);
         delete res->res_job.RunScripts;
      }
      break;
   case R_MSGS:
      if (res->res_msgs.mail_cmd) {
         free(res->res_msgs.mail_cmd);
      }
      if (res->res_msgs.operator_cmd) {
         free(res->res_msgs.operator_cmd);
      }
      free_msgs_res((MSGS *)res);  /* free message resource */
      res = NULL;
      break;
   default:
      printf(_("Unknown resource type %d in free_resource.\n"), type);
   }
   /* Common stuff again -- free the resource, recurse to next one */
   if (res) {
      free(res);
   }
}

/*
 * Save the new resource by chaining it into the head list for
 * the resource. If this is pass 2, we update any resource
 * pointers because they may not have been defined until
 * later in pass 1.
 */
bool save_resource(CONFIG *config, int type, RES_ITEM *items, int pass)
{
   URES *res;
   int rindex = type - r_first;
   int i, size = 0;
   bool error = false;

   /* Check Job requirements after applying JobDefs */
   if (type != R_JOB && type != R_JOBDEFS) {
      /*
       * Ensure that all required items are present
       */
      for (i=0; items[i].name; i++) {
         if (items[i].flags & ITEM_REQUIRED) {
            if (!bit_is_set(i, res_all.res_dir.hdr.item_present)) {
               Mmsg(config->m_errmsg, _("\"%s\" directive is required in \"%s\" resource, but not found.\n"),
                    items[i].name, resources[rindex].name);
               return false;
            }
         }
         /* If this triggers, take a look at lib/parse_conf.h */
         if (i >= MAX_RES_ITEMS) {
            Mmsg(config->m_errmsg, _("Too many directives in \"%s\" resource\n"), resources[rindex].name);
            return false;
         }
      }
   } else if (type == R_JOB) {
      /*
       * Ensure that the name item is present
       */
      if (items[0].flags & ITEM_REQUIRED) {
         if (!bit_is_set(0, res_all.res_dir.hdr.item_present)) {
            Mmsg(config->m_errmsg, _("\"%s\" directive is required in \"%s\" resource, but not found.\n"),
                 items[0].name, resources[rindex].name);
            return false;
         }
      }
   }

   /*
    * During pass 2 in each "store" routine, we looked up pointers
    * to all the resources referrenced in the current resource, now we
    * must copy their addresses from the static record to the allocated
    * record.
    */
   if (pass == 2) {
      switch (type) {
      /* Resources not containing a resource */
      case R_CATALOG:
      case R_MSGS:
      case R_FILESET:
      case R_DEVICE:
         break;

      /*
       * Resources containing another resource or alist. First
       *  look up the resource which contains another resource. It
       *  was written during pass 1.  Then stuff in the pointers to
       *  the resources it contains, which were inserted this pass.
       *  Finally, it will all be stored back.
       */
      case R_POOL:
         /* Find resource saved in pass 1 */
         if ((res = (URES *)GetResWithName(R_POOL, res_all.res_con.hdr.name)) == NULL) {
            Mmsg(config->m_errmsg, _("Cannot find Pool resource %s\n"), res_all.res_con.hdr.name);
            return false;
         }
         /* Explicitly copy resource pointers from this pass (res_all) */
         res->res_pool.NextPool = res_all.res_pool.NextPool;
         res->res_pool.RecyclePool = res_all.res_pool.RecyclePool;
         res->res_pool.ScratchPool = res_all.res_pool.ScratchPool;
         res->res_pool.storage    = res_all.res_pool.storage;
         res->res_pool.catalog    = res_all.res_pool.catalog;
         break;
      case R_CONSOLE:
         if ((res = (URES *)GetResWithName(R_CONSOLE, res_all.res_con.hdr.name)) == NULL) {
            Mmsg(config->m_errmsg, _("Cannot find Console resource %s\n"), res_all.res_con.hdr.name);
            return false;
         }
         res->res_con.tls_allowed_cns = res_all.res_con.tls_allowed_cns;
         break;
      case R_DIRECTOR:
         if ((res = (URES *)GetResWithName(R_DIRECTOR, res_all.res_dir.hdr.name)) == NULL) {
            Mmsg(config->m_errmsg, _("Cannot find Director resource %s\n"), res_all.res_dir.hdr.name);
            return false;
         }
         res->res_dir.messages = res_all.res_dir.messages;
         res->res_dir.tls_allowed_cns = res_all.res_dir.tls_allowed_cns;
         break;
      case R_AUTOCHANGER:          /* alias for R_STORAGE */
      case R_STORAGE:
         type = R_STORAGE;         /* force Storage type */
         if ((res = (URES *)GetResWithName(type, res_all.res_store.hdr.name)) == NULL) {
            Mmsg(config->m_errmsg, _("Cannot find Storage resource %s\n"),
                 res_all.res_dir.hdr.name);
            return false;
         }
         /* we must explicitly copy the device alist pointer */
         res->res_store.device   = res_all.res_store.device;
         res->res_store.changer = res_all.res_store.changer;
         res->res_store.shared_storage = res_all.res_store.shared_storage;
         res->res_store.autochanger = res_all.res_store.autochanger;
         /* The resource name is Autochanger instead of Storage
          * so we force the Autochanger attributes 
          */
         if (strcasecmp(resources[rindex].name, "autochanger") == 0) {
            /* The Autochanger resource might be already defined */
            res->res_store.changer = (res->res_store.changer == NULL)? &res->res_store : res->res_store.changer;
            res->res_store.autochanger = true;
         }
         break;
      case R_JOB:
      case R_JOBDEFS:
         if ((res = (URES *)GetResWithName(type, res_all.res_dir.hdr.name)) == NULL) {
            Mmsg(config->m_errmsg, _("Cannot find Job resource %s\n"),
                 res_all.res_dir.hdr.name);
            return false;
         }
         res->res_job.messages   = res_all.res_job.messages;
         res->res_job.schedule   = res_all.res_job.schedule;
         res->res_job.client     = res_all.res_job.client;
         res->res_job.fileset    = res_all.res_job.fileset;
         res->res_job.storage    = res_all.res_job.storage;
         res->res_job.base       = res_all.res_job.base;
         res->res_job.pool       = res_all.res_job.pool;
         res->res_job.next_pool  = res_all.res_job.next_pool;
         res->res_job.full_pool  = res_all.res_job.full_pool;
         res->res_job.vfull_pool = res_all.res_job.vfull_pool;
         res->res_job.inc_pool   = res_all.res_job.inc_pool;
         res->res_job.diff_pool  = res_all.res_job.diff_pool;
         res->res_job.verify_job = res_all.res_job.verify_job;
         res->res_job.jobdefs    = res_all.res_job.jobdefs;
         res->res_job.run_cmds   = res_all.res_job.run_cmds;
         res->res_job.RunScripts = res_all.res_job.RunScripts;

         /* TODO: JobDefs where/regexwhere doesn't work well (but this
          * is not very useful)
          * We have to set_bit(index, res_all.hdr.item_present);
          * or something like that
          */

         /* we take RegexWhere before all other options */
         if (!res->res_job.RegexWhere
             &&
             (res->res_job.strip_prefix ||
              res->res_job.add_suffix   ||
              res->res_job.add_prefix))
         {
            int len = bregexp_get_build_where_size(res->res_job.strip_prefix,
                                                   res->res_job.add_prefix,
                                                   res->res_job.add_suffix);
            res->res_job.RegexWhere = (char *) bmalloc (len * sizeof(char));
            bregexp_build_where(res->res_job.RegexWhere, len,
                                res->res_job.strip_prefix,
                                res->res_job.add_prefix,
                                res->res_job.add_suffix);
            /* TODO: test bregexp */
         }

         if (res->res_job.RegexWhere && res->res_job.RestoreWhere) {
            free(res->res_job.RestoreWhere);
            res->res_job.RestoreWhere = NULL;
         }

         break;
      case R_COUNTER:
         if ((res = (URES *)GetResWithName(R_COUNTER, res_all.res_counter.hdr.name)) == NULL) {
            Mmsg(config->m_errmsg, _("Cannot find Counter resource %s\n"), res_all.res_counter.hdr.name);
            return false;
         }
         res->res_counter.Catalog = res_all.res_counter.Catalog;
         res->res_counter.WrapCounter = res_all.res_counter.WrapCounter;
         break;

      case R_CLIENT:
         if ((res = (URES *)GetResWithName(R_CLIENT, res_all.res_client.name())) == NULL) {
            Mmsg(config->m_errmsg, _("Cannot find Client resource %s\n"), res_all.res_client.name());
            return false;
         }
         res->res_client.catalog = res_all.res_client.catalog;
         res->res_client.tls_allowed_cns = res_all.res_client.tls_allowed_cns;
         break;
      case R_SCHEDULE:
         /*
          * Schedule is a bit different in that it contains a RUN record
          * chain which isn't a "named" resource. This chain was linked
          * in by run_conf.c during pass 2, so here we jam the pointer
          * into the Schedule resource.
          */
         if ((res = (URES *)GetResWithName(R_SCHEDULE, res_all.res_client.name())) == NULL) {
            Mmsg(config->m_errmsg, _("Cannot find Schedule resource %s\n"), res_all.res_client.name());
            return false;
         }
         res->res_sch.run = res_all.res_sch.run;
         break;
      default:
         Emsg1(M_ERROR, 0, _("Unknown resource type %d in save_resource.\n"), type);
         error = true;
         break;
      }
      /* Note, the resource name was already saved during pass 1,
       * so here, we can just release it.
       */
      if (res_all.res_dir.hdr.name) {
         free(res_all.res_dir.hdr.name);
         res_all.res_dir.hdr.name = NULL;
      }
      if (res_all.res_dir.hdr.desc) {
         free(res_all.res_dir.hdr.desc);
         res_all.res_dir.hdr.desc = NULL;
      }
      return true;
   }

   /* R_AUTOCHANGER is alias so turn it into an R_STORAGE */
   if (type == R_AUTOCHANGER) {
      type = R_STORAGE;
      rindex = type - r_first;
   }

   /*
    * The following code is only executed during pass 1
    */
   switch (type) {
   case R_DIRECTOR:
      size = sizeof(DIRRES);
      break;
   case R_CONSOLE:
      size = sizeof(CONRES);
      break;
   case R_CLIENT:
      size =sizeof(CLIENT);
      break;
   case R_STORAGE:
      size = sizeof(STORE);
      break;
   case R_CATALOG:
      size = sizeof(CAT);
      break;
   case R_JOB:
   case R_JOBDEFS:
      size = sizeof(JOB);
      break;
   case R_FILESET:
      size = sizeof(FILESET);
      break;
   case R_SCHEDULE:
      size = sizeof(SCHED);
      break;
   case R_POOL:
      size = sizeof(POOL);
      break;
   case R_MSGS:
      size = sizeof(MSGS);
      break;
   case R_COUNTER:
      size = sizeof(COUNTER);
      break;
   case R_DEVICE:
      error = true;
      break;
   default:
      printf(_("Unknown resource type %d in save_resource.\n"), type);
      error = true;
      break;
   }
   /* Common */
   if (!error) {
      if (!config->insert_res(rindex, size)) {
         return false;
      }
   }
   return true;
}

void store_actiononpurge(LEX *lc, RES_ITEM *item, int index, int pass)
{
   uint32_t *destination = (uint32_t*)item->value;
   lex_get_token(lc, T_NAME);
   if (strcasecmp(lc->str, "truncate") == 0) {
      *destination = (*destination) | ON_PURGE_TRUNCATE;
   } else {
      scan_err2(lc, _("Expected one of: %s, got: %s"), "Truncate", lc->str);
      return;
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}

/*
 * Store an autochanger resource. Used by Autochanger and
 * SharedStorage direcives.
 */
void store_ac_res(LEX *lc, RES_ITEM *item, int index, int pass)
{
   RES *res;
   RES_ITEM *next = item + 1;

   lex_get_token(lc, T_NAME);
   Dmsg1(100, "Got name=%s\n", lc->str);
   /*
    * For backward compatibility, if yes/no, set the next item
    */
   if (strcasecmp(item->name, "autochanger") == 0) {
      if (strcasecmp(lc->str, "yes") == 0 || strcasecmp(lc->str, "true") == 0) {
         *(bool *)(next->value) = true;
         *(item->value) = NULL;
         Dmsg2(100, "Item=%s got value=%s\n", item->name, lc->str);
         scan_to_eol(lc);
         return;
      } else if (strcasecmp(lc->str, "no") == 0 || strcasecmp(lc->str, "false") == 0) {
         *(bool *)(next->value) = false;
         *(item->value) = NULL;
         Dmsg2(100, "Item=%s got value=%s\n", item->name, lc->str);
         scan_to_eol(lc);
         return;
      }
   }
   Dmsg2(100, "Item=%s got value=%s\n", item->name, lc->str);

   if (pass == 2) {
      res = GetResWithName(R_STORAGE, lc->str);
      if (res == NULL) {
         scan_err3(lc, _("Could not find Storage Resource %s referenced on line %d : %s\n"),
            lc->str, lc->line_no, lc->line);
         return;
      }
      if (*(item->value)) {
         scan_err3(lc, _("Attempt to redefine Storage resource \"%s\" referenced on line %d : %s\n"),
            item->name, lc->line_no, lc->line);
         return;
      }
      Dmsg2(100, "Store %s value=%p\n", lc->str, res);
      *(item->value) = (char *)res;
      if (strcasecmp(item->name, "autochanger") == 0) {
         *(bool *)(next->value) = true;
      }
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}


/*
 * Store Device. Note, the resource is created upon the
 *  first reference. The details of the resource are obtained
 *  later from the SD.
 */
void store_device(LEX *lc, RES_ITEM *item, int index, int pass)
{
   int rindex = R_DEVICE - r_first;
   int size = sizeof(DEVICE);

   if (pass == 1) {
      URES *ures;
      RES *res;

      lex_get_token(lc, T_NAME);
      rblist *list = res_head[rindex]->res_list;
      ures = (URES *)malloc(size);
      memset(ures, 0, size);
      ures->res_dev.hdr.name = bstrdup(lc->str);
      res = (RES *)ures;
      if (list->empty()) {
         list->insert(res, res_compare);
         res_head[rindex]->first = res;
         res_head[rindex]->last = res;
      } else {
         RES *item, *prev;
         prev = res_head[rindex]->last;
         item = (RES *)list->insert(res, res_compare);
         if (item == res) {
            prev->res_next = res;
            res_head[rindex]->last = res;
         } else {
            /* res not inserted */
            free(ures->res_dev.hdr.name);
            free(ures);
         }
      }
      scan_to_eol(lc);
      set_bit(index, res_all.hdr.item_present);
   } else {
      store_alist_res(lc, item, index, pass);
   }
}

/*
 * Store Migration/Copy type
 *
 */
void store_migtype(LEX *lc, RES_ITEM *item, int index, int pass)
{
   int i;

   lex_get_token(lc, T_NAME);
   /* Store the type both pass 1 and pass 2 */
   for (i=0; migtypes[i].type_name; i++) {
      if (strcasecmp(lc->str, migtypes[i].type_name) == 0) {
         *(uint32_t *)(item->value) = migtypes[i].job_type;
         i = 0;
         break;
      }
   }
   if (i != 0) {
      scan_err1(lc, _("Expected a Migration Job Type keyword, got: %s"), lc->str);
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}



/*
 * Store JobType (backup, verify, restore)
 *
 */
void store_jobtype(LEX *lc, RES_ITEM *item, int index, int pass)
{
   int i;

   lex_get_token(lc, T_NAME);
   /* Store the type both pass 1 and pass 2 */
   for (i=0; jobtypes[i].type_name; i++) {
      if (strcasecmp(lc->str, jobtypes[i].type_name) == 0) {
         *(uint32_t *)(item->value) = jobtypes[i].job_type;
         i = 0;
         break;
      }
   }
   if (i != 0) {
      scan_err1(lc, _("Expected a Job Type keyword, got: %s"), lc->str);
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}

/*
 * Store Job Level (Full, Incremental, ...)
 *
 */
void store_level(LEX *lc, RES_ITEM *item, int index, int pass)
{
   int i;

   lex_get_token(lc, T_NAME);
   /* Store the level pass 2 so that type is defined */
   for (i=0; joblevels[i].level_name; i++) {
      if (strcasecmp(lc->str, joblevels[i].level_name) == 0) {
         *(uint32_t *)(item->value) = joblevels[i].level;
         i = 0;
         break;
      }
   }
   if (i != 0) {
      scan_err1(lc, _("Expected a Job Level keyword, got: %s"), lc->str);
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}


void store_replace(LEX *lc, RES_ITEM *item, int index, int pass)
{
   int i;
   lex_get_token(lc, T_NAME);
   /* Scan Replacement options */
   for (i=0; ReplaceOptions[i].name; i++) {
      if (strcasecmp(lc->str, ReplaceOptions[i].name) == 0) {
         *(uint32_t *)(item->value) = ReplaceOptions[i].token;
         i = 0;
         break;
      }
   }
   if (i != 0) {
      scan_err1(lc, _("Expected a Restore replacement option, got: %s"), lc->str);
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}

/*
 * Store ACL (access control list)
 *
 */
void store_acl(LEX *lc, RES_ITEM *item, int index, int pass)
{
   int token;

   for (;;) {
      lex_get_token(lc, T_STRING);
      if (pass == 1) {
         if (((alist **)item->value)[item->code] == NULL) {
            ((alist **)item->value)[item->code] = New(alist(10, owned_by_alist));
            Dmsg1(900, "Defined new ACL alist at %d\n", item->code);
         }
         ((alist **)item->value)[item->code]->append(bstrdup(lc->str));
         Dmsg2(900, "Appended to %d %s\n", item->code, lc->str);
      }
      token = lex_get_token(lc, T_ALL);
      if (token == T_COMMA) {
         continue;                    /* get another ACL */
      }
      break;
   }
   set_bit(index, res_all.hdr.item_present);
}

/* We build RunScripts items here */
static RUNSCRIPT res_runscript;

/* Store a runscript->when in a bit field */
static void store_runscript_when(LEX *lc, RES_ITEM *item, int index, int pass)
{
   lex_get_token(lc, T_NAME);

   if (strcasecmp(lc->str, "before") == 0) {
      *(uint32_t *)(item->value) = SCRIPT_Before ;
   } else if (strcasecmp(lc->str, "after") == 0) {
      *(uint32_t *)(item->value) = SCRIPT_After;
   } else if (strcasecmp(lc->str, "aftervss") == 0) {
      *(uint32_t *)(item->value) = SCRIPT_AfterVSS;
   } else if (strcasecmp(lc->str, "aftersnapshot") == 0) {
      *(uint32_t *)(item->value) = SCRIPT_AfterVSS;
   } else if (strcasecmp(lc->str, "always") == 0) {
      *(uint32_t *)(item->value) = SCRIPT_Any;
   } else {
      scan_err2(lc, _("Expect %s, got: %s"), "Before, After, AfterVSS or Always", lc->str);
   }
   scan_to_eol(lc);
}

/* Store a runscript->target
 *
 */
static void store_runscript_target(LEX *lc, RES_ITEM *item, int index, int pass)
{
   lex_get_token(lc, T_STRING);

   if (pass == 2) {
      if (strcmp(lc->str, "%c") == 0) {
         ((RUNSCRIPT*) item->value)->set_target(lc->str);
      } else if (strcasecmp(lc->str, "yes") == 0) {
         ((RUNSCRIPT*) item->value)->set_target("%c");
      } else if (strcasecmp(lc->str, "no") == 0) {
         ((RUNSCRIPT*) item->value)->set_target("");
      } else {
         RES *res = GetResWithName(R_CLIENT, lc->str);
         if (res == NULL) {
            scan_err3(lc, _("Could not find config Resource %s referenced on line %d : %s\n"),
                      lc->str, lc->line_no, lc->line);
         }

         ((RUNSCRIPT*) item->value)->set_target(lc->str);
      }
   }
   scan_to_eol(lc);
}

/*
 * Store a runscript->command as a string and runscript->cmd_type as a pointer
 */
static void store_runscript_cmd(LEX *lc, RES_ITEM *item, int index, int pass)
{
   lex_get_token(lc, T_STRING);

   if (pass == 2) {
      Dmsg2(1, "runscript cmd=%s type=%c\n", lc->str, item->code);
      POOLMEM *c = get_pool_memory(PM_FNAME);
      /* Each runscript command takes 2 entries in commands list */
      pm_strcpy(c, lc->str);
      ((RUNSCRIPT*) item->value)->commands->prepend(c); /* command line */
      ((RUNSCRIPT*) item->value)->commands->prepend((void *)(intptr_t)item->code); /* command type */
   }
   scan_to_eol(lc);
}

static void store_short_runscript(LEX *lc, RES_ITEM *item, int index, int pass)
{
   lex_get_token(lc, T_STRING);
   alist **runscripts = (alist **)(item->value) ;

   if (pass == 2) {
      RUNSCRIPT *script = new_runscript();
      script->set_job_code_callback(job_code_callback_director);

      script->set_command(lc->str);

      /* TODO: remove all script->old_proto with bacula 1.42 */

      if (strcasecmp(item->name, "runbeforejob") == 0) {
         script->when = SCRIPT_Before;
         script->fail_on_error = true;
         script->set_target("");

      } else if (strcasecmp(item->name, "runafterjob") == 0) {
         script->when = SCRIPT_After;
         script->on_success = true;
         script->on_failure = false;
         script->set_target("");

      } else if (strcasecmp(item->name, "clientrunbeforejob") == 0) {
         script->old_proto = true;
         script->when = SCRIPT_Before;
         script->set_target("%c");
         script->fail_on_error = true;

      } else if (strcasecmp(item->name, "clientrunafterjob") == 0) {
         script->old_proto = true;
         script->when = SCRIPT_After;
         script->set_target("%c");
         script->on_success = true;
         script->on_failure = false;

      } else if (strcasecmp(item->name, "consolerunbeforejob") == 0) {
         script->when = SCRIPT_Before;
         script->set_target("");
         script->fail_on_error = true;
         script->set_command(NPRT(script->command), CONSOLE_CMD);

      } else if (strcasecmp(item->name, "consolerunafterjob") == 0) {
         script->when = SCRIPT_After;
         script->set_target("");
         script->on_success = true;
         script->on_failure = false;
         script->set_command(NPRT(script->command), CONSOLE_CMD);

      } else if (strcasecmp(item->name, "runafterfailedjob") == 0) {
         script->when = SCRIPT_After;
         script->on_failure = true;
         script->on_success = false;
         script->set_target("");
      }

      if (*runscripts == NULL) {
        *runscripts = New(alist(10, not_owned_by_alist));
      }

      (*runscripts)->append(script);
      script->debug();
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}

/* Store a bool in a bit field without modifing res_all.hdr
 * We can also add an option to store_bool to skip res_all.hdr
 */
void store_runscript_bool(LEX *lc, RES_ITEM *item, int index, int pass)
{
   lex_get_token(lc, T_NAME);
   if (strcasecmp(lc->str, "yes") == 0 || strcasecmp(lc->str, "true") == 0) {
      *(bool *)(item->value) = true;
   } else if (strcasecmp(lc->str, "no") == 0 || strcasecmp(lc->str, "false") == 0) {
      *(bool *)(item->value) = false;
   } else {
      scan_err2(lc, _("Expect %s, got: %s"), "YES, NO, TRUE, or FALSE", lc->str); /* YES and NO must not be translated */
   }
   scan_to_eol(lc);
}

/*
 * new RunScript items
 *   name     handler     value               code flags default_value
 */
static RES_ITEM runscript_items[] = {
 {"command",        store_runscript_cmd,  {(char **)&res_runscript},     SHELL_CMD, 0, 0},
 {"console",        store_runscript_cmd,  {(char **)&res_runscript},     CONSOLE_CMD, 0, 0},
 {"target",         store_runscript_target,{(char **)&res_runscript},          0,  0, 0},
 {"runsonsuccess",  store_runscript_bool, {(char **)&res_runscript.on_success},0,  0, 0},
 {"runsonfailure",  store_runscript_bool, {(char **)&res_runscript.on_failure},0,  0, 0},
 {"failjobonerror",store_runscript_bool, {(char **)&res_runscript.fail_on_error},0, 0, 0},
 {"abortjobonerror",store_runscript_bool, {(char **)&res_runscript.fail_on_error},0, 0, 0},
 {"runswhen",       store_runscript_when, {(char **)&res_runscript.when},      0,  0, 0},
 {"runsonclient",   store_runscript_target,{(char **)&res_runscript},          0,  0, 0}, /* TODO */
 {NULL, NULL, {0}, 0, 0, 0}
};

/*
 * Store RunScript info
 *
 *  Note, when this routine is called, we are inside a Job
 *  resource.  We treat the RunScript like a sort of
 *  mini-resource within the Job resource.
 */
void store_runscript(LEX *lc, RES_ITEM *item, int index, int pass)
{
   char *c;
   int token, i, t;
   alist **runscripts = (alist **)(item->value) ;

   Dmsg1(200, "store_runscript: begin store_runscript pass=%i\n", pass);

   token = lex_get_token(lc, T_SKIP_EOL);

   if (token != T_BOB) {
      scan_err1(lc, _("Expecting open brace. Got %s"), lc->str);
   }
   /* setting on_success, on_failure, fail_on_error */
   res_runscript.reset_default();

   if (pass == 2) {
      res_runscript.commands = New(alist(10, not_owned_by_alist));
   }

   while ((token = lex_get_token(lc, T_SKIP_EOL)) != T_EOF) {
      if (token == T_EOB) {
        break;
      }
      if (token != T_IDENTIFIER) {
        scan_err1(lc, _("Expecting keyword, got: %s\n"), lc->str);
      }
      for (i=0; runscript_items[i].name; i++) {
        if (strcasecmp(runscript_items[i].name, lc->str) == 0) {
           token = lex_get_token(lc, T_SKIP_EOL);
           if (token != T_EQUALS) {
              scan_err1(lc, _("expected an equals, got: %s"), lc->str);
           }

           /* Call item handler */
           runscript_items[i].handler(lc, &runscript_items[i], i, pass);
           i = -1;
           break;
        }
      }

      if (i >=0) {
        scan_err1(lc, _("Keyword %s not permitted in this resource"), lc->str);
      }
   }

   if (pass == 2) {
      /* run on client by default */
      if (res_runscript.target == NULL) {
         res_runscript.set_target("%c");
      }
      if (*runscripts == NULL) {
         *runscripts = New(alist(10, not_owned_by_alist));
      }
      /*
       * commands list contains 2 values per command
       *  - POOLMEM command string (ex: /bin/true)
       *  - int command type (ex: SHELL_CMD)
       */
      res_runscript.set_job_code_callback(job_code_callback_director);
      while ((c=(char*)res_runscript.commands->pop()) != NULL) {
         t = (intptr_t)res_runscript.commands->pop();
         RUNSCRIPT *script = new_runscript();
         memcpy(script, &res_runscript, sizeof(RUNSCRIPT));
         script->command = c;
         script->cmd_type = t;
         /* target is taken from res_runscript, each runscript object have
          * a copy
          */
         script->target = NULL;
         script->set_target(res_runscript.target);

         (*runscripts)->append(script);
         script->debug();
      }
      delete res_runscript.commands;
      /* setting on_success, on_failure... cleanup target field */
      res_runscript.reset_default(true);
   }

   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}

/* callback function for edit_job_codes */
/* See ../lib/util.c, function edit_job_codes, for more remaining codes */
extern "C" char *job_code_callback_director(JCR *jcr, const char* param, char *buf, int buflen)
{
   static char yes[] = "yes";
   static char no[] = "no";
   static char nothing[] = "";

   if (jcr == NULL) {
      return nothing;
   }
   ASSERTD(buflen < 255, "buflen must be long enough to hold an ip address");
   switch (param[0]) {
      case 'f':
         if (jcr->fileset) {
            return jcr->fileset->name();
         }
         break;
      case 'h':
         if (jcr->client) {
            POOL_MEM tmp;
            jcr->client->address(tmp.addr());
            bstrncpy(buf, tmp.c_str(), buflen);
            return buf;
         }
         break;
      case 'p':
         if (jcr->pool) {
            return jcr->pool->name();
         }
         break;
      case 'w':
         if (jcr->wstore) {
            return jcr->wstore->name();
         }
         break;
      case 'x':
         return jcr->spool_data ? yes : no;
      case 'D':
         return my_name;
      case 'C':
         return jcr->cloned ? yes : no;
      case 'I':
         if (buflen >= 50) {
            if (jcr->wjcr) {
               edit_uint64(jcr->wjcr->JobId, buf);
               return buf;
            } else {
               edit_uint64(0, buf);
               return buf;
            }
         }
   }
   return nothing;
}

bool parse_dir_config(CONFIG *config, const char *configfile, int exit_code)
{
   config->init(configfile, NULL, exit_code, (void *)&res_all, res_all_size,
      r_first, r_last, resources, &res_head);
   return config->parse_config();
}
