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
 *
 *   Bacula Director -- User Agent Prompt and Selection code
 *
 *     Kern Sibbald, October MMI
 *
 */

#include "bacula.h"
#include "dird.h"

/* Imported variables */
extern struct s_jl joblevels[];

int confirm_retention_yesno(UAContext *ua, utime_t ret, const char *msg)
{
   char ed1[100];
   int val;

   /* Look for "yes" in command line */
   if (find_arg(ua, NT_("yes")) != -1) {
      return 1;
   }

   for ( ;; ) {
       ua->info_msg(_("The current %s retention period is: %s\n"),
          msg, edit_utime(ret, ed1, sizeof(ed1)));
       if (!get_cmd(ua, _("Continue? (yes/no): "))) {
          return 0;
       }
       if (is_yesno(ua->cmd, &val)) {
          return val;           /* is 1 for yes, 0 for no */
       }
    }
    return 1;
}

/*
 * Confirm a retention period
 */
int confirm_retention(UAContext *ua, utime_t *ret, const char *msg)
{
   char ed1[100];
   int val;

   /* Look for "yes" in command line */
   if (find_arg(ua, NT_("yes")) != -1) {
      return 1;
   }

   for ( ;; ) {
       ua->info_msg(_("The current %s retention period is: %s\n"),
          msg, edit_utime(*ret, ed1, sizeof(ed1)));

       if (!get_cmd(ua, _("Continue? (yes/mod/no): "))) {
          return 0;
       }
       if (strcasecmp(ua->cmd, _("mod")) == 0) {
          if (!get_cmd(ua, _("Enter new retention period: "))) {
             return 0;
          }
          if (!duration_to_utime(ua->cmd, ret)) {
             ua->error_msg(_("Invalid period.\n"));
             continue;
          }
          continue;
       }
       if (is_yesno(ua->cmd, &val)) {
          return val;           /* is 1 for yes, 0 for no */
       }
    }
    return 1;
}

/*
 * Given a list of keywords, find the first one
 *  that is in the argument list.
 * Returns: -1 if not found
 *          index into list (base 0) on success
 */
int find_arg_keyword(UAContext *ua, const char **list)
{
   for (int i=1; i<ua->argc; i++) {
      for(int j=0; list[j]; j++) {
         if (strcasecmp(list[j], ua->argk[i]) == 0) {
            return j;
         }
      }
   }
   return -1;
}

/*
 * Given one keyword, find the first one that
 *   is in the argument list.
 * Returns: argk index (always gt 0)
 *          -1 if not found
 */
int find_arg(UAContext *ua, const char *keyword)
{
   for (int i=1; i<ua->argc; i++) {
      if (strcasecmp(keyword, ua->argk[i]) == 0) {
         return i;
      }
   }
   return -1;
}

/*
 * Given a single keyword, find it in the argument list, but
 *   it must have a value
 * Returns: -1 if not found or no value
 *           list index (base 0) on success
 */
int find_arg_with_value(UAContext *ua, const char *keyword)
{
   for (int i=1; i<ua->argc; i++) {
      if (strcasecmp(keyword, ua->argk[i]) == 0) {
         if (ua->argv[i]) {
            return i;
         } else {
            return -1;
         }
      }
   }
   return -1;
}

/*
 * Given a list of keywords, prompt the user
 * to choose one.
 *
 * Returns: -1 on failure
 *          index into list (base 0) on success
 */
int do_keyword_prompt(UAContext *ua, const char *msg, const char **list)
{
   int i;
   start_prompt(ua, _("You have the following choices:\n"));
   for (i=0; list[i]; i++) {
      add_prompt(ua, list[i]);
   }
   return do_prompt(ua, "", msg, NULL, 0);
}


/*
 * Select a Storage resource from prompt list
 *  If unique is set storage resources that have the main address are
 *   combined into one (i.e. they are all part of the same)
 *   storage.  Note, not all commands want this.
 */
STORE *select_storage_resource(UAContext *ua, bool unique)
{
   POOL_MEM tmp;
   char name[MAX_NAME_LENGTH];
   STORE *store;

   /* Does user want a full selection? */
   if (unique && find_arg(ua, NT_("select")) > 0) {
      unique = false;
   }
   start_prompt(ua, _("The defined Storage resources are:\n"));
   LockRes();
   foreach_res(store, R_STORAGE) {
      if (store->is_enabled() && acl_access_ok(ua, Storage_ACL, store->name())) {
         if (unique) {
            Mmsg(tmp, "%s:%d", store->address, store->SDport);
            add_prompt(ua, store->name(), tmp.c_str());
         } else {
            add_prompt(ua, store->name());
         }
      }
   }
   UnlockRes();
   if (do_prompt(ua, _("Storage"),  _("Select Storage resource"), name, sizeof(name)) < 0) {
      return NULL;
   }
   store = (STORE *)GetResWithName(R_STORAGE, name);
   return store;
}

/*
 * Select a FileSet resource from prompt list
 */
FILESET *select_fileset_resource(UAContext *ua)
{
   char name[MAX_NAME_LENGTH];
   FILESET *fs;

   start_prompt(ua, _("The defined FileSet resources are:\n"));
   LockRes();
   foreach_res(fs, R_FILESET) {
      if (acl_access_ok(ua, FileSet_ACL, fs->name())) {
         add_prompt(ua, fs->name());
      }
   }
   UnlockRes();
   if (do_prompt(ua, _("FileSet"), _("Select FileSet resource"), name, sizeof(name)) < 0) {
      return NULL;
   }
   fs = (FILESET *)GetResWithName(R_FILESET, name);
   return fs;
}


/*
 * Get a catalog resource from prompt list
 */
CAT *get_catalog_resource(UAContext *ua)
{
   char name[MAX_NAME_LENGTH];
   CAT *catalog = NULL;
   CLIENT *client = NULL;
   int i;

   for (i=1; i<ua->argc; i++) {
      if (strcasecmp(ua->argk[i], NT_("catalog")) == 0 && ua->argv[i]) {
         if (acl_access_ok(ua, Catalog_ACL, ua->argv[i])) {
            catalog = (CAT *)GetResWithName(R_CATALOG, ua->argv[i]);
            break;
         }
      }
      if (strcasecmp(ua->argk[i], NT_("client")) == 0 && ua->argv[i]) {
         if (acl_access_client_ok(ua, ua->argv[i], JT_BACKUP_RESTORE)) {
            client = (CLIENT *)GetResWithName(R_CLIENT, ua->argv[i]);
            break;
         }
      }
   }
   if (!catalog && client) {    /* Try to take the catalog from the client */
      catalog = client->catalog;
   }
   if (ua->gui && !catalog) {
      LockRes();
      catalog = (CAT *)GetNextRes(R_CATALOG, NULL);
      UnlockRes();
      if (!catalog) {
         ua->error_msg(_("Could not find a Catalog resource\n"));
         return NULL;
      } else if (!acl_access_ok(ua, Catalog_ACL, catalog->name())) {
         ua->error_msg(_("You must specify a \"use <catalog-name>\" command before continuing.\n"));
         return NULL;
      }
      return catalog;
   }
   if (!catalog) {
      start_prompt(ua, _("The defined Catalog resources are:\n"));
      LockRes();
      foreach_res(catalog, R_CATALOG) {
         if (acl_access_ok(ua, Catalog_ACL, catalog->name())) {
            add_prompt(ua, catalog->name());
         }
      }
      UnlockRes();
      if (do_prompt(ua, _("Catalog"),  _("Select Catalog resource"), name, sizeof(name)) < 0) {
         return NULL;
      }
      catalog = (CAT *)GetResWithName(R_CATALOG, name);
   }
   return catalog;
}


/*
 * Select a job to enable or disable
 */
JOB *select_enable_disable_job_resource(UAContext *ua, bool enable)
{
   char name[MAX_NAME_LENGTH];
   JOB *job;

   LockRes();
   if (enable) {
      start_prompt(ua, _("The disabled Job resources are:\n"));
   } else {
      start_prompt(ua, _("The enabled Job resources are:\n"));
   }
   foreach_res(job, R_JOB) {
      if (!acl_access_ok(ua, Job_ACL, job->name())) {
         continue;
      }
      if (job->is_enabled() == enable) {   /* Already enabled/disabled? */
         continue;                    /* yes, skip */
      }
      add_prompt(ua, job->name());
   }
   UnlockRes();
   if (do_prompt(ua, _("Job"), _("Select Job resource"), name, sizeof(name)) < 0) {
      return NULL;
   }
   job = (JOB *)GetResWithName(R_JOB, name);
   return job;
}

/*
 * Select a Job resource from prompt list
 */
JOB *select_job_resource(UAContext *ua)
{
   char name[MAX_NAME_LENGTH];
   JOB *job;

   start_prompt(ua, _("The defined Job resources are:\n"));
   LockRes();
   foreach_res(job, R_JOB) {
      if (job->is_enabled() && acl_access_ok(ua, Job_ACL, job->name())) {
         add_prompt(ua, job->name());
      }
   }
   UnlockRes();
   if (do_prompt(ua, _("Job"), _("Select Job resource"), name, sizeof(name)) < 0) {
      return NULL;
   }
   job = (JOB *)GetResWithName(R_JOB, name);
   return job;
}

/*
 * Select a Restore Job resource from argument or prompt
 */
JOB *get_restore_job(UAContext *ua)
{
   JOB *job;
   int i = find_arg_with_value(ua, "restorejob");
   if (i >= 0 && acl_access_ok(ua, Job_ACL, ua->argv[i])) {
      job = (JOB *)GetResWithName(R_JOB, ua->argv[i]);
      if (job && job->JobType == JT_RESTORE) {
         return job;
      }
      ua->error_msg(_("Error: Restore Job resource \"%s\" does not exist.\n"),
                    ua->argv[i]);
   }
   return select_restore_job_resource(ua);
}

/*
 * Select a Restore Job resource from prompt list
 */
JOB *select_restore_job_resource(UAContext *ua)
{
   char name[MAX_NAME_LENGTH];
   JOB *job;

   start_prompt(ua, _("The defined Restore Job resources are:\n"));
   LockRes();
   foreach_res(job, R_JOB) {
      if (job->JobType == JT_RESTORE && job->is_enabled() &&
          acl_access_ok(ua, Job_ACL, job->name())) {
         add_prompt(ua, job->name());
      }
   }
   UnlockRes();
   if (do_prompt(ua, _("Job"), _("Select Restore Job"), name, sizeof(name)) < 0) {
      return NULL;
   }
   job = (JOB *)GetResWithName(R_JOB, name);
   return job;
}

/*
 * Select a client to enable or disable
 */
CLIENT *select_enable_disable_client_resource(UAContext *ua, bool enable)
{
   char name[MAX_NAME_LENGTH];
   CLIENT *client;

   LockRes();
   start_prompt(ua, _("The defined Client resources are:\n"));
   foreach_res(client, R_CLIENT) {
      if (!acl_access_client_ok(ua, client->name(), JT_BACKUP_RESTORE)) {
         continue;
      }
      if (client->is_enabled() == enable) {   /* Already enabled/disabled? */
         continue;                       /* yes, skip */
      }
      add_prompt(ua, client->name());
   }
   UnlockRes();
   if (do_prompt(ua, _("Client"), _("Select Client resource"), name, sizeof(name)) < 0) {
      return NULL;
   }
   client = (CLIENT *)GetResWithName(R_CLIENT, name);
   return client;
}


/*
 * Select a client resource from prompt list
 */
CLIENT *select_client_resource(UAContext *ua, int32_t jobtype)
{
   char name[MAX_NAME_LENGTH];
   CLIENT *client;

   start_prompt(ua, _("The defined Client resources are:\n"));
   LockRes();
   foreach_res(client, R_CLIENT) {
      if (client->is_enabled() && acl_access_client_ok(ua, client->name(), jobtype)) {
         add_prompt(ua, client->name());
      }
   }
   UnlockRes();
   if (do_prompt(ua, _("Client"),  _("Select Client (File daemon) resource"), name, sizeof(name)) < 0) {
      return NULL;
   }
   client = (CLIENT *)GetResWithName(R_CLIENT, name);
   return client;
}

/*
 *  Get client resource, start by looking for
 *   client=<client-name>
 *  if we don't find the keyword, we prompt the user.
 */
CLIENT *get_client_resource(UAContext *ua, int32_t jobtype)
{
   CLIENT *client = NULL;
   int i;

   for (i=1; i<ua->argc; i++) {
      if ((strcasecmp(ua->argk[i], NT_("client")) == 0 ||
           strcasecmp(ua->argk[i], NT_("fd")) == 0) && ua->argv[i]) {
         if (!acl_access_client_ok(ua, ua->argv[i], jobtype)) {
            break;
         }
         client = (CLIENT *)GetResWithName(R_CLIENT, ua->argv[i]);
         if (client) {
            return client;
         }
         ua->error_msg(_("Error: Client resource %s does not exist.\n"), ua->argv[i]);
         break;
      }
   }
   return select_client_resource(ua, jobtype);
}

/*
 * Select a schedule to enable or disable
 */
SCHED *select_enable_disable_schedule_resource(UAContext *ua, bool enable)
{
   char name[MAX_NAME_LENGTH];
   SCHED *sched;

   LockRes();
   start_prompt(ua, _("The defined Schedule resources are:\n"));
   foreach_res(sched, R_SCHEDULE) {
      if (!acl_access_ok(ua, Schedule_ACL, sched->name())) {
         continue;
      }
      if (sched->is_enabled() == enable) {   /* Already enabled/disabled? */
         continue;                      /* yes, skip */
      }
      add_prompt(ua, sched->name());
   }
   UnlockRes();
   if (do_prompt(ua, _("Schedule"), _("Select Schedule resource"), name, sizeof(name)) < 0) {
      return NULL;
   }
   sched = (SCHED *)GetResWithName(R_SCHEDULE, name);
   return sched;
}


/* Scan what the user has entered looking for:
 *
 *  client=<client-name>
 *
 *  if error or not found, put up a list of client DBRs
 *  to choose from.
 *
 *   returns: 0 on error
 *            1 on success and fills in CLIENT_DBR
 */
bool get_client_dbr(UAContext *ua, CLIENT_DBR *cr, int32_t jobtype)
{
   int i;

   if (cr->Name[0]) {                 /* If name already supplied */
      if (db_get_client_record(ua->jcr, ua->db, cr)) {
         return 1;
      }
      ua->error_msg(_("Could not find Client %s: ERR=%s"), cr->Name, db_strerror(ua->db));
   }
   for (i=1; i<ua->argc; i++) {
      if ((strcasecmp(ua->argk[i], NT_("client")) == 0 ||
           strcasecmp(ua->argk[i], NT_("fd")) == 0) && ua->argv[i]) {
         if (!acl_access_client_ok(ua, ua->argv[i], jobtype)) {
            break;
         }
         bstrncpy(cr->Name, ua->argv[i], sizeof(cr->Name));
         if (!db_get_client_record(ua->jcr, ua->db, cr)) {
            ua->error_msg(_("Could not find Client \"%s\": ERR=%s"), ua->argv[i],
                     db_strerror(ua->db));
            cr->ClientId = 0;
            break;
         }
         return 1;
      }
   }
   if (!select_client_dbr(ua, cr, jobtype)) {  /* try once more by proposing a list */
      return 0;
   }
   return 1;
}

/*
 * Select a Client record from the catalog
 *  Returns 1 on success
 *          0 on failure
 */
bool select_client_dbr(UAContext *ua, CLIENT_DBR *cr, int32_t jobtype)
{
   CLIENT_DBR ocr;
   char name[MAX_NAME_LENGTH];
   int num_clients, i;
   uint32_t *ids;


   cr->ClientId = 0;
   if (!db_get_client_ids(ua->jcr, ua->db, &num_clients, &ids)) {
      ua->error_msg(_("Error obtaining client ids. ERR=%s\n"), db_strerror(ua->db));
      return 0;
   }
   if (num_clients <= 0) {
      ua->error_msg(_("No clients defined. You must run a job before using this command.\n"));
      return 0;
   }

   start_prompt(ua, _("Defined Clients:\n"));
   for (i=0; i < num_clients; i++) {
      ocr.ClientId = ids[i];
      if (!db_get_client_record(ua->jcr, ua->db, &ocr) ||
          !acl_access_client_ok(ua, ocr.Name, jobtype)) {
         continue;
      }
      add_prompt(ua, ocr.Name);
   }
   free(ids);
   if (do_prompt(ua, _("Client"),  _("Select the Client"), name, sizeof(name)) < 0) {
      return 0;
   }
   memset(&ocr, 0, sizeof(ocr));
   bstrncpy(ocr.Name, name, sizeof(ocr.Name));

   if (!db_get_client_record(ua->jcr, ua->db, &ocr)) {
      ua->error_msg(_("Could not find Client \"%s\": ERR=%s"), name, db_strerror(ua->db));
      return 0;
   }
   memcpy(cr, &ocr, sizeof(ocr));
   return 1;
}

/* Scan what the user has entered looking for:
 *
 *  argk=<pool-name>
 *
 *  where argk can be : pool, recyclepool, scratchpool, nextpool etc..
 *
 *  if error or not found, put up a list of pool DBRs
 *  to choose from.
 *
 *   returns: false on error
 *            true  on success and fills in POOL_DBR
 */
bool get_pool_dbr(UAContext *ua, POOL_DBR *pr, const char *argk)
{
   if (pr->Name[0]) {                 /* If name already supplied */
      if (db_get_pool_numvols(ua->jcr, ua->db, pr) &&
          acl_access_ok(ua, Pool_ACL, pr->Name)) {
         return true;
      }
      ua->error_msg(_("Could not find Pool \"%s\": ERR=%s"), pr->Name, db_strerror(ua->db));
   }
   if (!select_pool_dbr(ua, pr, argk)) {  /* try once more */
      return false;
   }
   return true;
}

/*
 * Select a Pool record from catalog
 * argk can be pool, recyclepool, scratchpool etc..
 */
bool select_pool_dbr(UAContext *ua, POOL_DBR *pr, const char *argk)
{
   POOL_DBR opr;
   char name[MAX_NAME_LENGTH];
   int num_pools, i;
   uint32_t *ids = NULL;

   for (i=1; i<ua->argc; i++) {
      if (strcasecmp(ua->argk[i], argk) == 0 && ua->argv[i] &&
          acl_access_ok(ua, Pool_ACL, ua->argv[i])) {
         bstrncpy(pr->Name, ua->argv[i], sizeof(pr->Name));
         if (!db_get_pool_numvols(ua->jcr, ua->db, pr)) {
            ua->error_msg(_("Could not find Pool \"%s\": ERR=%s"), ua->argv[i],
                     db_strerror(ua->db));
            pr->PoolId = 0;
            break;
         }
         return true;
      }
   }

   pr->PoolId = 0;
   if (!db_get_pool_ids(ua->jcr, ua->db, &num_pools, &ids)) {
      ua->error_msg(_("Error obtaining pool ids. ERR=%s\n"), db_strerror(ua->db));
      return 0;
   }
   if (num_pools <= 0) {
      ua->error_msg(_("No pools defined. Use the \"create\" command to create one.\n"));
      if (ids) {
         free(ids);
      }
      return false;
   }

   start_prompt(ua, _("Defined Pools:\n"));
   if (bstrcmp(argk, NT_("recyclepool"))) {
      add_prompt(ua, _("*None*"));
   }
   for (i=0; i < num_pools; i++) {
      opr.PoolId = ids[i];
      if (!db_get_pool_numvols(ua->jcr, ua->db, &opr) ||
          !acl_access_ok(ua, Pool_ACL, opr.Name)) {
         continue;
      }
      add_prompt(ua, opr.Name);
   }
   free(ids);
   if (do_prompt(ua, _("Pool"),  _("Select the Pool"), name, sizeof(name)) < 0) {
      return false;
   }

   memset(&opr, 0, sizeof(opr));
   /* *None* is only returned when selecting a recyclepool, and in that case
    * the calling code is only interested in opr.Name, so then we can leave
    * pr as all zero.
    */
   if (!bstrcmp(name, _("*None*"))) {
     bstrncpy(opr.Name, name, sizeof(opr.Name));

     if (!db_get_pool_numvols(ua->jcr, ua->db, &opr)) {
        ua->error_msg(_("Could not find Pool \"%s\": ERR=%s"), name, db_strerror(ua->db));
        return false;
     }
   }

   memcpy(pr, &opr, sizeof(opr));
   return true;
}

/*
 * Select a Pool and a Media (Volume) record from the database
 */
int select_pool_and_media_dbr(UAContext *ua, POOL_DBR *pr, MEDIA_DBR *mr)
{

   if (!select_media_dbr(ua, mr)) {
      return 0;
   }
   memset(pr, 0, sizeof(POOL_DBR));
   pr->PoolId = mr->PoolId;
   if (!db_get_pool_record(ua->jcr, ua->db, pr)) {
      ua->error_msg("%s", db_strerror(ua->db));
      return 0;
   }
   if (!acl_access_ok(ua, Pool_ACL, pr->Name)) {
      ua->error_msg(_("No access to Pool \"%s\"\n"), pr->Name);
      return 0;
   }
   return 1;
}

/* Select a Media (Volume) record from the database */
int select_media_dbr(UAContext *ua, MEDIA_DBR *mr)
{
   int i;
   int ret = 0;
   POOLMEM *err = get_pool_memory(PM_FNAME);
   *err=0;

   mr->clear();
   i = find_arg_with_value(ua, "volume");
   if (i >= 0) {
      if (is_name_valid(ua->argv[i], &err)) {
         bstrncpy(mr->VolumeName, ua->argv[i], sizeof(mr->VolumeName));
      } else {
         goto bail_out;
      }
   }
   if (mr->VolumeName[0] == 0) {
      POOL_DBR pr;
      memset(&pr, 0, sizeof(pr));
      /* Get the pool from pool=<pool-name> */
      if (!get_pool_dbr(ua, &pr)) {
         goto bail_out;
      }
      mr->PoolId = pr.PoolId;
      db_list_media_records(ua->jcr, ua->db, mr, prtit, ua, HORZ_LIST);
      if (!get_cmd(ua, _("Enter a Volume name or *MediaId: "))) {
         goto bail_out;
      }
      if (ua->cmd[0] == '*' && is_a_number(ua->cmd+1)) {
         mr->MediaId = str_to_int64(ua->cmd+1);
      } else if (is_name_valid(ua->cmd, &err)) {
         bstrncpy(mr->VolumeName, ua->cmd, sizeof(mr->VolumeName));
      } else {
         goto bail_out;
      }
   }

   if (!db_get_media_record(ua->jcr, ua->db, mr)) {
      pm_strcpy(err, db_strerror(ua->db));
      goto bail_out;
   }
   ret = 1;

bail_out:
   if (!ret && *err) {
      ua->error_msg("%s", err);
   }
   free_pool_memory(err);
   return ret;
}


/*
 * Select a pool resource from prompt list
 */
POOL *select_pool_resource(UAContext *ua)
{
   char name[MAX_NAME_LENGTH];
   POOL *pool;

   start_prompt(ua, _("The defined Pool resources are:\n"));
   LockRes();
   foreach_res(pool, R_POOL) {
      if (acl_access_ok(ua, Pool_ACL, pool->name())) {
         add_prompt(ua, pool->name());
      }
   }
   UnlockRes();
   if (do_prompt(ua, _("Pool"), _("Select Pool resource"), name, sizeof(name)) < 0) {
      return NULL;
   }
   pool = (POOL *)GetResWithName(R_POOL, name);
   return pool;
}


/*
 *  If you are thinking about using it, you
 *  probably want to use select_pool_dbr()
 *  or get_pool_dbr() above.
 */
POOL *get_pool_resource(UAContext *ua)
{
   POOL *pool = NULL;
   int i;

   i = find_arg_with_value(ua, "pool");
   if (i >= 0 && acl_access_ok(ua, Pool_ACL, ua->argv[i])) {
      pool = (POOL *)GetResWithName(R_POOL, ua->argv[i]);
      if (pool) {
         return pool;
      }
      ua->error_msg(_("Error: Pool resource \"%s\" does not exist.\n"), ua->argv[i]);
   }
   return select_pool_resource(ua);
}

/*
 * List all jobs and ask user to select one
 */
static int select_job_dbr(UAContext *ua, JOB_DBR *jr)
{
   db_list_job_records(ua->jcr, ua->db, jr, prtit, ua, HORZ_LIST);
   if (!get_pint(ua, _("Enter the JobId to select: "))) {
      return 0;
   }
   jr->JobId = ua->int64_val;
   if (!db_get_job_record(ua->jcr, ua->db, jr)) {
      ua->error_msg("%s", db_strerror(ua->db));
      return 0;
   }
   return jr->JobId;

}


/* Scan what the user has entered looking for:
 *
 *  jobid=nn
 *
 *  if error or not found, put up a list of Jobs
 *  to choose from.
 *
 *   returns: 0 on error
 *            JobId on success and fills in JOB_DBR
 */
int get_job_dbr(UAContext *ua, JOB_DBR *jr)
{
   int i;

   for (i=1; i<ua->argc; i++) {
      if (strcasecmp(ua->argk[i], NT_("ujobid")) == 0 && ua->argv[i]) {
         jr->JobId = 0;
         bstrncpy(jr->Job, ua->argv[i], sizeof(jr->Job));
      } else if (strcasecmp(ua->argk[i], NT_("jobid")) == 0 && ua->argv[i]) {
         jr->JobId = str_to_int64(ua->argv[i]);
         jr->Job[0] = 0;
      } else {
         continue;
      }
      if (!db_get_job_record(ua->jcr, ua->db, jr)) {
         ua->error_msg(_("Could not find Job \"%s\": ERR=%s"), ua->argv[i],
                  db_strerror(ua->db));
         jr->JobId = 0;
         break;
      }
      return jr->JobId;
   }

   jr->JobId = 0;
   jr->Job[0] = 0;

   for (i=1; i<ua->argc; i++) {
      if ((strcasecmp(ua->argk[i], NT_("jobname")) == 0 ||
           strcasecmp(ua->argk[i], NT_("job")) == 0) && ua->argv[i]) {
         jr->JobId = 0;
         bstrncpy(jr->Name, ua->argv[i], sizeof(jr->Name));
         break;
      }
   }
   if (!select_job_dbr(ua, jr)) {  /* try once more */
      return 0;
   }
   return jr->JobId;
}

/*
 * Implement unique set of prompts
 */
void start_prompt(UAContext *ua, const char *msg)
{
  if (ua->max_prompts == 0) {
     ua->max_prompts = 10;
     ua->prompt = (char **)bmalloc(sizeof(char *) * ua->max_prompts);
     ua->unique = (char **)bmalloc(sizeof(char *) * ua->max_prompts);
  }
  ua->num_prompts = 1;
  ua->prompt[0] = bstrdup(msg);
  ua->unique[0] = NULL;
}

/*
 * Add to prompts -- keeping them unique by name
 */
void add_prompt(UAContext *ua, const char *prompt, char *unique)
{
   int i;
   if (ua->num_prompts == ua->max_prompts) {
      ua->max_prompts *= 2;
      ua->prompt = (char **)brealloc(ua->prompt, sizeof(char *) *
         ua->max_prompts);
      ua->unique = (char **)brealloc(ua->unique, sizeof(char *) *
         ua->max_prompts);
    }
    for (i=1; i < ua->num_prompts; i++) {
       if (strcmp(ua->prompt[i], prompt) == 0) {
          return;
       } else if (unique && strcmp(ua->unique[i], unique) == 0) {
          return;
       }
    }
    ua->prompt[ua->num_prompts] = bstrdup(prompt);
    if (unique) {
       ua->unique[ua->num_prompts++] = bstrdup(unique);
    } else {
       ua->unique[ua->num_prompts++] = NULL;
    }
}

/*
 * Display prompts and get user's choice
 *
 *  Returns: -1 on error
 *            index base 0 on success, and choice
 *               is copied to prompt if not NULL
 *             prompt is set to the chosen prompt item string
 */
int do_prompt(UAContext *ua, const char *automsg, const char *msg,
              char *prompt, int max_prompt)
{
   int i, item;
   char pmsg[MAXSTRING];
   BSOCK *user = ua->UA_sock;

   if (prompt) {
      *prompt = 0;
   }
   if (ua->num_prompts == 2) {
      item = 1;
      if (prompt) {
         bstrncpy(prompt, ua->prompt[1], max_prompt);
      }
      ua->send_msg(_("Automatically selected %s: %s\n"), NPRTB(automsg), ua->prompt[1]);
      goto done;
   }
   /* If running non-interactive, bail out */
   if (ua->batch) {
      /* First print the choices he wanted to make */
      ua->send_msg(ua->prompt[0]);
      for (i=1; i < ua->num_prompts; i++) {
         ua->send_msg("%6d: %s\n", i, ua->prompt[i]);
      }
      /* Now print error message */
      ua->send_msg(_("Your request has multiple choices for \"%s\". Selection is not possible in batch mode.\n"), automsg);
      item = -1;
      goto done;
   }
   if (ua->api) user->signal(BNET_START_SELECT);
   ua->send_msg(ua->prompt[0]);
   for (i=1; i < ua->num_prompts; i++) {
      if (ua->api) {
         ua->send_msg("%s", ua->prompt[i]);
      } else {
         ua->send_msg("%6d: %s\n", i, ua->prompt[i]);
      }
   }
   if (ua->api) user->signal(BNET_END_SELECT);

   for ( ;; ) {
      /* First item is the prompt string, not the items */
      if (ua->num_prompts == 1) {
         ua->error_msg(_("Selection list for \"%s\" is empty!\n"), automsg);
         item = -1;                    /* list is empty ! */
         break;
      }
      if (ua->num_prompts == 2) {
         item = 1;
         ua->send_msg(_("Automatically selected: %s\n"), ua->prompt[1]);
         if (prompt) {
            bstrncpy(prompt, ua->prompt[1], max_prompt);
         }
         break;
      } else {
         sprintf(pmsg, "%s (1-%d): ", msg, ua->num_prompts-1);
      }
      /* Either a . or an @ will get you out of the loop */
      if (ua->api) user->signal(BNET_SELECT_INPUT);
      if (!get_pint(ua, pmsg)) {
         item = -1;                   /* error */
         ua->info_msg(_("Selection aborted, nothing done.\n"));
         break;
      }
      item = ua->pint32_val;
      if (item < 1 || item >= ua->num_prompts) {
         ua->warning_msg(_("Please enter a number between 1 and %d\n"), ua->num_prompts-1);
         continue;
      }
      if (prompt) {
         bstrncpy(prompt, ua->prompt[item], max_prompt);
      }
      break;
   }

done:
   for (i=0; i < ua->num_prompts; i++) {
      free(ua->prompt[i]);
      if (ua->unique[i]) free(ua->unique[i]);
   }
   ua->num_prompts = 0;
   return item>0 ? item-1 : item;
}

/*
 * Display prompts and get user's choice
 *
 *  Returns: -1 on error
 *            number of items selected and the choices are
 *               copied to selected if not NULL
 *            selected is an alist of the prompts chosen
 *              Note! selected must already be initialized.
 */
int do_alist_prompt(UAContext *ua, const char *automsg, const char *msg,
              alist *selected)
{
   int i, item;
   char pmsg[MAXSTRING];
   BSOCK *user = ua->UA_sock;
   sellist sl;

   /* First item is the prompt string, not the items */
   if (ua->num_prompts == 1) {
      ua->error_msg(_("Selection list for \"%s\" is empty!\n"), automsg);
      item = -1;                    /* list is empty ! */
      goto done;
   }
   if (ua->num_prompts == 2) {
      item = 1;
      selected->append(bstrdup(ua->prompt[1]));
      ua->send_msg(_("Automatically selected %s: %s\n"), automsg, ua->prompt[1]);
      goto done;
   }
   /* If running non-interactive, bail out */
   if (ua->batch) {
      /* First print the choices he wanted to make */
      ua->send_msg(ua->prompt[0]);
      for (i=1; i < ua->num_prompts; i++) {
         ua->send_msg("%6d: %s\n", i, ua->prompt[i]);
      }
      /* Now print error message */
      ua->send_msg(_("Your request has multiple choices for \"%s\". Selection is not possible in batch mode.\n"), automsg);
      item = -1;
      goto done;
   }
   if (ua->api) user->signal(BNET_START_SELECT);
   ua->send_msg(ua->prompt[0]);
   for (i=1; i < ua->num_prompts; i++) {
      if (ua->api) {
         ua->send_msg("%s", ua->prompt[i]);
      } else {
         ua->send_msg("%6d: %s\n", i, ua->prompt[i]);
      }
   }
   if (ua->api) user->signal(BNET_END_SELECT);

   sprintf(pmsg, "%s (1-%d): ", msg, ua->num_prompts-1);

   for ( ;; ) {
      bool ok = true;
      /* Either a . or an @ will get you out of the loop */
      if (ua->api) user->signal(BNET_SELECT_INPUT);

      if (!get_selection_list(ua, sl, pmsg, false)) {
         item = -1;
         break;
      }

      if (sl.is_all()) {
         for (i=1; i < ua->num_prompts; i++) {
            selected->append(bstrdup(ua->prompt[i]));
         }
      } else {
         while ( (item = sl.next()) > 0) {
            if (item < 1 || item >= ua->num_prompts) {
               ua->warning_msg(_("Please enter a number between 1 and %d\n"), ua->num_prompts-1);
               ok = false;
               break;
            }
            selected->append(bstrdup(ua->prompt[item]));
         }
      }
      if (ok) {
         item = selected->size();
         break;
      }
   }

done:
   for (i=0; i < ua->num_prompts; i++) {
      free(ua->prompt[i]);
      if (ua->unique[i]) free(ua->unique[i]);
   }
   ua->num_prompts = 0;
   return item;
}


/*
 * We scan what the user has entered looking for
 *    storage=<storage-resource>
 *    job=<job_name>
 *    jobid=<jobid>
 *    ?              (prompt him with storage list)
 *    <some-error>   (prompt him with storage list)
 *
 * If use_default is set, we assume that any keyword without a value
 *   is the name of the Storage resource wanted.
 */
STORE *get_storage_resource(UAContext *ua, bool use_default, bool unique)
{
   char store_name[MAX_NAME_LENGTH];
   STORE *store = NULL;
   int jobid;
   JCR *jcr;
   int i;
   char ed1[50];
   *store_name = 0;

   for (i=1; i<ua->argc; i++) {
      if (use_default && !ua->argv[i]) {
         /* Ignore slots, scan and barcode(s) keywords */
         if (strcasecmp("scan", ua->argk[i]) == 0 ||
             strcasecmp("barcode", ua->argk[i]) == 0 ||
             strcasecmp("barcodes", ua->argk[i]) == 0 ||
             strcasecmp("slots", ua->argk[i]) == 0) {
            continue;
         }
         /* Default argument is storage (except in enable/disable command) */
         if (store_name[0]) {
            ua->error_msg(_("Storage name given twice.\n"));
            return NULL;
         }
         bstrncpy(store_name, ua->argk[i], sizeof(store_name));
         if (store_name[0] == '?') {
            *store_name = 0;
            break;
         }
      } else {
         if (strcasecmp(ua->argk[i], NT_("storage")) == 0 ||
             strcasecmp(ua->argk[i], NT_("sd")) == 0) {
            bstrncpy(store_name, NPRTB(ua->argv[i]), sizeof(store_name));

         } else if (strcasecmp(ua->argk[i], NT_("jobid")) == 0) {
            jobid = str_to_int64(ua->argv[i]);
            if (jobid <= 0) {
               ua->error_msg(_("Expecting jobid=nn command, got: %s\n"), ua->argk[i]);
               return NULL;
            }
            if (!(jcr=get_jcr_by_id(jobid))) {
               ua->error_msg(_("JobId %s is not running.\n"), edit_int64(jobid, ed1));
               return NULL;
            }
            if (jcr->wstore) {
               bstrncpy(store_name, jcr->wstore->name(), sizeof(store_name));
            }
            free_jcr(jcr);

         } else if (strcasecmp(ua->argk[i], NT_("job")) == 0 ||
                    strcasecmp(ua->argk[i], NT_("jobname")) == 0) {
            if (!ua->argv[i]) {
               ua->error_msg(_("Expecting job=xxx, got: %s.\n"), ua->argk[i]);
               return NULL;
            }
            if (!(jcr=get_jcr_by_partial_name(ua->argv[i]))) {
               ua->error_msg(_("Job \"%s\" is not running.\n"), ua->argv[i]);
               return NULL;
            }
            if (jcr->wstore) {
               bstrncpy(store_name, jcr->wstore->name(), sizeof(store_name));
            }
            free_jcr(jcr);

         } else if (strcasecmp(ua->argk[i], NT_("ujobid")) == 0) {
            if (!ua->argv[i]) {
               ua->error_msg(_("Expecting ujobid=xxx, got: %s.\n"), ua->argk[i]);
               return NULL;
            }
            if ((jcr=get_jcr_by_full_name(ua->argv[i]))) {
               if (jcr->wstore) {
                  bstrncpy(store_name, jcr->wstore->name(), sizeof(store_name));
               }
               free_jcr(jcr);
            }
         }
         if (store_name[0]) {
            break;              /* We can stop the loop if we have something */
         }
      }
   }

   if (store_name[0] != 0) {
      store = (STORE *)GetResWithName(R_STORAGE, store_name);
      if (!store && strcmp(store_name, "storage") != 0) {
         /* Looks that the first keyword of the line was not a storage name, make
          * sure that it's not "storage=" before we print the following message
          */
         ua->error_msg(_("Storage resource \"%s\": not found\n"), store_name);
      }
   }
   if (store && !acl_access_ok(ua, Storage_ACL, store->name())) {
      store = NULL;
   }
   /* No keywords found, so present a selection list */
   if (!store) {
      store = select_storage_resource(ua, unique);
   }
   return store;
}

/* Get drive that we are working with for this storage */
int get_storage_drive(UAContext *ua, STORE *store)
{
   int i, drive = -1;
   /* Get drive for autochanger if possible */
   i = find_arg_with_value(ua, "drive");
   if (i >=0) {
      drive = atoi(ua->argv[i]);
   } else if (store && store->autochanger) {
      /* If our structure is not set ask SD for # drives */
      if (store->drives == 0) {
         store->drives = get_num_drives_from_SD(ua);
      }
      /* If only one drive, default = 0 */
      if (store->drives == 1) {
         drive = 0;
      } else {
         /* Ask user to enter drive number */
         ua->cmd[0] = 0;
         if (!get_cmd(ua, _("Enter autochanger drive[0]: "))) {
            drive = -1;  /* None */
         } else {
            drive = atoi(ua->cmd);
         }
     }
   }
   return drive;
}

/* Get slot that we are working with for this storage */
int get_storage_slot(UAContext *ua, STORE *store)
{
   int i, slot = -1;
   /* Get slot for autochanger if possible */
   i = find_arg_with_value(ua, "slot");
   if (i >=0) {
      slot = atoi(ua->argv[i]);
   } else if (store && store->autochanger) {
      /* Ask user to enter slot number */
      ua->cmd[0] = 0;
      if (!get_cmd(ua, _("Enter autochanger slot: "))) {
         slot = -1;  /* None */
      } else {
         slot = atoi(ua->cmd);
      }
   }
   return slot;
}



/*
 * Scan looking for mediatype=
 *
 *  if not found or error, put up selection list
 *
 *  Returns: 0 on error
 *           1 on success, MediaType is set
 */
int get_media_type(UAContext *ua, char *MediaType, int max_media)
{
   STORE *store;
   int i;

   i = find_arg_with_value(ua, "mediatype");
   if (i >= 0) {
      bstrncpy(MediaType, ua->argv[i], max_media);
      return 1;
   }

   start_prompt(ua, _("Media Types defined in conf file:\n"));
   LockRes();
   foreach_res(store, R_STORAGE) {
      if (store->is_enabled()) {
         add_prompt(ua, store->media_type);
      }
   }
   UnlockRes();
   return (do_prompt(ua, _("Media Type"), _("Select the Media Type"), MediaType, max_media) < 0) ? 0 : 1;
}

int get_level_code_from_name(const char *level_name)
{
   int ret = 0;
   if (!level_name) {
      return ret;
   }
   for (int i=0; joblevels[i].level_name; i++) {
      if (strcasecmp(level_name, joblevels[i].level_name) == 0) {
         ret = joblevels[i].level;
         break;
      }
   }
   return ret;
}

bool get_level_from_name(JCR *jcr, const char *level_name)
{
   int level = get_level_code_from_name(level_name);
   if (level > 0) {
      jcr->setJobLevel(level);
      return true;
   }
   return false;
}

static int count_running_jobs(UAContext *ua)
{
   int tjobs = 0;                  /* total # number jobs */
   int njobs = 0;
   JCR *jcr;
   /* Count Jobs running */
   foreach_jcr(jcr) {
      if (jcr->is_internal_job()) {      /* this is us */
         continue;
      }
      tjobs++;                    /* count of all jobs */
      if (!acl_access_ok(ua, Job_ACL, jcr->job->name())) {
         continue;               /* skip not authorized */
      }
      njobs++;                   /* count of authorized jobs */
   }
   endeach_jcr(jcr);

   if (njobs == 0) {            /* no authorized */
      if (tjobs == 0) {
         ua->send_msg(_("No Jobs running.\n"));
      } else {
         ua->send_msg(_("None of your jobs are running.\n"));
      }
   }
   return njobs;
}


/* Get a list of running jobs
 * "reason" is used in user messages
 * can be: cancel, limit, ...
 *  Returns: -1 on error
 *           nb of JCR on success (should be free_jcr() after)
 */
int select_running_jobs(UAContext *ua, alist *jcrs, const char *reason)
{
   int i;
   JCR *jcr = NULL;
   int njobs = 0;
   char JobName[MAX_NAME_LENGTH];
   char temp[256];
   alist *selected = NULL;

   for (i=1; i<ua->argc; i++) {
      if (strcasecmp(ua->argk[i], NT_("jobid")) == 0) {
         sellist sl;
         int32_t JobId;

         if (!ua->argv[i]) {
            ua->error_msg(_("No value given for \"jobid\".\n"));
            goto bail_out;
         }
         if (!sl.set_string(ua->argv[i], true)) {
            ua->send_msg("%s", sl.get_errmsg());
            goto bail_out;
         }
         foreach_sellist(JobId, &sl) {
            jcr = get_jcr_by_id(JobId);
            if (jcr && jcr->job && acl_access_ok(ua, Job_ACL, jcr->job->name())) {
               jcrs->append(jcr);
            } else if (jcr) {
               ua->error_msg(_("Unauthorized command from this console "
                               "for JobId=%d.\n"), JobId);
               free_jcr(jcr);
            } else {
               ua->warning_msg(_("Warning Job JobId=%d is not running.\n"), JobId);
            }
         }
         if (jcrs->size() == 0) {
            goto bail_out;               /* If we did not find specified jobid, get out */
         }
         break;

      /* TODO: might want to implement filters (client, status, etc...) */
      } else if (strcasecmp(ua->argk[i], NT_("all")) == 0) {
         foreach_jcr(jcr) {
            if (jcr->is_internal_job()) { /* Do not cancel consoles */
               continue;
            }
            if (!acl_access_ok(ua, Job_ACL, jcr->job->name())) {
               continue;               /* skip not authorized */
            }
            jcr->inc_use_count();
            jcrs->append(jcr);
         }
         endeach_jcr(jcr);

         /* If we have something and no "yes" on command line, get confirmation */
         if (jcrs->size() > 0 && find_arg(ua, NT_("yes")) < 0) {
            char nbuf[1000];
            bsnprintf(nbuf, sizeof(nbuf),  _("Confirm %s of %d Job%s (yes/no): "),
                      reason, jcrs->size(), jcrs->size()>1?"s":"");
            if (!get_yesno(ua, nbuf) || ua->pint32_val == 0) {
               goto bail_out;
            }
         }
         if (jcrs->size() == 0) {
            goto bail_out;               /* If we did not find specified jobid, get out */
         }
         break;

      } else if (strcasecmp(ua->argk[i], NT_("job")) == 0) {
         if (!ua->argv[i]) {
            ua->error_msg(_("No value given for \"job\".\n"));
            goto bail_out;
         }
         jcr = get_jcr_by_partial_name(ua->argv[i]);

         if (jcr && jcr->job && acl_access_ok(ua, Job_ACL, jcr->job->name())) {
            jcrs->append(jcr);

         } else if (jcr) {
            if (jcr->job) {
               ua->error_msg(_("Unauthorized command from this console "
                               "for job=%s.\n"), ua->argv[i]);
            }
            free_jcr(jcr);

         } else {
            ua->warning_msg(_("Warning Job %s is not running.\n"), ua->argv[i]);
         }
         if (jcrs->size() == 0) {
            goto bail_out;               /* If we did not find specified jobid, get out */
         }
         break;

      } else if (strcasecmp(ua->argk[i], NT_("ujobid")) == 0) {
         if (!ua->argv[i]) {
            ua->error_msg(_("No value given for \"ujobid\".\n"));
            goto bail_out;
         }
         jcr = get_jcr_by_full_name(ua->argv[i]);

         if (jcr && jcr->job && acl_access_ok(ua, Job_ACL, jcr->job->name())) {
            jcrs->append(jcr);

         } else if (jcr) {
            if (jcr->job) {
               ua->error_msg(_("Unauthorized command from this console "
                               "for ujobid=%s.\n"), ua->argv[i]);
            }
            free_jcr(jcr);

         } else {
            ua->warning_msg(_("Warning Job %s is not running.\n"), ua->argv[i]);
         }
         if (jcrs->size() == 0) {
            goto bail_out;               /* If we did not find specified jobid, get out */
         }
         break;
      }
   }

   if (jcrs->size() == 0) {
      /*
       * If we still do not have a jcr,
       *   throw up a list and ask the user to select one.
       */
      char *item;
      char buf[1000];
      njobs = count_running_jobs(ua);
      if (njobs == 0) {
         goto bail_out;
      }
      start_prompt(ua, _("Select Job(s):\n"));
      foreach_jcr(jcr) {
         char ed1[50];
         if (jcr->is_internal_job()) {      /* this is us */
            continue;
         }
         bsnprintf(buf, sizeof(buf), _("JobId=%s Job=%s"), edit_int64(jcr->JobId, ed1), jcr->Job);
         add_prompt(ua, buf);
      }
      endeach_jcr(jcr);
      bsnprintf(temp, sizeof(temp), _("Choose Job list to %s"), _(reason));
      selected = New(alist(5, owned_by_alist));
      if (do_alist_prompt(ua, _("Job"), temp, selected) < 0) {
         goto bail_out;
      }
      /* Possibly ask for confirmation */
      if (selected->size() > 0 && find_arg(ua, NT_("yes")) < 0) {
         char nbuf[1000];
         foreach_alist(item, selected) {
            ua->send_msg("%s\n", item);
         }
         bsnprintf(nbuf, sizeof(nbuf),  _("Confirm %s of %d Job%s (yes/no): "),
                   reason, selected->size(), selected->size()>1?"s":"");
         if (!get_yesno(ua, nbuf) || ua->pint32_val == 0) {
            goto bail_out;
         }
      }

      foreach_alist(item, selected) {
         if (sscanf(item, "JobId=%d Job=%127s", &njobs, JobName) != 2) {
            ua->warning_msg(_("Job \"%s\" not found.\n"), item);
            continue;
         }
         jcr = get_jcr_by_full_name(JobName);
         if (jcr) {
            jcrs->append(jcr);
         } else {
            ua->warning_msg(_("Job \"%s\" not found.\n"), JobName);
         }
      }
   }
bail_out:
   if (selected) delete selected;
   return jcrs->size();
}

/* Small helper to scan storage daemon commands and search for volumes */
int scan_storage_cmd(UAContext *ua, const char *cmd,
                     bool allfrompool,    /* Choose to select a specific volume or not */
                     int *drive,          /* Drive number */
                     MEDIA_DBR *mr,       /* Media Record, can have options already filled */
                     POOL_DBR  *pr,       /* Pool Record */
                     const char **action, /* action= argument, can be NULL if not relevant */
                     char *storage,       /* Storage name, must be MAX_NAME_LENGTH long */
                     int  *nb,            /* Number of media found */
                     uint32_t **results)  /* List of MediaId */
{
   bool allpools=false, has_vol = false;;
   STORE *store;

   *nb = 0;
   *results = NULL;

   /* Optional parameters */
   if (drive) {
      *drive = 0;
   }
   if (storage) {
      *storage = 0;
   }

   /* Look at arguments */
   for (int i=1; i<ua->argc; i++) {
      if (strcasecmp(ua->argk[i], NT_("allpools")) == 0) {
         allpools = true;

      } else if (strcasecmp(ua->argk[i], NT_("allfrompool")) == 0) {
         allfrompool = true;

      } else if (strcasecmp(ua->argk[i], NT_("volume")) == 0
                 && is_name_valid(ua->argv[i], NULL)) {
         bstrncpy(mr->VolumeName, ua->argv[i], sizeof(mr->VolumeName));
         has_vol = true;

      } else if (strcasecmp(ua->argk[i], NT_("mediatype")) == 0
                 && ua->argv[i]) {
         bstrncpy(mr->MediaType, ua->argv[i], sizeof(mr->MediaType));

      } else if (strcasecmp(ua->argk[i], NT_("drive")) == 0 && ua->argv[i]) {
         if (drive) {
            *drive = atoi(ua->argv[i]);
         } else {
            ua->warning_msg(_("Invalid argument \"drive\".\n"));
         }

      } else if (strcasecmp(ua->argk[i], NT_("action")) == 0
                 && is_name_valid(ua->argv[i], NULL)) {

         if (action) {
            *action = ua->argv[i];
         } else {
            ua->warning_msg(_("Invalid argument \"action\".\n"));
         }
      }
   }

   if (storage) {
      /* Choose storage */
      ua->jcr->wstore = store =  get_storage_resource(ua, false);
      if (!store) {
         goto bail_out;
      }
      bstrncpy(storage, store->dev_name(), MAX_NAME_LENGTH);
      set_storageid_in_mr(store, mr);
   }

   if (!open_db(ua)) {
      Dmsg0(100, "Can't open db\n");
      goto bail_out;
   }

   /*
    * Look for all volumes that are enabled 
    */
   mr->Enabled = 1;

   if (allfrompool && !has_vol) { /* We need a list of volumes */

      /* We don't take all pools and we don't have a volume in argument,
       * so we need to choose a pool 
       */
      if (!allpools) {
         /* force pool selection */
         POOL *pool = get_pool_resource(ua);

         if (!pool) {
            Dmsg0(100, "Can't get pool resource\n");
            goto bail_out;
         }
         bstrncpy(pr->Name, pool->name(), sizeof(pr->Name));
         if (!db_get_pool_record(ua->jcr, ua->db, pr)) {
            Dmsg0(100, "Can't get pool record\n");
            goto bail_out;
         }
         mr->PoolId = pr->PoolId;
      }

      if (!db_get_media_ids(ua->jcr, ua->db, mr, nb, results)) {
         Dmsg0(100, "No results from db_get_media_ids\n");
         goto bail_out;
      }

   } else {                     /* We want a single volume */
      MEDIA_DBR mr2;
      if (!select_media_dbr(ua, &mr2)) {
         goto bail_out;
      }
      mr->MediaId = mr2.MediaId;
      mr->Recycle = mr2.Recycle; /* Should be the same to find a result */
      if (!db_get_media_ids(ua->jcr, ua->db, mr, nb, results)) {
         Dmsg0(100, "No results from db_get_media_ids\n");
         goto bail_out;
      }
      *nb = 1;
      *results = (uint32_t *) malloc(1 * sizeof(uint32_t));
      *results[0] = mr2.MediaId;
   }

   if (*nb == 0) {
      goto bail_out;
   }
   return 1;

bail_out:
   if (!*nb) {
      ua->send_msg(_("No Volumes found to perform the command.\n"));
   }

   close_db(ua);
   ua->jcr->wstore = NULL;
   if (*results) {
      free(*results);
      *results = NULL;
   }
   *nb = 0;
   return 0;
}
