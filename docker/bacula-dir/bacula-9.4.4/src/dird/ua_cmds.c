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
 *   Bacula Director -- User Agent Commands
 *
 *     Kern Sibbald, September MM
 */

#include "bacula.h"
#include "dird.h"

/* Imported subroutines */

/* Imported variables */
extern jobq_t job_queue;              /* job queue */


/* Imported functions */
extern int autodisplay_cmd(UAContext *ua, const char *cmd);
extern int gui_cmd(UAContext *ua, const char *cmd);
extern int label_cmd(UAContext *ua, const char *cmd);
extern int list_cmd(UAContext *ua, const char *cmd);
extern int llist_cmd(UAContext *ua, const char *cmd);
extern int messagescmd(UAContext *ua, const char *cmd);
extern int prunecmd(UAContext *ua, const char *cmd);
extern int purge_cmd(UAContext *ua, const char *cmd);
extern int truncate_cmd(UAContext *ua, const char *cmd);  /* in ua_purge.c */
extern int query_cmd(UAContext *ua, const char *cmd);
extern int relabel_cmd(UAContext *ua, const char *cmd);
extern int restore_cmd(UAContext *ua, const char *cmd);
extern int retentioncmd(UAContext *ua, const char *cmd);
extern int show_cmd(UAContext *ua, const char *cmd);
extern int sqlquery_cmd(UAContext *ua, const char *cmd);
extern int status_cmd(UAContext *ua, const char *cmd);
extern int update_cmd(UAContext *ua, const char *cmd);

/* Forward referenced functions */
static int add_cmd(UAContext *ua, const char *cmd);
static int automount_cmd(UAContext *ua, const char *cmd);
static int cancel_cmd(UAContext *ua, const char *cmd);
static int create_cmd(UAContext *ua, const char *cmd);
static int delete_cmd(UAContext *ua, const char *cmd);
static int disable_cmd(UAContext *ua, const char *cmd);
static int enable_cmd(UAContext *ua, const char *cmd);
static int estimate_cmd(UAContext *ua, const char *cmd);
static int help_cmd(UAContext *ua, const char *cmd);
static int memory_cmd(UAContext *ua, const char *cmd);
static int mount_cmd(UAContext *ua, const char *cmd);
static int release_cmd(UAContext *ua, const char *cmd);
static int reload_cmd(UAContext *ua, const char *cmd);
static int setdebug_cmd(UAContext *ua, const char *cmd);
static int setbwlimit_cmd(UAContext *ua, const char *cmd);
static int setip_cmd(UAContext *ua, const char *cmd);
static int time_cmd(UAContext *ua, const char *cmd);
static int trace_cmd(UAContext *ua, const char *cmd);
static int unmount_cmd(UAContext *ua, const char *cmd);
static int use_cmd(UAContext *ua, const char *cmd);
static int cloud_cmd(UAContext *ua, const char *cmd);
static int var_cmd(UAContext *ua, const char *cmd);
static int version_cmd(UAContext *ua, const char *cmd);
static int wait_cmd(UAContext *ua, const char *cmd);

static void do_job_delete(UAContext *ua, JobId_t JobId);
static int delete_volume(UAContext *ua);
static int delete_pool(UAContext *ua);
static void delete_job(UAContext *ua);
static int delete_client(UAContext *ua);
static void do_storage_cmd(UAContext *ua, const char *command);

int qhelp_cmd(UAContext *ua, const char *cmd);
int quit_cmd(UAContext *ua, const char *cmd);

/* not all in alphabetical order.  New commands are added after existing commands with similar letters
   to prevent breakage of existing user scripts.  */
struct cmdstruct {
   const char *key;                             /* command */
   int (*func)(UAContext *ua, const char *cmd); /* handler */
   const char *help;            /* main purpose */
   const char *usage;           /* all arguments to build usage */
   const bool use_in_rs;        /* Can use it in Console RunScript */
};
static struct cmdstruct commands[] = {                                      /* Can use it in Console RunScript*/
 { NT_("add"),        add_cmd,     _("Add media to a pool"),   NT_("pool=<pool-name> storage=<storage> jobid=<JobId>"),  false},
 { NT_("autodisplay"), autodisplay_cmd,_("Autodisplay console messages"), NT_("on | off"),    false},
 { NT_("automount"),   automount_cmd,  _("Automount after label"),        NT_("on | off"),    false},
 { NT_("cancel"),     cancel_cmd,    _("Cancel a job"), NT_("jobid=<number-list> | job=<job-name> | ujobid=<unique-jobid> | inactive client=<client-name> storage=<storage-name> | all"), false},
 { NT_("cloud"),      cloud_cmd,     _("Specific Cloud commands"),
   NT_("[storage=<storage-name>] [volume=<vol>] [pool=<pool>] [allpools] [allfrompool] [mediatype=<type>] [drive=<number>] [slots=<number] \n"
       "\tstatus  | prune | list | upload | truncate"), true},
 { NT_("create"),     create_cmd,    _("Create DB Pool from resource"), NT_("pool=<pool-name>"),                    false},
 { NT_("delete"),     delete_cmd,    _("Delete volume, pool, client or job"), NT_("volume=<vol-name> | pool=<pool-name> | jobid=<id> | client=<client-name> | snapshot"), true},
 { NT_("disable"),    disable_cmd,   _("Disable a job, attributes batch process"), NT_("job=<name> | client=<name> | schedule=<name> | storage=<name> | batch"),  true},
 { NT_("enable"),     enable_cmd,    _("Enable a job, attributes batch process"), NT_("job=<name> | client=<name> | schedule=<name> | storage=<name> | batch"),   true},
 { NT_("estimate"),   estimate_cmd,  _("Performs FileSet estimate, listing gives full listing"),
   NT_("fileset=<fs> client=<cli> level=<level> accurate=<yes/no> job=<job> listing"), true},

 { NT_("exit"),       quit_cmd,      _("Terminate Bconsole session"), NT_(""),         false},
 { NT_("gui"),        gui_cmd,       _("Non-interactive gui mode"),   NT_("on | off"), false},
 { NT_("help"),       help_cmd,      _("Print help on specific command"),
   NT_("add autodisplay automount cancel create delete disable\n\tenable estimate exit gui label list llist"
       "\n\tmessages memory mount prune purge quit query\n\trestore relabel release reload run status"
       "\n\tsetbandwidth setdebug setip show sqlquery time trace unmount\n\tumount update use var version wait"
       "\n\tsnapshot"),         false},

 { NT_("label"),      label_cmd,     _("Label a tape"), NT_("storage=<storage> volume=<vol> pool=<pool> slot=<slot> drive=<nb> barcodes"), false},
 { NT_("list"),       list_cmd,      _("List objects from catalog"),
   NT_("jobs [client=<cli>] [jobid=<nn>] [ujobid=<name>] [job=<name>] [joberrors] [jobstatus=<s>] [level=<l>] [jobtype=<t>] [limit=<n>]|\n"
       "\tjobtotals | pools | volume | media <pool=pool-name> | files [type=<deleted|all>] jobid=<nn> | copies jobid=<nn> |\n"
       "\tjoblog jobid=<nn> | pluginrestoreconf jobid=<nn> restoreobjectid=<nn> | snapshot | \n"
       "\tfileindex=<mm> | clients\n"
      ), false},

 { NT_("llist"),      llist_cmd,     _("Full or long list like list command"),
   NT_("jobs [client=<cli>] [jobid=<nn>] [ujobid=<name>] [job=<name>] [joberrors] [jobstatus=<s>] [level=<l>] [jobtype=<t>] [order=<asc/desc>] [limit=<n>]|\n"
       "\tjobtotals | pools | volume | media <pool=pool-name> | files jobid=<nn> | copies jobid=<nn> |\n"
       "\tjoblog jobid=<nn> | pluginrestoreconf jobid=<nn> restoreobjectid=<nn> | snapshot |\n"
       "\tjobid=<nn> fileindex=<mm> | clients\n"), false},

 { NT_("messages"),   messagescmd,   _("Display pending messages"),   NT_(""),    false},
 { NT_("memory"),     memory_cmd,    _("Print current memory usage"), NT_(""),    true},
 { NT_("mount"),      mount_cmd,     _("Mount storage"),
   NT_("storage=<storage-name> slot=<num> drive=<num> [ device=<device-name> ] [ jobid=<id> | job=<job-name> ]"), false},

 { NT_("prune"),      prunecmd,      _("Prune expired records from catalog"),
   NT_("files | jobs | snapshot  [client=<client-name>] | client=<client-name> | \n"
       "\t[expired] [all | allpools | allfrompool] [pool=<pool>] [mediatype=<type>] volume=<volume-name> [yes]"),
   true},

 { NT_("purge"),      purge_cmd,     _("Purge records from catalog"), NT_("files jobs volume=<vol> [mediatype=<type> pool=<pool> allpools storage=<st> drive=<num>]"),  true},
 { NT_("quit"),       quit_cmd,      _("Terminate Bconsole session"), NT_(""),              false},
 { NT_("query"),      query_cmd,     _("Query catalog"), NT_("[<query-item-number>]"),      false},
 { NT_("restore"),    restore_cmd,   _("Restore files"),
   NT_("where=</path> client=<client> storage=<storage> bootstrap=<file> "
       "restorejob=<job> restoreclient=<cli> noautoparent"
       "\n\tcomment=<text> jobid=<jobid> jobuser=<user> jobgroup=<grp> copies done select all"), false},

 { NT_("relabel"),    relabel_cmd,   _("Relabel a tape"),
   NT_("storage=<storage-name> oldvolume=<old-volume-name>\n\tvolume=<newvolume-name> pool=<pool>"), false},

 { NT_("release"),    release_cmd,   _("Release storage"),  NT_("storage=<storage-name> [ device=<device-name> ] "),      false},
 { NT_("reload"),     reload_cmd,    _("Reload conf file"), NT_(""),                  true},
 { NT_("run"),        run_cmd,       _("Run a job"),
   NT_("job=<job-name> client=<client-name>\n\tfileset=<FileSet-name> level=<level-keyword>\n\tstorage=<storage-name>"
       " where=<directory-prefix>\n\twhen=<universal-time-specification> pool=<pool-name>\n\t"
       " nextpool=<next-pool-name> comment=<text> accurate=<bool> spooldata=<bool> yes"), false},

 { NT_("restart"),    restart_cmd,   _("Restart a job"),
   NT_("incomplete job=<job-name> client=<client-name>\n\tfileset=<FileSet-name> level=<level-keyword>\n\tstorage=<storage-name>"
       "when=<universal-time-specification>\n\tcomment=<text> spooldata=<bool> jobid=<jobid>"), false},

 { NT_("resume"),    restart_cmd,   _("Resume a job"),
   NT_("incomplete job=<job-name> client=<client-name>\n\tfileset=<FileSet-name> level=<level-keyword>\n\tstorage=<storage-name>"
       "when=<universal-time-specification>\n\tcomment=<text> spooldata=<bool> jobid=<jobid>"), false},

 { NT_("status"),     status_cmd,    _("Report status"),
   NT_("all | network [bytes=<nn-b>] | dir=<dir-name> | director | client=<client-name> |\n"
       "\tstorage=<storage-name> slots |\n"
       "\tschedule [job=<job-name>] [client=<cli-name>] [schedule=<sched-name>] [days=<nn>] [limit=<nn>]\n"
       "\t\t[time=<universal-time-specification>]"), true},

 { NT_("stop"),       cancel_cmd,    _("Stop a job"), NT_("jobid=<number-list> job=<job-name> ujobid=<unique-jobid> all"), false},
 { NT_("setdebug"),   setdebug_cmd,  _("Sets debug level"),
   NT_("level=<nn> tags=<tags> trace=0/1 options=<0tTc> tags=<tags> | client=<client-name> | dir | storage=<storage-name> | all"), true},

 { NT_("setbandwidth"),   setbwlimit_cmd,  _("Sets bandwidth"),
   NT_("limit=<speed> client=<client-name> jobid=<number> job=<job-name> ujobid=<unique-jobid>"), true},

 { NT_("snapshot"),   snapshot_cmd,  _("Handle snapshots"),
   NT_("[client=<client-name> | job=<job-name> | jobid=<jobid>] [delete | list | listclient | prune | sync | update]"), true},

 { NT_("setip"),      setip_cmd,     _("Sets new client address -- if authorized"), NT_(""),   false},
 { NT_("show"),       show_cmd,      _("Show resource records"),
   NT_("job=<xxx> |  pool=<yyy> | fileset=<aaa> | schedule=<sss> | client=<zzz> | storage=<sss> | disabled | all"), true},

 { NT_("sqlquery"),   sqlquery_cmd,  _("Use SQL to query catalog"), NT_(""),          false},
 { NT_("time"),       time_cmd,      _("Print current time"),       NT_(""),          true},
 { NT_("trace"),      trace_cmd,     _("Turn on/off trace to file"), NT_("on | off"), true},
 { NT_("truncate"),   truncate_cmd,  _("Truncate one or more Volumes"), NT_("volume=<vol> [mediatype=<type> pool=<pool> allpools storage=<st> drive=<num>]"),  true},
 { NT_("unmount"),    unmount_cmd,   _("Unmount storage"),
   NT_("storage=<storage-name> [ drive=<num> ] | jobid=<id> | job=<job-name>"), false},

 { NT_("umount"),     unmount_cmd,   _("Umount - for old-time Unix guys, see unmount"),
   NT_("storage=<storage-name> [ drive=<num> ] [ device=<dev-name> ]| jobid=<id> | job=<job-name>"), false},

 { NT_("update"),     update_cmd,    _("Update volume, pool or stats"),
   NT_("stats\n\tsnapshot\n\tpool=<poolname>\n\tslots storage=<storage> scan"
       "\n\tvolume=<volname> volstatus=<status> volretention=<time-def> cacheretention=<time-def>"
       "\n\t pool=<pool> recycle=<yes/no> slot=<number>\n\t inchanger=<yes/no>"
       "\n\t maxvolbytes=<size> maxvolfiles=<nb> maxvoljobs=<nb>"
       "\n\t enabled=<yes/no> recyclepool=<pool> actiononpurge=<action>"
       "\n\t allfrompool=<pool> fromallpools frompool"),true},
 { NT_("use"),        use_cmd,       _("Use catalog xxx"), NT_("catalog=<catalog>"),     false},
 { NT_("var"),        var_cmd,       _("Does variable expansion"), NT_(""),  false},
 { NT_("version"),    version_cmd,   _("Print Director version"),  NT_(""),  true},
 { NT_("wait"),       wait_cmd,      _("Wait until no jobs are running"),
   NT_("jobname=<name> | jobid=<nnn> | ujobid=<complete_name>"), false}
};

#define comsize ((int)(sizeof(commands)/sizeof(struct cmdstruct)))

const char *get_command(int index) {
   return commands[index].key;
}

/*
 * Execute a command from the UA
 */
bool do_a_command(UAContext *ua)
{
   int i;
   int len;
   bool ok = false;
   bool found = false;

   Dmsg1(900, "Command: %s\n", ua->argk[0]);
   if (ua->argc == 0) {
      return false;
   }

   if (ua->jcr->wstorage) {
      while (ua->jcr->wstorage->size()) {
         ua->jcr->wstorage->remove(0);
      }
   }

   len = strlen(ua->argk[0]);
   for (i=0; i<comsize; i++) {     /* search for command */
      if (strncasecmp(ua->argk[0],  commands[i].key, len) == 0) {
         ua->cmd_index = i;
         /* Check if command permitted, but "quit" is always OK */
         if (strcmp(ua->argk[0], NT_("quit")) != 0 &&
             !acl_access_ok(ua, Command_ACL, ua->argk[0], len)) {
            break;
         }
         /* Check if this command is authorized in RunScript */
         if (ua->runscript && !commands[i].use_in_rs) {
            ua->error_msg(_("Can't use %s command in a runscript"), ua->argk[0]);
            break;
         }
         if (ua->api) ua->signal(BNET_CMD_BEGIN);
         ok = (*commands[i].func)(ua, ua->cmd);   /* go execute command */
         if (ua->api) ua->signal(ok?BNET_CMD_OK:BNET_CMD_FAILED);
         found = ua->UA_sock && ua->UA_sock->is_stop() ? false : true;
         break;
      }
   }
   if (!found) {
      ua->error_msg(_("%s: is an invalid command.\n"), ua->argk[0]);
      ok = false;
   }
   return ok;
}

/*
 * This is a common routine used to stuff the Pool DB record defaults
 *   into the Media DB record just before creating a media (Volume)
 *   record.
 */
void set_pool_dbr_defaults_in_media_dbr(MEDIA_DBR *mr, POOL_DBR *pr)
{
   mr->PoolId = pr->PoolId;
   bstrncpy(mr->VolStatus, NT_("Append"), sizeof(mr->VolStatus));
   mr->Recycle = pr->Recycle;
   mr->VolRetention = pr->VolRetention;
   mr->CacheRetention = pr->CacheRetention;
   mr->VolUseDuration = pr->VolUseDuration;
   mr->ActionOnPurge = pr->ActionOnPurge;
   mr->RecyclePoolId = pr->RecyclePoolId;
   mr->MaxVolJobs = pr->MaxVolJobs;
   mr->MaxVolFiles = pr->MaxVolFiles;
   mr->MaxVolBytes = pr->MaxVolBytes;
   mr->LabelType = pr->LabelType;
   mr->Enabled = 1;
}


/*
 *  Add Volumes to an existing Pool
 */
static int add_cmd(UAContext *ua, const char *cmd)
{
   POOL_DBR pr;
   MEDIA_DBR mr;
   int num, i, max, startnum;
   char name[MAX_NAME_LENGTH];
   STORE *store;
   int Slot = 0, InChanger = 0;

   ua->send_msg(_(
"You probably don't want to be using this command since it\n"
"creates database records without labeling the Volumes.\n"
"You probably want to use the \"label\" command.\n\n"));

   if (!open_client_db(ua)) {
      return 1;
   }

   memset(&pr, 0, sizeof(pr));

   if (!get_pool_dbr(ua, &pr)) {
      return 1;
   }

   Dmsg4(120, "id=%d Num=%d Max=%d type=%s\n", pr.PoolId, pr.NumVols,
      pr.MaxVols, pr.PoolType);

   while (pr.MaxVols > 0 && pr.NumVols >= pr.MaxVols) {
      ua->warning_msg(_("Pool already has maximum volumes=%d\n"), pr.MaxVols);
      if (!get_pint(ua, _("Enter new maximum (zero for unlimited): "))) {
         return 1;
      }
      pr.MaxVols = ua->pint32_val;
   }

   /* Get media type */
   if ((store = get_storage_resource(ua, false/*no default*/)) != NULL) {
      bstrncpy(mr.MediaType, store->media_type, sizeof(mr.MediaType));
   } else if (!get_media_type(ua, mr.MediaType, sizeof(mr.MediaType))) {
      return 1;
   }

   if (pr.MaxVols == 0) {
      max = 1000;
   } else {
      max = pr.MaxVols - pr.NumVols;
   }
   for (;;) {
      char buf[100];
      bsnprintf(buf, sizeof(buf), _("Enter number of Volumes to create. 0=>fixed name. Max=%d: "), max);
      if (!get_pint(ua, buf)) {
         return 1;
      }
      num = ua->pint32_val;
      if (num < 0 || num > max) {
         ua->warning_msg(_("The number must be between 0 and %d\n"), max);
         continue;
      }
      break;
   }

   for (;;) {
      if (num == 0) {
         if (!get_cmd(ua, _("Enter Volume name: "))) {
            return 1;
         }
      } else {
         if (!get_cmd(ua, _("Enter base volume name: "))) {
            return 1;
         }
      }
      /* Don't allow | in Volume name because it is the volume separator character */
      if (!is_volume_name_legal(ua, ua->cmd)) {
         continue;
      }
      if (strlen(ua->cmd) >= MAX_NAME_LENGTH-10) {
         ua->warning_msg(_("Volume name too long.\n"));
         continue;
      }
      if (strlen(ua->cmd) == 0) {
         ua->warning_msg(_("Volume name must be at least one character long.\n"));
         continue;
      }
      break;
   }

   bstrncpy(name, ua->cmd, sizeof(name));
   if (num > 0) {
      bstrncat(name, "%04d", sizeof(name));

      for (;;) {
         if (!get_pint(ua, _("Enter the starting number: "))) {
            return 1;
         }
         startnum = ua->pint32_val;
         if (startnum < 1) {
            ua->warning_msg(_("Start number must be greater than zero.\n"));
            continue;
         }
         break;
      }
   } else {
      startnum = 1;
      num = 1;
   }

   if (store && store->autochanger) {
      if (!get_pint(ua, _("Enter slot (0 for none): "))) {
         return 1;
      }
      Slot = ua->pint32_val;
      if (!get_yesno(ua, _("InChanger? yes/no: "))) {
         return 1;
      }
      InChanger = ua->pint32_val;
   }

   set_pool_dbr_defaults_in_media_dbr(&mr, &pr);
   for (i=startnum; i < num+startnum; i++) {
      bsnprintf(mr.VolumeName, sizeof(mr.VolumeName), name, i);
      mr.Slot = Slot++;
      mr.InChanger = InChanger;
      mr.Enabled = 1;
      set_storageid_in_mr(store, &mr);
      Dmsg1(200, "Create Volume %s\n", mr.VolumeName);
      if (!db_create_media_record(ua->jcr, ua->db, &mr)) {
         ua->error_msg("%s", db_strerror(ua->db));
         return 1;
      }
//    if (i == startnum) {
//       first_id = mr.PoolId;
//    }
   }
   pr.NumVols += num;
   Dmsg0(200, "Update pool record.\n");
   if (db_update_pool_record(ua->jcr, ua->db, &pr) != 1) {
      ua->warning_msg("%s", db_strerror(ua->db));
      return 1;
   }
   ua->send_msg(_("%d Volumes created in pool %s\n"), num, pr.Name);

   return 1;
}

/*
 * Turn auto mount on/off
 *
 *  automount on
 *  automount off
 */
int automount_cmd(UAContext *ua, const char *cmd)
{
   char *onoff;

   if (ua->argc != 2) {
      if (!get_cmd(ua, _("Turn on or off? "))) {
            return 1;
      }
      onoff = ua->cmd;
   } else {
      onoff = ua->argk[1];
   }

   ua->automount = (strcasecmp(onoff, NT_("off")) == 0) ? 0 : 1;
   return 1;
}

/*
 * Cancel/Stop a job -- Stop marks it as Incomplete
 *   so that it can be restarted.
 */
static int cancel_cmd(UAContext *ua, const char *cmd)
{
   JCR    *jcr;
   bool    ret = true;
   int     nb;
   bool    cancel = strcasecmp(commands[ua->cmd_index].key, "cancel") == 0;
   alist  *jcrs = New(alist(5, not_owned_by_alist));

   /* If the user explicitely ask, we can send the cancel command to
    * the FD.
    */
   if (find_arg(ua, "inactive") > 0) {
      ret = cancel_inactive_job(ua);
      goto bail_out;
   }

   nb = select_running_jobs(ua, jcrs, commands[ua->cmd_index].key);

   foreach_alist(jcr, jcrs) {
      /* Execute the cancel command only if we don't have an error */
      if (nb != -1) {
         ret &= cancel_job(ua, jcr, 60, cancel);
      }
      free_jcr(jcr);
   }

bail_out:
   delete jcrs;
   return ret;
}

/*
 * This is a common routine to create or update a
 *   Pool DB base record from a Pool Resource. We handle
 *   the setting of MaxVols and NumVols slightly differently
 *   depending on if we are creating the Pool or we are
 *   simply bringing it into agreement with the resource (updage).
 *
 * Caution : RecyclePoolId isn't setup in this function.
 *           You can use set_pooldbr_recyclepoolid();
 *
 */
void set_pooldbr_from_poolres(POOL_DBR *pr, POOL *pool, e_pool_op op)
{
   bstrncpy(pr->PoolType, pool->pool_type, sizeof(pr->PoolType));
   if (op == POOL_OP_CREATE) {
      pr->MaxVols = pool->max_volumes;
      pr->NumVols = 0;
   } else {          /* update pool */
      if (pr->MaxVols != pool->max_volumes) {
         pr->MaxVols = pool->max_volumes;
      }
      if (pr->MaxVols != 0 && pr->MaxVols < pr->NumVols) {
         pr->MaxVols = pr->NumVols;
      }
   }
   pr->LabelType = pool->LabelType;
   pr->UseOnce = pool->use_volume_once;
   pr->UseCatalog = pool->use_catalog;
   pr->Recycle = pool->Recycle;
   pr->VolRetention = pool->VolRetention;
   pr->CacheRetention = pool->CacheRetention;
   pr->VolUseDuration = pool->VolUseDuration;
   pr->MaxVolJobs = pool->MaxVolJobs;
   pr->MaxVolFiles = pool->MaxVolFiles;
   pr->MaxVolBytes = pool->MaxVolBytes;
   pr->AutoPrune = pool->AutoPrune;
   pr->ActionOnPurge = pool->action_on_purge;
   pr->Recycle = pool->Recycle;
   if (pool->label_format) {
      bstrncpy(pr->LabelFormat, pool->label_format, sizeof(pr->LabelFormat));
   } else {
      bstrncpy(pr->LabelFormat, "*", sizeof(pr->LabelFormat));    /* none */
   }
}

/* set/update Pool.RecyclePoolId and Pool.ScratchPoolId in Catalog */
int update_pool_references(JCR *jcr, BDB *db, POOL *pool)
{
   POOL_DBR  pr;

   if (pool->ScratchPool == pool) {
      Jmsg(NULL, M_WARNING, 0,
           _("The ScratchPool directive for Pool \"%s\" is incorrect. Using default Scratch pool instead.\n"),
           pool->name());
      pool->ScratchPool = NULL;
   }

   if (!pool->RecyclePool && !pool->ScratchPool) {
      return 1;
   }

   memset(&pr, 0, sizeof(POOL_DBR));
   bstrncpy(pr.Name, pool->name(), sizeof(pr.Name));

   /* Don't compute NumVols here */
   if (!db_get_pool_record(jcr, db, &pr)) {
      return -1;                       /* not exists in database */
   }

   set_pooldbr_from_poolres(&pr, pool, POOL_OP_UPDATE);

   if (!set_pooldbr_references(jcr, db, &pr, pool)) {
      return -1;                      /* error */
   }

   /* NumVols is updated here */
   if (!db_update_pool_record(jcr, db, &pr)) {
      return -1;                      /* error */
   }
   return 1;
}

/* set POOL_DBR.RecyclePoolId and POOL_DBR.ScratchPoolId from Pool resource
 * works with set_pooldbr_from_poolres
 */
bool set_pooldbr_references(JCR *jcr, BDB *db, POOL_DBR *pr, POOL *pool)
{
   POOL_DBR rpool;
   bool ret = true;

   if (pool->RecyclePool) {
      memset(&rpool, 0, sizeof(POOL_DBR));

      bstrncpy(rpool.Name, pool->RecyclePool->name(), sizeof(rpool.Name));
      if (db_get_pool_record(jcr, db, &rpool)) {
        pr->RecyclePoolId = rpool.PoolId;
      } else {
        Jmsg(jcr, M_WARNING, 0,
        _("Can't set %s RecyclePool to %s, %s is not in database.\n" \
          "Try to update it with 'update pool=%s'\n"),
        pool->name(), rpool.Name, rpool.Name,pool->name());

        ret = false;
      }
   } else {                    /* no RecyclePool used, set it to 0 */
      pr->RecyclePoolId = 0;
   }

   if (pool->ScratchPool) {
      memset(&rpool, 0, sizeof(POOL_DBR));

      bstrncpy(rpool.Name, pool->ScratchPool->name(), sizeof(rpool.Name));
      if (db_get_pool_record(jcr, db, &rpool)) {
        pr->ScratchPoolId = rpool.PoolId;
      } else {
        Jmsg(jcr, M_WARNING, 0,
        _("Can't set %s ScratchPool to %s, %s is not in database.\n" \
          "Try to update it with 'update pool=%s'\n"),
        pool->name(), rpool.Name, rpool.Name,pool->name());
        ret = false;
      }
   } else {                    /* no ScratchPool used, set it to 0 */
      pr->ScratchPoolId = 0;
   }

   return ret;
}


/*
 * Create a pool record from a given Pool resource
 *   Also called from backup.c
 * Returns: -1  on error
 *           0  record already exists
 *           1  record created
 */

int create_pool(JCR *jcr, BDB *db, POOL *pool, e_pool_op op)
{
   POOL_DBR  pr;
   memset(&pr, 0, sizeof(POOL_DBR));
   bstrncpy(pr.Name, pool->name(), sizeof(pr.Name));

   if (db_get_pool_record(jcr, db, &pr)) {
      /* Pool Exists */
      if (op == POOL_OP_UPDATE) {  /* update request */
         set_pooldbr_from_poolres(&pr, pool, op);
         set_pooldbr_references(jcr, db, &pr, pool);
         db_update_pool_record(jcr, db, &pr);
      }
      return 0;                       /* exists */
   }

   set_pooldbr_from_poolres(&pr, pool, op);
   set_pooldbr_references(jcr, db, &pr, pool);

   if (!db_create_pool_record(jcr, db, &pr)) {
      return -1;                      /* error */
   }
   return 1;
}



/*
 * Create a Pool Record in the database.
 *  It is always created from the Resource record.
 */
static int create_cmd(UAContext *ua, const char *cmd)
{
   POOL *pool;

   if (!open_client_db(ua)) {
      return 1;
   }

   pool = get_pool_resource(ua);
   if (!pool) {
      return 1;
   }

   switch (create_pool(ua->jcr, ua->db, pool, POOL_OP_CREATE)) {
   case 0:
      ua->error_msg(_("Error: Pool %s already exists.\n"
               "Use update to change it.\n"), pool->name());
      break;

   case -1:
      ua->error_msg("%s", db_strerror(ua->db));
      break;

   default:
     break;
   }
   ua->send_msg(_("Pool %s created.\n"), pool->name());
   return 1;
}


extern DIRRES *director;
extern char *configfile;

static int setbwlimit_client(UAContext *ua, CLIENT *client, char *Job, int64_t limit)
{
   POOL_MEM buf;
   CLIENT *old_client;
   char ed1[50];
   if (!client) {
      return 1;
   }

   /* Connect to File daemon */
   old_client = ua->jcr->client;
   ua->jcr->client = client;
   ua->jcr->max_bandwidth = limit;

   /* Try to connect for 15 seconds */
   ua->send_msg(_("Connecting to Client %s at %s:%d\n"),
                client->name(), client->address(buf.addr()), client->FDport);
   if (!connect_to_file_daemon(ua->jcr, 1, 15, 0)) {
      ua->error_msg(_("Failed to connect to Client.\n"));
      goto bail_out;
   }
   Dmsg0(120, "Connected to file daemon\n");

   if (!send_bwlimit(ua->jcr, Job)) {
      ua->error_msg(_("Failed to set bandwidth limit to Client.\n"));

   } else {
      /* Note, we add 2000 OK that was sent by FD to us to message */
      ua->info_msg(_("2000 OK Limiting bandwidth to %sB/s %s\n"),
                   edit_uint64_with_suffix(limit, ed1), *Job?Job:_("on running and future jobs"));
   }

   ua->jcr->file_bsock->signal(BNET_TERMINATE);
   free_bsock(ua->jcr->file_bsock);
   ua->jcr->max_bandwidth = 0;

bail_out:
   ua->jcr->client = old_client;
   return 1;
}

static int setbwlimit_cmd(UAContext *ua, const char *cmd)
{
   int action = -1;
   CLIENT *client = NULL;
   char Job[MAX_NAME_LENGTH];
   *Job=0;
   uint64_t limit = 0;
   JCR *jcr = NULL;
   int i;

   const char *lst_all[] = { "job", "jobid", "jobname", "client", NULL };
   if (find_arg_keyword(ua, lst_all) < 0) {
       start_prompt(ua, _("Set Bandwidth choice:\n"));
       add_prompt(ua, _("Running Job")); /* 0 */
       add_prompt(ua, _("Running and future Jobs for a Client")); /* 1 */
       action = do_prompt(ua, "item", _("Choose where to limit the bandwidth"),
                          NULL, 0);
       if (action < 0) {
          return 1;
       }
   }

   i = find_arg_with_value(ua, "limit");
   if (i >= 0) {
      if (!speed_to_uint64(ua->argv[i], strlen(ua->argv[i]), &limit)) {
         ua->error_msg(_("Invalid value for limit parameter. Expecting speed.\n"));
         return 1;
      }
   } else {
      if (!get_cmd(ua, _("Enter new bandwidth limit: "))) {
         return 1;
      }
      if (!speed_to_uint64(ua->cmd, strlen(ua->cmd), &limit)) {
         ua->error_msg(_("Invalid value for limit parameter. Expecting speed.\n"));
         return 1;
      }
   }

   const char *lst[] = { "job", "jobid", "jobname", NULL };
   if (action == 0 || find_arg_keyword(ua, lst) > 0) {
      alist *jcrs = New(alist(10, not_owned_by_alist));
      select_running_jobs(ua, jcrs, "limit");
      foreach_alist(jcr, jcrs) {
         jcr->max_bandwidth = limit; /* TODO: see for locking (Should be safe)*/
         bstrncpy(Job, jcr->Job, sizeof(Job));
         client = jcr->client;
         setbwlimit_client(ua, client, Job, limit);
         free_jcr(jcr);
      }

   } else {
      client = get_client_resource(ua, JT_BACKUP_RESTORE);
      if (client) {
         setbwlimit_client(ua, client, Job, limit);
      }
   }
   return 1;
}

/*
 * Set a new address in a Client resource. We do this only
 *  if the Console name is the same as the Client name
 *  and the Console can access the client.
 */
static int setip_cmd(UAContext *ua, const char *cmd)
{
   CLIENT *client;
   char addr[1024];
   if (!ua->cons || !acl_access_client_ok(ua, ua->cons->name(), JT_BACKUP_RESTORE)) {
      ua->error_msg(_("Unauthorized command from this console.\n"));
      return 1;
   }
   LockRes();
   client = GetClientResWithName(ua->cons->name());

   if (!client) {
      ua->error_msg(_("Client \"%s\" not found.\n"), ua->cons->name());
      goto get_out;
   }
   /* MA Bug 6 remove ifdef */
   sockaddr_to_ascii(&(ua->UA_sock->client_addr),
                     sizeof(ua->UA_sock->client_addr), addr, sizeof(addr));
   client->setAddress(addr);
   ua->send_msg(_("Client \"%s\" address set to %s\n"),
            client->name(), addr);
get_out:
   UnlockRes();
   return 1;
}

/*
 * Does all sorts of enable/disable commands: batch, scheduler (not implemented)
 *  job, client, schedule, storage
 */
static void do_enable_disable_cmd(UAContext *ua, bool setting)
{
   JOB *job = NULL;
   CLIENT *client = NULL;
   SCHED *sched = NULL;
   int i;

   if (find_arg(ua, NT_("batch")) > 0) {
      ua->send_msg(_("Job Attributes Insertion %sabled\n"), setting?"en":"dis");
      db_disable_batch_insert(setting);
      return;
   }

   /*
    * if (find_arg(ua, NT_("scheduler")) > 0) {
    *    ua->send_msg(_("Job Scheduler %sabled\n"), setting?"en":"dis");
    *    return;
    * }
    */

   i = find_arg(ua, NT_("job"));
   if (i >= 0) {
      if (ua->argv[i]) {
         LockRes();
         job = GetJobResWithName(ua->argv[i]);
         UnlockRes();
      } else {
         job = select_enable_disable_job_resource(ua, setting);
         if (!job) {
            return;
         }
      }
   }
   if (job) {
      if (!acl_access_ok(ua, Job_ACL, job->name())) {
         ua->error_msg(_("Unauthorized command from this console.\n"));
         return;
      }
      job->setEnabled(setting);
      ua->send_msg(_("Job \"%s\" %sabled\n"), job->name(), setting?"en":"dis");
   }

   i = find_arg(ua, NT_("client"));
   if (i >= 0) {
      if (ua->argv[i]) {
         LockRes();
         client = GetClientResWithName(ua->argv[i]);
         UnlockRes();
      } else {
         client = select_enable_disable_client_resource(ua, setting);
         if (!client) {
            return;
         }
      }
   }
   if (client) {
      if (!acl_access_client_ok(ua, client->name(), JT_BACKUP_RESTORE)) {
         ua->error_msg(_("Unauthorized command from this console.\n"));
         return;
      }
      client->setEnabled(setting);
      ua->send_msg(_("Client \"%s\" %sabled\n"), client->name(), setting?"en":"dis");
   }

   i = find_arg(ua, NT_("schedule"));
   if (i >= 0) {
      if (ua->argv[i]) {
         LockRes();
         sched = (SCHED *)GetResWithName(R_SCHEDULE, ua->argv[i]);
         UnlockRes();
      } else {
         sched = select_enable_disable_schedule_resource(ua, setting);
         if (!sched) {
            return;
         }
      }
   }
   if (sched) {
      if (!acl_access_ok(ua, Schedule_ACL, sched->name())) {
         ua->error_msg(_("Unauthorized command from this console.\n"));
         return;
      }
      sched->setEnabled(setting);
      ua->send_msg(_("Schedule \"%s\" %sabled\n"), sched->name(), setting?"en":"dis");
   }

   i = find_arg(ua, NT_("storage"));
   if (i >= 0) {
      do_storage_cmd(ua, setting?"enable":"disable");
   }

   if (i < 0 && !sched && !client && !job) {
      ua->error_msg(_("You must enter one of the following keywords: job, client, schedule, or storage.\n"));
   }

   return;
}

static int enable_cmd(UAContext *ua, const char *cmd)
{
   do_enable_disable_cmd(ua, true);
   return 1;
}

static int disable_cmd(UAContext *ua, const char *cmd)
{
   do_enable_disable_cmd(ua, false);
   return 1;
}

static void do_dir_setdebug(UAContext *ua, int64_t level, int trace_flag, char *options, int64_t tags)
{
   debug_level = level;
   debug_level_tags = tags;
   set_trace(trace_flag);
   set_debug_flags(options);
}

static void do_storage_setdebug(UAContext *ua, STORE *store,
               int64_t level, int trace_flag, int hangup, int blowup,
               char *options, char *tags)
{
   BSOCK *sd;
   USTORE lstore;

   lstore.store = store;
   pm_strcpy(lstore.store_source, _("unknown source"));
   set_wstorage(ua->jcr, &lstore);
   /* Try connecting for up to 15 seconds */
   ua->send_msg(_("Connecting to Storage daemon %s at %s:%d\n"),
      store->name(), store->address, store->SDport);
   if (!connect_to_storage_daemon(ua->jcr, 1, 15, 0)) {
      ua->error_msg(_("Failed to connect to Storage daemon.\n"));
      return;
   }
   Dmsg0(120, _("Connected to storage daemon\n"));
   sd = ua->jcr->store_bsock;
   sd->fsend("setdebug=%ld trace=%ld hangup=%ld blowup=%ld options=%s tags=%s\n",
             (int32_t)level, trace_flag, hangup, blowup, options, NPRTB(tags));
   if (sd->recv() >= 0) {
      ua->send_msg("%s", sd->msg);
   }
   sd->signal(BNET_TERMINATE);
   free_bsock(ua->jcr->store_bsock);
   return;
}

/*
 * For the client, we have the following values that can be set
 *  level = debug level
 *  trace = send debug output to a file
 *  options = various options for debug or specific FD behavior
 *  hangup = how many records to send to FD before hanging up
 *    obviously this is most useful for testing restarting
 *    failed jobs.
 *  blowup = how many records to send to FD before blowing up the FD.
 */
static void do_client_setdebug(UAContext *ua, CLIENT *client,
               int64_t level, int trace, int hangup, int blowup,
               char *options, char *tags)
{
   POOL_MEM buf;
   CLIENT *old_client;
   BSOCK *fd;

   /* Connect to File daemon */

   old_client = ua->jcr->client;
   ua->jcr->client = client;
   /* Try to connect for 15 seconds */
   ua->send_msg(_("Connecting to Client %s at %s:%d\n"),
                client->name(), client->address(buf.addr()), client->FDport);

   if (!connect_to_file_daemon(ua->jcr, 1, 15, 0)) {
      ua->error_msg(_("Failed to connect to Client.\n"));
      ua->jcr->client = old_client;
      return;
   }
   Dmsg0(120, "Connected to file daemon\n");

   fd = ua->jcr->file_bsock;
   if (ua->jcr->FDVersion <= 10) {
      fd->fsend("setdebug=%ld trace=%d hangup=%d\n",
                (int32_t)level, trace, hangup);
   } else {
      fd->fsend("setdebug=%ld trace=%d hangup=%d blowup=%d options=%s tags=%s\n",
                (int32_t)level, trace, hangup, blowup, options, NPRTB(tags));
   }
   if (fd->recv() >= 0) {
      ua->send_msg("%s", fd->msg);
   }
   fd->signal(BNET_TERMINATE);
   free_bsock(ua->jcr->file_bsock);
   ua->jcr->client = old_client;
   return;
}


static void do_all_setdebug(UAContext *ua, int64_t level,
               int trace_flag, int hangup, int blowup,
               char *options, char *tags)
{
   STORE *store, **unique_store;
   CLIENT *client, **unique_client;
   POOL_MEM buf1, buf2;
   int i, j, found;
   int64_t t=0;

   /* Director */
   debug_parse_tags(tags, &t);
   do_dir_setdebug(ua, level, trace_flag, options, t);

   /* Count Storage items */
   LockRes();
   store = NULL;
   i = 0;
   foreach_res(store, R_STORAGE) {
      i++;
   }
   unique_store = (STORE **) malloc(i * sizeof(STORE));
   /* Find Unique Storage address/port */
   store = (STORE *)GetNextRes(R_STORAGE, NULL);
   i = 0;
   unique_store[i++] = store;
   while ((store = (STORE *)GetNextRes(R_STORAGE, (RES *)store))) {
      found = 0;
      for (j=0; j<i; j++) {
         if (strcmp(unique_store[j]->address, store->address) == 0 &&
             unique_store[j]->SDport == store->SDport) {
            found = 1;
            break;
         }
      }
      if (!found) {
         unique_store[i++] = store;
         Dmsg2(140, "Stuffing: %s:%d\n", store->address, store->SDport);
      }
   }
   UnlockRes();

   /* Call each unique Storage daemon */
   for (j=0; j<i; j++) {
      do_storage_setdebug(ua, unique_store[j], level, trace_flag,
         hangup, blowup, options, tags);
   }
   free(unique_store);

   /* Count Client items */
   LockRes();
   client = NULL;
   i = 0;
   foreach_res(client, R_CLIENT) {
      i++;
   }
   unique_client = (CLIENT **) malloc(i * sizeof(CLIENT));
   /* Find Unique Client address/port */
   client = (CLIENT *)GetNextRes(R_CLIENT, NULL);
   i = 0;
   unique_client[i++] = client;
   while ((client = (CLIENT *)GetNextRes(R_CLIENT, (RES *)client))) {
      found = 0;
      for (j=0; j<i; j++) {
         if (strcmp(unique_client[j]->address(buf1.addr()), client->address(buf2.addr())) == 0 &&
             unique_client[j]->FDport == client->FDport) {
            found = 1;
            break;
         }
      }
      if (!found) {
         unique_client[i++] = client;
         Dmsg2(140, "Stuffing: %s:%d\n", client->address(buf1.addr()), client->FDport);
      }
   }
   UnlockRes();

   /* Call each unique File daemon */
   for (j=0; j<i; j++) {
      do_client_setdebug(ua, unique_client[j], level, trace_flag,
         hangup, blowup, options, tags);
   }
   free(unique_client);
}

/*
 * setdebug level=nn all trace=1/0
 */
static int setdebug_cmd(UAContext *ua, const char *cmd)
{
   STORE *store;
   CLIENT *client;
   int64_t level=0, tags=0;
   int trace_flag = -1;
   int hangup = -1;
   int blowup = -1;
   int i;
   char *tags_str=NULL;
   char options[60];

   Dmsg1(120, "setdebug:%s:\n", cmd);

   *options = 0;
   i = find_arg_with_value(ua, "options");
   if (i >= 0) {
      bstrncpy(options, ua->argv[i], sizeof(options) - 1);
   }
   level = -1;
   i = find_arg_with_value(ua, "level");
   if (i >= 0) {
      level = str_to_int64(ua->argv[i]);
   }
   if (level < 0) {
      if (!get_pint(ua, _("Enter new debug level: "))) {
         return 1;
      }
      level = ua->pint32_val;
   }

   /* Better to send the tag string instead of tweaking the level
    * in case where we extend the tag or change the representation
    */
   i = find_arg_with_value(ua, "tags");
   if (i > 0) {
      tags_str = ua->argv[i];
      if (!debug_parse_tags(tags_str, &tags)) {
         ua->error_msg(_("Incorrect tags found on command line %s\n"), tags_str);
         return 1;
      }
   }

   /* Look for trace flag. -1 => not change */
   i = find_arg_with_value(ua, "trace");
   if (i >= 0) {
      trace_flag = atoi(ua->argv[i]);
      if (trace_flag > 0) {
         trace_flag = 1;
      }
   }

   /* Look for hangup (debug only) flag. -1 => not change */
   i = find_arg_with_value(ua, "hangup");
   if (i >= 0) {
      hangup = atoi(ua->argv[i]);
   }

   /* Look for blowup (debug only) flag. -1 => not change */
   i = find_arg_with_value(ua, "blowup");
   if (i >= 0) {
      blowup = atoi(ua->argv[i]);
   }

   /* General debug? */
   for (i=1; i<ua->argc; i++) {
      if (strcasecmp(ua->argk[i], "all") == 0) {
         do_all_setdebug(ua, level, trace_flag, hangup, blowup, options, tags_str);
         return 1;
      }
      if (strcasecmp(ua->argk[i], "dir") == 0 ||
          strcasecmp(ua->argk[i], "director") == 0) {
         do_dir_setdebug(ua, level, trace_flag, options, tags);
         return 1;
      }
      if (strcasecmp(ua->argk[i], "client") == 0 ||
          strcasecmp(ua->argk[i], "fd") == 0) {
         client = NULL;
         if (ua->argv[i]) {
            client = GetClientResWithName(ua->argv[i]);
            if (client) {
               do_client_setdebug(ua, client, level, trace_flag,
                  hangup, blowup, options, tags_str);
               return 1;
            }
         }
         client = select_client_resource(ua, JT_BACKUP_RESTORE);
         if (client) {
            do_client_setdebug(ua, client, level, trace_flag,
               hangup, blowup, options, tags_str);
            return 1;
         }
      }

      if (strcasecmp(ua->argk[i], NT_("store")) == 0 ||
          strcasecmp(ua->argk[i], NT_("storage")) == 0 ||
          strcasecmp(ua->argk[i], NT_("sd")) == 0) {
         store = NULL;
         if (ua->argv[i]) {
            store = GetStoreResWithName(ua->argv[i]);
            if (store) {
               do_storage_setdebug(ua, store, level, trace_flag,
                  hangup, blowup, options, tags_str);
               return 1;
            }
         }
         store = get_storage_resource(ua, false/*no default*/, true/*unique*/);
         if (store) {
            do_storage_setdebug(ua, store, level, trace_flag,
               hangup, blowup, options, tags_str);
            return 1;
         }
      }
   }
   /*
    * We didn't find an appropriate keyword above, so
    * prompt the user.
    */
   start_prompt(ua, _("Available daemons are: \n"));
   add_prompt(ua, _("Director"));
   add_prompt(ua, _("Storage"));
   add_prompt(ua, _("Client"));
   add_prompt(ua, _("All"));
   switch(do_prompt(ua, "", _("Select daemon type to set debug level"), NULL, 0)) {
   case 0:                         /* Director */
      do_dir_setdebug(ua, level, trace_flag, options, tags);
      break;
   case 1:
      store = get_storage_resource(ua, false/*no default*/, true/*unique*/);
      if (store) {
         do_storage_setdebug(ua, store, level, trace_flag, hangup, blowup,
            options, tags_str);
      }
      break;
   case 2:
      client = select_client_resource(ua, JT_BACKUP_RESTORE);
      if (client) {
         do_client_setdebug(ua, client, level, trace_flag, hangup, blowup,
            options, tags_str);
      }
      break;
   case 3:
      do_all_setdebug(ua, level, trace_flag, hangup, blowup, options, tags_str);
      break;
   default:
      break;
   }
   return 1;
}

/*
 * Turn debug tracing to file on/off
 */
static int trace_cmd(UAContext *ua, const char *cmd)
{
   char *onoff;

   if (ua->argc != 2) {
      if (!get_cmd(ua, _("Turn on or off? "))) {
         return 1;
      }
      onoff = ua->cmd;
   } else {
      onoff = ua->argk[1];
   }

   set_trace((strcasecmp(onoff, NT_("off")) == 0) ? false : true);
   return 1;
}

static int var_cmd(UAContext *ua, const char *cmd)
{
   POOLMEM *val = get_pool_memory(PM_FNAME);
   char *var;

   if (!open_client_db(ua)) {
      return 1;
   }
   for (var=ua->cmd; *var != ' '; ) {    /* skip command */
      var++;
   }
   while (*var == ' ') {                 /* skip spaces */
      var++;
   }
   Dmsg1(100, "Var=%s:\n", var);
   variable_expansion(ua->jcr, var, &val);
   ua->send_msg("%s\n", val);
   free_pool_memory(val);
   return 1;
}

static int estimate_cmd(UAContext *ua, const char *cmd)
{
   JOB *job = NULL;
   CLIENT *client = NULL;
   FILESET *fileset = NULL;
   POOL_MEM buf;
   int listing = 0;
   char since[MAXSTRING];
   JCR *jcr = ua->jcr;
   int accurate=-1;

   jcr->setJobType(JT_BACKUP);
   jcr->start_time = time(NULL);
   jcr->setJobLevel(L_FULL);

   for (int i=1; i<ua->argc; i++) {
      if (strcasecmp(ua->argk[i], NT_("client")) == 0 ||
          strcasecmp(ua->argk[i], NT_("fd")) == 0) {
         if (ua->argv[i]) {
            client = GetClientResWithName(ua->argv[i]);
            if (!client) {
               ua->error_msg(_("Client \"%s\" not found.\n"), ua->argv[i]);
               return 1;
            }
            if (!acl_access_client_ok(ua, client->name(), JT_BACKUP)) {
               ua->error_msg(_("No authorization for Client \"%s\"\n"), client->name());
               return 1;
            }
            continue;
         } else {
            ua->error_msg(_("Client name missing.\n"));
            return 1;
         }
      }
      if (strcasecmp(ua->argk[i], NT_("job")) == 0) {
         if (ua->argv[i]) {
            job = GetJobResWithName(ua->argv[i]);
            if (!job) {
               ua->error_msg(_("Job \"%s\" not found.\n"), ua->argv[i]);
               return 1;
            }
            if (!acl_access_ok(ua, Job_ACL, job->name())) {
               ua->error_msg(_("No authorization for Job \"%s\"\n"), job->name());
               return 1;
            }
            continue;
         } else {
            ua->error_msg(_("Job name missing.\n"));
            return 1;
         }

      }
      if (strcasecmp(ua->argk[i], NT_("fileset")) == 0) {
         if (ua->argv[i]) {
            fileset = GetFileSetResWithName(ua->argv[i]);
            if (!fileset) {
               ua->error_msg(_("Fileset \"%s\" not found.\n"), ua->argv[i]);
               return 1;
            }
            if (!acl_access_ok(ua, FileSet_ACL, fileset->name())) {
               ua->error_msg(_("No authorization for FileSet \"%s\"\n"), fileset->name());
               return 1;
            }
            continue;
         } else {
            ua->error_msg(_("Fileset name missing.\n"));
            return 1;
         }
      }
      if (strcasecmp(ua->argk[i], NT_("listing")) == 0) {
         listing = 1;
         continue;
      }
      if (strcasecmp(ua->argk[i], NT_("level")) == 0) {
         if (ua->argv[i]) {
            if (!get_level_from_name(ua->jcr, ua->argv[i])) {
               ua->error_msg(_("Level \"%s\" not valid.\n"), ua->argv[i]);
               return 1;
            }
            continue;
         } else {
            ua->error_msg(_("Level value missing.\n"));
            return 1;
         }
      }
      if (strcasecmp(ua->argk[i], NT_("accurate")) == 0) {
         if (ua->argv[i]) {
            if (!is_yesno(ua->argv[i], &accurate)) {
               ua->error_msg(_("Invalid value for accurate. "
                               "It must be yes or no.\n"));
               return 1;
            }
            continue;
         } else {
            ua->error_msg(_("Accurate value missing.\n"));
            return 1;
         }
      }
   }
   if (!job && !(client && fileset)) {
      if (!(job = select_job_resource(ua))) {
         return 1;
      }
   }
   if (!job) {
      job = GetJobResWithName(ua->argk[1]);
      if (!job) {
         ua->error_msg(_("No job specified.\n"));
         return 1;
      }
      if (!acl_access_ok(ua, Job_ACL, job->name())) {
         ua->error_msg(_("No authorization for Job \"%s\"\n"), job->name());
         return 1;
      }
   }
   jcr->job = job;
   if (!client) {
      client = job->client;
   }
   if (!fileset) {
      fileset = job->fileset;
   }
   jcr->client = client;
   jcr->fileset = fileset;
   close_db(ua);
   if (job->pool->catalog) {
      ua->catalog = job->pool->catalog;
   } else {
      ua->catalog = client->catalog;
   }

   if (!open_db(ua)) {
      return 1;
   }

   init_jcr_job_record(jcr);

   if (!get_or_create_client_record(jcr)) {
      return 1;
   }
   if (!get_or_create_fileset_record(jcr)) {
      return 1;
   }

   get_level_since_time(ua->jcr, since, sizeof(since));

   ua->send_msg(_("Connecting to Client %s at %s:%d\n"),
                jcr->client->name(), jcr->client->address(buf.addr()), jcr->client->FDport);
   if (!connect_to_file_daemon(jcr, 1, 15, 0)) {
      ua->error_msg(_("Failed to connect to Client.\n"));
      return 1;
   }

   /* The level string change if accurate mode is enabled */
   if (accurate >= 0) {
      jcr->accurate = accurate;
   } else {
      jcr->accurate = job->accurate;
   }

   if (!send_level_command(jcr)) {
      goto bail_out;
   }

   if (!send_include_list(jcr)) {
      ua->error_msg(_("Error sending include list.\n"));
      goto bail_out;
   }

   if (!send_exclude_list(jcr)) {
      ua->error_msg(_("Error sending exclude list.\n"));
      goto bail_out;
   }

   /*
    * If the job is in accurate mode, we send the list of
    * all files to FD.
    */
   Dmsg1(40, "estimate accurate=%d\n", jcr->accurate);
   if (!send_accurate_current_files(jcr)) {
      goto bail_out;
   }

   jcr->file_bsock->fsend("estimate listing=%d\n", listing);
   while (jcr->file_bsock->recv() >= 0) {
      ua->send_msg("%s", jcr->file_bsock->msg);
   }

bail_out:
   if (jcr->file_bsock) {
      jcr->file_bsock->signal(BNET_TERMINATE);
      free_bsock(ua->jcr->file_bsock);
   }
   return 1;
}

/*
 * print time
 */
static int time_cmd(UAContext *ua, const char *cmd)
{
   char sdt[50];
   time_t ttime = time(NULL);
   struct tm tm;
   (void)localtime_r(&ttime, &tm);
   strftime(sdt, sizeof(sdt), "%a %d-%b-%Y %H:%M:%S", &tm);
   ua->send_msg("%s\n", sdt);
   return 1;
}

/*
 * reload the conf file
 */
extern "C" void reload_config(int sig);

static int reload_cmd(UAContext *ua, const char *cmd)
{
   reload_config(1);
   return 1;
}

/*
 * Delete Pool records (should purge Media with it).
 *
 *  delete pool=<pool-name>
 *  delete volume pool=<pool-name> volume=<name>
 *  delete jobid=xxx
 */
static int delete_cmd(UAContext *ua, const char *cmd)
{
   static const char *keywords[] = {
      NT_("volume"),
      NT_("pool"),
      NT_("jobid"),
      NT_("snapshot"),
      NT_("client"),
      NULL};

   /* Deleting large jobs can take time! */
   if (!open_new_client_db(ua)) {
      return 1;
   }

   switch (find_arg_keyword(ua, keywords)) {
   case 0:
      delete_volume(ua);
      return 1;
   case 1:
      delete_pool(ua);
      return 1;
   case 2:
      int i;
      while ((i=find_arg(ua, "jobid")) > 0) {
         delete_job(ua);
         *ua->argk[i] = 0;         /* zap keyword already visited */
      }
      return 1;
   case 3:
      delete_snapshot(ua);
      return 1;
   case 4:
      delete_client(ua);
      return 1;
   default:
      break;
   }

   ua->warning_msg(_(
"In general it is not a good idea to delete either a\n"
"Pool or a Volume since they may contain data.\n\n"));

   switch (do_keyword_prompt(ua, _("Choose catalog item to delete"), keywords)) {
   case 0:
      delete_volume(ua);
      break;
   case 1:
      delete_pool(ua);
      break;
   case 2:
      delete_job(ua);
      return 1;
   case 3:
      delete_snapshot(ua);
      return 1;
   case 4:
      delete_client(ua);
      return 1;
   default:
      ua->warning_msg(_("Nothing done.\n"));
      break;
   }
   return 1;
}

/*
 * delete_job has been modified to parse JobID lists like the
 * following:
 * delete JobID=3,4,6,7-11,14
 *
 * Thanks to Phil Stracchino for the above addition.
 */
static void delete_job(UAContext *ua)
{
   int JobId;               /* not JobId_t because it's unsigned and not compatible with sellist */
   char buf[256];
   sellist sl;

   int i = find_arg_with_value(ua, NT_("jobid"));
   if (i >= 0) {
      if (!sl.set_string(ua->argv[i], true)) {
         ua->warning_msg("%s", sl.get_errmsg());
         return;
      }

      if (sl.size() > 25 && (find_arg(ua, "yes") < 0)) {
         bsnprintf(buf, sizeof(buf),
                   _("Are you sure you want to delete %d JobIds ? (yes/no): "), sl.size());
         if (!get_yesno(ua, buf) || ua->pint32_val==0) {
            return;
         }
      }

      foreach_sellist(JobId, &sl) {
         do_job_delete(ua, JobId);
      }

   } else if (!get_pint(ua, _("Enter JobId to delete: "))) {
      return;

   } else {
      JobId = ua->int64_val;
      do_job_delete(ua, JobId);
   }
}

/*
 * do_job_delete now performs the actual delete operation atomically
 */
static void do_job_delete(UAContext *ua, JobId_t JobId)
{
   char ed1[50];

   edit_int64(JobId, ed1);
   purge_jobs_from_catalog(ua, ed1);
   ua->send_msg(_("JobId=%s and associated records deleted from the catalog.\n"), ed1);
}

/*
 * Delete media records from database -- dangerous
 */
static int delete_volume(UAContext *ua)
{
   MEDIA_DBR mr;
   char buf[1000];
   db_list_ctx lst;

   if (!select_media_dbr(ua, &mr)) {
      return 1;
   }
   ua->warning_msg(_("\nThis command will delete volume %s\n"
      "and all Jobs saved on that volume from the Catalog\n"),
      mr.VolumeName);

   if (find_arg(ua, "yes") >= 0) {
      ua->pint32_val = 1; /* Have "yes" on command line already" */
   } else {
      bsnprintf(buf, sizeof(buf), _("Are you sure you want to delete Volume \"%s\"? (yes/no): "),
         mr.VolumeName);
      if (!get_yesno(ua, buf)) {
         return 1;
      }
   }
   if (!ua->pint32_val) {
      return 1;
   }

   /* If not purged, do it */
   if (strcmp(mr.VolStatus, "Purged") != 0) {
      if (!db_get_volume_jobids(ua->jcr, ua->db, &mr, &lst)) {
         ua->error_msg(_("Can't list jobs on this volume\n"));
         return 1;
      }
      if (lst.count) {
         purge_jobs_from_catalog(ua, lst.list);
      }
   }

   db_delete_media_record(ua->jcr, ua->db, &mr);
   return 1;
}

/*
 * Delete a pool record from the database -- dangerous
 * TODO: Check if the resource is still defined?
 */
static int delete_pool(UAContext *ua)
{
   POOL_DBR  pr;
   char buf[200];

   memset(&pr, 0, sizeof(pr));

   if (!get_pool_dbr(ua, &pr)) {
      return 1;
   }
   bsnprintf(buf, sizeof(buf), _("Are you sure you want to delete Pool \"%s\"? (yes/no): "),
      pr.Name);
   if (!get_yesno(ua, buf)) {
      return 1;
   }
   if (ua->pint32_val) {
      db_delete_pool_record(ua->jcr, ua->db, &pr);
   }
   return 1;
}

/*
 * Delete a client record from the database
 */
static int delete_client(UAContext *ua)
{
   CLIENT *client;
   CLIENT_DBR  cr;
   char buf[200];
   db_list_ctx lst;

   memset(&cr, 0, sizeof(cr));

   if (!get_client_dbr(ua, &cr, 0)) {
      return 1;
   }

   client = (CLIENT*) GetResWithName(R_CLIENT, cr.Name);

   if (client) {
      ua->error_msg(_("Unable to delete Client \"%s\", the resource is still defined in the configuration.\n"), cr.Name);
      return 1;
   }

   if (!db_get_client_jobids(ua->jcr, ua->db, &cr, &lst)) {
      ua->error_msg(_("Can't list jobs on this client\n"));
      return 1;
   }

   if (find_arg(ua, "yes") > 0) {
      ua->pint32_val = 1;

   } else {
      if (lst.count  == 0) {
         bsnprintf(buf, sizeof(buf), _("Are you sure you want to delete Client \"%s\? (yes/no): "), cr.Name);
      } else {
         bsnprintf(buf, sizeof(buf), _("Are you sure you want to delete Client \"%s\" and purge %d job(s)? (yes/no): "), cr.Name, lst.count);
      }
      if (!get_yesno(ua, buf)) {
         return 1;
      }
   }

   if (ua->pint32_val) {
      if (lst.count) {
         ua->send_msg(_("Purging %d job(s).\n"), lst.count);
         purge_jobs_from_catalog(ua, lst.list);
      }
      ua->send_msg(_("Deleting client \"%s\".\n"), cr.Name);
      db_delete_client_record(ua->jcr, ua->db, &cr);
   }
   return 1;
}

int memory_cmd(UAContext *ua, const char *cmd)
{
   garbage_collect_memory();
   list_dir_status_header(ua);
   sm_dump(false, true);
   return 1;
}

static void do_storage_cmd(UAContext *ua, const char *command)
{
   USTORE store;
   BSOCK *sd;
   JCR *jcr = ua->jcr;
   char dev_name[MAX_NAME_LENGTH];
   int drive, i;
   int slot;

   if (!open_client_db(ua)) {
      return;
   }
   Dmsg2(120, "%s: %s\n", command, ua->UA_sock->msg);

   store.store = get_storage_resource(ua, true/*arg is storage*/);
   if (!store.store) {
      return;
   }
   pm_strcpy(store.store_source, _("unknown source"));
   set_wstorage(jcr, &store);
   drive = get_storage_drive(ua, store.store);
   /* For the disable/enable/unmount commands, the slot is not mandatory */
   if (strcasecmp(command, "disable") == 0 ||
        strcasecmp(command, "enable") == 0 ||
        strcasecmp(command, "unmount")  == 0) {
      slot = 0;
   } else {
      slot = get_storage_slot(ua, store.store);
   }
   /* Users may set a device name directly on the command line */
   if ((i = find_arg_with_value(ua, "device")) > 0) {
      POOLMEM *errmsg = get_pool_memory(PM_NAME);
      if (!is_name_valid(ua->argv[i], &errmsg)) {
         ua->error_msg(_("Invalid device name. %s"), errmsg);
         free_pool_memory(errmsg);
         return;
      }
      free_pool_memory(errmsg);
      bstrncpy(dev_name, ua->argv[i], sizeof(dev_name));

   } else {                     /* We take the default device name */
      bstrncpy(dev_name, store.store->dev_name(), sizeof(dev_name));
   }

   Dmsg3(120, "Found storage, MediaType=%s DevName=%s drive=%d\n",
      store.store->media_type, store.store->dev_name(), drive);
   Dmsg4(120, "Cmd: %s %s drive=%d slot=%d\n", command, dev_name, drive, slot);

   if (!connect_to_storage_daemon(jcr, 10, SDConnectTimeout, 1)) {
      ua->error_msg(_("Failed to connect to Storage daemon.\n"));
      return;
   }
   sd = jcr->store_bsock;
   bash_spaces(dev_name);
   sd->fsend("%s %s drive=%d slot=%d\n", command, dev_name, drive, slot);
   while (sd->recv() >= 0) {
      ua->send_msg("%s", sd->msg);
   }
   sd->signal(BNET_TERMINATE);
   free_bsock(ua->jcr->store_bsock);
}

/*
 * mount [storage=<name>] [drive=nn] [slot=mm]
 */
static int mount_cmd(UAContext *ua, const char *cmd)
{
   do_storage_cmd(ua, "mount")  ;          /* mount */
   return 1;
}


/*
 * unmount [storage=<name>] [drive=nn]
 */
static int unmount_cmd(UAContext *ua, const char *cmd)
{
   do_storage_cmd(ua, "unmount");          /* unmount */
   return 1;
}


/*
 * release [storage=<name>] [drive=nn]
 */
static int release_cmd(UAContext *ua, const char *cmd)
{
   do_storage_cmd(ua, "release");          /* release */
   return 1;
}

/*
 * cloud functions, like to upload cached parts to cloud.
 */
int cloud_volumes_cmd(UAContext *ua, const char *cmd, const char *mode)
{
   int drive = -1;
   int nb = 0;
   uint32_t *results = NULL;
   MEDIA_DBR mr;
   POOL_DBR pr;
   BSOCK *sd = NULL;
   char storage[MAX_NAME_LENGTH];
   const char *action = mode;
   memset(&pr, 0, sizeof(pr));

   /*
    * Look for all volumes that are enabled and
    *  have more the 200 bytes.
    */
   mr.Enabled = 1;
   mr.Recycle = -1;             /* All Recycle status */
   if (strcmp("prunecache", mode) == 0) {
      mr.CacheRetention = 1;
      action = "truncate cache";
   }

   if (!scan_storage_cmd(ua, cmd, false, /* fromallpool*/
                         &drive, &mr, &pr, NULL, storage,
                         &nb, &results))
   {
      goto bail_out;
   }

   if ((sd=open_sd_bsock(ua)) == NULL) {
      Dmsg0(100, "Can't open connection to sd\n");
      goto bail_out;
   }

   /*
    * Loop over the candidate Volumes and upload parts
    */
   for (int i=0; i < nb; i++) {
      bool ok=false;
      mr.clear();
      mr.MediaId = results[i];
      if (!db_get_media_record(ua->jcr, ua->db, &mr)) {
         goto bail_out;
      }

      /* Protect us from spaces */
      bash_spaces(mr.VolumeName);
      bash_spaces(mr.MediaType);
      bash_spaces(pr.Name);
      bash_spaces(storage);

      sd->fsend("%s Storage=%s Volume=%s PoolName=%s MediaType=%s "
                "Slot=%d drive=%d CacheRetention=%lld\n",
                action, storage, mr.VolumeName, pr.Name, mr.MediaType,
                mr.Slot, drive, mr.CacheRetention);

      unbash_spaces(mr.VolumeName);
      unbash_spaces(mr.MediaType);
      unbash_spaces(pr.Name);
      unbash_spaces(storage);

      /* Check for valid response */
      while (bget_dirmsg(sd) >= 0) {
         if (strncmp(sd->msg, "3000 OK truncate cache", 22) == 0) {
            ua->send_msg("%s", sd->msg);
            ok = true;

         } else if (strncmp(sd->msg, "3000 OK", 7) == 0) {
            ua->send_msg(_("The volume \"%s\" has been uploaded\n"), mr.VolumeName);
            ok = true;


         } else if (strncmp(sd->msg, "39", 2) == 0) {
            ua->warning_msg("%s", sd->msg);

         } else {
            ua->send_msg("%s", sd->msg);
         }
      }
      if (!ok) {
         ua->warning_msg(_("Unable to %s for volume \"%s\"\n"), action, mr.VolumeName);
      }
   }

bail_out:
   close_db(ua);
   close_sd_bsock(ua);
   ua->jcr->wstore = NULL;
   if (results) {
      free(results);
   }

   return 1;
}

/* List volumes in the cloud */
/* TODO: Update the code for .api 2 and llist */
static int cloud_list_cmd(UAContext *ua, const char *cmd)
{
   int drive = -1;
   int64_t size, mtime;
   STORE *store = NULL;
   MEDIA_DBR mr;
   POOL_DBR pr;
   BSOCK *sd = NULL;
   char storage[MAX_NAME_LENGTH];
   char ed1[50], ed2[50];
   bool first=true;
   uint32_t maxpart=0, part;
   uint64_t maxpart_size=0;
   memset(&pr, 0, sizeof(pr));
   memset(&mr, 0, sizeof(mr));

   /* Look at arguments */
   for (int i=1; i<ua->argc; i++) {
      if (strcasecmp(ua->argk[i], NT_("volume")) == 0
          && is_name_valid(ua->argv[i], NULL)) {
         bstrncpy(mr.VolumeName, ua->argv[i], sizeof(mr.VolumeName));

      } else if (strcasecmp(ua->argk[i], NT_("drive")) == 0 && ua->argv[i]) {
         drive = atoi(ua->argv[i]);
      }
   }

   if (!open_client_db(ua)) {
      goto bail_out;
   }

   /* Choose storage */
   ua->jcr->wstore = store = get_storage_resource(ua, false);
   if (!store) {
      goto bail_out;
   }
   bstrncpy(storage, store->dev_name(), sizeof(storage));
   bstrncpy(mr.MediaType, store->media_type, sizeof(mr.MediaType));

   if ((sd=open_sd_bsock(ua)) == NULL) {
      Dmsg0(100, "Can't open connection to SD\n");
      goto bail_out;
   }

   /* Protect us from spaces */
   bash_spaces(mr.MediaType);
   bash_spaces(storage);
   bash_spaces(mr.VolumeName);

   sd->fsend("cloudlist Storage=%s Volume=%s MediaType=%s Slot=%d drive=%d\n",
             storage, mr.VolumeName,  mr.MediaType, mr.Slot, drive);

   if (mr.VolumeName[0]) {      /* Want to list parts */
      const char *output_hformat="| %8d | %12sB | %20s |\n";
      uint64_t volsize=0;
      /* Check for valid response */
      while (sd->recv() >= 0) {
         if (sscanf(sd->msg, "part=%d size=%lld mtime=%lld", &part, &size, &mtime) != 3) {
            if (sd->msg[0] == '3') {
               ua->send_msg("%s", sd->msg);
            }
            continue;
         }
         /* Print information */
         if (first) {
            ua->send_msg(_("+----------+---------------+----------------------+\n"));
            ua->send_msg(_("|   Part   |     Size      |   MTime              |\n"));
            ua->send_msg(_("+----------+---------------+----------------------+\n"));
            first=false;
         }
         if (part > maxpart) {
            maxpart = part;
            maxpart_size = size;
         }
         volsize += size;
         ua->send_msg(output_hformat, part, edit_uint64_with_suffix(size, ed1), bstrftimes(ed2, sizeof(ed2), mtime));
      }
      if (!first) {
         ua->send_msg(_("+----------+---------------+----------------------+\n"));
      }
      /* TODO: See if we fix the catalog record directly */
      if (db_get_media_record(ua->jcr, ua->db, &mr)) {
         POOL_MEM errmsg, tmpmsg;
         if (mr.LastPartBytes != maxpart_size) {
            Mmsg(tmpmsg, "Error on volume \"%s\". Catalog LastPartBytes mismatch %lld != %lld\n",
                 mr.VolumeName, mr.LastPartBytes, maxpart_size);
            pm_strcpy(errmsg, tmpmsg.c_str());
         }
         if (mr.VolCloudParts != maxpart) {
            Mmsg(tmpmsg, "Error on volume \"%s\". Catalog VolCloudParts mismatch %ld != %ld\n",
                 mr.VolumeName, mr.VolCloudParts, maxpart);
            pm_strcpy(errmsg, tmpmsg.c_str());
         }
         if (strlen(errmsg.c_str()) > 0) {
            ua->error_msg("\n%s", errmsg.c_str());
         }
      }
   } else {                     /* TODO: Get the last part if possible? */
      const char *output_hformat="| %18s | %9s | %20s | %20s | %12sB |\n";

      /* Check for valid response */
      while (sd->recv() >= 0) {
         if (sscanf(sd->msg, "volume=%127s", mr.VolumeName) != 1) {
            if (sd->msg[0] == '3') {
               ua->send_msg("%s", sd->msg);
            }
            continue;
         }
         unbash_spaces(mr.VolumeName);

         mr.MediaId = 0;

         if (mr.VolumeName[0] && db_get_media_record(ua->jcr, ua->db, &mr)) {
            memset(&pr, 0, sizeof(POOL_DBR));
            pr.PoolId = mr.PoolId;
            if (!db_get_pool_record(ua->jcr, ua->db, &pr)) {
               strcpy(pr.Name, "?");
            }

            if (first) {
               ua->send_msg(_("+--------------------+-----------+----------------------+----------------------+---------------+\n"));
               ua->send_msg(_("|    Volume Name     |   Status  |     Media Type       |       Pool           |    VolBytes   |\n"));
               ua->send_msg(_("+--------------------+-----------+----------------------+----------------------+---------------+\n"));
               first=false;
            }
            /* Print information */
            ua->send_msg(output_hformat, mr.VolumeName, mr.VolStatus, mr.MediaType, pr.Name,
                         edit_uint64_with_suffix(mr.VolBytes, ed1));
         }
      }
      if (!first) {
         ua->send_msg(_("+--------------------+-----------+----------------------+----------------------+---------------+\n"));
      }
   }

bail_out:
   close_db(ua);
   close_sd_bsock(ua);
   ua->jcr->wstore = NULL;
   return 1;
}

/* Ask client to create/prune/delete a snapshot via the command line */
static int cloud_cmd(UAContext *ua, const char *cmd)
{
   for (int i=0; i<ua->argc; i++) {
      if (strcasecmp(ua->argk[i], NT_("upload")) == 0) {
         return cloud_volumes_cmd(ua, cmd, "upload");

      } else if (strcasecmp(ua->argk[i], NT_("list")) == 0) {
         return cloud_list_cmd(ua, cmd);

      } else if (strcasecmp(ua->argk[i], NT_("truncate")) == 0) {
         return cloud_volumes_cmd(ua, cmd, "truncate cache");

      } else if (strcasecmp(ua->argk[i], NT_("status")) == 0) {

      } else if (strcasecmp(ua->argk[i], NT_("prune")) == 0) {
         return cloud_volumes_cmd(ua, cmd, "prunecache");

      } else {
         continue;
      }
   }

   for ( ;; ) {

      start_prompt(ua, _("Cloud choice: \n"));
      add_prompt(ua, _("List Cloud Volumes in the Cloud"));
      add_prompt(ua, _("Upload a Volume to the Cloud"));
      add_prompt(ua, _("Prune the Cloud Cache"));
      add_prompt(ua, _("Truncate a Volume Cache"));
      add_prompt(ua, _("Done"));

      switch(do_prompt(ua, "", _("Select action to perform on Cloud"), NULL, 0)) {
      case 0:                   /* list cloud */
         cloud_list_cmd(ua, cmd);
         break;
      case 1:                   /* upload */
         cloud_volumes_cmd(ua, cmd, "upload");
         break;
      case 2:                   /* Prune cache */
         cloud_volumes_cmd(ua, cmd, "prunecache");
         break;
      case 3:                   /* Truncate cache */
         cloud_volumes_cmd(ua, cmd, "truncate cache");
         break;
      default:
         ua->info_msg(_("Selection terminated.\n"));
         return 1;
      }
   }
   return 1;
}

/*
 * Switch databases
 *   use catalog=<name>
 */
static int use_cmd(UAContext *ua, const char *cmd)
{
   CAT *oldcatalog, *catalog;


   close_db(ua);                      /* close any previously open db */
   oldcatalog = ua->catalog;

   if (!(catalog = get_catalog_resource(ua))) {
      ua->catalog = oldcatalog;
   } else {
      ua->catalog = catalog;
   }
   if (open_db(ua)) {
      ua->send_msg(_("Using Catalog name=%s DB=%s\n"),
         ua->catalog->name(), ua->catalog->db_name);
   }
   return 1;
}

int quit_cmd(UAContext *ua, const char *cmd)
{
   ua->quit = true;
   return 1;
}

/* Handler to get job status */
static int status_handler(void *ctx, int num_fields, char **row)
{
   char *val = (char *)ctx;

   if (row[0]) {
      *val = row[0][0];
   } else {
      *val = '?';               /* Unknown by default */
   }

   return 0;
}

/*
 * Wait until no job is running
 */
int wait_cmd(UAContext *ua, const char *cmd)
{
   JCR *jcr;
   int i;
   time_t stop_time = 0;

   /*
    * no args
    * Wait until no job is running
    */
   if (ua->argc == 1) {
      bmicrosleep(0, 200000);            /* let job actually start */
      for (bool running=true; running; ) {
         running = false;
         foreach_jcr(jcr) {
            if (!jcr->is_internal_job()) {
               running = true;
               break;
            }
         }
         endeach_jcr(jcr);

         if (running) {
            bmicrosleep(1, 0);
         }
      }
      return 1;
   }

   i = find_arg_with_value(ua, NT_("timeout"));
   if (i > 0 && ua->argv[i]) {
      stop_time = time(NULL) + str_to_int64(ua->argv[i]);
   }

   /* we have jobid, jobname or ujobid argument */

   uint32_t jobid = 0 ;

   if (!open_client_db(ua)) {
      ua->error_msg(_("ERR: Can't open db\n")) ;
      return 1;
   }

   for (int i=1; i<ua->argc; i++) {
      if (strcasecmp(ua->argk[i], "jobid") == 0) {
         if (!ua->argv[i]) {
            break;
         }
         jobid = str_to_int64(ua->argv[i]);
         break;
      } else if (strcasecmp(ua->argk[i], "jobname") == 0 ||
                 strcasecmp(ua->argk[i], "job") == 0) {
         if (!ua->argv[i]) {
            break;
         }
         jcr=get_jcr_by_partial_name(ua->argv[i]) ;
         if (jcr) {
            jobid = jcr->JobId ;
            free_jcr(jcr);
         }
         break;
      } else if (strcasecmp(ua->argk[i], "ujobid") == 0) {
         if (!ua->argv[i]) {
            break;
         }
         jcr=get_jcr_by_full_name(ua->argv[i]) ;
         if (jcr) {
            jobid = jcr->JobId ;
            free_jcr(jcr);
         }
         break;
      /* Wait for a mount request */
      } else if (strcasecmp(ua->argk[i], "mount") == 0) {
         for (bool waiting=false; !waiting; ) {
            foreach_jcr(jcr) {
               if (!jcr->is_internal_job() &&
                   (jcr->JobStatus == JS_WaitMedia || jcr->JobStatus == JS_WaitMount ||
                    jcr->SDJobStatus == JS_WaitMedia || jcr->SDJobStatus == JS_WaitMount))
               {
                  waiting = true;
                  break;
               }
            }
            endeach_jcr(jcr);
            if (waiting) {
               break;
            }
            if (stop_time && (time(NULL) >= stop_time)) {
               ua->warning_msg(_("Wait on mount timed out\n"));
               return 1;
            }
            bmicrosleep(1, 0);
         }
         return 1;
      }
   }

   if (jobid == 0) {
      ua->error_msg(_("ERR: Job was not found\n"));
      return 1 ;
   }

   /*
    * We wait the end of a specific job
    */

   bmicrosleep(0, 200000);            /* let job actually start */
   for (bool running=true; running; ) {
      running = false;

      jcr=get_jcr_by_id(jobid) ;

      if (jcr) {
         running = true ;
         free_jcr(jcr);
      }

      if (running) {
         bmicrosleep(1, 0);
      }
   }

   /*
    * We have to get JobStatus
    */

   int status ;
   char jobstatus = '?';        /* Unknown by default */
   char buf[256] ;

   bsnprintf(buf, sizeof(buf),
             "SELECT JobStatus FROM Job WHERE JobId='%i'", jobid);


   db_sql_query(ua->db, buf, status_handler, (void *)&jobstatus);

   switch (jobstatus) {
   case JS_Error:
      status = 1 ;         /* Warning */
      break;

   case JS_Incomplete:
   case JS_FatalError:
   case JS_ErrorTerminated:
   case JS_Canceled:
      status = 2 ;         /* Critical */
      break;

   case JS_Warnings:
   case JS_Terminated:
      status = 0 ;         /* Ok */
      break;

   default:
      status = 3 ;         /* Unknown */
      break;
   }

   ua->send_msg("JobId=%i\n", jobid) ;
   ua->send_msg("JobStatus=%s (%c)\n",
                job_status_to_str(jobstatus, 0),
                jobstatus) ;

   if (ua->gui || ua->api) {
      ua->send_msg("ExitStatus=%i\n", status) ;
   }

   return 1;
}


static int help_cmd(UAContext *ua, const char *cmd)
{
   int i;
   ua->send_msg(_("  Command       Description\n  =======       ===========\n"));
   for (i=0; i<comsize; i++) {
      if (ua->argc == 2) {
         if (!strcasecmp(ua->argk[1], commands[i].key)) {
            ua->send_msg(_("  %-13s %s\n\nArguments:\n\t%s\n"), commands[i].key,
                         commands[i].help, commands[i].usage);
            break;
         }
      } else {
         ua->send_msg(_("  %-13s %s\n"), commands[i].key, commands[i].help);
      }
   }
   if (i == comsize && ua->argc == 2) {
      ua->send_msg(_("\nCan't find %s command.\n\n"), ua->argk[1]);
   }
   ua->send_msg(_("\nWhen at a prompt, entering a period cancels the command.\n\n"));
   return 1;
}

int qhelp_cmd(UAContext *ua, const char *cmd)
{
   int i,j;
   /* Want to display only commands */
   j = find_arg(ua, NT_("all"));
   if (j >= 0) {
      for (i=0; i<comsize; i++) {
         ua->send_msg("%s\n", commands[i].key);
      }
      return 1;
   }
   /* Want to display a specific help section */
   j = find_arg_with_value(ua, NT_("item"));
   if (j >= 0 && ua->argk[j]) {
      for (i=0; i<comsize; i++) {
         if (bstrcmp(commands[i].key, ua->argv[j])) {
            ua->send_msg("%s\n", commands[i].usage);
            break;
         }
      }
      return 1;
   }
   /* Want to display everything */
   for (i=0; i<comsize; i++) {
      ua->send_msg("%s %s -- %s\n", commands[i].key, commands[i].help, commands[i].usage);
   }
   return 1;
}

#if 1
static int version_cmd(UAContext *ua, const char *cmd)
{
   ua->send_msg(_("%s Version: %s (%s) %s %s %s %s\n"), my_name, VERSION, BDATE,
                HOST_OS, DISTNAME, DISTVER, NPRTB(director->verid));
   return 1;
}
#else
/*
 *  Test code -- turned on only for debug testing
 */
static int version_cmd(UAContext *ua, const char *cmd)
{
   dbid_list ids;
   POOL_MEM query(PM_MESSAGE);
   open_db(ua);
   Mmsg(query, "select MediaId from Media,Pool where Pool.PoolId=Media.PoolId and Pool.Name='Full'");
   db_get_query_dbids(ua->jcr, ua->db, query, ids);
   ua->send_msg("num_ids=%d max_ids=%d tot_ids=%d\n", ids.num_ids, ids.max_ids, ids.tot_ids);
   for (int i=0; i < ids.num_ids; i++) {
      ua->send_msg("id=%d\n", ids.DBId[i]);
   }
   close_db(ua);
   return 1;
}
#endif

/*
 * This call uses open_client_db() and force a
 * new dedicated connection to the catalog
 */
bool open_new_client_db(UAContext *ua)
{
   bool ret;

   /* Force a new dedicated connection */
   ua->force_mult_db_connections = true;
   ret = open_client_db(ua);
   ua->force_mult_db_connections = false;

   return ret;
}

/*
 * This call explicitly checks for a catalog=xxx and
 *  if given, opens that catalog.  It also checks for
 *  client=xxx and if found, opens the catalog
 *  corresponding to that client. If we still don't
 *  have a catalog, look for a Job keyword and get the
 *  catalog from its client record.
 */
bool open_client_db(UAContext *ua)
{
   int i;
   CAT *catalog;
   CLIENT *client;
   JOB *job;

   /* Try for catalog keyword */
   i = find_arg_with_value(ua, NT_("catalog"));
   if (i >= 0) {
      if (!acl_access_ok(ua, Catalog_ACL, ua->argv[i])) {
         ua->error_msg(_("No authorization for Catalog \"%s\"\n"), ua->argv[i]);
         return false;
      }
      catalog = GetCatalogResWithName(ua->argv[i]);
      if (catalog) {
         if (ua->catalog && ua->catalog != catalog) {
            close_db(ua);
         }
         ua->catalog = catalog;
         return open_db(ua);
      }
   }

   /* Try for client keyword */
   i = find_arg_with_value(ua, NT_("client"));
   if (i >= 0) {
      if (!acl_access_client_ok(ua, ua->argv[i], JT_BACKUP_RESTORE)) {
         ua->error_msg(_("No authorization for Client \"%s\"\n"), ua->argv[i]);
         return false;
      }
      client = GetClientResWithName(ua->argv[i]);
      if (client) {
         catalog = client->catalog;
         if (ua->catalog && ua->catalog != catalog) {
            close_db(ua);
         }
         if (!acl_access_ok(ua, Catalog_ACL, catalog->name())) {
            ua->error_msg(_("No authorization for Catalog \"%s\"\n"), catalog->name());
            return false;
         }
         ua->catalog = catalog;
         return open_db(ua);
      }
   }

   /* Try for Job keyword */
   i = find_arg_with_value(ua, NT_("job"));
   if (i >= 0) {
      if (!acl_access_ok(ua, Job_ACL, ua->argv[i])) {
         ua->error_msg(_("No authorization for Job \"%s\"\n"), ua->argv[i]);
         return false;
      }
      job = GetJobResWithName(ua->argv[i]);
      if (job) {
         catalog = job->client->catalog;
         if (ua->catalog && ua->catalog != catalog) {
            close_db(ua);
         }
         if (!acl_access_ok(ua, Catalog_ACL, catalog->name())) {
            ua->error_msg(_("No authorization for Catalog \"%s\"\n"), catalog->name());
            return false;
         }
         ua->catalog = catalog;
         return open_db(ua);
      }
   }

   return open_db(ua);
}


/*
 * Open the catalog database.
 */
bool open_db(UAContext *ua)
{
   bool mult_db_conn;

   /* With a restricted console, we can't share a SQL connection */
   if (ua->cons) {
      ua->force_mult_db_connections = true;
   }

   /* The force_mult_db_connections is telling us if we modify the
    * private or the shared link
    */
   if (ua->force_mult_db_connections) {
      ua->db = ua->private_db;

   } else {
      ua->db = ua->shared_db;
   }

   if (ua->db) {
      return true;
   }

   if (!ua->catalog) {
      ua->catalog = get_catalog_resource(ua);
      if (!ua->catalog) {
         ua->error_msg( _("Could not find a Catalog resource\n"));
         return false;
      }
   }

   /* Some modules like bvfs need their own catalog connection */
   mult_db_conn = ua->catalog->mult_db_connections;
   if (ua->force_mult_db_connections) {
      mult_db_conn = true;
   }

   ua->jcr->catalog = ua->catalog;

   Dmsg0(100, "UA Open database\n");
   ua->db = db_init_database(ua->jcr, ua->catalog->db_driver,
                             ua->catalog->db_name,
                             ua->catalog->db_user,
                             ua->catalog->db_password, ua->catalog->db_address,
                             ua->catalog->db_port, ua->catalog->db_socket,
                             ua->catalog->db_ssl_mode, ua->catalog->db_ssl_key,
                             ua->catalog->db_ssl_cert, ua->catalog->db_ssl_ca,
                             ua->catalog->db_ssl_capath, ua->catalog->db_ssl_cipher,
                             mult_db_conn, ua->catalog->disable_batch_insert);
   if (!ua->db || !db_open_database(ua->jcr, ua->db)) {
      ua->error_msg(_("Could not open catalog database \"%s\".\n"),
                 ua->catalog->db_name);
      if (ua->db) {
         ua->error_msg("%s", db_strerror(ua->db));
      }
      close_db(ua);
      return false;
   }
   ua->jcr->db = ua->db;

   /* Depending on the type of connection, we set the right variable */
   if (ua->force_mult_db_connections) {
      ua->private_db = ua->db;

   } else {
      ua->shared_db = ua->db;
   }
   /* With a restricted console, the DB backend should know restrictions about
    * Pool, Job, etc...
    */
   if (ua->cons) {
      ua->db->set_acl(ua->jcr, DB_ACL_JOB, ua->cons->ACL_lists[Job_ACL]);
      ua->db->set_acl(ua->jcr, DB_ACL_CLIENT, ua->cons->ACL_lists[Client_ACL]);
      ua->db->set_acl(ua->jcr, DB_ACL_POOL, ua->cons->ACL_lists[Pool_ACL]);
      ua->db->set_acl(ua->jcr, DB_ACL_FILESET, ua->cons->ACL_lists[FileSet_ACL]);

      /* For RestoreClient and BackupClient, we take also in account the Client list */
      ua->db->set_acl(ua->jcr, DB_ACL_RCLIENT,
                      ua->cons->ACL_lists[Client_ACL],
                      ua->cons->ACL_lists[RestoreClient_ACL]);

      ua->db->set_acl(ua->jcr, DB_ACL_BCLIENT,
                      ua->cons->ACL_lists[Client_ACL],
                      ua->cons->ACL_lists[BackupClient_ACL]);
   }
   if (!ua->api) {
      ua->send_msg(_("Using Catalog \"%s\"\n"), ua->catalog->name());
   }
   Dmsg1(150, "DB %s opened\n", ua->catalog->db_name);
   return true;
}

void close_db(UAContext *ua)
{
   if (ua->jcr) {
      ua->jcr->db = NULL;
   }

   if (ua->shared_db) {
      db_close_database(ua->jcr, ua->shared_db);
      ua->shared_db = NULL;
   }

   if (ua->private_db) {
      db_close_database(ua->jcr, ua->private_db);
      ua->private_db = NULL;
   }

   ua->db = NULL;
}
