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
 * Main program to test loading and running Bacula plugins.
 *   Destined to become Bacula pluginloader, ...
 *
 * Kern Sibbald, October 2007
 */
#include "bacula.h"
#include "dird.h"
#include "dir_plugins.h"

const int dbglvl = 50;
const char *plugin_type = "-dir.so";


/* Forward referenced functions */
static bRC baculaGetValue(bpContext *ctx, brDirVariable var, void *value);
static bRC baculaSetValue(bpContext *ctx, bwDirVariable var, void *value);
static bRC baculaRegisterEvents(bpContext *ctx, ...);
static bRC baculaJobMsg(bpContext *ctx, const char *file, int line,
  int type, utime_t mtime, const char *fmt, ...);
static bRC baculaDebugMsg(bpContext *ctx, const char *file, int line,
  int level, const char *fmt, ...);
static bool is_plugin_compatible(Plugin *plugin);


/* Bacula info */
static bDirInfo binfo = {
   sizeof(bDirFuncs),
   DIR_PLUGIN_INTERFACE_VERSION
};

/* Bacula entry points */
static bDirFuncs bfuncs = {
   sizeof(bDirFuncs),
   DIR_PLUGIN_INTERFACE_VERSION,
   baculaRegisterEvents,
   baculaGetValue,
   baculaSetValue,
   baculaJobMsg,
   baculaDebugMsg
};

/*
 * Bacula private context
 */
struct bacula_ctx {
   JCR *jcr;                             /* jcr for plugin */
   bRC  rc;                              /* last return code */
   bool disabled;                        /* set if plugin disabled */
};

static bool is_plugin_disabled(bpContext *plugin_ctx)
{
   bacula_ctx *b_ctx;
   if (!plugin_ctx) {
      return true;
   }
   b_ctx = (bacula_ctx *)plugin_ctx->bContext;
   return b_ctx->disabled;
}

#ifdef needed
static bool is_plugin_disabled(JCR *jcr)
{
   return is_plugin_disabled(jcr->plugin_ctx);
}
#endif

/*
 * Create a plugin event
 */
int generate_plugin_event(JCR *jcr, bDirEventType eventType, void *value)
{
   bpContext *plugin_ctx;
   bDirEvent event;
   Plugin *plugin;
   int i = 0;
   bRC rc = bRC_OK;

   if (!b_plugin_list || !jcr || !jcr->plugin_ctx_list) {
      return bRC_OK;                  /* Return if no plugins loaded */
   }
   if (jcr->is_job_canceled()) {
      return bRC_Cancel;
   }

   bpContext *plugin_ctx_list = (bpContext *)jcr->plugin_ctx_list;
   event.eventType = eventType;

   Dmsg2(dbglvl, "dir-plugin_ctx_list=%p JobId=%d\n", jcr->plugin_ctx_list, jcr->JobId);

   foreach_alist_index(i, plugin, b_plugin_list) {
      plugin_ctx = &plugin_ctx_list[i];
      if (is_plugin_disabled(plugin_ctx)) {
         continue;
      }
      rc = dirplug_func(plugin)->handlePluginEvent(plugin_ctx, &event, value);
      if (rc != bRC_OK) {
         break;
      }
   }

   return rc;
}

/*
 * Print to file the plugin info.
 */
void dump_dir_plugin(Plugin *plugin, FILE *fp)
{
   if (!plugin) {
      return ;
   }
   pDirInfo *info = (pDirInfo *) plugin->pinfo;
   fprintf(fp, "\tversion=%d\n", info->version);
   fprintf(fp, "\tdate=%s\n", NPRTB(info->plugin_date));
   fprintf(fp, "\tmagic=%s\n", NPRTB(info->plugin_magic));
   fprintf(fp, "\tauthor=%s\n", NPRTB(info->plugin_author));
   fprintf(fp, "\tlicence=%s\n", NPRTB(info->plugin_license));
   fprintf(fp, "\tversion=%s\n", NPRTB(info->plugin_version));
   fprintf(fp, "\tdescription=%s\n", NPRTB(info->plugin_description));
}

/**
 * This entry point is called internally by Bacula to ensure
 *  that the plugin IO calls come into this code.
 */
void load_dir_plugins(const char *plugin_dir)
{
   Plugin *plugin;
   int i;

   Dmsg0(dbglvl, "Load Director plugins\n");
   if (!plugin_dir) {
      Dmsg0(dbglvl, "No Director plugin directory!\n");
      return;
   }
   b_plugin_list = New(alist(10, not_owned_by_alist));
   if (!load_plugins((void *)&binfo, (void *)&bfuncs, plugin_dir, plugin_type,
                is_plugin_compatible)) {
      /* Either none found, or some error */
      if (b_plugin_list->size() == 0) {
         delete b_plugin_list;
         b_plugin_list = NULL;
         Dmsg0(dbglvl, "No plugins loaded\n");
         return;
      }
   }
   /*
    * Verify that the plugin is acceptable, and print information
    *  about it.
    */
   foreach_alist_index(i, plugin, b_plugin_list) {
      Jmsg(NULL, M_INFO, 0, _("Loaded plugin: %s\n"), plugin->file);
      Dmsg1(dbglvl, "Loaded plugin: %s\n", plugin->file);
   }

   Dmsg1(dbglvl, "num plugins=%d\n", b_plugin_list->size());
   dbg_plugin_add_hook(dump_dir_plugin);
}

/**
 * Check if a plugin is compatible.  Called by the load_plugin function
 *  to allow us to verify the plugin.
 */
static bool is_plugin_compatible(Plugin *plugin)
{
   pDirInfo *info = (pDirInfo *)plugin->pinfo;
   Dmsg0(50, "is_plugin_compatible called\n");
   if (chk_dbglvl(50)) {
      dump_dir_plugin(plugin, stdin);
   }
   if (strcmp(info->plugin_magic, DIR_PLUGIN_MAGIC) != 0) {
      Jmsg(NULL, M_ERROR, 0, _("Plugin magic wrong. Plugin=%s wanted=%s got=%s\n"),
           plugin->file, DIR_PLUGIN_MAGIC, info->plugin_magic);
      Dmsg3(50, "Plugin magic wrong. Plugin=%s wanted=%s got=%s\n",
           plugin->file, DIR_PLUGIN_MAGIC, info->plugin_magic);

      return false;
   }
   if (info->version != DIR_PLUGIN_INTERFACE_VERSION) {
      Jmsg(NULL, M_ERROR, 0, _("Plugin version incorrect. Plugin=%s wanted=%d got=%d\n"),
           plugin->file, DIR_PLUGIN_INTERFACE_VERSION, info->version);
      Dmsg3(50, "Plugin version incorrect. Plugin=%s wanted=%d got=%d\n",
           plugin->file, DIR_PLUGIN_INTERFACE_VERSION, info->version);
      return false;
   }
   if (strcmp(info->plugin_license, "Bacula AGPLv3") != 0 &&
       strcmp(info->plugin_license, "AGPLv3") != 0 &&
       strcmp(info->plugin_license, "Bacula") != 0) {
      Jmsg(NULL, M_ERROR, 0, _("Plugin license incompatible. Plugin=%s license=%s\n"),
           plugin->file, info->plugin_license);
      Dmsg2(50, "Plugin license incompatible. Plugin=%s license=%s\n",
           plugin->file, info->plugin_license);
      return false;
   }
   if (info->size != sizeof(pDirInfo)) {
      Jmsg(NULL, M_ERROR, 0,
           _("Plugin size incorrect. Plugin=%s wanted=%d got=%d\n"),
           plugin->file, sizeof(pDirInfo), info->size);
      return false;
   }

   return true;
}


/*
 * Create a new instance of each plugin for this Job
 */
void new_plugins(JCR *jcr)
{
   Plugin *plugin;
   int i = 0;

   Dmsg0(dbglvl, "=== enter new_plugins ===\n");
   if (!b_plugin_list) {
      Dmsg0(dbglvl, "No Director plugin list!\n");
      return;
   }
   if (jcr->is_job_canceled()) {
      return;
   }

   int num = b_plugin_list->size();

   Dmsg1(dbglvl, "dir-plugin-list size=%d\n", num);
   if (num == 0) {
      return;
   }

   jcr->plugin_ctx_list = (bpContext *)malloc(sizeof(bpContext) * num);

   bpContext *plugin_ctx_list = jcr->plugin_ctx_list;
   Dmsg2(dbglvl, "Instantiate dir-plugin_ctx_list=%p JobId=%d\n", jcr->plugin_ctx_list, jcr->JobId);
   foreach_alist_index(i, plugin, b_plugin_list) {
      /* Start a new instance of each plugin */
      bacula_ctx *b_ctx = (bacula_ctx *)malloc(sizeof(bacula_ctx));
      memset(b_ctx, 0, sizeof(bacula_ctx));
      b_ctx->jcr = jcr;
      plugin_ctx_list[i].bContext = (void *)b_ctx;
      plugin_ctx_list[i].pContext = NULL;
      if (dirplug_func(plugin)->newPlugin(&plugin_ctx_list[i]) != bRC_OK) {
         b_ctx->disabled = true;
      }
   }
}

/*
 * Free the plugin instances for this Job
 */
void free_plugins(JCR *jcr)
{
   Plugin *plugin;
   int i = 0;

   if (!b_plugin_list || !jcr->plugin_ctx_list) {
      return;
   }

   bpContext *plugin_ctx_list = (bpContext *)jcr->plugin_ctx_list;
   Dmsg2(dbglvl, "Free instance dir-plugin_ctx_list=%p JobId=%d\n", jcr->plugin_ctx_list, jcr->JobId);
   foreach_alist_index(i, plugin, b_plugin_list) {
      /* Free the plugin instance */
      dirplug_func(plugin)->freePlugin(&plugin_ctx_list[i]);
      free(plugin_ctx_list[i].bContext);     /* free Bacula private context */
   }
   free(plugin_ctx_list);
   jcr->plugin_ctx_list = NULL;
}


/* ==============================================================
 *
 * Callbacks from the plugin
 *
 * ==============================================================
 */
static bRC baculaGetValue(bpContext *ctx, brDirVariable var, void *value)
{
   JCR *jcr;
   POOL_DBR pr;
   bRC ret = bRC_OK;

   if (!ctx) {
      return bRC_Error;
   }
   jcr = ((bacula_ctx *)ctx->bContext)->jcr;
   if (!jcr) {
      return bRC_Error;
   }
   if (!value) {
      return bRC_Error;
   }
   switch (var) {
   case bDirVarJobId:
      *((int *)value) = jcr->JobId;
      Dmsg1(dbglvl, "dir-plugin: return bDirVarJobId=%d\n", jcr->JobId);
      break;
   case bDirVarJobName:
      *((char **)value) = jcr->Job;
      Dmsg1(dbglvl, "Bacula: return Job name=%s\n", jcr->Job);
      break;
   case bDirVarJob:
      *((char **)value) = jcr->job->hdr.name;
      Dmsg1(dbglvl, "Bacula: return bDirVarJob=%s\n", jcr->job->hdr.name);
      break;
   case bDirVarLevel:
      *((int *)value) = jcr->getJobLevel();
      Dmsg1(dbglvl, "Bacula: return bDirVarLevel=%c\n", jcr->getJobLevel());
      break;
   case bDirVarType:
      *((int *)value) = jcr->getJobType();
      Dmsg1(dbglvl, "Bacula: return bDirVarType=%c\n", jcr->getJobType());
      break;
   case bDirVarClient:
      *((char **)value) = jcr->client->hdr.name;
      Dmsg1(dbglvl, "Bacula: return bDirVarClient=%s\n", jcr->client->hdr.name);
      break;
   case bDirVarNumVols:
      bmemset(&pr, 0, sizeof(pr));
      bstrncpy(pr.Name, jcr->pool->hdr.name, sizeof(pr.Name));
      if (!db_get_pool_numvols(jcr, jcr->db, &pr)) {
         ret=bRC_Error;
      }
      *((int *)value) = pr.NumVols;
      Dmsg1(dbglvl, "Bacula: return bDirVarNumVols=%d\n", pr.NumVols);
      break;
   case bDirVarPool:
      *((char **)value) = jcr->pool->hdr.name;
      Dmsg1(dbglvl, "Bacula: return bDirVarPool=%s\n", jcr->pool->hdr.name);
      break;
   case bDirVarStorage:
      if (jcr->wstore) {
         *((char **)value) = jcr->wstore->hdr.name;
      } else if (jcr->rstore) {
         *((char **)value) = jcr->rstore->hdr.name;
      } else {
         *((char **)value) = NULL;
         ret=bRC_Error;
      }
      Dmsg1(dbglvl, "Bacula: return bDirVarStorage=%s\n", NPRT(*((char **)value)));
      break;
   case bDirVarWriteStorage:
      if (jcr->wstore) {
         *((char **)value) = jcr->wstore->hdr.name;
      } else {
         *((char **)value) = NULL;
         ret=bRC_Error;
      }
      Dmsg1(dbglvl, "Bacula: return bDirVarWriteStorage=%s\n", NPRT(*((char **)value)));
      break;
   case bDirVarReadStorage:
      if (jcr->rstore) {
         *((char **)value) = jcr->rstore->hdr.name;
      } else {
         *((char **)value) = NULL;
         ret=bRC_Error;
      }
      Dmsg1(dbglvl, "Bacula: return bDirVarReadStorage=%s\n", NPRT(*((char **)value)));
      break;
   case bDirVarCatalog:
      *((char **)value) = jcr->catalog->hdr.name;
      Dmsg1(dbglvl, "Bacula: return bDirVarCatalog=%s\n", jcr->catalog->hdr.name);
      break;
   case bDirVarMediaType:
      if (jcr->wstore) {
         *((char **)value) = jcr->wstore->media_type;
      } else if (jcr->rstore) {
         *((char **)value) = jcr->rstore->media_type;
      } else {
         *((char **)value) = NULL;
         ret=bRC_Error;
      }
      Dmsg1(dbglvl, "Bacula: return bDirVarMediaType=%s\n", NPRT(*((char **)value)));
      break;
   case bDirVarJobStatus:
      *((int *)value) = jcr->JobStatus;
      Dmsg1(dbglvl, "Bacula: return bDirVarJobStatus=%c\n", jcr->JobStatus);
      break;
   case bDirVarPriority:
      *((int *)value) = jcr->JobPriority;
      Dmsg1(dbglvl, "Bacula: return bDirVarPriority=%d\n", jcr->JobPriority);
      break;
   case bDirVarVolumeName:
      *((char **)value) = jcr->VolumeName;
      Dmsg1(dbglvl, "Bacula: return bDirVarVolumeName=%s\n", jcr->VolumeName);
      break;
   case bDirVarCatalogRes:
      ret = bRC_Error;
      break;
   case bDirVarJobErrors:
      *((int *)value) = jcr->JobErrors;
      Dmsg1(dbglvl, "Bacula: return bDirVarErrors=%d\n", jcr->JobErrors);
      break;
   case bDirVarJobFiles:
      *((int *)value) = jcr->JobFiles;
      Dmsg1(dbglvl, "Bacula: return bDirVarFiles=%d\n", jcr->JobFiles);
      break;
   case bDirVarSDJobFiles:
      *((int *)value) = jcr->SDJobFiles;
      Dmsg1(dbglvl, "Bacula: return bDirVarSDFiles=%d\n", jcr->SDJobFiles);
      break;
   case bDirVarSDErrors:
      *((int *)value) = jcr->SDErrors;
      Dmsg1(dbglvl, "Bacula: return bDirVarSDErrors=%d\n", jcr->SDErrors);
      break;
   case bDirVarFDJobStatus:
      *((int *)value) = jcr->FDJobStatus;
      Dmsg1(dbglvl, "Bacula: return bDirVarFDJobStatus=%c\n", jcr->FDJobStatus);
      break;
   case bDirVarSDJobStatus:
      *((int *)value) = jcr->SDJobStatus;
      Dmsg1(dbglvl, "Bacula: return bDirVarSDJobStatus=%c\n", jcr->SDJobStatus);
      break;
   default:
      break;
   }
   return ret;
}

static bRC baculaSetValue(bpContext *ctx, bwDirVariable var, void *value)
{
   JCR *jcr;
   if (!value || !ctx) {
      return bRC_Error;
   }
// Dmsg1(dbglvl, "bacula: baculaGetValue var=%d\n", var);
   jcr = ((bacula_ctx *)ctx->bContext)->jcr;
   if (!jcr) {
      return bRC_Error;
   }
// Dmsg1(dbglvl, "Bacula: jcr=%p\n", jcr);
   /* Nothing implemented yet */
   Dmsg1(dbglvl, "dir-plugin: baculaSetValue var=%d\n", var);
   return bRC_OK;
}

static bRC baculaRegisterEvents(bpContext *ctx, ...)
{
   va_list args;
   uint32_t event;

   va_start(args, ctx);
   while ((event = va_arg(args, uint32_t))) {
      Dmsg1(dbglvl, "dir-Plugin wants event=%u\n", event);
   }
   va_end(args);
   return bRC_OK;
}

static bRC baculaJobMsg(bpContext *ctx, const char *file, int line,
  int type, utime_t mtime, const char *fmt, ...)
{
   va_list arg_ptr;
   char buf[2000];
   JCR *jcr;

   if (ctx) {
      jcr = ((bacula_ctx *)ctx->bContext)->jcr;
   } else {
      jcr = NULL;
   }

   va_start(arg_ptr, fmt);
   bvsnprintf(buf, sizeof(buf), fmt, arg_ptr);
   va_end(arg_ptr);
   Jmsg(jcr, type, mtime, "%s", buf);
   return bRC_OK;
}

static bRC baculaDebugMsg(bpContext *ctx, const char *file, int line,
  int level, const char *fmt, ...)
{
   va_list arg_ptr;
   char buf[2000];

   va_start(arg_ptr, fmt);
   bvsnprintf(buf, sizeof(buf), fmt, arg_ptr);
   va_end(arg_ptr);
   d_msg(file, line, level, "%s", buf);
   return bRC_OK;
}

#ifdef TEST_PROGRAM

int main(int argc, char *argv[])
{
   char plugin_dir[1000];
   JCR mjcr1, mjcr2;
   JCR *jcr1 = &mjcr1;
   JCR *jcr2 = &mjcr2;

   strcpy(my_name, "test-dir");

   getcwd(plugin_dir, sizeof(plugin_dir)-1);
   load_dir_plugins(plugin_dir);

   jcr1->JobId = 111;
   new_plugins(jcr1);

   jcr2->JobId = 222;
   new_plugins(jcr2);

   generate_plugin_event(jcr1, bDirEventJobStart, (void *)"Start Job 1");
   generate_plugin_event(jcr1, bDirEventJobEnd);
   generate_plugin_event(jcr2, bDirEventJobStart, (void *)"Start Job 1");
   free_plugins(jcr1);
   generate_plugin_event(jcr2, bDirEventJobEnd);
   free_plugins(jcr2);

   unload_plugins();

   Dmsg0(dbglvl, "dir-plugin: OK ...\n");
   close_memory_pool();
   sm_dump(false);
   return 0;
}

#endif /* TEST_PROGRAM */
