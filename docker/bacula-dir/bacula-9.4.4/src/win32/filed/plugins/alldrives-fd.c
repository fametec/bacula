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

#ifndef BUILDING_DLL
# define BUILDING_DLL
#endif
#define BUILD_PLUGIN

#include "bacula.h"
#include "fd_plugins.h"
#include "lib/mem_pool.h"

/* from lib/scan.c */
extern int parse_args(POOLMEM *cmd, POOLMEM **args, int *argc,
                      char **argk, char **argv, int max_args);
#define Dmsg(context, level, message, ...) bfuncs->DebugMessage(context, __FILE__, __LINE__, level, message, ##__VA_ARGS__)

#ifdef __cplusplus
extern "C" {
#endif

#define PLUGIN_LICENSE      "Bacula"
#define PLUGIN_AUTHOR       "Eric Bollengier"
#define PLUGIN_DATE         "Oct 2013"
#define PLUGIN_VERSION      "1.2"
#define PLUGIN_DESCRIPTION  "Select all local drives"

/* Forward referenced functions */
static bRC newPlugin(bpContext *ctx);
static bRC freePlugin(bpContext *ctx);
static bRC getPluginValue(bpContext *ctx, pVariable var, void *value);
static bRC setPluginValue(bpContext *ctx, pVariable var, void *value);
static bRC handlePluginEvent(bpContext *ctx, bEvent *event, void *value);
static bRC startBackupFile(bpContext *ctx, struct save_pkt *sp);
static bRC endBackupFile(bpContext *ctx);
static bRC pluginIO(bpContext *ctx, struct io_pkt *io);
static bRC startRestoreFile(bpContext *ctx, const char *cmd);
static bRC endRestoreFile(bpContext *ctx);
static bRC createFile(bpContext *ctx, struct restore_pkt *rp);
static bRC setFileAttributes(bpContext *ctx, struct restore_pkt *rp);


/* Pointers to Bacula functions */
static bFuncs *bfuncs = NULL;
static bInfo  *binfo = NULL;

static pInfo pluginInfo = {
   sizeof(pluginInfo),
   FD_PLUGIN_INTERFACE_VERSION,
   FD_PLUGIN_MAGIC,
   PLUGIN_LICENSE,
   PLUGIN_AUTHOR,
   PLUGIN_DATE,
   PLUGIN_VERSION,
   PLUGIN_DESCRIPTION
};

static pFuncs pluginFuncs = {
   sizeof(pluginFuncs),
   FD_PLUGIN_INTERFACE_VERSION,

   /* Entry points into plugin */
   newPlugin,                         /* new plugin instance */
   freePlugin,                        /* free plugin instance */
   getPluginValue,
   setPluginValue,
   handlePluginEvent,
   startBackupFile,
   endBackupFile,
   startRestoreFile,
   endRestoreFile,
   pluginIO,
   createFile,
   setFileAttributes,
   NULL,                        /* No checkFiles */
   NULL                         /* No ACL/XATTR */
};

/*
 * Plugin called here when it is first loaded
 */
bRC DLL_IMP_EXP 
loadPlugin(bInfo *lbinfo, bFuncs *lbfuncs, pInfo **pinfo, pFuncs **pfuncs)
{
   bfuncs = lbfuncs;                  /* set Bacula funct pointers */
   binfo  = lbinfo;

   *pinfo  = &pluginInfo;             /* return pointer to our info */
   *pfuncs = &pluginFuncs;            /* return pointer to our functions */

   return bRC_OK;
}

/*
 * Plugin called here when it is unloaded, normally when
 *  Bacula is going to exit.
 */
bRC DLL_IMP_EXP
unloadPlugin() 
{
   return bRC_OK;
}

#define get_self(ctx) ((barg *)ctx->pContext)

class barg {
public:
   POOLMEM *args;
   POOLMEM *cmd;
   char *argk[MAX_CMD_ARGS];    /* Argument keywords */
   char *argv[MAX_CMD_ARGS];    /* Argument values */
   int argc;   
   char *exclude;
   bool snapshot_only;

   barg() {
      args = cmd = NULL;
      exclude = NULL;
      argc = 0;
      snapshot_only = false;
   }

   ~barg() {
      free_and_null_pool_memory(args);
      free_and_null_pool_memory(cmd);
   }

   /*
    * Given a single keyword, find it in the argument list, but
    *   it must have a value
    * Returns: -1 if not found or no value
    *           list index (base 0) on success
    */
   int find_arg_with_value(const char *keyword)
   {
      for (int i=0; i<argc; i++) {
         if (strcasecmp(keyword, argk[i]) == 0) {
            if (argv[i]) {
               return i;
            } else {
               return -1;
            }
         }
      }
      return -1;
   }

   /* parse command line
    *  search for exclude="A,B,C,D"
    *  populate this->exclude with simple string "ABCD"
    */
   void parse(char *command) {
      char *p;
      char *q;
      if ((p = strchr(command, ':')) == NULL) {
         Dmsg(NULL, 10, "No options\n");
         return;
      }

      args = get_pool_memory(PM_FNAME);
      cmd = get_pool_memory(PM_FNAME);
      
      pm_strcpy(cmd, ++p);      /* copy string after : */
      parse_args(cmd, &args, &argc, argk, argv, MAX_CMD_ARGS);
      
      for (int i=0; i < argc ; i++) {
         if (strcmp(argk[i], "exclude") == 0) {
            /* a,B,C d => ABCD */
            q = p = exclude = argv[i];
            for (; *p ; p++) {
               if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
                  *q = toupper(*p);
                  q++;
               }
            }
            *q = 0;
            Dmsg(NULL, 50, "%s => %s\n", command, exclude);

         } else if (strcmp(argk[i], "snapshot") == 0) {
            Dmsg(NULL, 50, "Doing only snapshot\n");
            snapshot_only = true;

         } else {
            Dmsg(NULL, 10, "Unknown keyword %s\n", argk[i]);
         }
      }
   }
};

/*
 * Called here to make a new instance of the plugin -- i.e. when
 *  a new Job is started.  There can be multiple instances of
 *  each plugin that are running at the same time.  Your
 *  plugin instance must be thread safe and keep its own
 *  local data.
 */
static bRC newPlugin(bpContext *ctx)
{
   barg *self = new barg();
   ctx->pContext = (void *)self;        /* set our context pointer */
   return bRC_OK;
}

/*
 * Release everything concerning a particular instance of a 
 *  plugin. Normally called when the Job terminates.
 */
static bRC freePlugin(bpContext *ctx)
{
   barg *self = get_self(ctx);
   if (self) {
      delete self;
   }
   return bRC_OK;
}

/*
 * Called by core code to get a variable from the plugin.
 *   Not currently used.
 */
static bRC getPluginValue(bpContext *ctx, pVariable var, void *value) 
{
// printf("plugin: getPluginValue var=%d\n", var);
   return bRC_OK;
}

/* 
 * Called by core code to set a plugin variable.
 *  Not currently used.
 */
static bRC setPluginValue(bpContext *ctx, pVariable var, void *value) 
{
// printf("plugin: setPluginValue var=%d\n", var);
   return bRC_OK;
}

/* TODO: use findlib/drivetype instead */
static bool drivetype(const char *fname, char *dt, int dtlen)
{
   CHAR rootpath[4];
   UINT type;

   /* Copy Drive Letter, colon, and backslash to rootpath */
   bstrncpy(rootpath, fname, 3);
   rootpath[3] = '\0';

   type = GetDriveType(rootpath);

   switch (type) {
   case DRIVE_REMOVABLE:   bstrncpy(dt, "removable", dtlen);   return true;
   case DRIVE_FIXED:       bstrncpy(dt, "fixed", dtlen);       return true;
   case DRIVE_REMOTE:      bstrncpy(dt, "remote", dtlen);      return true;
   case DRIVE_CDROM:       bstrncpy(dt, "cdrom", dtlen);       return true;
   case DRIVE_RAMDISK:     bstrncpy(dt, "ramdisk", dtlen);     return true;
   case DRIVE_UNKNOWN:
   case DRIVE_NO_ROOT_DIR:
   default:
      return false;
   }
}

static void add_drives(bpContext *ctx, char *cmd)
{
   char buf[32];
   char dt[100];
   char drive;
   barg *arg = get_self(ctx);
   arg->parse(cmd);

   if (arg->snapshot_only) {
      return;
   }

   for (drive = 'A'; drive <= 'Z'; drive++) {
      if (arg->exclude && strchr(arg->exclude, drive)) {
         Dmsg(ctx, 10, "%c is in exclude list\n", drive);
         continue;
      }
      snprintf(buf, sizeof(buf), "%c:/", drive);
      if (drivetype(buf, dt, sizeof(dt))) {
         if (strcmp(dt, "fixed") == 0) {
            Dmsg(ctx, 10, "Adding %c to include list\n", drive);
            bfuncs->AddInclude(ctx, buf);
            snprintf(buf, sizeof(buf), "%c:/pagefile.sys", drive);
            bfuncs->AddExclude(ctx, buf);
            snprintf(buf, sizeof(buf), "%c:/System Volume Information", drive);
            bfuncs->AddExclude(ctx, buf);
         } else {
            Dmsg(ctx, 10, "Discarding %c from include list\n", drive);
         }
      }
   }
}

static void add_snapshot(bpContext *ctx, char *ret)
{
   char  buf[32];
   char  dt[100];
   char  drive;
   char *p = ret;
   barg *arg = get_self(ctx);

   /* Start from blank */
   *p = 0;

   if (!arg->snapshot_only) {
      return;
   }

   for (drive = 'A'; drive <= 'Z'; drive++) {
      if (arg->exclude && strchr(arg->exclude, drive)) {
         Dmsg(ctx, 10, "%c is in exclude list\n", drive);
         continue;
      }
      
      snprintf(buf, sizeof(buf), "%c:/", drive);
      
      if (drivetype(buf, dt, sizeof(dt))) {
         if (strcmp(dt, "fixed") == 0) {
            Dmsg(ctx, 10, "Adding %c to snapshot list\n", drive);
            *p++ = drive;
         } else {
            Dmsg(ctx, 10, "Discarding %c from snapshot list\n", drive);
         }
      }
   }
   *p = 0;
   Dmsg(ctx, 10, "ret = %s\n", ret);
}

/*
 * Called by Bacula when there are certain events that the
 *   plugin might want to know.  The value depends on the
 *   event.
 */
static bRC handlePluginEvent(bpContext *ctx, bEvent *event, void *value)
{
   barg arguments;

   switch (event->eventType) {
   case bEventPluginCommand:
      add_drives(ctx, (char *)value); /* command line */
      break;

   case bEventVssPrepareSnapshot:
      add_snapshot(ctx, (char *)value); /* snapshot list */
      break;
   default:
      break;
   }
   
   return bRC_OK;
}

/*
 * Called when starting to backup a file.  Here the plugin must
 *  return the "stat" packet for the directory/file and provide
 *  certain information so that Bacula knows what the file is.
 *  The plugin can create "Virtual" files by giving them a
 *  name that is not normally found on the file system.
 */
static bRC startBackupFile(bpContext *ctx, struct save_pkt *sp)
{
   return bRC_Stop;
}

/*
 * Done backing up a file.
 */
static bRC endBackupFile(bpContext *ctx)
{ 
   return bRC_Stop;
}

/*
 * Do actual I/O.  Bacula calls this after startBackupFile
 *   or after startRestoreFile to do the actual file 
 *   input or output.
 */
static bRC pluginIO(bpContext *ctx, struct io_pkt *io)
{
   io->status = 0;
   io->io_errno = 0;
   return bRC_Error;
}

static bRC startRestoreFile(bpContext *ctx, const char *cmd)
{
   return bRC_Error;
}

static bRC endRestoreFile(bpContext *ctx)
{
   return bRC_Error;
}

/*
 * Called here to give the plugin the information needed to
 *  re-create the file on a restore.  It basically gets the
 *  stat packet that was created during the backup phase.
 *  This data is what is needed to create the file, but does
 *  not contain actual file data.
 */
static bRC createFile(bpContext *ctx, struct restore_pkt *rp)
{
   return bRC_Error;
}

/*
 * Called after the file has been restored. This can be used to
 *  set directory permissions, ...
 */
static bRC setFileAttributes(bpContext *ctx, struct restore_pkt *rp)
{
   return bRC_Error;
}


#ifdef __cplusplus
}
#endif
