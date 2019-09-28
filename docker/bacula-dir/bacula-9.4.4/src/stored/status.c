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
 *  This file handles the status command
 *
 *     Kern Sibbald, May MMIII
 *
 *
 */

#include "bacula.h"
#include "stored.h"
#include "lib/status.h"
#include "sd_plugins.h"

/* Imported functions */
extern void dbg_print_plugin(FILE *fp);

/* Imported variables */
extern BSOCK *filed_chan;
extern void *start_heap;

/* Static variables */
static char OKqstatus[]   = "3000 OK .status\n";
static char DotStatusJob[] = "JobId=%d JobStatus=%c JobErrors=%d\n";


/* Forward referenced functions */
static void sendit(POOL_MEM &msg, int len, STATUS_PKT *sp);
static void sendit(const char *msg, int len, void *arg);
static void dbg_sendit(const char *msg, int len, void *arg);
static void send_blocked_status(DEVICE *dev, STATUS_PKT *sp);
static void send_device_status(DEVICE *dev, STATUS_PKT *sp);
static void list_running_jobs(STATUS_PKT *sp);
static void list_jobs_waiting_on_reservation(STATUS_PKT *sp);
static void list_status_header(STATUS_PKT *sp);
static void list_devices(STATUS_PKT *sp, char *name=NULL);
static void list_plugins(STATUS_PKT *sp);
static void list_cloud_transfers(STATUS_PKT *sp, bool verbose);

void status_alert_callback(void *ctx, const char *short_msg,
   const char *long_msg, char *Volume, int severity,
   int flags, int alertno, utime_t alert_time)
{
   STATUS_PKT *sp = (STATUS_PKT *)ctx;
   const char *type = "Unknown";
   POOL_MEM send_msg(PM_MESSAGE);
   char edt[50];
   int len;

   switch (severity) {
   case 'C':
      type = "Critical";
      break;
   case 'W':
      type = "Warning";
      break;
   case 'I':
      type = "Info";
      break;
   }
   bstrftimes(edt, sizeof(edt), alert_time);
   if (chk_dbglvl(10)) {
      len = Mmsg(send_msg, _("    %s Alert: at %s Volume=\"%s\" flags=0x%x alert=%s\n"),
         type, edt, Volume, flags, long_msg);
   } else {
      len = Mmsg(send_msg, _("    %s Alert: at %s Volume=\"%s\" alert=%s\n"),
         type, edt, Volume, short_msg);
   }
   sendit(send_msg, len, sp);
}


/*
 * Status command from Director
 */
void output_status(STATUS_PKT *sp)
{
   POOL_MEM msg(PM_MESSAGE);
   int len;

   list_status_header(sp);

   /*
    * List running jobs
    */
   list_running_jobs(sp);

   /*
    * List jobs stuck in reservation system
    */
   list_jobs_waiting_on_reservation(sp);

   /*
    * List terminated jobs (defined in lib/status.h)
    */
   list_terminated_jobs(sp);

   /*
    * List devices
    */
   list_devices(sp);

   /*
    * List cloud transfers
    */
   list_cloud_transfers(sp, false);


   len = Mmsg(msg, _("Used Volume status:\n"));
   if (!sp->api) sendit(msg, len, sp);

   list_volumes(sendit, (void *)sp);
   if (!sp->api) sendit("====\n\n", 6, sp);

   list_spool_stats(sendit, (void *)sp);
   if (!sp->api) sendit("====\n\n", 6, sp);

   if (chk_dbglvl(10)) {
      dbg_print_plugin(stdout);
   }
}

static void list_resources(STATUS_PKT *sp)
{
#ifdef when_working
   POOL_MEM msg(PM_MESSAGE);
   int len;

   len = Mmsg(msg, _("\nSD Resources:\n"));
   if (!sp->api) sendit(msg, len, sp);
   dump_resource(R_DEVICE, resources[R_DEVICE-r_first], sp);
   if (!sp->api) sendit("====\n\n", 6, sp);
#endif
}

#ifdef xxxx
static find_device(char *devname)
{
   foreach_res(device, R_DEVICE) {
      if (strcasecmp(device->hdr.name, devname) == 0) {
         found = true;
         break;
      }
   }
   if (!found) {
      foreach_res(changer, R_AUTOCHANGER) {
         if (strcasecmp(changer->hdr.name, devname) == 0) {
            break;
         }
      }
   }
}
#endif

static void api_list_one_device(char *name, DEVICE *dev, STATUS_PKT *sp)
{
   OutputWriter ow(sp->api_opts);
   int zero=0;
   int blocked=0;
   uint64_t f, t;
   const char *p=NULL;

   if (!dev) {
      return;
   }

   dev->get_freespace(&f, &t);

   ow.get_output(OT_START_OBJ,
                 OT_STRING, "name", dev->device->hdr.name,
                 OT_STRING, "archive_device", dev->archive_name(),
                 OT_STRING, "type", dev->print_type(),
                 OT_STRING, "media_type", dev->device->media_type,
                 OT_INT,    "open", (int)dev->is_open(),
                 OT_INT,    "writers",      dev->num_writers,
                 OT_INT32,  "maximum_concurrent_jobs", dev->max_concurrent_jobs,
                 OT_INT64,  "maximum_volume_size", dev->max_volume_size,
                 OT_INT,    "read_only", dev->device->read_only,
                 OT_INT,    "autoselect", dev->device->autoselect,
                 OT_INT,    "enabled", dev->enabled,
                 OT_INT64,  "free_space", f,
                 OT_INT64,  "total_space", t,
                 OT_INT64,  "devno",     dev->devno,
                 OT_END);

   if (dev->is_open()) {
      if (dev->is_labeled()) {
         ow.get_output(OT_STRING, "mounted", dev->blocked()?"0":"1",
                       OT_STRING, "waiting", dev->blocked()?"1":"0",
                       OT_STRING, "volume",  dev->VolHdr.VolumeName,
                       OT_STRING, "pool",    NPRTB(dev->pool_name),
                       OT_END);
      } else {
         ow.get_output(OT_INT,    "mounted", zero,
                       OT_INT,    "waiting", zero,
                       OT_STRING, "volume",  "",
                       OT_STRING, "pool",    "",
                       OT_END);
      }

      blocked = 1;
      switch(dev->blocked()) {
      case BST_UNMOUNTED:
         p = "User unmounted";
         break;
      case BST_UNMOUNTED_WAITING_FOR_SYSOP:
         p = "User unmounted during wait for media/mount";
         break;
      case BST_DOING_ACQUIRE:
         p = "Device is being initialized";
         break;
      case BST_WAITING_FOR_SYSOP:
         p = "Waiting for mount or create a volume";
         break;
      case BST_WRITING_LABEL:
         p = "Labeling a Volume";
         break;
      default:
         blocked=0;
         p = NULL;
      }

      /* TODO: give more information about blocked status
       * and the volume needed if WAITING for SYSOP
       */
      ow.get_output(OT_STRING, "blocked_desc", NPRTB(p),
                    OT_INT,    "blocked",      blocked,
                    OT_END);

      ow.get_output(OT_INT, "append", (int)dev->can_append(),
                    OT_END);

      if (dev->can_append()) {
         ow.get_output(OT_INT64, "bytes",  dev->VolCatInfo.VolCatBytes,
                       OT_INT32, "blocks", dev->VolCatInfo.VolCatBlocks,
                       OT_END);

      } else {  /* reading */
         ow.get_output(OT_INT64, "bytes",  dev->VolCatInfo.VolCatRBytes,
                       OT_INT32, "blocks", dev->VolCatInfo.VolCatReads, /* might not be blocks */
                       OT_END);

      }
      ow.get_output(OT_INT, "file",  dev->file,
                    OT_INT, "block", dev->block_num,
                    OT_END);
   } else {
      ow.get_output(OT_INT,    "mounted", zero,
                    OT_INT,    "waiting", zero,
                    OT_STRING, "volume",  "",
                    OT_STRING, "pool",    "",
                    OT_STRING, "blocked_desc", "",
                    OT_INT,    "blocked", zero,
                    OT_INT,    "append",  zero,
                    OT_INT,    "bytes",   zero,
                    OT_INT,    "blocks",  zero,
                    OT_INT,    "file",    zero,
                    OT_INT,    "block",   zero,
                    OT_END);
   }

   p = ow.get_output(OT_END_OBJ, OT_END);
   sendit(p, strlen(p), sp);
}


static void list_one_device(char *name, DEVICE *dev, STATUS_PKT *sp)
{
   char b1[35], b2[35], b3[35];
   POOL_MEM msg(PM_MESSAGE);
   int len;
   int bpb;

   if (sp->api > 1) {
      api_list_one_device(name, dev, sp);
      return;
   }

   if (!dev) {
      len = Mmsg(msg, _("\nDevice \"%s\" is not open or does not exist.\n"),
                 name);
      sendit(msg, len, sp);
      if (!sp->api) sendit("==\n", 4, sp);
      return;
   }

   if (dev->is_open()) {
      if (dev->is_labeled()) {
         len = Mmsg(msg, _("\nDevice %s is %s %s:\n"
                           "    Volume:      %s\n"
                           "    Pool:        %s\n"
                           "    Media type:  %s\n"),
            dev->print_type(), dev->print_name(),
            dev->blocked()?_("waiting for"):_("mounted with"),
            dev->VolHdr.VolumeName,
            dev->pool_name[0]?dev->pool_name:_("*unknown*"),
            dev->device->media_type);
         sendit(msg, len, sp);
      } else {
         len = Mmsg(msg, _("\nDevice %s: %s open but no Bacula volume is currently mounted.\n"),
            dev->print_type(), dev->print_name());
         sendit(msg, len, sp);
      }
      if (dev->can_append()) {
         bpb = dev->VolCatInfo.VolCatBlocks;
         if (bpb <= 0) {
            bpb = 1;
         }
         bpb = dev->VolCatInfo.VolCatBytes / bpb;
         len = Mmsg(msg, _("    Total Bytes=%s Blocks=%s Bytes/block=%s\n"),
            edit_uint64_with_commas(dev->VolCatInfo.VolCatBytes, b1),
            edit_uint64_with_commas(dev->VolCatInfo.VolCatBlocks, b2),
            edit_uint64_with_commas(bpb, b3));
         sendit(msg, len, sp);
      } else {  /* reading */
         bpb = dev->VolCatInfo.VolCatReads;
         if (bpb <= 0) {
            bpb = 1;
         }
         if (dev->VolCatInfo.VolCatRBytes > 0) {
            bpb = dev->VolCatInfo.VolCatRBytes / bpb;
         } else {
            bpb = 0;
         }
         len = Mmsg(msg, _("    Total Bytes Read=%s Blocks Read=%s Bytes/block=%s\n"),
            edit_uint64_with_commas(dev->VolCatInfo.VolCatRBytes, b1),
            edit_uint64_with_commas(dev->VolCatInfo.VolCatReads, b2),
            edit_uint64_with_commas(bpb, b3));
         sendit(msg, len, sp);
      }
      len = Mmsg(msg, _("    Positioned at File=%s Block=%s\n"),
         edit_uint64_with_commas(dev->file, b1),
         edit_uint64_with_commas(dev->block_num, b2));
      sendit(msg, len, sp);
   } else {
      len = Mmsg(msg, _("\nDevice %s: %s is not open.\n"),
                 dev->print_type(), dev->print_name());
      sendit(msg, len, sp);
   }
   send_blocked_status(dev, sp);

   /* TODO: We need to check with Mount command, maybe we can
    * display this number only when the device is open.
    */
   if (dev->is_file()) {
      char ed1[50];
      uint64_t f, t;
      dev->get_freespace(&f, &t);
      if (t > 0) {              /* We might not have access to numbers */
         len = Mmsg(msg, _("   Available %sSpace=%sB\n"),
                    dev->is_cloud() ? _("Cache ") : "",
                    edit_uint64_with_suffix(f, ed1));
         sendit(msg, len, sp);
      }
   }

   dev->show_tape_alerts((DCR *)sp, list_short, list_all, status_alert_callback);

   if (!sp->api) sendit("==\n", 4, sp);
}

void _dbg_list_one_device(char *name, DEVICE *dev, const char *file, int line)
{
   STATUS_PKT sp;
   sp.bs = NULL;
   sp.callback = dbg_sendit;
   sp.context = NULL;
   d_msg(file, line, 0, "Called dbg_list_one_device():");
   list_one_device(name, dev, &sp);
   send_device_status(dev, &sp);
}

static void list_one_autochanger(char *name, AUTOCHANGER *changer, STATUS_PKT *sp)
{
   int     len;
   char   *p;
   DEVRES *device;
   POOL_MEM msg(PM_MESSAGE);
   OutputWriter ow(sp->api_opts);

   if (sp->api > 1) {
      ow.get_output(OT_START_OBJ,
                    OT_STRING,    "autochanger",  changer->hdr.name,
                    OT_END);

      ow.start_group("devices");

      foreach_alist(device, changer->device) {
         ow.get_output(OT_START_OBJ,
                       OT_STRING, "name",  device->hdr.name,
                       OT_STRING, "device",device->device_name,
                       OT_END_OBJ,
                       OT_END);
      }

      ow.end_group();

      p = ow.get_output(OT_END_OBJ, OT_END);
      sendit(p, strlen(p), sp);

   } else {

      len = Mmsg(msg, _("Autochanger \"%s\" with devices:\n"),
                 changer->hdr.name);
      sendit(msg, len, sp);

      foreach_alist(device, changer->device) {
         if (device->dev) {
            len = Mmsg(msg, "   %s\n", device->dev->print_name());
            sendit(msg, len, sp);
         } else {
            len = Mmsg(msg, "   %s\n", device->hdr.name);
            sendit(msg, len, sp);
         }
      }
   }
}

static void list_devices(STATUS_PKT *sp, char *name)
{
   int len;
   DEVRES *device;
   AUTOCHANGER *changer;
   POOL_MEM msg(PM_MESSAGE);

   if (!sp->api) {
      len = Mmsg(msg, _("\nDevice status:\n"));
      sendit(msg, len, sp);
   }

   foreach_res(changer, R_AUTOCHANGER) {
      if (!name || strcmp(changer->hdr.name, name) == 0) {
         list_one_autochanger(changer->hdr.name, changer, sp);
      }
   }

   foreach_res(device, R_DEVICE) {
      if (!name || strcmp(device->hdr.name, name) == 0) {
         list_one_device(device->hdr.name, device->dev, sp);
      }
   }
   if (!sp->api) sendit("====\n\n", 6, sp);
}

static void list_cloud_transfers(STATUS_PKT *sp, bool verbose)
{
   bool first=true;
   int len;
   DEVRES *device;
   POOL_MEM msg(PM_MESSAGE);

   foreach_res(device, R_DEVICE) {
      if (device->dev && device->dev->is_cloud()) {

         if (first) {
            if (!sp->api) {
               len = Mmsg(msg, _("Cloud transfer status:\n"));
               sendit(msg, len, sp);
            }
            first = false;
         }

         cloud_dev *cdev = (cloud_dev*)device->dev;
         len = cdev->get_cloud_upload_transfer_status(msg, verbose);
         sendit(msg, len, sp);
         len = cdev->get_cloud_download_transfer_status(msg, verbose);
         sendit(msg, len, sp);
         break; /* only once, transfer mgr are shared */
      }
   }

   if (!first && !sp->api) sendit("====\n\n", 6, sp);
}

static void api_list_sd_status_header(STATUS_PKT *sp)
{
   char *p;
   alist drivers(10, not_owned_by_alist);
   OutputWriter wt(sp->api_opts);

   sd_list_loaded_drivers(&drivers);
   wt.start_group("header");
   wt.get_output(
      OT_STRING, "name",        my_name,
      OT_STRING, "version",     VERSION " (" BDATE ")",
      OT_STRING, "uname",       HOST_OS " " DISTNAME " " DISTVER,
      OT_UTIME,  "started",     daemon_start_time,
      OT_INT64,  "pid",         (int64_t)getpid(),
      OT_INT,    "jobs_run",    num_jobs_run,
      OT_INT,    "jobs_running",job_count(),
      OT_INT,    "ndevices",    ((rblist *)res_head[R_DEVICE-r_first]->res_list)->size(),
      OT_INT,    "nautochgr",   ((rblist *)res_head[R_AUTOCHANGER-r_first]->res_list)->size(),
      OT_PLUGINS,"plugins",     b_plugin_list,
      OT_ALIST_STR, "drivers",  &drivers,
      OT_END);
   p = wt.end_group();
   sendit(p, strlen(p), sp);
}

static void list_status_header(STATUS_PKT *sp)
{
   char dt[MAX_TIME_LENGTH];
   char b1[35], b2[35], b3[35], b4[35], b5[35];
   POOL_MEM msg(PM_MESSAGE);
   int len;

   if (sp->api) {
      api_list_sd_status_header(sp);
      return;
   }

   len = Mmsg(msg, _("%s %sVersion: %s (%s) %s %s %s\n"),
              my_name, "", VERSION, BDATE, HOST_OS, DISTNAME, DISTVER);
   sendit(msg, len, sp);

   bstrftime_nc(dt, sizeof(dt), daemon_start_time);


   len = Mmsg(msg, _("Daemon started %s. Jobs: run=%d, running=%d.\n"),
        dt, num_jobs_run, job_count());
   sendit(msg, len, sp);
   len = Mmsg(msg, _(" Heap: heap=%s smbytes=%s max_bytes=%s bufs=%s max_bufs=%s\n"),
         edit_uint64_with_commas((char *)sbrk(0)-(char *)start_heap, b1),
         edit_uint64_with_commas(sm_bytes, b2),
         edit_uint64_with_commas(sm_max_bytes, b3),
         edit_uint64_with_commas(sm_buffers, b4),
         edit_uint64_with_commas(sm_max_buffers, b5));
   sendit(msg, len, sp);
   len = Mmsg(msg, " Sizes: boffset_t=%d size_t=%d int32_t=%d int64_t=%d "
              "mode=%d,%d newbsr=%d\n",
              (int)sizeof(boffset_t), (int)sizeof(size_t), (int)sizeof(int32_t),
              (int)sizeof(int64_t), (int)DEVELOPER_MODE, 0, use_new_match_all);
   sendit(msg, len, sp);
   len = Mmsg(msg, _(" Res: ndevices=%d nautochgr=%d\n"),
      ((rblist *)res_head[R_DEVICE-r_first]->res_list)->size(),
      ((rblist *)res_head[R_AUTOCHANGER-r_first]->res_list)->size());
   sendit(msg, len, sp);
   list_plugins(sp);
}

static void send_blocked_status(DEVICE *dev, STATUS_PKT *sp)
{
   POOL_MEM msg(PM_MESSAGE);
   int len;

   if (!dev) {
      len = Mmsg(msg, _("No DEVICE structure.\n\n"));
      sendit(msg, len, sp);
      return;
   }
   if (!dev->enabled) {
      len = Mmsg(msg, _("   Device is disabled. User command.\n"));
      sendit(msg, len, sp);
   }
   switch (dev->blocked()) {
   case BST_UNMOUNTED:
      len = Mmsg(msg, _("   Device is BLOCKED. User unmounted.\n"));
      sendit(msg, len, sp);
      break;
   case BST_UNMOUNTED_WAITING_FOR_SYSOP:
      len = Mmsg(msg, _("   Device is BLOCKED. User unmounted during wait for media/mount.\n"));
      sendit(msg, len, sp);
      break;
   case BST_WAITING_FOR_SYSOP:
      {
         DCR *dcr;
         bool found_jcr = false;
         dev->Lock();
         dev->Lock_dcrs();
         foreach_dlist(dcr, dev->attached_dcrs) {
            if (dcr->jcr->JobStatus == JS_WaitMount) {
               len = Mmsg(msg, _("   Device is BLOCKED waiting for mount of volume \"%s\",\n"
                                 "       Pool:        %s\n"
                                 "       Media type:  %s\n"),
                          dcr->VolumeName,
                          dcr->pool_name,
                          dcr->media_type);
               sendit(msg, len, sp);
               found_jcr = true;
            } else if (dcr->jcr->JobStatus == JS_WaitMedia) {
               len = Mmsg(msg, _("   Device is BLOCKED waiting to create a volume for:\n"
                                 "       Pool:        %s\n"
                                 "       Media type:  %s\n"),
                          dcr->pool_name,
                          dcr->media_type);
               sendit(msg, len, sp);
               found_jcr = true;
            }
         }
         dev->Unlock_dcrs();
         dev->Unlock();
         if (!found_jcr) {
            len = Mmsg(msg, _("   Device is BLOCKED waiting for media.\n"));
            sendit(msg, len, sp);
         }
      }
      break;
   case BST_DOING_ACQUIRE:
      len = Mmsg(msg, _("   Device is being initialized.\n"));
      sendit(msg, len, sp);
      break;
   case BST_WRITING_LABEL:
      len = Mmsg(msg, _("   Device is blocked labeling a Volume.\n"));
      sendit(msg, len, sp);
      break;
   default:
      break;
   }
   /* Send autochanger slot status */
   if (dev->is_autochanger()) {
      if (dev->get_slot() > 0) {
         len = Mmsg(msg, _("   Slot %d %s loaded in drive %d.\n"),
            dev->get_slot(), dev->is_open()?"is": "was last", dev->drive_index);
         sendit(msg, len, sp);
      } else if (dev->get_slot() <= 0) {
         len = Mmsg(msg, _("   Drive %d is not loaded.\n"), dev->drive_index);
         sendit(msg, len, sp);
      }
   }
   if (chk_dbglvl(1)) {
      send_device_status(dev, sp);
   }
}

void send_device_status(DEVICE *dev, STATUS_PKT *sp)
{
   POOL_MEM msg(PM_MESSAGE);
   int len;
   DCR *dcr = NULL;
   bool found = false;
   char b1[35];
   

   if (chk_dbglvl(5)) {
      len = Mmsg(msg, _("Configured device capabilities:\n"));
      sendit(msg, len, sp);
      len = Mmsg(msg, "   %sEOF %sBSR %sBSF %sFSR %sFSF %sEOM %sREM %sRACCESS %sAUTOMOUNT %sLABEL %sANONVOLS %sALWAYSOPEN\n",
         dev->capabilities & CAP_EOF ? "" : "!",
         dev->capabilities & CAP_BSR ? "" : "!",
         dev->capabilities & CAP_BSF ? "" : "!",
         dev->capabilities & CAP_FSR ? "" : "!",
         dev->capabilities & CAP_FSF ? "" : "!",
         dev->capabilities & CAP_EOM ? "" : "!",
         dev->capabilities & CAP_REM ? "" : "!",
         dev->capabilities & CAP_RACCESS ? "" : "!",
         dev->capabilities & CAP_AUTOMOUNT ? "" : "!",
         dev->capabilities & CAP_LABEL ? "" : "!",
         dev->capabilities & CAP_ANONVOLS ? "" : "!",
         dev->capabilities & CAP_ALWAYSOPEN ? "" : "!");
      sendit(msg, len, sp);
   }

   len = Mmsg(msg, _("Device state:\n"));
   sendit(msg, len, sp);
   len = Mmsg(msg, "   %sOPENED %sTAPE %sLABEL %sAPPEND %sREAD %sEOT %sWEOT %sEOF %sWORM %sNEXTVOL %sSHORT %sMOUNTED %sMALLOC\n",
      dev->is_open() ? "" : "!",
      dev->is_tape() ? "" : "!",
      dev->is_labeled() ? "" : "!",
      dev->can_append() ? "" : "!",
      dev->can_read() ? "" : "!",
      dev->at_eot() ? "" : "!",
      dev->state & ST_WEOT ? "" : "!",
      dev->at_eof() ? "" : "!",
      dev->is_worm() ?  "" : "!",
      dev->state & ST_NEXTVOL ?  "" : "!",
      dev->state & ST_SHORT ?  "" : "!",
      dev->state & ST_MOUNTED ?  "" : "!",
      dev->state & ST_MALLOC ?  "" : "!");
   sendit(msg, len, sp);

   len = Mmsg(msg, _("   Writers=%d reserves=%d blocked=%d enabled=%d usage=%s\n"), dev->num_writers,
              dev->num_reserved(), dev->blocked(), dev->enabled,
               edit_uint64_with_commas(dev->usage, b1));

   sendit(msg, len, sp);

   len = Mmsg(msg, _("Attached JobIds: "));
   sendit(msg, len, sp);
   dev->Lock();
   dev->Lock_dcrs();
   foreach_dlist(dcr, dev->attached_dcrs) {
      if (dcr->jcr) {
         if (found) {
            sendit(",", 1, sp);
         }
         len = Mmsg(msg, "%d", (int)dcr->jcr->JobId);
         sendit(msg, len, sp);
         found = true;
      }
   }
   dev->Unlock_dcrs();
   dev->Unlock();
   sendit("\n", 1, sp);

   len = Mmsg(msg, _("Device parameters:\n"));
   sendit(msg, len, sp);
   len = Mmsg(msg, _("   Archive name: %s Device name: %s\n"), dev->archive_name(),
      dev->name());
   sendit(msg, len, sp);
   len = Mmsg(msg, _("   File=%u block=%u\n"), dev->file, dev->block_num);
   sendit(msg, len, sp);
   len = Mmsg(msg, _("   Min block=%u Max block=%u\n"), dev->min_block_size, dev->max_block_size);
   sendit(msg, len, sp);
}

static void api_list_running_jobs(STATUS_PKT *sp)
{
   char *p1, *p2, *p3;
   int i1, i2, i3;
   OutputWriter ow(sp->api_opts);

   uint64_t inst_bps, total_bps;
   int inst_sec, total_sec;
   JCR *jcr;
   DCR *dcr, *rdcr;
   time_t now = time(NULL);

   foreach_jcr(jcr) {
      if (jcr->getJobType() == JT_SYSTEM) {
         continue;
      }
      ow.get_output(OT_CLEAR,
                    OT_START_OBJ,
                    OT_INT32,   "jobid",     jcr->JobId,
                    OT_STRING,  "job",       jcr->Job,
                    OT_JOBLEVEL,"level",     jcr->getJobLevel(),
                    OT_JOBTYPE, "type",      jcr->getJobType(),
                    OT_JOBSTATUS,"status",   jcr->JobStatus,
                    OT_PINT64,  "jobbytes",  jcr->JobBytes,
                    OT_INT32,   "jobfiles",  jcr->JobFiles,
                    OT_UTIME,   "starttime", jcr->start_time,
                    OT_INT32,   "errors",    jcr->JobErrors,
                    OT_INT32,   "newbsr",    (int32_t)jcr->use_new_match_all,
                    OT_END);

      dcr = jcr->dcr;
      rdcr = jcr->read_dcr;

      p1 = p2 = p3 = NULL;
      if (rdcr && rdcr->device) {
         p1 = rdcr->VolumeName;
         p2 = rdcr->pool_name;
         p3 = rdcr->device->hdr.name;
      }
      ow.get_output(OT_STRING,  "read_volume",  NPRTB(p1),
                    OT_STRING,  "read_pool",    NPRTB(p2),
                    OT_STRING,  "read_device",  NPRTB(p3),
                    OT_END);

      p1 = p2 = p3 = NULL;
      i1 = i2 = i3 = 0;
      if (dcr && dcr->device) {
         p1 = dcr->VolumeName;
         p2 = dcr->pool_name;
         p3 = dcr->device->hdr.name;
         i1 = dcr->spooling;
         i2 = dcr->despooling;
         i3 = dcr->despool_wait;
      }

      ow.get_output(OT_STRING,  "write_volume",  NPRTB(p1),
                    OT_STRING,  "write_pool",    NPRTB(p2),
                    OT_STRING,  "write_device",  NPRTB(p3),
                    OT_INT,     "spooling",      i1,
                    OT_INT,     "despooling",    i2,
                    OT_INT,     "despool_wait",  i3,
                    OT_END);

      if (jcr->last_time == 0) {
         jcr->last_time = jcr->run_time;
      }

      total_sec = now - jcr->run_time;
      inst_sec = now - jcr->last_time;

      if (total_sec <= 0) {
         total_sec = 1;
      }
      if (inst_sec <= 0) {
         inst_sec = 1;
      }

      /* Instanteous bps not smoothed */
      inst_bps = (jcr->JobBytes - jcr->LastJobBytes) / inst_sec;
      if (jcr->LastRate == 0) {
         jcr->LastRate = inst_bps;
      }

      /* Smooth the instantaneous bps a bit */
      inst_bps = (2 * jcr->LastRate + inst_bps) / 3;
      /* total bps (AveBytes/sec) since start of job */
      total_bps = jcr->JobBytes / total_sec;

      p1 = ow.get_output(OT_PINT64, "avebytes_sec",   total_bps,
                         OT_PINT64, "lastbytes_sec",  inst_bps,
                         OT_END_OBJ,
                         OT_END);

      sendit(p1, strlen(p1), sp);

      /* Update only every 10 seconds */
      if (now - jcr->last_time > 10) {
         jcr->LastRate = inst_bps;
         jcr->LastJobBytes = jcr->JobBytes;
         jcr->last_time = now;
      }
   }
   endeach_jcr(jcr);

}

static void list_running_jobs(STATUS_PKT *sp)
{
   bool found = false;
   uint64_t inst_bps, total_bps;
   int inst_sec, total_sec;
   JCR *jcr;
   DCR *dcr, *rdcr;
   char JobName[MAX_NAME_LENGTH];
   char b1[50], b2[50], b3[50], b4[50];
   int len;
   POOL_MEM msg(PM_MESSAGE);
   time_t now = time(NULL);

   if (sp->api > 1) {
      api_list_running_jobs(sp);
      return;
   }

   len = Mmsg(msg, _("\nRunning Jobs:\n"));
   if (!sp->api) sendit(msg, len, sp);

   foreach_jcr(jcr) {
      if (jcr->JobStatus == JS_WaitFD) {
         len = Mmsg(msg, _("%s Job %s waiting for Client connection.\n"),
            job_type_to_str(jcr->getJobType()), jcr->Job);
         sendit(msg, len, sp);
      }
      dcr = jcr->dcr;
      rdcr = jcr->read_dcr;
      if ((dcr && dcr->device) || (rdcr && rdcr->device)) {
         bstrncpy(JobName, jcr->Job, sizeof(JobName));
         /* There are three periods after the Job name */
         char *p;
         for (int i=0; i<3; i++) {
            if ((p=strrchr(JobName, '.')) != NULL) {
               *p = 0;
            }
         }
         if (rdcr && rdcr->device) {
            len = Mmsg(msg, _("Reading: %s %s job %s JobId=%d Volume=\"%s\"\n"
                            "    pool=\"%s\" device=%s newbsr=%d\n"),
                   job_level_to_str(jcr->getJobLevel()),
                   job_type_to_str(jcr->getJobType()),
                   JobName,
                   jcr->JobId,
                   rdcr->VolumeName,
                   rdcr->pool_name,
                   rdcr->dev?rdcr->dev->print_name():
                            rdcr->device->device_name,
                   jcr->use_new_match_all
               );
            sendit(msg, len, sp);
         } else if (dcr && dcr->device) {
            len = Mmsg(msg, _("Writing: %s %s job %s JobId=%d Volume=\"%s\"\n"
                            "    pool=\"%s\" device=%s\n"),
                   job_level_to_str(jcr->getJobLevel()),
                   job_type_to_str(jcr->getJobType()),
                   JobName,
                   jcr->JobId,
                   dcr->VolumeName,
                   dcr->pool_name,
                   dcr->dev?dcr->dev->print_name():
                            dcr->device->device_name);
            sendit(msg, len, sp);
            len= Mmsg(msg, _("    spooling=%d despooling=%d despool_wait=%d\n"),
                   dcr->spooling, dcr->despooling, dcr->despool_wait);
            sendit(msg, len, sp);
         }
         if (jcr->last_time == 0) {
            jcr->last_time = jcr->run_time;
         }
         total_sec = now - jcr->run_time;
         inst_sec = now - jcr->last_time;
         if (total_sec <= 0) {
            total_sec = 1;
         }
         if (inst_sec <= 0) {
            inst_sec = 1;
         }
         /* Instanteous bps not smoothed */
         inst_bps = (jcr->JobBytes - jcr->LastJobBytes) / inst_sec;
         if (jcr->LastRate == 0) {
            jcr->LastRate = inst_bps;
         }
         /* Smooth the instantaneous bps a bit */
         inst_bps = (2 * jcr->LastRate + inst_bps) / 3;
         /* total bps (AveBytes/sec) since start of job */
         total_bps = jcr->JobBytes / total_sec;
         len = Mmsg(msg, _("    Files=%s Bytes=%s AveBytes/sec=%s LastBytes/sec=%s\n"),
            edit_uint64_with_commas(jcr->JobFiles, b1),
            edit_uint64_with_commas(jcr->JobBytes, b2),
            edit_uint64_with_commas(total_bps, b3),
            edit_uint64_with_commas(inst_bps, b4));
         sendit(msg, len, sp);
         /* Update only every 10 seconds */
         if (now - jcr->last_time > 10) {
            jcr->LastRate = inst_bps;
            jcr->LastJobBytes = jcr->JobBytes;
            jcr->last_time = now;
         }
         found = true;
#ifdef DEBUG
         if (jcr->file_bsock) {
            len = Mmsg(msg, _("    FDReadSeqNo=%s in_msg=%u out_msg=%d fd=%d\n"),
               edit_uint64_with_commas(jcr->file_bsock->read_seqno, b1),
               jcr->file_bsock->in_msg_no, jcr->file_bsock->out_msg_no,
               jcr->file_bsock->m_fd);
            sendit(msg, len, sp);
         } else {
            len = Mmsg(msg, _("    FDSocket closed\n"));
            sendit(msg, len, sp);
         }
#endif
      }
   }
   endeach_jcr(jcr);

   if (!found) {
      len = Mmsg(msg, _("No Jobs running.\n"));
      if (!sp->api) sendit(msg, len, sp);
   }
   if (!sp->api) sendit("====\n", 5, sp);
}

static void list_jobs_waiting_on_reservation(STATUS_PKT *sp)
{
   JCR *jcr;
   POOL_MEM msg(PM_MESSAGE);
   int len;

   len = Mmsg(msg, _("\nJobs waiting to reserve a drive:\n"));
   if (!sp->api) sendit(msg, len, sp);

   foreach_jcr(jcr) {
      if (!jcr->reserve_msgs) {
         continue;
      }
      send_drive_reserve_messages(jcr, sendit, sp);
   }
   endeach_jcr(jcr);

   if (!sp->api) sendit("====\n", 5, sp);
}


static void sendit(const char *msg, int len, void *sp)
{
   sendit(msg, len, (STATUS_PKT *)sp);
}

static void sendit(POOL_MEM &msg, int len, STATUS_PKT *sp)
{
   BSOCK *bs = sp->bs;
   if (bs) {
      bs->msg = check_pool_memory_size(bs->msg, len+1);
      memcpy(bs->msg, msg.c_str(), len+1);
      bs->msglen = len+1;
      bs->send();
   } else {
      sp->callback(msg.c_str(), len, sp->context);
   }
}

static void dbg_sendit(const char *msg, int len, void *sp)
{
   if (len > 0) {
      Dmsg0(-1, msg);
   }
}

/*
 * Status command from Director
 */
bool status_cmd(JCR *jcr)
{
   BSOCK *dir = jcr->dir_bsock;
   STATUS_PKT sp;

   dir->fsend("\n");
   sp.bs = dir;
   output_status(&sp);
   dir->signal(BNET_EOD);
   return true;
}

/*
 * .status command from Director
 */
bool qstatus_cmd(JCR *jcr)
{
   BSOCK *dir = jcr->dir_bsock;
   JCR *njcr;
   s_last_job* job;
   STATUS_PKT sp;
   POOLMEM *args = get_pool_memory(PM_MESSAGE);
   char *argk[MAX_CMD_ARGS];          /* argument keywords */
   char *argv[MAX_CMD_ARGS];          /* argument values */
   int argc;                          /* number of arguments */
   bool ret=true;
   char *cmd;
   char *device=NULL;
   int api = true;

   sp.bs = dir;

   parse_args(dir->msg, &args, &argc, argk, argv, MAX_CMD_ARGS);

   /* .status xxxx at the minimum */
   if (argc < 2 || strcmp(argk[0], ".status") != 0) {
      pm_strcpy(jcr->errmsg, dir->msg);
      dir->fsend(_("3900 No arg in .status command: %s\n"), jcr->errmsg);
      dir->signal(BNET_EOD);
      return false;
   }

   cmd = argk[1];
   unbash_spaces(cmd);

   /* The status command can contain some arguments
    * i=0 => .status
    * i=1 => [running | current | last | ... ]
    */
   for (int i=0 ; i < argc ; i++) {
      if (!strcmp(argk[i], "device") && argv[i]) {
         device = argv[i];
         unbash_spaces(device);

      } else if (!strcmp(argk[i], "api") && argv[i]) {
         api = atoi(argv[i]);

      } else if (!strcmp(argk[i], "api_opts") && argv[i]) {
         strncpy(sp.api_opts, argv[i], sizeof(sp.api_opts));;
      }
   }

   Dmsg1(100, "cmd=%s\n", cmd);

   if (strcasecmp(cmd, "current") == 0) {
      dir->fsend(OKqstatus, cmd);
      foreach_jcr(njcr) {
         if (njcr->JobId != 0) {
            dir->fsend(DotStatusJob, njcr->JobId, njcr->JobStatus, njcr->JobErrors);
         }
      }
      endeach_jcr(njcr);
   } else if (strcasecmp(cmd, "last") == 0) {
      dir->fsend(OKqstatus, cmd);
      if ((last_jobs) && (last_jobs->size() > 0)) {
         job = (s_last_job*)last_jobs->last();
         dir->fsend(DotStatusJob, job->JobId, job->JobStatus, job->Errors);
      }
   } else if (strcasecmp(cmd, "header") == 0) {
       sp.api = api;
       list_status_header(&sp);
   } else if (strcasecmp(cmd, "running") == 0) {
       sp.api = api;
       list_running_jobs(&sp);
   } else if (strcasecmp(cmd, "waitreservation") == 0) {
       sp.api = api;
       list_jobs_waiting_on_reservation(&sp);
   } else if (strcasecmp(cmd, "devices") == 0) {
       sp.api = api;
       list_devices(&sp, device);
   } else if (strcasecmp(cmd, "volumes") == 0) {
       sp.api = api;
       list_volumes(sendit, &sp);
   } else if (strcasecmp(cmd, "spooling") == 0) {
       sp.api = api;
       list_spool_stats(sendit, &sp);
   } else if (strcasecmp(cmd, "terminated") == 0) {
       sp.api = api;
       list_terminated_jobs(&sp); /* defined in lib/status.h */
   } else if (strcasecmp(cmd, "resources") == 0) {
       sp.api = api;
       list_resources(&sp);
   } else if (strcasecmp(cmd, "cloud") == 0) {
      list_cloud_transfers(&sp, true);
   } else {
      pm_strcpy(jcr->errmsg, dir->msg);
      dir->fsend(_("3900 Unknown arg in .status command: %s\n"), jcr->errmsg);
      dir->signal(BNET_EOD);
      ret = false;
   }
   dir->signal(BNET_EOD);
   free_pool_memory(args);
   return ret;
}

/* List plugins and drivers */
static void list_plugins(STATUS_PKT *sp)
{
   POOL_MEM msg(PM_MESSAGE);
   alist drivers(10, not_owned_by_alist);
   int len;

   if (b_plugin_list && b_plugin_list->size() > 0) {
      Plugin *plugin;
      pm_strcpy(msg, " Plugin: ");
      foreach_alist(plugin, b_plugin_list) {
         len = pm_strcat(msg, plugin->file);
         /* Print plugin version when debug activated */
         if (debug_level > 0 && plugin->pinfo) {
            pm_strcat(msg, "(");
            pm_strcat(msg, NPRT(sdplug_info(plugin)->plugin_version));
            len = pm_strcat(msg, ")");
         }
         if (len > 80) {
            pm_strcat(msg, "\n   ");
         } else {
            pm_strcat(msg, " ");
         }
      }
      len = pm_strcat(msg, "\n");
      sendit(msg.c_str(), len, sp);
   }
   sd_list_loaded_drivers(&drivers);
   if (drivers.size() > 0) {
      char *drv;
      pm_strcpy(msg, " Drivers: ");
      foreach_alist(drv, (&drivers)) {
         len = pm_strcat(msg, drv);
         if (len > 80) {
            pm_strcat(msg, "\n   ");
         } else {
            pm_strcat(msg, " ");
         }
      }
      len = pm_strcat(msg, "\n");
      sendit(msg.c_str(), len, sp);
   }
}
