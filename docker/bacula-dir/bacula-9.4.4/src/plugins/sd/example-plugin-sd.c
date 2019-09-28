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
 * Sample Storage daemon Plugin program
 *
 *  Kern Sibbald, October 2007
 */
#include "bacula.h"         /* General Bacula headers */
#include "stored.h"         /* Pull in storage daemon headers */

#ifdef __cplusplus
extern "C" {
#endif

#define PLUGIN_LICENSE      "AGPLv3"
#define PLUGIN_AUTHOR       "Kern Sibbald"
#define PLUGIN_DATE         "November 2011"
#define PLUGIN_VERSION      "2"
#define PLUGIN_DESCRIPTION  "Test Storage Daemon Plugin"

/* Forward referenced functions */
static bRC newPlugin(bpContext *ctx);
static bRC freePlugin(bpContext *ctx);
static bRC getPluginValue(bpContext *ctx, psdVariable var, void *value);
static bRC setPluginValue(bpContext *ctx, psdVariable var, void *value);
static bRC handlePluginEvent(bpContext *ctx, bsdEvent *event, void *value);
static bRC handleGlobalPluginEvent(bsdEvent *event, void *value);


/* Pointers to Bacula functions */
static bsdFuncs *bfuncs = NULL;
static bsdInfo  *binfo = NULL;

static psdInfo pluginInfo = {
   sizeof(pluginInfo),
   SD_PLUGIN_INTERFACE_VERSION,
   SD_PLUGIN_MAGIC,
   PLUGIN_LICENSE,
   PLUGIN_AUTHOR,
   PLUGIN_DATE,
   PLUGIN_VERSION,
   PLUGIN_DESCRIPTION
};

static psdFuncs pluginFuncs = {
   sizeof(pluginFuncs),
   SD_PLUGIN_INTERFACE_VERSION,

   /* Entry points into plugin */
   newPlugin,                         /* new plugin instance */
   freePlugin,                        /* free plugin instance */
   getPluginValue,
   setPluginValue,
   handlePluginEvent,
   handleGlobalPluginEvent
};

/*
 * loadPlugin() and unloadPlugin() are entry points that are
 *  exported, so Bacula can directly call these two entry points
 *  they are common to all Bacula plugins.
 *
 * External entry point called by Bacula to "load the plugin
 */
bRC DLL_IMP_EXP
loadPlugin(bsdInfo *lbinfo, bsdFuncs *lbfuncs, psdInfo **pinfo, psdFuncs **pfuncs)
{
   bfuncs = lbfuncs;                /* set Bacula funct pointers */
   binfo  = lbinfo;
   printf("example-plugin-sd: Loaded: size=%d version=%d\n", bfuncs->size, bfuncs->version);
   *pinfo  = &pluginInfo;             /* return pointer to our info */
   *pfuncs = &pluginFuncs;            /* return pointer to our functions */
   printf("example-plugin-sd: Loaded\n");
   return bRC_OK;
}

/*
 * External entry point to unload the plugin 
 */
bRC DLL_IMP_EXP
unloadPlugin() 
{
   printf("example-plugin-sd: Unloaded\n");
   return bRC_OK;
}

/*
 * The following entry points are accessed through the function 
 *   pointers we supplied to Bacula. Each plugin type (dir, fd, sd)
 *   has its own set of entry points that the plugin must define.
 */
/*
 * Create a new instance of the plugin i.e. allocate our private storage
 */
static bRC newPlugin(bpContext *ctx)
{
   int JobId = 0;
   bfuncs->getBaculaValue(ctx, bsdVarJobId, (void *)&JobId);
   printf("example-plugin-sd: newPlugin JobId=%d\n", JobId);
   bfuncs->registerBaculaEvents(ctx, 1, 2, 0);
   return bRC_OK;
}

/*
 * Free a plugin instance, i.e. release our private storage
 */
static bRC freePlugin(bpContext *ctx)
{
   int JobId = 0;
   bfuncs->getBaculaValue(ctx, bsdVarJobId, (void *)&JobId);
   printf("example-plugin-sd: freePlugin JobId=%d\n", JobId);
   return bRC_OK;
}

/*
 * Return some plugin value (none defined)
 */
static bRC getPluginValue(bpContext *ctx, psdVariable var, void *value) 
{
   printf("example-plugin-sd: getPluginValue var=%d\n", var);
   return bRC_OK;
}

/*
 * Set a plugin value (none defined)
 */
static bRC setPluginValue(bpContext *ctx, psdVariable var, void *value) 
{
   printf("example-plugin-sd: setPluginValue var=%d\n", var);
   return bRC_OK;
}

/*
 * Handle an event that was generated in Bacula
 */
static bRC handlePluginEvent(bpContext *ctx, bsdEvent *event, void *value)
{
   char *name;
   switch (event->eventType) {
   case bsdEventJobStart:
      printf("example-plugin-sd: HandleEvent JobStart :%s:\n", (char *)value);
      break;
   case bsdEventJobEnd:
      printf("example-plugin-sd: HandleEvent JobEnd\n");
      break;
   }
   bfuncs->getBaculaValue(ctx, bsdVarJobName, (void *)&name);
   printf("Job Name=%s\n", name);
   bfuncs->JobMessage(ctx, __FILE__, __LINE__, 1, 0, "JobMesssage message");
   bfuncs->DebugMessage(ctx, __FILE__, __LINE__, 1, "DebugMesssage message");
   return bRC_OK;
}

/*
 * Handle a Global event -- no context
 */
static bRC handleGlobalPluginEvent(bsdEvent *event, void *value)
{
   return bRC_OK;
}


#ifdef __cplusplus
}
#endif
