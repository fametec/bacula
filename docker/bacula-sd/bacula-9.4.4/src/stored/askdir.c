/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2019 Kern Sibbald

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
 *  Subroutines to handle Catalog reqests sent to the Director
 *   Reqests/commands from the Director are handled in dircmd.c
 *
 *   Kern Sibbald, December 2000
 */

#include "bacula.h"                   /* pull in global headers */
#include "stored.h"                   /* pull in Storage Deamon headers */

static const int dbglvl = 200;

/* Requests sent to the Director */
static char Find_media[]   = "CatReq JobId=%ld FindMedia=%d pool_name=%s media_type=%s vol_type=%d\n";
static char Get_Vol_Info[] = "CatReq JobId=%ld GetVolInfo VolName=%s write=%d\n";
static char Update_media[] = "CatReq JobId=%ld UpdateMedia VolName=%s"
   " VolJobs=%u VolFiles=%u VolBlocks=%u VolBytes=%s VolABytes=%s"
   " VolHoleBytes=%s VolHoles=%u VolMounts=%u"
   " VolErrors=%u VolWrites=%u MaxVolBytes=%s EndTime=%s VolStatus=%s"
   " Slot=%d relabel=%d InChanger=%d VolReadTime=%s VolWriteTime=%s"
   " VolFirstWritten=%s VolType=%u VolParts=%d VolCloudParts=%d"
   " LastPartBytes=%lld Enabled=%d Recycle=%d\n";
static char Create_jobmedia[] = "CatReq JobId=%ld CreateJobMedia\n";
static char FileAttributes[] = "UpdCat JobId=%ld FileAttributes ";

/* Responses received from the Director */
static char OK_media[] = "1000 OK VolName=%127s VolJobs=%u VolFiles=%lu"
   " VolBlocks=%lu VolBytes=%lld VolABytes=%lld"
   " VolHoleBytes=%lld VolHoles=%lu VolMounts=%lu"
   " VolErrors=%lu VolWrites=%lu"
   " MaxVolBytes=%lld VolCapacityBytes=%lld VolStatus=%20s"
   " Slot=%ld MaxVolJobs=%lu MaxVolFiles=%lu InChanger=%ld"
   " VolReadTime=%lld VolWriteTime=%lld EndFile=%lu EndBlock=%lu"
   " VolType=%lu LabelType=%ld MediaId=%lld ScratchPoolId=%lld"
   " VolParts=%d VolCloudParts=%d LastPartBytes=%lld Enabled=%d"
   " Recycle=%d\n";


static char OK_create[] = "1000 OK CreateJobMedia\n";

static bthread_mutex_t vol_info_mutex = BTHREAD_MUTEX_PRIORITY(PRIO_SD_VOL_INFO);

#ifdef needed

Note: if you turn this on, be sure to add the Recycle Flag

static char Device_update[] = "DevUpd JobId=%ld device=%s "
   "append=%d read=%d num_writers=%d "
   "open=%d labeled=%d offline=%d "
   "reserved=%d max_writers=%d "
   "autoselect=%d autochanger=%d "
   "enabled=%d "
   "changer_name=%s media_type=%s volume_name=%s\n";


/** Send update information about a device to Director */
bool dir_update_device(JCR *jcr, DEVICE *dev)
{
   BSOCK *dir = jcr->dir_bsock;
   POOL_MEM dev_name, VolumeName, MediaType, ChangerName;
   DEVRES *device = dev->device;
   bool ok;

   pm_strcpy(dev_name, device->hdr.name);
   bash_spaces(dev_name);
   if (dev->is_labeled()) {
      pm_strcpy(VolumeName, dev->VolHdr.VolumeName);
   } else {
      pm_strcpy(VolumeName, "*");
   }
   bash_spaces(VolumeName);
   pm_strcpy(MediaType, device->media_type);
   bash_spaces(MediaType);
   if (device->changer_res) {
      pm_strcpy(ChangerName, device->changer_res->hdr.name);
      bash_spaces(ChangerName);
   } else {
      pm_strcpy(ChangerName, "*");
   }
   ok = dir->fsend(Device_update,
      jcr->JobId,
      dev_name.c_str(),
      dev->can_append()!=0,
      dev->can_read()!=0, dev->num_writers,
      dev->is_open()!=0, dev->is_labeled()!=0,
      dev->is_offline()!=0, dev->reserved_device,
      dev->is_tape()?100000:1,
      dev->autoselect, 0,
      dev->enabled,
      ChangerName.c_str(), MediaType.c_str(), VolumeName.c_str());
   Dmsg1(dbglvl, ">dird: %s\n", dir->msg);
   return ok;
}

bool dir_update_changer(JCR *jcr, AUTOCHANGER *changer)
{
   BSOCK *dir = jcr->dir_bsock;
   POOL_MEM dev_name, MediaType;
   DEVRES *device;
   bool ok;

   pm_strcpy(dev_name, changer->hdr.name);
   bash_spaces(dev_name);
   device = (DEVRES *)changer->device->first();
   pm_strcpy(MediaType, device->media_type);
   bash_spaces(MediaType);
   /* This is mostly to indicate that we are here */
   ok = dir->fsend(Device_update,
      jcr->JobId,
      dev_name.c_str(),         /* Changer name */
      0, 0, 0,                  /* append, read, num_writers */
      0, 0, 0,                  /* is_open, is_labeled, offline */
      0, 0,                     /* reserved, max_writers */
      0,                        /* Autoselect */
      0,                        /* Enabled */
      changer->device->size(),  /* Number of devices */
      "0",                      /* PoolId */
      "*",                      /* ChangerName */
      MediaType.c_str(),        /* MediaType */
      "*");                     /* VolName */
   Dmsg1(dbglvl, ">dird: %s\n", dir->msg);
   return ok;
}
#endif


static AskDirHandler *askdir_handler = NULL; /* must be true when inside a "btools" */

/*
 * btools must call this function, to modify behavior of some functions here
 */
AskDirHandler *init_askdir_handler(AskDirHandler *new_askdir_handler)
{
   AskDirHandler *old = askdir_handler;
   askdir_handler = new_askdir_handler;
   return old;
}

/*
 * Alternate function used by btools
 */
bool AskDirHandler::dir_ask_sysop_to_mount_volume(DCR *dcr, bool /*writing*/)
{
   DEVICE *dev = dcr->dev;
   fprintf(stderr, _("Mount Volume \"%s\" on device %s and press return when ready: "),
      dcr->VolumeName, dev->print_name());
   dev->close(dcr);
   getchar();
   return true;
}

bool AskDirHandler::dir_get_volume_info(DCR *dcr, const char *VolumeName, enum get_vol_info_rw  writing)
{
   Dmsg0(100, "Fake dir_get_volume_info\n");
   dcr->setVolCatName(VolumeName);
   Dmsg2(500, "Vol=%s VolType=%d\n", dcr->getVolCatName(), dcr->VolCatInfo.VolCatType);
   return 1;
}

/**
 * Send current JobStatus to Director
 */
bool dir_send_job_status(JCR *jcr)
{
   if (askdir_handler) {
      return askdir_handler->dir_send_job_status(jcr);
   }

   return jcr->sendJobStatus();
}

/**
 * Common routine for:
 *   dir_get_volume_info()
 * and
 *   dir_find_next_appendable_volume()
 *
 *  NOTE!!! All calls to this routine must be protected by
 *          locking vol_info_mutex before calling it so that
 *          we don't have one thread modifying the parameters
 *          and another reading them.
 *
 *  Returns: true  on success and vol info in dcr->VolCatInfo
 *           false on failure
 */
static bool do_get_volume_info(DCR *dcr)
{
    JCR *jcr = dcr->jcr;
    BSOCK *dir = jcr->dir_bsock;
    VOLUME_CAT_INFO vol;
    int n;
    int32_t Enabled, Recycle;
    int32_t InChanger;

    dcr->setVolCatInfo(false);
    if (dir->recv() <= 0) {
       Dmsg0(dbglvl, "getvolname error bnet_recv\n");
       Mmsg(jcr->errmsg, _("Network error on bnet_recv in req_vol_info.\n"));
       return false;
    }
    memset(&vol, 0, sizeof(vol));
    n = sscanf(dir->msg, OK_media, vol.VolCatName,
               &vol.VolCatJobs, &vol.VolCatFiles,
               &vol.VolCatBlocks, &vol.VolCatAmetaBytes,
               &vol.VolCatAdataBytes, &vol.VolCatHoleBytes,
               &vol.VolCatHoles, &vol.VolCatMounts, &vol.VolCatErrors,
               &vol.VolCatWrites, &vol.VolCatMaxBytes,
               &vol.VolCatCapacityBytes, vol.VolCatStatus,
               &vol.Slot, &vol.VolCatMaxJobs, &vol.VolCatMaxFiles,
               &InChanger, &vol.VolReadTime, &vol.VolWriteTime,
               &vol.EndFile, &vol.EndBlock, &vol.VolCatType,
               &vol.LabelType, &vol.VolMediaId, &vol.VolScratchPoolId,
               &vol.VolCatParts, &vol.VolCatCloudParts,
               &vol.VolLastPartBytes, &Enabled, &Recycle);
    Dmsg2(dbglvl, "<dird n=%d %s", n, dir->msg);
    if (n != 31) {
       Dmsg1(dbglvl, "get_volume_info failed: ERR=%s", dir->msg);
       /*
        * Note, we can get an error here either because there is
        *  a comm problem, or if the volume is not a suitable
        *  volume to use, so do not issue a Jmsg() here, do it
        *  in the calling routine.
        */
       Mmsg(jcr->errmsg, _("Error getting Volume info: %s"), dir->msg);
       return false;
    }
    vol.InChanger = InChanger;        /* bool in structure */
    vol.VolEnabled = Enabled;         /* bool in structure */
    vol.VolRecycle = Recycle;         /* bool in structure */
    vol.is_valid = true;
    vol.VolCatBytes = vol.VolCatAmetaBytes + vol.VolCatAdataBytes;
    unbash_spaces(vol.VolCatName);
    bstrncpy(dcr->VolumeName, vol.VolCatName, sizeof(dcr->VolumeName));
    dcr->VolCatInfo = vol;            /* structure assignment */

    Dmsg3(dbglvl, "do_reqest_vol_info return true slot=%d Volume=%s MediaId=%lld\n",
          dcr->VolCatInfo.Slot, dcr->VolCatInfo.VolCatName, dcr->VolCatInfo.VolMediaId);
    Dmsg5(dbglvl, "Dir returned VolCatAmetaBytes=%lld VolCatAdataBytes=%lld Status=%s Vol=%s MediaId=%lld\n",
       dcr->VolCatInfo.VolCatAmetaBytes, dcr->VolCatInfo.VolCatAdataBytes,
       dcr->VolCatInfo.VolCatStatus, dcr->VolCatInfo.VolCatName,
       dcr->VolCatInfo.VolMediaId);
    return true;
}


/**
 * Get Volume info for a specific volume from the Director's Database
 *
 * Returns: true  on success   (Director guarantees that Pool and MediaType
 *                              are correct and VolStatus==Append or
 *                              VolStatus==Recycle)
 *          false on failure
 *
 *          Volume information returned in dcr->VolCatInfo
 */
bool dir_get_volume_info(DCR *dcr,
                         const char *VolumeName,
                         enum get_vol_info_rw writing)
{
   if (askdir_handler) {
      return askdir_handler->dir_get_volume_info(dcr, VolumeName, writing);
   }

   JCR *jcr = dcr->jcr;
   BSOCK *dir = jcr->dir_bsock;

   P(vol_info_mutex);
   dcr->setVolCatName(VolumeName);
   bash_spaces(dcr->getVolCatName());
   dir->fsend(Get_Vol_Info, jcr->JobId, dcr->getVolCatName(),
      writing==GET_VOL_INFO_FOR_WRITE?1:0);
   Dmsg1(dbglvl, ">dird %s", dir->msg);
   unbash_spaces(dcr->getVolCatName());
   bool ok = do_get_volume_info(dcr);
   V(vol_info_mutex);
   return ok;
}



/**
 * Get info on the next appendable volume in the Director's database
 *
 * Returns: true  on success dcr->VolumeName is volume
 *                reserve_volume() called on Volume name
 *          false on failure dcr->VolumeName[0] == 0
 *                also sets dcr->found_in_use if at least one
 *                in use volume was found.
 *
 *          Volume information returned in dcr
 *
 */
bool dir_find_next_appendable_volume(DCR *dcr)
{
    /* SD tools setup a handler because they have no connection to Dir */
    if (askdir_handler) {
       return askdir_handler->dir_find_next_appendable_volume(dcr);
    }

    JCR *jcr = dcr->jcr;
    BSOCK *dir = jcr->dir_bsock;
    bool rtn;
    char lastVolume[MAX_NAME_LENGTH];
    int nb_retry;

    /*
     * Calculate the number of possible drives + 30 for the size of the
     *  Volume list to consider.
     */
    nb_retry = ((rblist *)res_head[R_DEVICE-r_first]->res_list)->size() + 30;
    Dmsg2(dbglvl, "dir_find_next_appendable_volume: reserved=%d Vol=%s\n",
       dcr->is_reserved(), dcr->VolumeName);
    Mmsg(jcr->errmsg, "Unknown error\n");

    /*
     * Try the thirty oldest or most available volumes.  Note,
     *   the most available could already be mounted on another
     *   drive, so we continue looking for a not in use Volume.
     */
    lock_volumes();
    P(vol_info_mutex);
    dcr->clear_found_in_use();
    lastVolume[0] = 0;
    for (int vol_index=1;  vol_index < nb_retry; vol_index++) {
       bash_spaces(dcr->media_type);
       bash_spaces(dcr->pool_name);
       dir->fsend(Find_media, jcr->JobId, vol_index, dcr->pool_name, dcr->media_type,
                  dcr->dev->dev_type);
       unbash_spaces(dcr->media_type);
       unbash_spaces(dcr->pool_name);
       Dmsg1(dbglvl, ">dird %s", dir->msg);
       if (do_get_volume_info(dcr)) {
          /* Give up if we get the same volume name twice */
          if (lastVolume[0] && strcmp(lastVolume, dcr->VolumeName) == 0) {
             Mmsg(jcr->errmsg, "Director returned same volume name=%s twice.\n",
                lastVolume);
             Dmsg1(dbglvl, "Got same vol = %s\n", lastVolume);
             break;
          }
          /* If VolCatAdataBytes, we have ALIGNED_DEV */
          if (dcr->VolCatInfo.VolCatType == 0 && dcr->VolCatInfo.VolCatAdataBytes != 0) {
             dcr->VolCatInfo.VolCatType = B_ALIGNED_DEV;
          }
          /*
           * If we have VolType and we are disk or aligned, the VolType must match
           */
          /* ***FIXME*** find better way to handle voltype */
          if (dcr->VolCatInfo.VolCatType != 0 &&
              (dcr->dev->dev_type == B_FILE_DEV || dcr->dev->dev_type == B_ALIGNED_DEV ||
               dcr->dev->dev_type == B_CLOUD_DEV) &&
               dcr->dev->dev_type != (int)dcr->VolCatInfo.VolCatType) {
             Dmsg2(000, "Skip vol. Wanted VolType=%d Got=%d\n", dcr->dev->dev_type, dcr->VolCatInfo.VolCatType);
             continue;
          }
          bstrncpy(lastVolume, dcr->VolumeName, sizeof(lastVolume));
          if (dcr->can_i_write_volume()) {
             Dmsg1(dbglvl, "Call reserve_volume for write. Vol=%s\n", dcr->VolumeName);
             if (reserve_volume(dcr, dcr->VolumeName) == NULL) {
                Dmsg1(dbglvl, "%s", jcr->errmsg);
                if (dcr->dev->must_wait()) {
                   rtn = false;
                   dcr->VolumeName[0] = 0;
                   goto get_out;
                }
                continue;
             }
             Dmsg1(dbglvl, "dir_find_next_appendable_volume return true. vol=%s\n",
                dcr->VolumeName);
             rtn = true;
             goto get_out;
          } else {
             Mmsg(jcr->errmsg, "Volume %s is in use.\n", dcr->VolumeName);
             Dmsg1(dbglvl, "Volume %s is in use.\n", dcr->VolumeName);
             /* If volume is not usable, it is in use by someone else */
             dcr->set_found_in_use();
             continue;
          }
       }
       Dmsg2(dbglvl, "No vol. index %d return false. dev=%s\n", vol_index,
          dcr->dev->print_name());
       break;
    }
    rtn = false;
    dcr->VolumeName[0] = 0;

get_out:
    V(vol_info_mutex);
    unlock_volumes();
    if (!rtn && dcr->VolCatInfo.VolScratchPoolId != 0) {
       Jmsg(jcr, M_WARNING, 0, "%s", jcr->errmsg);
       Dmsg2(000, "!!!!!!!!! Volume=%s rejected ScratchPoolId=%lld\n", dcr->VolumeName,
          dcr->VolCatInfo.VolScratchPoolId);
       Dmsg1(000, "%s", jcr->errmsg);
    //} else {
    //   Dmsg3(000, "Rtn=%d Volume=%s ScratchPoolId=%lld\n", rtn, dcr->VolumeName,
    //      dcr->VolCatInfo.VolScratchPoolId);
    }
    return rtn;
}


/*
 * After writing a Volume, send the updated statistics
 * back to the director. The information comes from the
 * dev record.
 */
bool dir_update_volume_info(DCR *dcr, bool label, bool update_LastWritten,
                            bool use_dcr_only)
{
   if (askdir_handler) {
      return askdir_handler->dir_update_volume_info(dcr, label, update_LastWritten, use_dcr_only);
   }

   JCR *jcr = dcr->jcr;
   BSOCK *dir = jcr->dir_bsock;
   DEVICE *dev = dcr->ameta_dev;
   VOLUME_CAT_INFO vol;
   char ed1[50], ed2[50], ed3[50], ed4[50], ed5[50], ed6[50], ed7[50], ed8[50];
   int InChanger, Enabled, Recycle;
   bool ok = false;
   POOL_MEM VolumeName;

   /* If system job, do not update catalog, except if we explicitly force it. */
   if (jcr->getJobType() == JT_SYSTEM &&
       !dcr->force_update_volume_info) {
      return true;
   }

   /* Lock during Volume update */
   P(vol_info_mutex);
   dev->Lock_VolCatInfo();

   if (use_dcr_only) {
      vol = dcr->VolCatInfo;        /* structure assignment */
   } else {
      /* Just labeled or relabeled the tape */
      if (label) {
         dev->setVolCatStatus("Append");
      }
      vol = dev->VolCatInfo;        /* structure assignment */
   }

   /* This happens when nothing to update after fixup_device ... */
   if (vol.VolCatName[0] == 0) {
      Dmsg0(50, "Volume Name is NULL\n");
      goto bail_out;
   }
   Dmsg4(100, "Update cat VolBytes=%lld VolABytes=%lld Status=%s Vol=%s\n",
      vol.VolCatAmetaBytes, vol.VolCatAdataBytes, vol.VolCatStatus, vol.VolCatName);
// if (update_LastWritten) {
      vol.VolLastWritten = time(NULL);
// }
   /* worm cannot be recycled, ensure catalog correct */
   if (dev->is_worm() && vol.VolRecycle) {
      Jmsg(jcr, M_INFO, 0, _("WORM cassette detected: setting Recycle=No on Volume=\"%s\"\n"), vol.VolCatName);
      vol.VolRecycle = false;
   }
   pm_strcpy(VolumeName, vol.VolCatName);
   bash_spaces(VolumeName);
   InChanger = vol.InChanger;
   Enabled = vol.VolEnabled;
   Recycle = vol.VolRecycle;
   /* Insanity test */
   if (vol.VolCatHoleBytes > (((uint64_t)2)<<60)) {
      Pmsg1(010, "VolCatHoleBytes too big: %lld. Reset to zero.\n",
         vol.VolCatHoleBytes);
      vol.VolCatHoleBytes = 0;
   }
   /* Set device type where this Volume used */
   if (vol.VolCatType == 0) {
      vol.VolCatType = dev->dev_type;
   }

   /* Do not lock device here because it may be locked from label */
   if (!jcr->is_canceled()) {
      dir->fsend(Update_media, jcr->JobId,
         VolumeName.c_str(), vol.VolCatJobs, vol.VolCatFiles,
         vol.VolCatBlocks, edit_uint64(vol.VolCatAmetaBytes, ed1),
         edit_uint64(vol.VolCatAdataBytes, ed2),
         edit_uint64(vol.VolCatHoleBytes, ed3),
         vol.VolCatHoles, vol.VolCatMounts, vol.VolCatErrors,
         vol.VolCatWrites, edit_uint64(vol.VolCatMaxBytes, ed4),
         edit_uint64(vol.VolLastWritten, ed5),
         vol.VolCatStatus, vol.Slot, label,
         InChanger,                      /* bool in structure */
         edit_int64(vol.VolReadTime, ed6),
         edit_int64(vol.VolWriteTime, ed7),
         edit_uint64(vol.VolFirstWritten, ed8),
         vol.VolCatType,
         vol.VolCatParts,
         vol.VolCatCloudParts,
         vol.VolLastPartBytes,
         Enabled,
         Recycle);
       Dmsg1(100, ">dird %s", dir->msg);
      /*
       * We sent info directly from dev to the Director.
       *  What the Director sends back is first read into
       *  the dcr with do_get_volume_info()
       */
      if (!do_get_volume_info(dcr)) {
         Jmsg(jcr, M_FATAL, 0, "%s", jcr->errmsg);
         Dmsg2(dbglvl, _("Didn't get vol info vol=%s: ERR=%s"),
            vol.VolCatName, jcr->errmsg);
         goto bail_out;
      }
      Dmsg1(100, "get_volume_info() %s", dir->msg);

      /* Update dev Volume info in case something changed (e.g. expired) */
      if (!use_dcr_only) {
         dev->VolCatInfo.Slot = dcr->VolCatInfo.Slot;
         bstrncpy(dev->VolCatInfo.VolCatStatus, dcr->VolCatInfo.VolCatStatus, sizeof(vol.VolCatStatus));
         dev->VolCatInfo.VolCatAdataBytes = dcr->VolCatInfo.VolCatAdataBytes;
         dev->VolCatInfo.VolCatAmetaBytes = dcr->VolCatInfo.VolCatAmetaBytes;
         dev->VolCatInfo.VolCatHoleBytes = dcr->VolCatInfo.VolCatHoleBytes;
         dev->VolCatInfo.VolCatHoles = dcr->VolCatInfo.VolCatHoles;
         dev->VolCatInfo.VolCatPadding = dcr->VolCatInfo.VolCatPadding;
         dev->VolCatInfo.VolCatAmetaPadding = dcr->VolCatInfo.VolCatAmetaPadding;
         dev->VolCatInfo.VolCatAdataPadding = dcr->VolCatInfo.VolCatAdataPadding;
         dev->VolCatInfo.VolCatFiles = dcr->VolCatInfo.VolCatFiles;
         dev->VolCatInfo.VolCatBytes = dcr->VolCatInfo.VolCatBytes;
         dev->VolCatInfo.VolCatMounts = dcr->VolCatInfo.VolCatMounts;
         dev->VolCatInfo.VolCatJobs = dcr->VolCatInfo.VolCatJobs;
         dev->VolCatInfo.VolCatFiles = dcr->VolCatInfo.VolCatFiles;
         dev->VolCatInfo.VolCatRecycles = dcr->VolCatInfo.VolCatRecycles;
         dev->VolCatInfo.VolCatWrites = dcr->VolCatInfo.VolCatWrites;
         dev->VolCatInfo.VolCatReads = dcr->VolCatInfo.VolCatReads;
         dev->VolCatInfo.VolEnabled = dcr->VolCatInfo.VolEnabled;
         dev->VolCatInfo.VolCatMaxBytes = dcr->VolCatInfo.VolCatMaxBytes;
         dev->VolCatInfo.VolRecycle = dcr->VolCatInfo.VolRecycle;
      }
      ok = true;
   }

bail_out:
   dev->Unlock_VolCatInfo();
   V(vol_info_mutex);
   return ok;
}

struct JOBMEDIA_ITEM {
   dlink link;
   int64_t  VolMediaId;
   uint64_t StartAddr;
   uint64_t EndAddr;
   uint32_t VolFirstIndex;
   uint32_t VolLastIndex;
   uint32_t StartFile;
   uint32_t EndFile;
   uint32_t StartBlock;
   uint32_t EndBlock;
};

void create_jobmedia_queue(JCR *jcr)
{
   JOBMEDIA_ITEM *item = NULL;
   jcr->jobmedia_queue = New(dlist(item, &item->link));
}

bool flush_jobmedia_queue(JCR *jcr)
{
   if (askdir_handler) {
      return askdir_handler->flush_jobmedia_queue(jcr);
   }

   JOBMEDIA_ITEM *item;
   BSOCK *dir = jcr->dir_bsock;
   bool ok;

   if (!jcr->jobmedia_queue || jcr->jobmedia_queue->size() == 0) {
      return true;     /* should never happen */
   }
   Dmsg1(400, "=== Flush jobmedia queue = %d\n", jcr->jobmedia_queue->size());

   dir->fsend(Create_jobmedia, jcr->JobId);
   foreach_dlist(item, jcr->jobmedia_queue) {
      if (jcr->is_JobStatus(JS_Incomplete)) {
         if (item->VolFirstIndex >= dir->get_lastFileIndex()) {
            continue;
         }
         if (item->VolLastIndex >= dir->get_lastFileIndex()) {
            item->VolLastIndex = dir->get_lastFileIndex() - 1;
         }
      }
      ok = dir->fsend("%u %u %u %u %u %u %lld\n",
         item->VolFirstIndex, item->VolLastIndex,
         item->StartFile, item->EndFile,
         item->StartBlock, item->EndBlock,
         item->VolMediaId);
      /* Keep track of last FileIndex flushed */
      dir->set_lastFlushIndex(item->VolLastIndex);
      Dmsg2(400, "sd->dir: ok=%d Jobmedia=%s", ok, dir->msg);
   }
   dir->signal(BNET_EOD);
   jcr->jobmedia_queue->destroy();

   if (dir->recv() <= 0) {
      Dmsg0(dbglvl, "create_jobmedia error bnet_recv\n");
      Jmsg(jcr, M_FATAL, 0, _("Error creating JobMedia records: ERR=%s\n"),
           dir->bstrerror());
      return false;
   }
   Dmsg1(210, "<dird %s", dir->msg);
   if (strcmp(dir->msg, OK_create) != 0) {
      Dmsg1(dbglvl, "Bad response from Dir: %s\n", dir->msg);
      Jmsg(jcr, M_FATAL, 0, _("Error creating JobMedia records: %s\n"), dir->msg);
      return false;
   }
   return true;
}


/*
 * After writing a Volume, create the JobMedia record.
 */
bool dir_create_jobmedia_record(DCR *dcr, bool zero)
{
   if (askdir_handler) {
      return askdir_handler->dir_create_jobmedia_record(dcr, zero);
   }

   JCR *jcr = dcr->jcr;
   BSOCK *dir = jcr->dir_bsock;
   JOBMEDIA_ITEM *item;
   bool ok = true;;

   if (!zero && !dcr->WroteVol) {
      return true;
   }
   if (!zero && dcr->VolLastIndex == 0) {
      Pmsg7(0/*dbglvl*/, "Discard: JobMedia Vol=%s wrote=%d MediaId=%lld FI=%lu LI=%lu StartAddr=%lld EndAddr=%lld\n",
         dcr->VolumeName, dcr->WroteVol, dcr->VolMediaId,
         dcr->VolFirstIndex, dcr->VolLastIndex, dcr->StartAddr, dcr->EndAddr);
      return true;                    /* nothing written to the Volume */
   }
   /* Throw out records where the start address is bigger than the end */
   if (!zero && dcr->StartAddr > dcr->EndAddr) {
      Pmsg7(0/*dbglvl*/, "Discard: JobMedia Vol=%s wrote=%d MediaId=%lld FI=%lu LI=%lu StartAddr=%lld EndAddr=%lld\n",
         dcr->VolumeName, dcr->WroteVol, dcr->VolMediaId,
         dcr->VolFirstIndex, dcr->VolLastIndex, dcr->StartAddr, dcr->EndAddr);
      return true;
   }

   /* If system job, do not update catalog */
   if (jcr->getJobType() == JT_SYSTEM) {
      return true;
   }

   /* Throw out records where FI is zero -- i.e. nothing done */
   if (!zero && dcr->VolFirstIndex == 0 &&
        (dcr->StartAddr != 0 || dcr->EndAddr != 0)) {
      Pmsg7(0/*dbglvl*/, "Discard: JobMedia Vol=%s wrote=%d MediaId=%lld FI=%lu LI=%lu StartAddr=%lld EndAddr=%lld\n",
         dcr->VolumeName, dcr->WroteVol, dcr->VolMediaId,
         dcr->VolFirstIndex, dcr->VolLastIndex, dcr->StartAddr, dcr->EndAddr);
      return true;
   }

   /*
    * If this Job is incomplete, we need to backup the FileIndex
    *  to the last correctly saved file so that the JobMedia
    *  LastIndex is correct.
    *
    */
   if (jcr->is_JobStatus(JS_Incomplete)) {
      dcr->VolLastIndex = dir->get_lastFileIndex();
      Dmsg1(100, "======= Set FI=%ld\n", dcr->VolLastIndex);
   }

   Dmsg7(100, "Queue JobMedia Vol=%s wrote=%d MediaId=%lld FI=%lu LI=%lu StartAddr=%lld EndAddr=%lld\n",
      dcr->VolumeName, dcr->WroteVol, dcr->VolMediaId,
      dcr->VolFirstIndex, dcr->VolLastIndex, dcr->StartAddr, dcr->EndAddr);
   item = (JOBMEDIA_ITEM *)malloc(sizeof(JOBMEDIA_ITEM));
   if (zero) {
      item->VolFirstIndex = item->VolLastIndex = 0;
      item->StartFile = item->EndFile = 0;
      item->StartBlock = item->EndBlock = 0;
      item->StartAddr = item->EndAddr = 0;
      item->VolMediaId = dcr->VolMediaId;
   } else {
      item->VolFirstIndex = dcr->VolFirstIndex;
      item->VolLastIndex = dcr->VolLastIndex;
      item->StartFile = (uint32_t)(dcr->StartAddr >> 32);
      item->EndFile = (uint32_t)(dcr->EndAddr >> 32);
      item->StartBlock = (uint32_t)dcr->StartAddr;
      item->EndBlock = (uint32_t)dcr->EndAddr;
      item->StartAddr = dcr->StartAddr;
      item->EndAddr = dcr->EndAddr;
      item->VolMediaId = dcr->VolMediaId;
   }
   jcr->jobmedia_queue->append(item);
   /* Flush at queue size of 1000 jobmedia records */
   if (zero || jcr->jobmedia_queue->size() >= 1000) {
      ok = flush_jobmedia_queue(jcr);
   }

   dcr->VolFirstIndex = dcr->VolLastIndex = 0;
   dcr->StartAddr = dcr->EndAddr = 0;
   dcr->VolMediaId = 0;
   dcr->WroteVol = false;
   return ok;
}

/*
 * Update File Attribute data
 * We do the following:
 *  1. expand the bsock buffer to be large enough
 *  2. Write a "header" into the buffer with serialized data
 *    VolSessionId
 *    VolSeesionTime
 *    FileIndex
 *    Stream
 *    data length that follows
 *    start of raw byte data from the Device record.
 * Note, this is primarily for Attribute data, but can
 *   also handle any device record. The Director must know
 *   the raw byte data format that is defined for each Stream.
 * Now Restore Objects pass through here STREAM_RESTORE_OBJECT
 */
bool dir_update_file_attributes(DCR *dcr, DEV_RECORD *rec)
{
   if (askdir_handler) {
      return askdir_handler->dir_update_file_attributes(dcr, rec);
   }

   JCR *jcr = dcr->jcr;
   BSOCK *dir = jcr->dir_bsock;
   ser_declare;

#ifdef NO_ATTRIBUTES_TEST
   return true;
#endif

   dir->msg = check_pool_memory_size(dir->msg, sizeof(FileAttributes) +
                MAX_NAME_LENGTH + sizeof(DEV_RECORD) + rec->data_len + 1);
   dir->msglen = bsnprintf(dir->msg, sizeof(FileAttributes) +
                MAX_NAME_LENGTH + 1, FileAttributes, jcr->JobId);
   ser_begin(dir->msg + dir->msglen, 0);
   ser_uint32(rec->VolSessionId);
   ser_uint32(rec->VolSessionTime);
   ser_int32(rec->FileIndex);
   ser_int32(rec->Stream);
   ser_uint32(rec->data_len);
   ser_bytes(rec->data, rec->data_len);
   dir->msglen = ser_length(dir->msg);
   Dmsg1(1800, ">dird %s\n", dir->msg);    /* Attributes */
   if (rec->maskedStream == STREAM_UNIX_ATTRIBUTES ||
       rec->maskedStream == STREAM_UNIX_ATTRIBUTES_EX) {
      Dmsg2(1500, "==== set_data_end FI=%ld %s\n", rec->FileIndex, rec->data);
      dir->set_data_end(rec->FileIndex);    /* set offset of valid data */
   }
   return dir->send();
}


/**
 *   Request the sysop to create an appendable volume
 *
 *   Entered with device blocked.
 *   Leaves with device blocked.
 *
 *   Returns: true  on success (operator issues a mount command)
 *            false on failure
 *              Note, must create dev->errmsg on error return.
 *
 *    On success, dcr->VolumeName and dcr->VolCatInfo contain
 *      information on suggested volume, but this may not be the
 *      same as what is actually mounted.
 *
 *    When we return with success, the correct tape may or may not
 *      actually be mounted. The calling routine must read it and
 *      verify the label.
 */
bool dir_ask_sysop_to_create_appendable_volume(DCR *dcr)
{
   if (askdir_handler) {
      return askdir_handler->dir_ask_sysop_to_create_appendable_volume(dcr);
   }

   int stat = W_TIMEOUT;
   DEVICE *dev = dcr->dev;
   JCR *jcr = dcr->jcr;
   bool got_vol = false;

   if (job_canceled(jcr)) {
      dev->poll = false;
      return false;
   }
   Dmsg0(400, "enter dir_ask_sysop_to_create_appendable_volume\n");
   ASSERT(dev->blocked());
   for ( ;; ) {
      if (job_canceled(jcr)) {
         Mmsg(dev->errmsg,
              _("Job %s canceled while waiting for mount on Storage Device \"%s\".\n"),
              jcr->Job, dev->print_name());
         Jmsg(jcr, M_INFO, 0, "%s", dev->errmsg);
         dev->poll = false;
         return false;
      }
      got_vol = dir_find_next_appendable_volume(dcr);   /* get suggested volume */
      if (got_vol) {
         goto get_out;
      } else {
         dev->clear_wait();
         if (stat == W_TIMEOUT || stat == W_MOUNT) {
            Mmsg(dev->errmsg, _(
"Job %s is waiting. Cannot find any appendable volumes.\n"
"Please use the \"label\" command to create a new Volume for:\n"
"    Storage:      %s\n"
"    Pool:         %s\n"
"    Media type:   %s\n"),
               jcr->Job,
               dev->print_name(),
               dcr->pool_name,
               dcr->media_type);
            Jmsg(jcr, M_MOUNT, 0, "%s", dev->errmsg);
            Dmsg1(dbglvl, "%s", dev->errmsg);
         }
      }

      jcr->sendJobStatus(JS_WaitMedia);

      stat = wait_for_sysop(dcr);
      Dmsg1(dbglvl, "Back from wait_for_sysop stat=%d\n", stat);
      if (dev->poll) {
         Dmsg1(dbglvl, "Poll timeout in create append vol on device %s\n", dev->print_name());
         continue;
      }

      if (stat == W_TIMEOUT) {
         if (!double_dev_wait_time(dev)) {
            Mmsg(dev->errmsg, _("Max time exceeded waiting to mount Storage Device %s for Job %s\n"),
               dev->print_name(), jcr->Job);
            Jmsg(jcr, M_FATAL, 0, "%s", dev->errmsg);
            Dmsg1(dbglvl, "Gave up waiting on device %s\n", dev->print_name());
            dev->poll = false;
            return false;             /* exceeded maximum waits */
         }
         continue;
      }
      if (stat == W_ERROR) {
         berrno be;
         Mmsg0(dev->errmsg, _("pthread error in mount_next_volume.\n"));
         Jmsg(jcr, M_FATAL, 0, "%s", dev->errmsg);
         dev->poll = false;
         return false;
      }
      Dmsg1(dbglvl, "Someone woke me for device %s\n", dev->print_name());
   }

get_out:
   jcr->sendJobStatus(JS_Running);
   Dmsg0(dbglvl, "leave dir_ask_sysop_to_create_appendable_volume\n");
   return true;
}

/*
 *   Request to mount specific Volume
 *
 *   Entered with device blocked and dcr->VolumeName is desired
 *      volume.
 *   Leaves with device blocked.
 *
 *   Returns: true  on success (operator issues a mount command)
 *            false on failure
 *                  Note, must create dev->errmsg on error return.
 *
 */
bool dir_ask_sysop_to_mount_volume(DCR *dcr, bool write_access)
{
   if (askdir_handler) {
      return askdir_handler->dir_ask_sysop_to_mount_volume(dcr, write_access);
   }

   int stat = W_TIMEOUT;
   DEVICE *dev = dcr->dev;
   JCR *jcr = dcr->jcr;

   Dmsg0(400, "enter dir_ask_sysop_to_mount_volume\n");
   if (!dcr->VolumeName[0]) {
      Mmsg0(dev->errmsg, _("Cannot request another volume: no volume name given.\n"));
      dev->poll = false;
      return false;
   }

   if (dcr->no_mount_request) {
      Mmsg(dev->errmsg, _("The current operation doesn't support mount request\n"));
      dev->poll = false;
      return false;
   }

   for ( ;; ) {
      if (job_canceled(jcr)) {
         Mmsg(dev->errmsg,
              _("Job %s canceled while waiting for mount on Storage Device \"%s\".\n"),
              jcr->Job, dev->print_name());
         Jmsg(jcr, M_INFO, 0, "%s", dev->errmsg);
         dev->poll = false;
         return false;
      }
      /*
       * If we are not polling, and the wait timeout or the
       *   user explicitly did a mount, send him the message.
       *   Otherwise skip it.
       */
      if (!dev->poll && (stat == W_TIMEOUT || stat == W_MOUNT)) {
         const char *msg;
         if (write_access) {
            msg = _("%sPlease mount append Volume \"%s\" or label a new one for:\n"
              "    Job:          %s\n"
              "    Storage:      %s\n"
              "    Pool:         %s\n"
              "    Media type:   %s\n");
         } else {
            msg = _("%sPlease mount read Volume \"%s\" for:\n"
              "    Job:          %s\n"
              "    Storage:      %s\n"
              "    Pool:         %s\n"
              "    Media type:   %s\n");
         }
         Jmsg(jcr, M_MOUNT, 0, msg,
              dev->is_nospace()?_("\n\nWARNING: device is full! Please add more disk space then ...\n\n"):"",
              dcr->VolumeName,
              jcr->Job,
              dev->print_name(),
              dcr->pool_name,
              dcr->media_type);
         Dmsg3(400, "Mount \"%s\" on device \"%s\" for Job %s\n",
               dcr->VolumeName, dev->print_name(), jcr->Job);
      }

      jcr->sendJobStatus(JS_WaitMount);

      stat = wait_for_sysop(dcr);          /* wait on device */
      Dmsg1(100, "Back from wait_for_sysop stat=%d\n", stat);
      if (dev->poll) {
         Dmsg1(100, "Poll timeout in mount vol on device %s\n", dev->print_name());
         Dmsg1(100, "Blocked=%s\n", dev->print_blocked());
         goto get_out;
      }

      if (stat == W_TIMEOUT) {
         if (!double_dev_wait_time(dev)) {
            Mmsg(dev->errmsg, _("Max time exceeded waiting to mount Storage Device %s for Job %s\n"),
               dev->print_name(), jcr->Job);
            Jmsg(jcr, M_FATAL, 0, "%s", dev->errmsg);
            Dmsg1(400, "Gave up waiting on device %s\n", dev->print_name());
            dev->poll = false;
            return false;             /* exceeded maximum waits */
         }
         continue;
      }
      if (stat == W_ERROR) {
         berrno be;
         Mmsg(dev->errmsg, _("pthread error in mount_volume\n"));
         Jmsg(jcr, M_FATAL, 0, "%s", dev->errmsg);
         dev->poll = false;
         return false;
      }
      Dmsg1(100, "Someone woke me for device %s\n", dev->print_name());
      break;
   }

get_out:
   if (job_canceled(jcr)) {
      Mmsg(dev->errmsg, _("Job %s canceled while waiting for mount on Storage Device %s.\n"),
           jcr->Job, dev->print_name());
      dev->poll = false;
      return false;
   }

   jcr->sendJobStatus(JS_Running);
   Dmsg0(100, "leave dir_ask_sysop_to_mount_volume\n");
   return true;
}
