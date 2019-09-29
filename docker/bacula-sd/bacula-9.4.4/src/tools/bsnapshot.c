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

#include "bacula.h"
#include "lib/ini.h"

#ifdef HAVE_SUN_OS
#include <sys/types.h>
#include <sys/mkdev.h>          /* Define major() and minor() */
#endif

#define Dmsg(level,  ...) do { \
   if (level <= debug_level) { \
      fprintf(debug, "%s:%d ", __FILE__ , __LINE__);    \
      fprintf(debug, __VA_ARGS__ );                          \
   }  \
 } while (0)

#define Pmsg(level,  ...) do { \
   if (level <= debug_level) { \
      fprintf(stderr, "%s:%d ", __FILE__ , __LINE__ ); \
      fprintf(stderr, __VA_ARGS__ );                   \
   }  \
 } while (0)

#define BSNAPSHOT_CONF SYSCONFDIR "/bsnapshot.conf"

static FILE *debug = NULL;

static void usage(const char *msg=NULL)
{
   if (msg) {
      fprintf(stderr, _("ERROR %s\n\n"), msg);
   }

   fprintf(stderr, _(
           "Bacula %s (%s)\n\n"
           "Usage: bsnapshot\n"
           "   -d level     Set debug level\n"
           "   -v           Verbose\n"
           "   -s           Use sudo\n"
           "   -o logfile   send debug to logfile\n"
           "   -V volume    volume\n"
           "   -T type      volume type\n"
           "   -t           check compatibility\n"
           "   -c           specify configuration file\n"
           "\n"), VERSION, LSMDATE);
   exit(2);
}

static const char *Months[] = {
   NULL,
   "Jan",
   "Feb",
   "Mar",
   "Apr",
   "Mai",
   "Jun",
   "Jul",
   "Aug",
   "Sep",
   "Oct",
   "Nov",
   "Dec"
};

/* Skip leading slash(es) */
static bool makedir(char *path)
{
   char *p = path;

   while (IsPathSeparator(*p)) {
      p++;
   }
   while ((p = first_path_separator(p))) {
      char save_p;
      save_p = *p;
      *p = 0;
      mkdir(path, 0700);
      *p = save_p;
      while (IsPathSeparator(*p)) {
         p++;
      }
   }
   /* If not having a ending / */
   if (!IsPathSeparator(path[strlen(path) - 1])) {
      mkdir(path, 0700);
   }
   return true;
}

/* Strip trailing junk and " */
void strip_quotes(char *str)
{
   strip_trailing_junk(str);
   for(char *p = str; *p ; p++) {
      if (*p == '"') {
         *p = ' ';
      }
   }
}

static void set_trace_file(const char *path)
{
   char dt[MAX_TIME_LENGTH];
   if (debug && debug != stderr) {
      fclose(debug);
   }
   debug = fopen(path, "a");
   if (!debug) {
      debug = stderr;
   } else {
      Dmsg(10, "Starting bsnapshot %s\n",  
           bstrftime(dt, MAX_TIME_LENGTH, time(NULL)));
   }
}

/* Small function to avoid double // in path name */
static  void path_concat(POOLMEM *&dest, const char *path1, const char *path2, const char *path3) {
   int last;

   last = pm_strcpy(dest, path1);
   last = MAX(last - 1, 0);

   /* Check if the last char of dest is / and the first of path2 is / */
   if (dest[last] == '/') {
      if (path2[0] == '/') {
         dest[last] = 0;
      }
   } else {
      if (path2[0] != '/') {
         pm_strcat(dest, "/");
      }
   }

   last = pm_strcat(dest, path2); 
   last = MAX(last - 1, 0);

   if (path3) {
      if (dest[last] == '/') {
         if (path3[0] == '/') {
            dest[last] = 0;
         }
      } else {
         if (path3[0] != '/') {
            pm_strcat(dest, "/");
         }
      }
      pm_strcat(dest, path3);
   }
}

static struct ini_items bsnap_cfg[] = {
 // name                handler          comment  required   default
 { "trace",             ini_store_str,      "",      0,        NULL},
 { "debug",             ini_store_int32,    "",      0,        NULL},
 { "sudo",              ini_store_bool,     "",      0,        NULL},
 { "disabled",          ini_store_bool,     "",      0,        "no"},
 { "retry",             ini_store_int32,    "",      0,        "3"},
 { "lvm_snapshot_size", ini_store_alist_str,"",      0,        NULL},
 { "skip_volume",       ini_store_alist_str,"",      0,        NULL},
 { "snapshot_dir",      ini_store_str,      "",      0,        NULL},
 { "fail_job_on_error", ini_store_bool,     "",      0,        "yes"},
 { NULL,                NULL,             NULL,      0,        NULL}
};

class arguments {
public:
   char *action;                /* list, create, delete... */
   char *volume;                /* snapshot device */
   char *device;                /* original device name */
   char *name;                  /* snapshot name */
   char *mountpoint;            /* device mountpoint */
   char *snapmountpoint;        /* snapshot mountpoint */
   char *type;                  /* snapshot type */
   char *fstype;                /* filesystem type */
   const char *snapdir;         /* .snapshot */
   const char *sudo;            /* prepend sudo to commands */
   int  verbose;
   int  retry;                  /* retry some operations */
   bool disabled;               /* disabled by config file */
   bool fail_job_on_error;      /* Fail job on snapshot error */
   ConfigFile ini;              /* Configuration file */
   POOL_MEM config_file;        /* Path to a config file */

   arguments():
      action(getenv("SNAPSHOT_ACTION")),
      volume(getenv("SNAPSHOT_VOLUME")),
      device(getenv("SNAPSHOT_DEVICE")),
      name(  getenv("SNAPSHOT_NAME")),
      mountpoint(getenv("SNAPSHOT_MOUNTPOINT")),
      snapmountpoint(getenv("SNAPSHOT_SNAPMOUNTPOINT")),
      type(  getenv("SNAPSHOT_TYPE")),
      fstype(getenv("SNAPSHOT_FSTYPE")),
      snapdir(".snapshots"),
      sudo(""),
      verbose(0),
      retry(3),
      disabled(false),
      fail_job_on_error(true)
   {
      struct stat sp;
      ini.register_items(bsnap_cfg, sizeof(struct ini_items));

      if (stat(BSNAPSHOT_CONF, &sp) == 0) {
         Dmsg(10, "conf=%s\n", BSNAPSHOT_CONF);
         pm_strcpy(config_file, BSNAPSHOT_CONF);
      }
   };

   ~arguments() {
   };

   bool validate() {
      int pos;
      if (!action) {
         return false;
      }
      if (strcmp(config_file.c_str(), "") != 0) {
         Dmsg(10, "Reading configuration from %s\n", config_file.c_str());
         if (!ini.parse(config_file.c_str())) {
            printf("status=1 error=\"Unable to parse %s\"\n",
                   config_file.c_str());
            return false;
         }
         pos = ini.get_item("debug");
         if (ini.items[pos].found && debug_level == 0) {
            debug_level = ini.items[pos].val.int32val;
         }
         pos = ini.get_item("trace");
         if (ini.items[pos].found) {
            set_trace_file(ini.items[pos].val.strval);
         }
         pos = ini.get_item("sudo");
         if (ini.items[pos].found && ini.items[pos].val.boolval) {
            sudo = "sudo ";
         }
         pos = ini.get_item("snapshot_dir");
         if (ini.items[pos].found) {
            snapdir = ini.items[pos].val.strval;
         }
         pos = ini.get_item("retry");
         if (ini.items[pos].found) {
            retry = ini.items[pos].val.int32val;
         }
         pos = ini.get_item("disabled");
         if (ini.items[pos].found) {
            disabled = ini.items[pos].val.boolval;
         }
         pos = ini.get_item("fail_job_on_error");
         if (ini.items[pos].found) {
            fail_job_on_error = ini.items[pos].val.boolval;
         }
      }
      return true;
   };
};

class snapshot {
public:
   const char *type;            /* snapshot type, btrfs, zfs, etc.. */
   POOLMEM    *cmd;             /* buffer to edit a command */
   POOLMEM    *path;            /* buffer to edit volume path */
   POOLMEM    *fname;           /* used for split_path_and_filename */
   POOLMEM    *errmsg;          /* buffer to edit error message */
   arguments  *arg;             /* program argument */
   int         pnl;             /* path length */
   int         fnl;             /* fname length */

   snapshot(arguments *a, const char *t):
      type(t),
      cmd(get_pool_memory(PM_NAME)),
      path(get_pool_memory(PM_NAME)),
      fname(get_pool_memory(PM_NAME)),
      errmsg(get_pool_memory(PM_NAME)),
      arg(a),
      pnl(0),
      fnl(0)
   {
   };

   virtual ~snapshot() {
      free_pool_memory(cmd);
      free_pool_memory(path);
      free_pool_memory(fname);
      free_pool_memory(errmsg);
   };

   /* Basically, we check parameters here that are
    * common to all backends
    */
   virtual int mount() {
      Dmsg(10, "[%s] Doing mount command\n", type);
      if (!arg->volume || !arg->name || !arg->device || !arg->mountpoint) {
         Dmsg(10, "volume=%s name=%s device=%s mountpoint=%s\n",
              NPRT(arg->volume), NPRT(arg->name), 
              NPRT(arg->device), NPRT(arg->mountpoint)); 
         return 0;
      }
      return 1;
   };

   virtual int unmount() {
      Dmsg(10, "[%s] Doing unmount command on %s\n", type, 
           NPRT(arg->snapmountpoint));
      if (!arg->snapmountpoint) {
         Dmsg(10, "snapmountpoint=%s\n", NPRT(arg->snapmountpoint)); 
         return 0;
      }
      return 1;
   };

   virtual int support() {
      Dmsg(10, "[%s] Doing support on %s (%s)\n", type, NPRT(arg->mountpoint), 
           NPRT(arg->device));
      if (!arg->fstype || !arg->mountpoint || !arg->device) {
         Dmsg(10, "fstype=%s mountpoint=%s device=%s\n",
              NPRT(arg->fstype), NPRT(arg->mountpoint), NPRT(arg->device)); 
         return 0;
      }
      return 1;
   };

   virtual int check() {
      Dmsg(10, "[%s] Doing check on %s\n", type, NPRT(arg->mountpoint));
      if (!arg->mountpoint) {
         Dmsg(10, "mountpoint=%s\n", NPRT(arg->mountpoint)); 
         return 0;
      }
      return 1;
   };

   virtual int create() {
      Dmsg(10, "[%s] Doing create %s\n", type, NPRT(arg->mountpoint));
      if (!arg->mountpoint || !arg->name || !arg->device) {
         Dmsg(10, "mountpoint=%s name=%s device=%s\n",
              NPRT(arg->mountpoint), NPRT(arg->name), NPRT(arg->device)); 
         return 0;
      }
      return 1;
   };

   virtual int del() {
      Dmsg(10, "[%s] Doing del %s\n", type, NPRT(arg->volume));
      if (!arg->volume || !arg->name) {
         Dmsg(10, "volume=%s name=%s\n",
              NPRT(arg->volume), NPRT(arg->name)); 
         return 0;
      }
      return 1;
   };

   virtual int list() {
      Dmsg(10, "[%s] Doing list on %s\n", type, NPRT(arg->device));
      if (!arg->type || !arg->device || !arg->mountpoint) {
         return 0;
      }
      return 1;
   };

   virtual int subvolumes() {
      Dmsg(10, "[%s] Doing subvolumes %s\n", type, NPRT(arg->mountpoint));
      if (!arg->fstype || !arg->device || !arg->mountpoint) {
         return 0;
      }
      return 1;
   };

   /* Function used in create() to know if we mark the error as FATAL */
   int get_error_code() {
      Dmsg1(0, "get_error_code = %d\n", (int)arg->fail_job_on_error);
      /* 1 is OK */
      if (arg->fail_job_on_error) {
         return 0;           /* Fatal */
      }
      return 2;              /* Error */
   };
};

/* Structure used to sort subvolumes with btrfs backend */
struct vols {
   rblink  link;
   int64_t id;
   int     count;
   char    uuid[MAX_NAME_LENGTH];
   char    puuid[MAX_NAME_LENGTH];
   char    otime[MAX_NAME_LENGTH];
   char    path[1];
};

int vols_compare_id(void *item1, void *item2)
{
   vols *vol1 = (vols *) item1;
   vols *vol2 = (vols *) item2;

   if (vol1->id > vol2->id) {
      return 1;
      
   } else if (vol1->id < vol2->id) {
      return -1;

   } else {
      return 0;
   }
}

int vols_compare_uuid(void *item1, void *item2)
{
   vols *vol1 = (vols *) item1;
   vols *vol2 = (vols *) item2;

   return strcmp(vol1->uuid, vol2->uuid);
}

/* btrfs backend */
class btrfs: public snapshot {
public:
   btrfs(arguments *arg): snapshot(arg, "btrfs")  {};

   /* With BTRFS, the volume is already mounted */
   int mount() {
      if (!snapshot::mount()) {
         return 0;
      }
      split_path_and_filename(arg->volume, &path, &pnl, &fname, &fnl);
      fprintf(stdout, "status=1 snapmountpoint=\"%s\" snapdirectory=\"%s\"\n",
              arg->volume, path);
      return 1;
   };

   int unmount() {
      if (!snapshot::unmount()) {
         return 0;
      }
      printf("status=1\n");
      return 1;
   };

   int support() {
      if (!snapshot::support()) {
         return 0;
      }
      /* If the fstype is btrfs, snapshots are supported */
/*
      Mmsg(cmd, "%sbtrfs filesystem label \"%s\"", arg->sudo, arg->mountpoint);
      if (run_program(cmd, 60, errmsg)) {
         printf("status=0 type=btrfs\n");
         return 0;
      }
      Dmsg(0, "output=%s\n", errmsg);
*/
      printf("status=1 device=\"%s\" type=btrfs\n", arg->mountpoint);
      return 1;
   };

   int check() {
      if (!snapshot::check()) {
         return 0;
      }
      return 1;
   };

   int create() {
      utime_t createdate = 0;
      char ed1[50];
      if (!snapshot::create()) {
         return 0;
      }

      Mmsg(path, "%s/%s", arg->mountpoint, arg->snapdir);
      if (!makedir(path)) {
         printf("status=%d error=\"Unable to create mountpoint directory %s errno=%d\n",
                get_error_code(),
                arg->mountpoint, errno);
         return 0;
      }

      Dmsg(10, "mountpoint=%s snapdir=%s name=%s\n", arg->mountpoint, arg->snapdir, arg->name);
      path_concat(path, arg->mountpoint, arg->snapdir, arg->name);
      Dmsg(10, "path=%s\n", path);

      /* Create the actual btrfs snapshot */
      Mmsg(cmd, "%sbtrfs subvolume snapshot -r \"%s\" \"%s\"", 
           arg->sudo, arg->mountpoint, path);

      if (run_program(cmd, 60, errmsg)) {
         Dmsg(10, "Unable to create snapshot %s %s\n", arg->mountpoint, errmsg);
         strip_quotes(errmsg);
         printf("status=%d error=\"Unable to create snapshot %s\"\n",
                get_error_code(),
                errmsg);
         return 0;
      }

      /* On SLES12 btrfs 3.16, commands on "/" returns "doesn't belong to btrfs mount point" */
      Mmsg(cmd, "%sbtrfs subvolume show \"%s\"", arg->sudo, path);
      if (run_program_full_output(cmd, 60, errmsg)) {
         Dmsg(10, "Unable to display snapshot stats %s %s\n", arg->mountpoint, errmsg);

      } else {
         /* TODO: Check that btrfs subvolume show is reporting "Creation time:" */
         char *p = strstr(errmsg, "Creation time:");
         if (p) {
            p += strlen("Creation time:");
            skip_spaces(&p);
            createdate = str_to_utime(p);

         } else {
            Dmsg(10, "Unable to find Creation time on %s %s\n", arg->mountpoint, errmsg);
         }
      }

      if (!createdate) {
         createdate = time(NULL);
      }
      printf("status=1 volume=\"%s\" createtdate=%s type=btrfs\n", 
             path, edit_uint64(createdate, ed1));
      return 1;
   };

   int del() {
      if (!snapshot::del()) {
         return 0;
      }

      Mmsg(cmd, "%sbtrfs subvolume delete \"%s\"", arg->sudo, arg->volume);
      if (run_program(cmd, 300, errmsg)) {
         Dmsg(10, "Unable to delete snapshot %s\n", errmsg);
         strip_quotes(errmsg);
         printf("status=0 type=btrfs error=\"%s\"\n", errmsg);
         return 0;
      }
      printf("status=1\n");
      return 1;
   };

   /* btrfs subvolume list -u -q -s /tmp/regress/btrfs
    * ID 259 gen 52 top level 5 parent_uuid - uuid baf4b5d7-28d0-9b4a-856e-36e6fd4fbc96 path .snapshots/aaa
    */
   int list() {
      char *p, *p2, *end, *path;
      char  id[50], day[50], hour[50];
      struct vols *v = NULL, *v2;
      rblist *lst;

      if (!snapshot::list()) {
         return 0;
      }
      Mmsg(cmd, "%sbtrfs subvolume list -u -q -o -s \"%s\"", arg->sudo, arg->mountpoint);
      if (run_program_full_output(cmd, 300, errmsg)) {
         Dmsg(10, "Unable to list snapshot %s\n", errmsg);
         strip_quotes(errmsg);
         printf("status=0 type=btrfs error=\"%s\"\n", errmsg);
         return 0;
      }

      lst = New(rblist(v, &v->link));

      /* ID 259 gen 52 top level 5 parent_uuid - uuid baf4b5d7-28d0-9b4a-856e-36e6fd4fbc96 path .snapshots/aaa */
      for (p = errmsg; p && *p ;) {
         Dmsg(20, "getting subvolumes from %s", p);

         /* Replace final \n by \0 to have strstr() happy */
         end = strchr(p, '\n');

         /* If end=NULL, we are at the end of the buffer (without trailing \n) */
         if (end) { 
            *end = 0;
         }

         /* Each line is supposed to start with "ID", and end with "path" */
         bool ok = false;
         if (sscanf(p, "ID %50s ", id) == 1) {              /* We found ID, look for path */
            p2 = strstr(p, "path ");
            if (p2) {
               path = p2 + strlen("path ");
               v = (struct vols*) malloc(sizeof (vols) + strlen(path) + 1);
               *v->otime = *v->uuid = *v->puuid = 0;
               v->id = str_to_int64(id);
               v->count = 0;
               strcpy(v->path, path);

               p2 = strstr(p, "otime");
               if (p2 && sscanf(p2, "otime %50s %50s", day, hour) == 2) {
                  bsnprintf(v->otime, sizeof(v->otime), "%s %s", day, hour);
               }

               p2 = strstr(p, "parent_uuid ");
               if (p2 && sscanf(p2, "parent_uuid %127s", v->puuid) == 1) {

                  p2 = strstr(p, " uuid ");
                  if (p2 && sscanf(p2, " uuid %127s", v->uuid) == 1) {

                     v2 = (struct vols *)lst->insert(v, vols_compare_uuid);
                     if (v2 != v) {
                        v2->count++;
                        free(v);
                     }
                     ok = true;
                     /* Replace final \n by \0 to have strstr() happy */
                     Dmsg(10, "puuid=%s uuid=%s path=%s\n", v2->puuid, v2->uuid, v2->path);
                  } 
               } 
            }
         }
         if (!ok) {
            Dmsg(10, "Unable to decode \"%s\" line\n", p);
         }
         if (end) {
            *end = '\n';
            end++;
         }
         /* If end==NULL, we stop */
         p = end;
      }

      foreach_rblist(v, lst) {
         char *name = v->path;
         int   len = strlen(arg->snapdir);
         if ((p = strstr(v->path, arg->snapdir))) {
            name = p + len + ((arg->snapdir[len-1] == '/') ? 0 : 1);
         }
         printf("volume=\"%s%s%s\" name=\"%s\" device=\"%s\" createdate=\"%s\" type=\"btrfs\"\n",
                arg->mountpoint,
                arg->mountpoint[strlen(arg->mountpoint) - 1] == '/' ? "": "/",
                v->path,
                name,
                arg->mountpoint,
                v->otime
            );
      }

      delete lst;
      return 1;
   };

   void scan_subvolumes(char *buf, rblist *lst) {
      char *p, *end;
      char  id[50];
      bool  ok;
      struct vols *elt1 = NULL, *elt2 = NULL;

      /* btrfs subvolume list /var/lib/pacman/
       * ID 349 gen 383 top level 5 path test
       * ID 354 gen 391 cgen 391 top level 5 otime 2014-11-05 17:49:07 path .snapshots/aa
       */
      for (p = buf; p && *p ;) {
         Dmsg(20, "getting subvolumes from %s", p);

         /* Replace final \n by \0 to have strstr() happy */
         end = strchr(p, '\n');
         /* If end=NULL, we are at the end of the buffer (without trailing \n) */
         if (end) { 
            *end = 0;
         }

         /* Each line is supposed to start with "ID", and end with "path" */
         ok = (sscanf(p, "ID %50s ", id) == 1);
         if (ok) {              /* We found ID, look for path */
            p = strstr(p, "path ");
            if (p) {
               p += strlen("path ");

               elt1 = (struct vols *) malloc(sizeof(struct vols) + strlen(p) + 1);
               elt1->id = str_to_int64(id);
               elt1->count = 0;
               strcpy(elt1->path, p);
               Dmsg(10, "Found path %s for id %s\n", elt1->path, id);
               elt2 = (struct vols *)lst->insert(elt1, vols_compare_id);
               if (elt2 != elt1) {
                  elt2->count++;
                  free(elt1);
               }
            } else {
               Dmsg(10, "Unable to find the path in this line\n");
            }

         } else {
            Dmsg(10, "Unable to decode %s line\n", p);
         }
         if (end) {
            *end = '\n';
            end++;
         }
         /* If end==NULL, we stop */
         p = end;
      } 
   };

   /* List subvolumes, they may not be listed by mount */
   int subvolumes() {
      rblist      *lst;
      struct stat  sp;
      struct vols *elt1 = NULL;
      char   ed1[50];

      Mmsg(cmd, "%sbtrfs subvolume show \"%s\"", arg->sudo, arg->mountpoint);
      if (run_program_full_output(cmd, 300, errmsg)) {
         Dmsg(10, "Unable to get information %s\n", errmsg);
         strip_quotes(errmsg);
         printf("status=0 type=btrfs error=\"%s\"\n", errmsg);
         return 0;
      }

      /* TODO: Very week way to analyse FS */
      if (!strstr(errmsg, "is btrfs root")) {
         printf("status=0 type=btrfs error=\"Not btrfs root fs\"\n");
         return 0;
      }
      
      Mmsg(cmd, "%sbtrfs subvolume list -s \"%s\"", arg->sudo, arg->mountpoint);
      if (run_program_full_output(cmd, 300, errmsg)) {
         Dmsg(10, "Unable to list snapshot snapshot %s\n", errmsg);
         strip_quotes(errmsg);
         printf("status=0 type=btrfs error=\"%s\"\n", errmsg);
         return 0;
      }

      lst = New(rblist(elt1, &elt1->link));
      scan_subvolumes(errmsg, lst);

      Mmsg(cmd, "%sbtrfs subvolume list \"%s\"", arg->sudo, arg->mountpoint);
      if (run_program_full_output(cmd, 300, errmsg)) {
         Dmsg(10, "Unable to list subvolume %s\n", errmsg);
         strip_quotes(errmsg);
         printf("status=0 type=btrfs error=\"%s\"\n", errmsg);
         delete lst;
         return 0;
      }
      scan_subvolumes(errmsg, lst);

      foreach_rblist(elt1, lst) {
         if (elt1->count > 0) { /* Looks to be a snapshot, we saw two entries */
            continue;
         }

         path_concat(path, arg->mountpoint, elt1->path, NULL);

         if (stat(path, &sp) == 0) {
            printf("dev=%s mountpoint=\"%s\" fstype=btrfs\n", 
                   edit_uint64(sp.st_dev, ed1), path);

         } else {
            Dmsg(10, "Unable to stat %s (%s)\n", elt1->path, path);
         }
      }
      delete lst;
      return 1;
   };
};

/* Create pool
 * zpool create pool /dev/device
 * zfs create pool/eric
 * zfs set mountpoint=/mnt test/eric
 * zfs mount pool/eric
 */
/* zfs backend */
class zfs: public snapshot {
public:
   zfs(arguments *arg): snapshot(arg, "zfs")  {
      arg->snapdir = ".zfs/snapshot";
   };

   /* With ZFS, the volume is already mounted 
    * but on linux https://github.com/zfsonlinux/zfs/issues/173
    * we need to use the mount command.
    * TODO: Adapt the code for solaris
    */
   int mount() {
      struct stat sp;

      if (!snapshot::mount()) {
         return 0;
      }

      path_concat(path, arg->mountpoint, arg->snapdir, arg->name);

      if (stat(path, &sp) != 0) {
         /* See if we can change the snapdir attribute */
         Mmsg(cmd, "%szfs set snapdir=visible \"%s\"", arg->sudo, arg->device);
         if (run_program(cmd, 60, errmsg)) {
            Dmsg(10, "Unable to change the snapdir attribute %s %s\n", arg->device, errmsg);
            strip_quotes(errmsg);
            printf("status=0 error=\"Unable to mount snapshot %s\"\n", errmsg);
            return 0;
         }
         if (stat(path, &sp) != 0) {
            Dmsg(10, "Unable to get the snapdir %s %s\n", arg->snapdir, arg->device);
            strip_quotes(errmsg);
            printf("status=0 error=\"Unable to mount snapshot, no snapdir %s\"\n", arg->snapdir);
            return 0;
         }
      }
#if 0                           /* On linux, this function is broken for now */
      makedir(path);
      Mmsg(cmd, "%smount -t %s \"%s\" \"%s\"", arg->sudo, arg->fstype, arg->volume, path);
      if (run_program(cmd, 60, errmsg)) {
         Dmsg(10, "Unable to create mount snapshot %s %s\n", arg->volume, errmsg);
         strip_quotes(errmsg);
         printf("status=0 error=\"Unable to mount snapshot %s\"\n", errmsg);
         return 0;
      }

#endif
      fprintf(stdout, "status=1 snapmountpoint=\"%s\" snapdirectory=\"%s/%s\"\n",
              path, arg->mountpoint, arg->snapdir);
      return 1;
   };

   /* No need to unmount something special */
   int unmount() {
      printf("status=1\n");
      return 1;
   };

   int support() {
      if (!snapshot::support()) {
         return 0;
      }
      Mmsg(cmd, "%szfs list -H -o name \"%s\"", arg->sudo, arg->mountpoint);
      if (run_program(cmd, 60, errmsg)) {
         Dmsg(10, "Unable to get device %s %s\n", arg->mountpoint, errmsg);
         strip_quotes(errmsg);
         printf("status=0 error=\"Unable to get device %s\"\n", errmsg);
         return 0;
      }
      strip_trailing_junk(errmsg);
      /* If the fstype is zfs, snapshots are supported */
      printf("status=1 device=\"%s\" type=zfs\n", errmsg);
      return 1;
   };

   int create() {
      char ed1[50];

      if (!snapshot::create()) {
         return 0;
      }

      Mmsg(path, "%s@%s", arg->device, arg->name);

      /* Create the actual zfs snapshot */
      Mmsg(cmd, "%szfs snapshot \"%s\"", arg->sudo, path);

      if (run_program(cmd, 60, errmsg)) {
         Dmsg(10, "Unable to create snapshot %s %s\n", arg->device, errmsg);
         strip_quotes(errmsg);
         printf("status=%d error=\"Unable to create snapshot %s\"\n",
                get_error_code(),
                errmsg);
         return 0;
      }

      Mmsg(cmd, "%szfs get -p creation \"%s\"", arg->sudo, path);
      if (run_program_full_output(cmd, 60, errmsg)) {
         Dmsg(10, "Unable to display snapshot stats %s %s\n", arg->device, errmsg);
         strip_quotes(errmsg);
         printf("status=%d error=\"Unable to get snapshot info %s\"\n",
                get_error_code(),
                errmsg);
         return 0;
      } 

      /* TODO: Check that zfs get is reporting "creation" time */
      Mmsg(cmd, "NAME PROPERTY VALUE SOURCE\n%s creation %%s", path);
      if (sscanf(errmsg, cmd, ed1) == 1) {
         Dmsg(10, "Found CreateTDate=%s\n", ed1);
         printf("status=1 volume=\"%s\" createtdate=%s type=zfs\n", 
                path, ed1);

      } else {
         printf("status=1 volume=\"%s\" createtdate=%s type=zfs\n", 
                path, edit_uint64(time(NULL), ed1));
      }
      return 1;
   };

   int del() {
      if (!snapshot::del()) {
         return 0;
      }

      Mmsg(cmd, "%szfs destroy \"%s\"", arg->sudo, arg->volume);
      if (run_program(cmd, 300, errmsg)) {
         Dmsg(10, "Unable to delete snapshot %s\n", errmsg);
         strip_quotes(errmsg);
         printf("status=0 type=zfs error=\"%s\"\n", errmsg);
         return 0;
      }
      printf("status=1\n");
      return 1;
   };

   /* zfs list -t snapshot 
    * test/eric@snap1    17K      -    21K  -
    * test/eric@snap2    17K      -    21K  -
    * 
    * it is possible to change fields to display with -o 
    */
   int list() {
      POOL_MEM buf2;
      if (!snapshot::list()) {
         return 0;
      }

      Mmsg(cmd, "%szfs list -t snapshot -H -o name,used,creation", arg->sudo);
      /* rpool@basezone_snap00   0       Fri Mar  6  9:55 2015  */
      if (run_program_full_output(cmd, 60, errmsg)) {
         Dmsg(10, "Unable to list snapshot %s\n", errmsg);
         strip_quotes(errmsg);
         printf("status=0 error=\"Unable to list snapshot %s\"\n", errmsg);
         return 0;
      }

      int  i = 1, Day, Year, Hour, Min;
      char DayW[50], Month[50], CreateDate[50];
      const char *buf[4];

      buf[0] = errmsg;
      for (char *p = errmsg; p && *p ; p++) {
         if (*p == '\n') {
            *p = 0;
            /* Flush the current one */
            if (!arg->device || strcmp(arg->device, buf[0]) == 0) {

               if (sscanf(buf[3], "%s %s %d %d:%d %d",
                          DayW, Month, &Day, &Hour, &Min, &Year) == 6)
               {
                  /* Get a clean iso format */
                  for (int j=1; j <= 12 ; j++) {
                     if (strcmp(Month, Months[j]) == 0) {
                        snprintf(Month, sizeof(Month), "%02d", j);
                     }
                  }
                  snprintf(CreateDate, sizeof(CreateDate), "%d-%s-%02d %02d:%02d:00",
                           Year, Month, Day, Hour, Min);
                  buf[3] = CreateDate;
               }
               printf("volume=\"%s@%s\" name=\"%s\" device=\"%s\" size=\"%s\" "
                      "createdate=\"%s\" status=1 error=\"\" type=\"zfs\"\n",
                      buf[0], buf[1], buf[1], buf[0], buf[2], buf[3]);
            } else {
               Dmsg(10, "Do not list %s@%s\n", buf[0], buf[1]);
            }

            i = 1;
            buf[0] = p+1;
            buf[1] = buf[2] = buf[3] = "";

         } else if ((*p == '\t' || *p == '@') && i < 4) {
            buf[i++] = p+1;
            *p = 0;
         }
      }

      return 1;
   };
};

/* Structure of the LVS output */
typedef struct {
   const char *name;
   int         pos;
} Header;

/* -1 is mandatory, -2 is optionnal */
static Header lvs_header[] = {
   /* KEEP FIRST */
   {"Path",  -1},        /* Volume Path: /dev/ubuntu-vg/root */
   {"DMPath",-2},        /* Device mapper Path /dev/mapper/ubuntu--vg-root */
   {"LV",    -1},        /* Volume Name: root  */
   {"Attr",  -1},        /* Attributes:  -wi-ao--- */
   {"KMaj",  -1},        /* Kernel Major: 252 */
   {"KMin",  -1},        /* Kernel Minor: 0 */
   {"LSize", -1},        /* Size (b)  */
   {"#Seg",  -1},        /* Number of segments */
   {"Origin",-1},
   {"OSize", -1},
   {"Snap%", -1},
   {"Time",  -1},        /* Creation date  */
   {NULL,    -1}
};

static Header vgs_header[] = {
   /* KEEP FIRST */
   {"VG",    -1},        /* VG Name: vgroot  */
   {"VSize", -1},        /* Size */
   {"VFree", -1},        /* Space left */
   {"#Ext",  -1},        /* Nb Ext */
   {"Free",  -1},        /* Nb Ext free */
   {"Ext",   -1},        /* Ext size */
   {NULL,    -1}
};

/* LVM backend, not finished */
class lvm: public snapshot {
public:
   alist *lvs, *vgs;
   int    lvs_nbelt, vgs_nbelt;

   lvm(arguments *arg):
     snapshot(arg, "lvm"), lvs(NULL), vgs(NULL), lvs_nbelt(0),
        vgs_nbelt(0) {};

   ~lvm() {
      free_header(lvs, lvs_nbelt);
      free_header(vgs, vgs_nbelt);
   };

   void free_header(alist *lst, int nbelt) {
      if (lst) {
         char **current;
         /* cleanup at the end */
         foreach_alist(current, lst) {
            for (int j=0; j < nbelt ; j++) {
               Dmsg(50, "current[%d] = %s\n", j, current[j]);
               free(current[j]);
            }
            free(current);
         }
         delete lst;
      }
   };

   char *get_vg_from_lv_path(char *path, char *vg, int max) {
      char *p;

      if (!path) {
         return NULL;
      }

      /* Make a copy of the path */
      bstrncpy(vg, path, max);
      path = vg;

      if (strncmp(path, "/dev/", 5) != 0) {
         Dmsg(10, "Strange path %s\n", path);
         return NULL;
      }
      path += 5;             /* skip /dev/ */

      /* End the string at the last / */
      p = strchr(path, '/');
      if (!p) {
         Dmsg(10, "Strange end of path %s\n", path);
         return NULL;
      }
      *p = 0;

      return path;
   };

   /* Report the space available on VG */
   int64_t get_space_available(char *lv) {
      char  buf[512];
      char *vgname = get_vg_from_lv_path(get_lv_value(lv, "Path"), 
                                         buf, sizeof(buf));

      if (vgname) {
         char *s = get_vg_value(vgname, "VFree");
         if (s) {
            return str_to_int64(s);

         } else {
            Dmsg(10, "Unable to get VFree\n");
         }

      } else {
         Dmsg(10, "Unable to get VG from %s\n", lv);
      }
      return -1;
   };

   /* return vg_ssd-pacman */
   char *get_lv_from_dm(char *dm, POOLMEM **ret, uint32_t *major, uint32_t *minor) {
      struct stat sp;
      char *p, *start;
      uint32_t maj, min;

      /* Looks to be a device mapper, need to convert the name */
      if (strncmp(dm, "/dev/dm", strlen("/dev/dm")) != 0) {
         return NULL;
      }
      if (stat(dm, &sp) < 0) {
         return NULL;
      }

      Mmsg(cmd, "%sdmsetup ls", arg->sudo);
      if (run_program_full_output(cmd, 60, errmsg)) {
         Dmsg(10, "Unable to query dmsetup %s\n", errmsg);
         return NULL;
      }
      /* vg_ssd-pacman-real     (254:1)
       * vg_ssd-pacman  (254:0)
       * or
       * vg_ssd-pacman-real     (254, 1)
       * vg_ssd-pacman-real     (254, 1)
       */
      *ret = check_pool_memory_size(*ret, strlen(errmsg)+1);
      for (start = p = errmsg; *p ; p++) {
         if (*p == '\n') {
            *p = 0;
            if (sscanf(start, "%s (%d:%d)", *ret, &maj, &min) == 3 ||
                sscanf(start, "%s (%d, %d)", *ret, &maj, &min) == 3)
            {
               if (maj == major(sp.st_rdev) &&
                   min == minor(sp.st_rdev))
               {
                  return *ret;
               }
            }
            start = p+1;
         }
      }
      return NULL;
   };

   /* The LV path from name or dmpath */
   char **get_lv(char *lv) {
      char **elt = NULL, *dm = NULL;
      int path = get_value_pos(lvs_header, "Path");
      int dmpath = get_value_pos(lvs_header, "DMPath");
      int kmaj = get_value_pos(lvs_header, "KMaj");
      int kmin = get_value_pos(lvs_header, "KMin");
      uint32_t min = 0, maj = 0;
      POOLMEM *buf = get_pool_memory(PM_FNAME);

      if (!lv || (path < 0 && dmpath < 0)) {
         Dmsg(10, "Unable to get LV parameters\n");
         goto bail_out;
      }

      dm = get_lv_from_dm(lv, &buf, &maj, &min);
      Dmsg(50, "%s = get_lv_from_dm(%s, %s, %d, %d)\n", dm, lv, buf, maj, min);

      /* HERE: Need to loop over LVs */
      foreach_alist(elt, lvs) {
         if (path > 0 && strcmp(NPRT(elt[path]), lv) == 0) {
            goto bail_out;
         }

         if (dmpath > 0 && strcmp(NPRT(elt[dmpath]), lv) == 0) {
            goto bail_out;
         }

         /* Try by Minor/Major if comming from device mapper */
         if ((maj && kmaj && str_to_uint64(elt[kmaj]) == maj) &&
             (min && kmin && str_to_uint64(elt[kmin]) == min))
         {
            goto bail_out;
         }

         /* Find if /dev/mapper/vg_ssd-pacman matches vg_ssd-pacman */
         if (dm && dmpath && strlen(elt[dmpath]) > strlen("/dev/mapper/")) {
            if (strcmp(elt[dmpath] + strlen("/dev/mapper/"), dm) == 0) {
               goto bail_out;
            }
         }

         /* Special case for old LVM where mapper path doesn't exist */
         if (dmpath < 0 && strncmp("/dev/mapper/", lv, 12) == 0) {

            POOLMEM *buf2 = get_memory(strlen(elt[path])*2+10);
            pm_strcpy(buf2, "/dev/mapper/");

            char *d = buf2 + 12; /* Skip /dev/mapper/ */
            bool ret = false;

            /* Keep the same path, but escape - to -- and / to - */
            for (char *p = elt[path]+5; *p ; p++) {
               if (*p == '-') {
                  *d++ = *p;
               }
               /* Escape / to - if needed */
               *d++ = (*p == '/') ? '-' : *p;
            }
            *d = 0;
            ret = (strcmp(buf2, lv) == 0);
            free_pool_memory(buf2);

            if (ret) {
               goto bail_out;
            }
         }
      }
      Dmsg(10, "%s not found in lv list\n", lv);
      return NULL;              /* not found */

   bail_out:
      if (buf) {
         free_pool_memory(buf);
      }
      return elt;
   };

   /* Report LV Size in bytes */
   int64_t get_lv_size(char *name) {
      char **elt = get_lv(arg->device);
      int sp;

      if (!elt) {
         return -1;
      }

      sp = get_value_pos(lvs_header, "LSize");
      /* Check if we have enough space on the VG */
      return str_to_int64(elt[sp]);
   };

   char *get_lv_value(char *name, const char *value) {
      return get_value(lvs_header, lvs_nbelt, lvs, name, value);
   };

   int get_value_pos(Header *header, const char *value) {
      for (int i = 0; header[i].name ; i++) {
         if (strcmp(header[i].name, value) == 0) {
            return header[i].pos;
         }
      }
      return -1;                /* not found */
   };
   
   /* Return an element value */
   char *get_value(Header *header, int nbelt, alist *lst,
                   char *name, const char *value) {
      char **elt;
      int    pos = get_value_pos(header, value);
      int    id  = header[0].pos; /* position name */

      if (pos < 0 || id == -1) {
         return NULL;
      }
      /* Loop over elements we have, and return the value that is asked */
      foreach_alist(elt, lst) {
         if (strcmp(NPRT(elt[id]), name) == 0) {
            return elt[pos];
         }
      }
      return NULL;
   };

   /* Return a parameter for a VolumeGroup */
   char *get_vg_value(char *vg, const char *value) {
      return get_value(vgs_header, vgs_nbelt, vgs, vg, value);
   };

   /* Get snapshot size, look in config file if needed */
   int get_lvm_snapshot_size(char *lv) {
      char *tmp, **elt;
      uint64_t s, size;
      int    sp;
      alist *lst;

      int pos = arg->ini.get_item("lvm_snapshot_size");
      if (!arg->ini.items[pos].found) {
         return -1;             /* Nothing specified, stop here */
      }

      lst = arg->ini.items[pos].val.alistval;
      if (lst) {
         /* /dev/ubuntu-vg/root:100M 
          * /dev/ubuntu-vg/home:10%
          * /dev/ubuntu-vg/var:200GB
          */
         foreach_alist(tmp, lst) {
            char *p = strchr(tmp, ':');

            /* Check the LV name */
            if (p && strncmp(tmp, lv, p - tmp) != 0) {
               continue;
            }

            /* This is a percent */
            if (strchr(p+1, '%') != NULL) {
               Dmsg(10, "Found a %%\n");
               s = str_to_int64(p+1);

               /* Compute the requested size */
               sp = get_value_pos(lvs_header, "LSize");
               elt = get_lv(lv);
               size = str_to_int64(elt[sp]);
               return size * (s / 100);
            }

            /* It might be a size */
            if (size_to_uint64(p+1, strlen(p+1), &s)) {
               Dmsg(10, "Found size %ld\n", s);
               return s;
            }
            Dmsg(10, "Unable to use %s\n", tmp);
            return -1;
         }
      }
      return -1;
   };

   int create() {
      char   *name, *ts, buf[128], *lvname;
      int64_t size, ssize, maxsize;
      if (!snapshot::create()) {
         return 0;
      }

      if (!parse_lvs_output() ||
          !parse_vgs_output())
      {
         printf("status=%d error=\"Unable parse lvs or vgs output\"\n",
                get_error_code());
         return 0;
      }

      path_concat(path, arg->mountpoint, arg->snapdir, arg->name);

      if (!makedir(path)) {
         printf("status=%d error=\"Unable to create mountpoint directory %s errno=%d\n",
                get_error_code(),
                arg->mountpoint, errno);
         return 0;
      }

      name = get_lv_value(arg->device, "LV");
      size = get_lv_size(arg->device);
      if (size < 0) {
         printf("status=%d error=\"Unable to get lv size\"\n",
                get_error_code());
         return 0;
      }

      ssize = get_lvm_snapshot_size(arg->device);
      if (ssize > 0) {
         size = ssize;
      } else {
         size = size / 10;         /* Ask to get 10% */
      }

      size = (size / 512L) * 512L;

      lvname = get_lv_value(arg->device, "Path");
      maxsize = get_space_available(lvname);
      Dmsg(10, "maxsize=%ld size=%ld\n", maxsize, size);

      if (maxsize < 0) {
         printf("status=%d error=\"Unable to detect maxsize\" type=lvm\n",
                get_error_code());
         return 0;
      }

      if (size > maxsize) {
         char ed1[50], ed2[50];
         printf("status=%d error=\"Not enough space left on VG %sB, "
                "%sB is required\" type=lvm\n",
                get_error_code(),
                edit_uint64_with_suffix(maxsize, ed1),
                edit_uint64_with_suffix(size, ed2));
         return 0;
      }

      /* TODO: Need to get the volume name and add the snapshot
       * name at the end 
       */
      Mmsg(cmd, "%slvcreate -s -n \"%s_%s\" -L %lldb \"%s\"", 
           arg->sudo, name, arg->name, size, arg->device);
      if (run_program(cmd, 60, errmsg)) {
         Dmsg(10, "Unable to create snapshot %s %s\n", arg->name, errmsg);
         strip_quotes(errmsg);
         printf("status=0 error=\"Unable to create snapshot %s\"\n", errmsg);
         return 0;
      }
      if (!parse_lvs_output()) {
         Dmsg(10, "Unable to parse lvm output after snapshot creation\n");
         printf("status=0 error=\"Unable to parse lvs\"\n");
         return 0;
      }

      Mmsg(cmd, "%s_%s", arg->device, arg->name);
      ts = get_lv_value(cmd, "Time");
      if (!ts) {
         Dmsg(10, "Unable to find snapshot in lvs output\n");
         bstrftimes(buf, sizeof(buf), time(NULL));
         ts = buf;
      }
      Dmsg(10, "status=1 volume=\"%s_%s\" createdate=\"%s\" type=lvm\n",
             arg->device, arg->name, ts);
      printf("status=1 volume=\"%s_%s\" createdate=\"%s\" type=lvm\n",
             arg->device, arg->name, ts);
      return 1;
   };

   int del() {
      if (!snapshot::del()) {
         return 0;
      }
      Mmsg(cmd, "%slvremove -f \"%s\"", 
           arg->sudo, arg->volume);

      if (run_program(cmd, 60, errmsg)) {
         Dmsg(10, "Unable to delete snapshot %s %s\n", arg->name, errmsg);
         strip_quotes(errmsg);
         printf("status=0 error=\"Unable to delete snapshot %s\"\n", errmsg);
         return 0;
      }

      printf("status=1\n");
      return 1;
   };

   int check() {
      if (!snapshot::check()) {
         return 0;
      }
      parse_vgs_output();
      for (int i = 0; vgs_header[i].name ; i++) {
         if (vgs_header[i].pos == -1) {
            printf("status=0 error=\"Unable to use output of vgs command."
                   " %s is missing.\"\n", 
                   vgs_header[i].name);
            return 0;
         }
      }

      parse_lvs_output();
      for (int i = 0; lvs_header[i].name ; i++) {
         if (lvs_header[i].pos == -1) {
            printf("status=0 error=\"Unable to use output of lvs command."
                   " %s is missing.\"\n",
                   lvs_header[i].name);
            return 0;
         }
      }
      return 1;
   };

   void strip_double_slashes(char *fname)
   {
      char *p = fname;
      while (p && *p) {
         p = strpbrk(p, "/\\");
         if (p != NULL) {
            if (IsPathSeparator(p[1])) {
               strcpy(p, p+1);
            }
            p++;
         }
      }
   };

   int mount() {
      if (!snapshot::mount()) {
         return 0;
      }

      path_concat(path, arg->mountpoint, arg->snapdir, arg->name);

      if (!makedir(path)) {
         printf("status=0 error=\"Unable to create mount point %s errno=%d\"\n", 
                path, errno);
         return 0;
      }

      Mmsg(cmd, "%smount -o ro \"%s\" \"%s\"", arg->sudo, arg->volume, path);
      if (run_program(cmd, 60, errmsg) != 0) {
         Dmsg(10, "Unable to mount volume. ERR=%s\n", errmsg);
         strip_quotes(errmsg);
         printf("status=0 error=\"Unable to mount the device %s\"\n", errmsg);
         return 0;
      }

      Dmsg(10, "status=1 snapmountpoint=\"%s\" snapdirectory=\"%s/%s\"\n", 
            path, arg->mountpoint, arg->snapdir);
      printf("status=1 snapmountpoint=\"%s\" snapdirectory=\"%s/%s\"\n", 
             path, arg->mountpoint, arg->snapdir);
      return 1;
   };

   int unmount() {
      int ret, retry = arg->retry;

      if (!snapshot::unmount()) {
         return 0;
      }

      Mmsg(cmd, "%sumount \"%s\"", arg->sudo, arg->snapmountpoint);
      do {
         ret = run_program(cmd, 60, errmsg);
         if (ret != 0) {
            Dmsg(10, "Unable to unmount the directory. ERR=%s\n", errmsg);
            sleep(3);
         }
      } while (ret != 0 && retry-- > 0);

      if (ret != 0) {
         Dmsg(10, "Unable to mount volume. ERR=%s\n", errmsg);
         strip_quotes(errmsg);
         printf("status=0 error=\"Unable to umount the device %s\"\n", errmsg);
         return 0;
      }

      retry = arg->retry;
      do {
         Dmsg(10, "Trying to delete mountpoint %s\n", arg->snapmountpoint);
         if ((ret = rmdir(arg->snapmountpoint)) != 0) {
            sleep(3);
         }
      } while (retry-- > 0 && ret != 0);

      if (ret != 0) {
         berrno be;
         Dmsg(10, "Unable to delete mountpoint after unmount\n");
         printf("error=\"Unable to delete mountpoint after unmount errno=%s\"",
                be.bstrerror(errno));
      }
      printf(" status=1\n");
      return 1;
   };

   /* TODO: Here we need to check LVM settings */
   int support() {
      char **elt;
      int  mp;

      if (!snapshot::support()) {
         return 0;
      }
      if (!check()) {
         return 0;
      }

      elt = get_lv(arg->device);

      if (!elt) {
         Dmsg(10, "Not detected as LVM\n");
         printf("status=0 error=\"Not detected as LVM\"\n");
         return 0;
      }
      mp = get_value_pos(lvs_header ,"Path");
      printf("status=1 device=\"%s\" type=lvm\n", elt[mp]);
      return 1;
   };

   /* count the number of column in the output */
   int count_col(char *l, char sep) {
      int nb=0;
      for (char *p = l ; *p ; p++) {
         if (*p == sep) {
            nb++;
         }
      }
      return nb;
   };

   /* Decode the Attr field */
   int decode_attr(char *l) {
      /*
       * Volume  type:  (m)irrored,  (M)irrored  without initial sync,
       * (o)rigin, (O)rigin  with  merging  snapshot,  (r)aid,  (R)aid
       * without   initial   sync,   (s)napshot,  merging  (S)napshot,
       * (p)vmove, (v)irtual, mirror or raid (i)mage, mirror  or  raid
       * (I)mage out-of-sync, mirror (l)og device, under (c)onversion,
       * thin (V)olume, (t)hin pool, (T)hin pool data,  raid  or  thin
       * pool m(e)tadata
       */

      return 0;
   };

   bool parse_vgs_output() {
      Mmsg(cmd, "%svgs -o vg_all --separator=; --units b --nosuffix", arg->sudo);
      if (vgs) {
         free_header(vgs, vgs_nbelt);
         vgs_nbelt=0;
      }
      vgs = New(alist(10, not_owned_by_alist));
      if (!parse_output(cmd, vgs, &vgs_nbelt, vgs_header)) {
         return false;
      }
      return true;
   };

   bool parse_lvs_output() {
      Mmsg(cmd, "%slvs -o lv_all --separator=; --units b --nosuffix", arg->sudo);
      if (lvs) {
         free_header(lvs, lvs_nbelt);
         lvs_nbelt=0;
      }
      lvs = New(alist(10, not_owned_by_alist));
      if (!parse_output(cmd, lvs, &lvs_nbelt, lvs_header)) {
         return false;
      }
      return true;
   };

   /* Function to parse LVM command output */
   bool parse_output(char *cmd, alist *ret, int *ret_nbelt, Header *hdr) {
      char *p;
      int   i=0;
      int   pos=0;
      int   nbelt=0;
      char  buf[2048];          /* Size for a single line */
      bool  header_done=false;

      if (run_program_full_output(cmd, 60, errmsg)) {
         strip_quotes(errmsg);
         Dmsg(10, "Unable to run lvs. ERR=%s\n", errmsg);
         return false;
      }

      char **current = NULL;

      for (p = errmsg; *p ; p++) {
         if (*p == ';') {        /* We have a separator, handle current value */
            buf[i]=0;
            if (!header_done) {
               nbelt++; /* Keep the number of element in the line */

               /* Find if we need this value, and where to store it */
               for (int j=0; hdr[j].name ; j++) {
                  if (strcasecmp(buf, hdr[j].name) == 0) {
                     hdr[j].pos = pos;
                     break;
                  }
               }

            } else {
               if (pos == 0) {
                  /* First item, need to allocate new array */
                  current = (char **)malloc(nbelt * sizeof(char *) + 1);
                  memset(current, 0, nbelt * sizeof(char *) + 1);
                  ret->append(current);
               }
               /* Keep the current value */
               current[pos] = bstrdup(buf);
            }
            pos++;
            i = 0;
         } else if (*p == '\n') {
            /* We deal with a new line, so the header is done (if in) */
            header_done = true;
            i = 0;
            pos = 0;

         } else if (i < (int)sizeof(buf)) {
            buf[i++] = *p;

         } else {
            Dmsg(10, "Output too big !!! %s\n", errmsg);
            break;
         }
      }
      *ret_nbelt = nbelt;
      return true;
   };

   int list() {
      char **elt, **elt2 = NULL;
      const char *err = NULL;
      int    p_attr, p_path, p_origin, p_time, p_size;
      POOLMEM *p, *f, *d;
      int    fnl, pnl, status;

      if (!snapshot::list()) {
         return false;
      }

      if (!parse_lvs_output()) {
         return false;
      }

      p_attr = get_value_pos(lvs_header, "Attr");
      p_path = get_value_pos(lvs_header, "Path");
      p_time = get_value_pos(lvs_header, "Time");
      p_size = get_value_pos(lvs_header, "Snap%");
      p_origin = get_value_pos(lvs_header, "Origin");

      if (p_time < 0 || p_origin < 0) {
         printf("status=1 error=\"Unable to get snapshot Origin from lvs command\"\n");
         return false;
      }

      p = get_pool_memory(PM_FNAME);
      f = get_pool_memory(PM_FNAME);
      d = get_pool_memory(PM_FNAME);

      elt2 = get_lv(arg->device);
 
      /* TODO: We need to get the device name from the mount point */
      foreach_alist(elt, lvs) {
         char *attr = elt[p_attr];
         /* swi-a-s-- */
         if (attr[0] == 's') {
            if (attr[4] == 'I') {
               /* 5  State:  (a)ctive, (s)uspended, (I)nvalid snapshot, invalid (S)uspended
                *            snapshot, snapshot (m)erge failed, suspended snapshot (M)erge
                *            failed, mapped (d)evice present without tables, mapped device
                *            present with (i)nactive table, (X) unknown
                */
               status = 0;
               err = "Invalid snapshot";
            } else {
               status = 1;
               err = "";
            }

            split_path_and_filename(elt[p_path], &p, &pnl, &f, &fnl);
            Mmsg(d, "%s%s", p, elt[p_origin]);

            if ((!arg->device || strcmp(arg->device, d) == 0) ||
                (elt2 && strcmp(elt2[p_path], d) == 0))
            {
               /* On LVM, the name is LV_SnapshotName, we can strip the LV_ if we find it */
               Mmsg(p, "%s_", d); /* /dev/mapper/vg_ssd/test_ */
               if (strncmp(p, elt[p_path], strlen(p)) == 0) {
                  pm_strcpy(f, elt[p_path] + strlen(p));/* test_MySnapshot_2020.. => MySnapshot_2020 */
               }

               printf("volume=\"%s\" device=\"%s\" name=\"%s\" createdate=\"%s\" size=\"%s\" "
                      "status=%d error=\"%s\" type=lvm\n",
                      elt[p_path], d, f, elt[p_time], elt[p_size], status, err);
            }
         }
      }
      free_pool_memory(p);
      free_pool_memory(f);
      free_pool_memory(d);
      return true;
   };
};

/* The simulator is using a simple symlink */
class simulator: public snapshot {
public:
   simulator(arguments *arg): snapshot(arg, "simulator") {};

   int mount() {
      if (!snapshot::mount()) {
         return 0;
      }
      split_path_and_filename(arg->volume, &path, &pnl, &fname, &fnl);
      printf("status=1 snapmountpoint=\"%s\" snapdirectory=\"%s\"\n",
             arg->volume, path);
      return 1;
   };

   int unmount() {
      printf("status=1\n");
      return 1;
   };

   int support() {
      if (!snapshot::support()) {
         return 0;
      }
      if (access(arg->mountpoint, W_OK) != 0) {
         printf("status=0 device=\"%s\" type=simulator "
                "error=\"Unable to access mountpoint\"\n", 
                arg->mountpoint);
         return 0;
      }
      printf("status=1 device=\"%s\" type=simulator\n", arg->mountpoint);
      return 1;
   };

   int create() {
      char    ed1[50];
      utime_t now;

      if (!snapshot::create()) {
         return 0;
      }
      Mmsg(path, "%s/%s", arg->mountpoint, arg->snapdir);
      makedir(path);
      now = time(NULL);
      Mmsg(cmd, "ln -vsf \"%s\" \"%s\"", arg->mountpoint, path);
      if (run_program(cmd, 60, errmsg)) {
         Dmsg(10, "Unable to create symlink. ERR=%s\n", errmsg);
         strip_quotes(errmsg);
         printf("status=%d error=\"Unable to umount the device %s\"\n",
                get_error_code(),
                errmsg);
      }
      printf("status=1 volume=\"%s\" createtdate=%s type=simulator\n", 
             path, edit_uint64(now, ed1));
      return 1;
   };

   int del() {
      int ret;
      if (!snapshot::del()) {
         return 0;
      }
      ret = unlink(arg->volume);
      printf("status=%d\n", (ret == 0)? 1 : 0);
      return 1;
   };
};

snapshot *detect_snapshot_backend(arguments *arg)
{
   if (arg->type) {
      if (strcasecmp(arg->type, "btrfs") == 0) {
         return new btrfs(arg);

      } else if (strcasecmp(arg->type, "lvm") == 0) {
         return new lvm(arg);

      } else if (strcasecmp(arg->type, "simulator") == 0) {
         return new simulator(arg);

      } else if (strcasecmp(arg->type, "zfs") == 0) {
         return new zfs(arg);
      }
   }
   if (arg->fstype) {
      if (strcasecmp(arg->fstype, "btrfs") == 0) {
         return new btrfs(arg);

      } else if (strcasecmp(arg->fstype, "tmpfs") == 0) {
         return new simulator(arg);

      /* TODO: Need to find something smarter here */
      } else if (strcasecmp(arg->fstype, "ext4") == 0) {
         return new lvm(arg);

      } else if (strcasecmp(arg->fstype, "xfs") == 0) {
         return new lvm(arg);

      } else if (strcasecmp(arg->fstype, "ext3") == 0) {
         return new lvm(arg);

      } else if (strcasecmp(arg->fstype, "zfs") == 0 ||
                 strcasecmp(arg->fstype, "fuse.zfs") == 0) 
      {
         return new zfs(arg);
      }
   }
   Dmsg(10, "Backend not found\n");
   return NULL;
}

/* defined in jcr.c */
void create_jcr_key();

int main(int argc, char **argv)
{
   snapshot *snap;
   arguments arg;
   char      ch;
   int       ret=0;
   struct stat sp;

   set_trace_file("/dev/null");
   setlocale(LC_ALL, "");
   setenv("LANG", "C", true);
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");
   lmgr_init_thread();
   OSDependentInit();
   init_stack_dump();
   my_name_is(argc, argv, "bsnapshot");
   create_jcr_key();

   while ((ch = getopt(argc, argv, "?d:vc:so:V:T:t")) != -1) {
      switch (ch) {
      case 'd':                       /* set debug level */
         debug_level = atoi(optarg);
         if (debug_level <= 0) {
            debug_level = 1;
         }
         break;

      case 'v':
         arg.verbose++;
         break;

      case 's':                 /* use sudo */
         arg.sudo = "sudo ";
         break;

      case 'c':                 /* config file */
         pm_strcpy(arg.config_file, optarg);
         if (stat(optarg, &sp) < 0) {
            Pmsg(000, "Unable to access %s. ERR=%s\n",optarg, strerror(errno));
            usage(_("Unable to open -p argument for reading"));
         }
         break;

      case 'o':                 /* where to send the debug output */
         set_trace_file(optarg);
         break;

      case 't':
         arg.action = (char *)"check";
         break;

      case 'V':                 /* set volume name */
         arg.volume = optarg;
         break;

      case 'T':                 /* device type */
         arg.type = optarg;
         break;
      default:
         usage();
      }
   }

   argc -= optind;
   argv += optind;

   if (!arg.validate()) {
      usage();
   }

   if (arg.disabled) {
      Dmsg(10, "disabled from config file\n");
      exit (1);
   }

   snap = detect_snapshot_backend(&arg);

   if (!snap) {
      printf("status=0 error=\"Unable to detect snapshot backend\"");
      exit(0);
   }

   start_watchdog();

   if (strcasecmp(arg.action, "mount") == 0) {
      ret = snap->mount();

   } else if (strcasecmp(arg.action, "support") == 0) {
      ret = snap->support();

   } else if (strcasecmp(arg.action, "create") == 0) {
      ret = snap->create();

   } else if (strcasecmp(arg.action, "delete") == 0) {
      ret = snap->del();

   } else if (strcasecmp(arg.action, "subvolumes") == 0) {
      ret = snap->subvolumes();

   } else if (strcasecmp(arg.action, "list") == 0) {
      ret = snap->list();

   } else if (strcasecmp(arg.action, "check") == 0) {
      ret = snap->check();

   } else if (strcasecmp(arg.action, "unmount") == 0) {
      ret = snap->unmount();
   }

   delete snap;
   stop_watchdog();
   close_memory_pool();
   lmgr_cleanup_main();

   Dmsg(10, "exit code = %d\n", (ret == 1) ? 0 : 1);
   return (ret == 1)? 0 : 1;
}
