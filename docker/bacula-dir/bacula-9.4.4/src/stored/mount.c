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
 *  Routines for handling mounting tapes for reading and for
 *    writing.
 *
 *   Kern Sibbald, August MMII
 *
 */

#include "bacula.h"                   /* pull in global headers */
#include "stored.h"                   /* pull in Storage Deamon headers */

/* Make sure some EROFS is defined */
#ifndef EROFS                         /* read-only file system */
#define EROFS -1                      /* make impossible errno */
#endif

static pthread_mutex_t mount_mutex = PTHREAD_MUTEX_INITIALIZER;

enum {
   try_next_vol = 1,
   try_read_vol,
   try_error,
   try_default
};

enum {
   check_next_vol = 1,
   check_ok,
   check_read_vol,
   check_error
};

/*
 * If release is set, we rewind the current volume,
 * which we no longer want, and ask the user (console)
 * to mount the next volume.
 *
 *  Continue trying until we get it, and then ensure
 *  that we can write on it.
 *
 * This routine returns a 0 only if it is REALLY
 *  impossible to get the requested Volume.
 *
 * This routine is entered with the device blocked, but not
 *   locked.
 *
 */
bool DCR::mount_next_write_volume()
{
   int retry = 0;
   bool ask = false, recycle, autochanger;
   DCR *dcr = this;

   Enter(200);
   set_ameta();
   Dmsg2(100, "Enter mount_next_volume(release=%d) dev=%s\n", dev->must_unload(),
      dev->print_name());

   init_device_wait_timers(dcr);

   P(mount_mutex);

   /*
    * Attempt to mount the next volume. If something non-fatal goes
    *  wrong, we come back here to re-try (new op messages, re-read
    *  Volume, ...)
    */
mount_next_vol:
   Dmsg1(100, "mount_next_vol retry=%d\n", retry);
   /* Ignore retry if this is poll request */
   if (dev->is_nospace() || retry++ > 4) {
      /* Last ditch effort before giving up, force operator to respond */
      VolCatInfo.Slot = 0;
      V(mount_mutex);
      if (!dir_ask_sysop_to_mount_volume(dcr, SD_APPEND)) {
         Jmsg(jcr, M_FATAL, 0, _("Too many errors trying to mount %s device %s.\n"),
              dev->print_type(), dev->print_name());
         goto no_lock_bail_out;
      }
      P(mount_mutex);
      Dmsg1(90, "Continue after dir_ask_sysop_to_mount. must_load=%d\n", dev->must_load());
   }
   if (job_canceled(jcr)) {
      Jmsg(jcr, M_FATAL, 0, _("Job %d canceled.\n"), jcr->JobId);
      goto bail_out;
   }
   recycle = false;

   if (dev->must_unload()) {
      ask = true;                     /* ask operator to mount tape */
   }
   do_unload();
   do_swapping(SD_APPEND);
   do_load(SD_APPEND);

   if (!find_a_volume()) {
      goto bail_out;
   }

   if (job_canceled(jcr)) {
      goto bail_out;
   }
   Dmsg3(100, "After find_a_volume. Vol=%s Slot=%d VolType=%d\n",
         getVolCatName(), VolCatInfo.Slot, VolCatInfo.VolCatType);

   dev->notify_newvol_in_attached_dcrs(getVolCatName());

   /*
    * Get next volume and ready it for append
    * This code ensures that the device is ready for
    * writing. We start from the assumption that there
    * may not be a tape mounted.
    *
    * If the device is a file, we create the output
    * file. If it is a tape, we check the volume name
    * and move the tape to the end of data.
    *
    */
   dcr->setVolCatInfo(false);   /* out of date when Vols unlocked */
   if (autoload_device(dcr, SD_APPEND, NULL) > 0) {
      autochanger = true;
      ask = false;
   } else {
      autochanger = false;
      VolCatInfo.Slot = 0;
      if (dev->is_autochanger() && !VolCatInfo.InChanger) {
         ask = true;      /* not in changer, do not retry */
      } else {
         ask = retry >= 2;
      }
   }
   Dmsg1(100, "autoload_dev returns %d\n", autochanger);
   /*
    * If we autochanged to correct Volume or (we have not just
    *   released the Volume AND we can automount) we go ahead
    *   and read the label. If there is no tape in the drive,
    *   we will fail, recurse and ask the operator the next time.
    */
   if (!dev->must_unload() && dev->is_tape() && dev->has_cap(CAP_AUTOMOUNT)) {
      Dmsg0(250, "(1)Ask=0\n");
      ask = false;                 /* don't ask SYSOP this time */
   }
   /* Don't ask if not removable */
   if (!dev->is_removable()) {
      Dmsg0(250, "(2)Ask=0\n");
      ask = false;
   }
   Dmsg2(100, "Ask=%d autochanger=%d\n", ask, autochanger);

   if (ask) {
      V(mount_mutex);
      dcr->setVolCatInfo(false);   /* out of date when Vols unlocked */
      if (!dir_ask_sysop_to_mount_volume(dcr, SD_APPEND)) {
         Dmsg0(150, "Error return ask_sysop ...\n");
         goto no_lock_bail_out;
      }
      P(mount_mutex);
   }
   if (job_canceled(jcr)) {
      goto bail_out;
   }
   Dmsg3(100, "want vol=%s devvol=%s dev=%s\n", VolumeName,
      dev->VolHdr.VolumeName, dev->print_name());

   if (dev->poll && dev->has_cap(CAP_CLOSEONPOLL)) {
      dev->close(this);
      free_volume(dev);
   }

   /* Try autolabel if enabled */
   Dmsg1(100, "Try open Vol=%s\n", getVolCatName());
   if (!dev->open_device(dcr, OPEN_READ_WRITE)) {
      Dmsg1(100, "Try autolabel Vol=%s\n", getVolCatName());
      if (!dev->poll) {
         try_autolabel(false);      /* try to create a new volume label */
      }
   }
   while (!dev->open_device(dcr, OPEN_READ_WRITE)) {
      Dmsg1(100, "open_device failed: ERR=%s", dev->bstrerror());
      if (dev->is_file() && dev->is_removable()) {
         bool ok = true;
         Dmsg0(150, "call scan_dir_for_vol\n");
         if (ok && dev->scan_dir_for_volume(dcr)) {
            if (dev->open_device(dcr, OPEN_READ_WRITE)) {
               break;                    /* got a valid volume */
            }
         }
      }
      if (try_autolabel(false) == try_read_vol) {
         break;                       /* created a new volume label */
      }

      /* ***FIXME*** if autochanger, before giving up try unload and load */

      Jmsg4(jcr, M_WARNING, 0, _("Open of %s device %s Volume \"%s\" failed: ERR=%s\n"),
            dev->print_type(), dev->print_name(), dcr->VolumeName, dev->bstrerror());

      /* If not removable, Volume is broken. This is a serious issue here. */
      if (dev->is_file() && !dev->is_removable()) {
         Dmsg3(40, "Volume \"%s\" not loaded on %s device %s.\n",
               dcr->VolumeName, dev->print_type(), dev->print_name());
         if (dev->dev_errno == EACCES || dev->dev_errno == EROFS) {
            mark_volume_read_only();
         } else {
            mark_volume_in_error();
         }

      } else {
         Dmsg0(100, "set_unload\n");
         if (dev->dev_errno == EACCES || dev->dev_errno == EROFS) {
            mark_volume_read_only();
         }
         dev->set_unload();              /* force ask sysop */
         ask = true;
      }

      Dmsg0(100, "goto mount_next_vol\n");
      goto mount_next_vol;
   }

   /*
    * Now check the volume label to make sure we have the right tape mounted
    */
read_volume:
   switch (check_volume_label(ask, autochanger)) {
   case check_next_vol:
      Dmsg0(50, "set_unload\n");
      dev->set_unload();                 /* want a different Volume */
      Dmsg0(100, "goto mount_next_vol\n");
      goto mount_next_vol;
   case check_read_vol:
      goto read_volume;
   case check_error:
      goto bail_out;
   case check_ok:
      break;
   }
   /*
    * Check that volcatinfo is good
    */
   if (!dev->haveVolCatInfo()) {
      Dmsg0(100, "Do not have volcatinfo\n");
      if (!find_a_volume()) {
         goto mount_next_vol;
      }
      dev->set_volcatinfo_from_dcr(this);
   }

   /*
    * See if we have a fresh tape or a tape with data.
    *
    * Note, if the LabelType is PRE_LABEL, it was labeled
    *  but never written. If so, rewrite the label but set as
    *  VOL_LABEL.  We rewind and return the label (reconstructed)
    *  in the block so that in the case of a new tape, data can
    *  be appended just after the block label.  If we are writing
    *  a second volume, the calling routine will write the label
    *  before writing the overflow block.
    *
    *  If the tape is marked as Recycle, we rewrite the label.
    */
   recycle = strcmp(dev->VolCatInfo.VolCatStatus, "Recycle") == 0;
   if (dev->VolHdr.LabelType == PRE_LABEL || recycle) {
      dcr->WroteVol = false;
      if (!dev->rewrite_volume_label(dcr, recycle)) {
         mark_volume_in_error();
         goto mount_next_vol;
      }
   } else {
      /*
       * OK, at this point, we have a valid Bacula label, but
       * we need to position to the end of the volume, since we are
       * just now putting it into append mode.
       */
      Dmsg1(100, "Device previously written, moving to end of data. Expect %lld bytes\n",
           dev->VolCatInfo.VolCatBytes);
      Jmsg(jcr, M_INFO, 0, _("Volume \"%s\" previously written, moving to end of data.\n"),
         VolumeName);

      if (!dev->eod(dcr)) {
         Dmsg3(050, "Unable to position to end of data on %s device %s: ERR=%s\n",
            dev->print_type(), dev->print_name(), dev->bstrerror());
         Jmsg(jcr, M_ERROR, 0, _("Unable to position to end of data on %s device %s: ERR=%s\n"),
            dev->print_type(), dev->print_name(), dev->bstrerror());
         mark_volume_in_error();
         goto mount_next_vol;
      }

      if (!dev->is_eod_valid(dcr)) {
         Dmsg0(100, "goto mount_next_vol\n");
         goto mount_next_vol;
      }

      dev->VolCatInfo.VolCatMounts++;      /* Update mounts */
      Dmsg1(150, "update volinfo mounts=%d\n", dev->VolCatInfo.VolCatMounts);
      if (!dir_update_volume_info(dcr, false, false)) {
         goto bail_out;
      }

      /* Return an empty block */
      empty_block(block);             /* we used it for reading so set for write */
   }
   dev->set_append();
   Dmsg1(150, "set APPEND, normal return from mount_next_write_volume. dev=%s\n",
      dev->print_name());

   V(mount_mutex);
   return true;

bail_out:
   V(mount_mutex);

no_lock_bail_out:
   Leave(200);
   return false;
}

/*
 * This routine is meant to be called once the first pass
 *   to ensure that we have a candidate volume to mount.
 *   Otherwise, we ask the sysop to created one.
 * Note, mount_mutex is already locked on entry and thus
 *   must remain locked on exit from this function.
 */
bool DCR::find_a_volume()
{
   DCR *dcr = this;
   bool ok;

   if (!is_suitable_volume_mounted()) {
      bool have_vol = false;
      /* Do we have a candidate volume? */
      if (dev->vol) {
         bstrncpy(VolumeName, dev->vol->vol_name, sizeof(VolumeName));
         have_vol = dir_get_volume_info(this, VolumeName, GET_VOL_INFO_FOR_WRITE);
      }
      /*
       * Get Director's idea of what tape we should have mounted.
       *    in dcr->VolCatInfo
       */
      if (!have_vol) {
         Dmsg0(200, "Before dir_find_next_appendable_volume.\n");
         while (!dir_find_next_appendable_volume(dcr)) {
            Dmsg0(200, "not dir_find_next\n");
            if (job_canceled(jcr)) {
               return false;
            }
            /*
             * Unlock the mount mutex while waiting or
             *   the Director for a new volume
             */
            V(mount_mutex);
            if (dev->must_wait()) {
               int retries = 5;
               Dmsg0(40, "No appendable volume. Calling wait_for_device\n");
               wait_for_device(dcr, retries);
               ok = true;
            } else {
               ok = dir_ask_sysop_to_create_appendable_volume(dcr);
            }
            P(mount_mutex);
            if (!ok || job_canceled(jcr)) {
               return false;
            }
            Dmsg0(150, "Again dir_find_next_append...\n");
         }
         dev->clear_wait();
      }
   }
   if (dcr->haveVolCatInfo()) {
      return true;
   }
   return dir_get_volume_info(dcr, VolumeName, GET_VOL_INFO_FOR_WRITE);
}

int DCR::check_volume_label(bool &ask, bool &autochanger)
{
   int vol_label_status;

   Enter(200);

   set_ameta();
   /*
    * If we are writing to a stream device, ASSUME the volume label
    *  is correct.
    */
   if (dev->has_cap(CAP_STREAM)) {
      vol_label_status = VOL_OK;
      create_volume_header(dev, VolumeName, "Default", false);
      dev->VolHdr.LabelType = PRE_LABEL;
   } else {
      vol_label_status = dev->read_dev_volume_label(this);
   }
   if (job_canceled(jcr)) {
      goto check_bail_out;
   }

   Dmsg2(150, "Want dirVol=%s dirStat=%s\n", VolumeName,
      VolCatInfo.VolCatStatus);

   /*
    * At this point, dev->VolCatInfo has what is in the drive, if anything,
    *          and   dcr->VolCatInfo has what the Director wants.
    */
   switch (vol_label_status) {
   case VOL_OK:
      Dmsg1(150, "Vol OK name=%s\n", dev->VolHdr.VolumeName);
      dev->VolCatInfo = VolCatInfo;       /* structure assignment */
      break;                    /* got a Volume */
   case VOL_NAME_ERROR:
      VOLUME_CAT_INFO dcrVolCatInfo, devVolCatInfo;
      char saveVolumeName[MAX_NAME_LENGTH];

      Dmsg2(40, "Vol NAME Error Have=%s, want=%s\n", dev->VolHdr.VolumeName, VolumeName);
      if (dev->is_volume_to_unload()) {
         ask = true;
         goto check_next_volume;
      }

#ifdef xxx
      /* If not removable, Volume is broken */
      if (!dev->is_removable()) {
         Jmsg3(jcr, M_WARNING, 0, _("Volume \"%s\" not loaded on %s device %s.\n"),
            VolumeName, dev->print_type(), dev->print_name());
         Dmsg3(40, "Volume \"%s\" not loaded on %s device %s.\n",
            VolumeName, dev->print_type(), dev->print_name());
         mark_volume_in_error();
         goto check_next_volume;
      }
#endif

      /*
       * OK, we got a different volume mounted. First save the
       *  requested Volume info (dcr) structure, then query if
       *  this volume is really OK. If not, put back the desired
       *  volume name, mark it not in changer and continue.
       */
      dcrVolCatInfo = VolCatInfo;      /* structure assignment */
      devVolCatInfo = dev->VolCatInfo;      /* structure assignment */
      /* Check if this is a valid Volume in the pool */
      bstrncpy(saveVolumeName, VolumeName, sizeof(saveVolumeName));
      bstrncpy(VolumeName, dev->VolHdr.VolumeName, sizeof(VolumeName));
      if (!dir_get_volume_info(this, VolumeName, GET_VOL_INFO_FOR_WRITE)) {
         POOL_MEM vol_info_msg;
         pm_strcpy(vol_info_msg, jcr->dir_bsock->msg);  /* save error message */
         /* Restore desired volume name, note device info out of sync */
         /* This gets the info regardless of the Pool */
         bstrncpy(VolumeName, dev->VolHdr.VolumeName, sizeof(VolumeName));
         if (autochanger && !dir_get_volume_info(this, VolumeName, GET_VOL_INFO_FOR_READ)) {
            /*
             * If we get here, we know we cannot write on the Volume,
             *  and we know that we cannot read it either, so it
             *  is not in the autochanger.
             */
            mark_volume_not_inchanger();
         }
         dev->VolCatInfo = devVolCatInfo;    /* structure assignment */
         dev->set_unload();                  /* unload this volume */
         Jmsg(jcr, M_WARNING, 0, _("Director wanted Volume \"%s\".\n"
              "    Current Volume \"%s\" not acceptable because:\n"
              "    %s"),
             dcrVolCatInfo.VolCatName, dev->VolHdr.VolumeName,
             vol_info_msg.c_str());
         ask = true;
         /* Restore saved DCR before continuing */
         bstrncpy(VolumeName, saveVolumeName, sizeof(VolumeName));
         VolCatInfo = dcrVolCatInfo;  /* structure assignment */
         goto check_next_volume;
      }
      /*
       * This was not the volume we expected, but it is OK with
       * the Director, so use it.
       */
      Dmsg1(150, "Got new Volume name=%s\n", VolumeName);
      dev->VolCatInfo = VolCatInfo;   /* structure assignment */
      Dmsg1(100, "Call reserve_volume=%s\n", dev->VolHdr.VolumeName);
      if (reserve_volume(this, dev->VolHdr.VolumeName) == NULL) {
         if (!jcr->errmsg[0]) {
            Jmsg3(jcr, M_WARNING, 0, _("Could not reserve volume %s on %s device %s\n"),
               dev->VolHdr.VolumeName, dev->print_type(), dev->print_name());
         } else {
            Jmsg(jcr, M_WARNING, 0, "%s", jcr->errmsg);
         }
         ask = true;
         dev->setVolCatInfo(false);
         setVolCatInfo(false);
         goto check_next_volume;
      }
      break;                /* got a Volume */
   /*
    * At this point, we assume we have a blank tape mounted.
    */
   case VOL_IO_ERROR:
      /* Fall through wanted */
   case VOL_NO_LABEL:
      switch (try_autolabel(true)) {
      case try_next_vol:
         goto check_next_volume;
      case try_read_vol:
         goto check_read_volume;
      case try_error:
         goto check_bail_out;
      case try_default:
         break;
      }
      /* NOTE! Fall-through wanted. */
   case VOL_NO_MEDIA:
   default:
      Dmsg0(200, "VOL_NO_MEDIA or default.\n");
      /* Send error message */
      if (!dev->poll) {
      } else {
         Dmsg1(200, "Msg suppressed by poll: %s\n", jcr->errmsg);
      }
      ask = true;
      /* Needed, so the medium can be changed */
      if (dev->requires_mount()) {
         dev->close(this);
         free_volume(dev);
      }
      goto check_next_volume;
   }
   Leave(200);
   return check_ok;

check_next_volume:
   dev->setVolCatInfo(false);
   setVolCatInfo(false);
   Leave(200);
   return check_next_vol;

check_bail_out:
   Leave(200);
   return check_error;

check_read_volume:
   Leave(200);
   return check_read_vol;

}


bool DCR::is_suitable_volume_mounted()
{
   bool ok;

   /* Volume mounted? */
   if (dev->VolHdr.VolumeName[0] == 0 || dev->swap_dev || dev->must_unload()) {
      return false;                      /* no */
   }
   bstrncpy(VolumeName, dev->VolHdr.VolumeName, sizeof(VolumeName));
   ok = dir_get_volume_info(this, VolumeName, GET_VOL_INFO_FOR_WRITE);
   if (!ok) {
      Dmsg1(40, "dir_get_volume_info failed: %s", jcr->errmsg);
      dev->set_wait();
   }
   return ok;
}

bool DCR::do_unload()
{
   if (dev->must_unload()) {
      Dmsg1(100, "must_unload release %s\n", dev->print_name());
      release_volume();
   }
   return false;
}

bool DCR::do_load(bool is_writing)
{
   if (dev->must_load()) {
      Dmsg1(100, "Must load dev=%s\n", dev->print_name());
      if (autoload_device(this, is_writing, NULL) > 0) {
         dev->clear_load();
         return true;
      }
      return false;
   }
   return true;
}

void DCR::do_swapping(bool is_writing)
{
   /*
    * See if we are asked to swap the Volume from another device
    *  if so, unload the other device here, and attach the
    *  volume to our drive.
    */
   if (dev->swap_dev) {
      if (dev->swap_dev->must_unload()) {
         if (dev->vol) {
            dev->swap_dev->set_slot(dev->vol->get_slot());
         }
         Dmsg2(100, "Swap unloading slot=%d %s\n", dev->swap_dev->get_slot(),
               dev->swap_dev->print_name());
         unload_dev(this, dev->swap_dev);
      }
      if (dev->vol) {
         dev->vol->clear_swapping();
         Dmsg1(100, "=== set in_use vol=%s\n", dev->vol->vol_name);
         dev->vol->clear_in_use();
         dev->VolHdr.VolumeName[0] = 0;  /* don't yet have right Volume */
      } else {
         Dmsg1(100, "No vol on dev=%s\n", dev->print_name());
      }
      if (dev->swap_dev->vol) {
         Dmsg2(100, "Vol=%s on dev=%s\n", dev->swap_dev->vol->vol_name,
              dev->swap_dev->print_name());
      }
      Dmsg2(100, "Set swap_dev=NULL for dev=%s swap_dev=%s\n",
         dev->print_name(), dev->swap_dev->print_name());
      dev->swap_dev = NULL;
   } else {
      if (dev->vol) {
      Dmsg1(100, "No swap_dev set. dev->vol=%p\n", dev->vol);
      } else {
      Dmsg1(100, "No swap_dev set. dev->vol=%p\n", dev->vol);
      }
   }
}

/*
 * If permitted, we label the device, make sure we can do
 *   it by checking that the VolCatBytes is zero => not labeled,
 *   once the Volume is labeled we don't want to label another
 *   blank tape with the same name.  For disk, we go ahead and
 *   label it anyway, because the OS insures that there is only
 *   one Volume with that name.
 * As noted above, at this point dcr->VolCatInfo has what
 *   the Director wants and dev->VolCatInfo has info on the
 *   previous tape (or nothing).
 *
 * Return codes are:
 *   try_next_vol        label failed, look for another volume
 *   try_read_vol        labeled volume, now re-read the label
 *   try_error           hard error (catalog update)
 *   try_default         I couldn't do anything
 */
int DCR::try_autolabel(bool opened)
{
   DCR *dcr = this;

   if (dev->poll && !dev->is_tape()) {
      Dmsg0(100, "No autolabel because polling.\n");
      return try_default;       /* if polling, don't try to create new labels */
   }
   /* For a tape require it to be opened and read before labeling */
   if (!opened && (dev->is_tape() || dev->is_null())) {
      return try_default;
   }
   if (dev->has_cap(CAP_LABEL) && (VolCatInfo.VolCatBytes == 0 ||
         (!dev->is_tape() && strcmp(VolCatInfo.VolCatStatus,
                                "Recycle") == 0))) {
      Dmsg1(40, "Create new volume label vol=%s\n", VolumeName);
      /* Create a new Volume label and write it to the device */
      if (!dev->write_volume_label(dcr, VolumeName,
             pool_name, false, /* no relabel */ false /* defer label */)) {
         Dmsg2(100, "write_vol_label failed. vol=%s, pool=%s\n",
           VolumeName, pool_name);
         if (opened) {
            mark_volume_in_error();
         }
         return try_next_vol;
      }
      Dmsg0(150, "dir_update_vol_info. Set Append\n");
      /* Copy Director's info into the device info */
      dev->VolCatInfo = VolCatInfo;    /* structure assignment */
      if (!dir_update_volume_info(dcr, true, true)) {  /* indicate tape labeled */
         Dmsg3(100, "Update_vol_info failed no autolabel Volume \"%s\" on %s device %s.\n",
            VolumeName, dev->print_type(), dev->print_name());
         return try_error;
      }
      Jmsg(dcr->jcr, M_INFO, 0, _("Labeled new Volume \"%s\" on %s device %s.\n"),
         VolumeName, dev->print_type(), dev->print_name());
      Dmsg3(100, "Labeled new Volume \"%s\" on %s device %s.\n",
         VolumeName, dev->print_type(), dev->print_name());
      return try_read_vol;   /* read label we just wrote */
   } else {
      Dmsg4(40, "=== Cannot autolabel: cap_label=%d VolCatBytes=%lld is_tape=%d VolCatStatus=%s\n",
         dev->has_cap(CAP_LABEL), VolCatInfo.VolCatBytes, dev->is_tape(),
         VolCatInfo.VolCatStatus);
   }
   if (!dev->has_cap(CAP_LABEL) && VolCatInfo.VolCatBytes == 0) {
      Jmsg(jcr, M_WARNING, 0, _("%s device %s not configured to autolabel Volumes.\n"),
         dev->print_type(), dev->print_name());
   }
#ifdef xxx
   /* If not removable, Volume is broken */
   if (!dev->is_removable()) {
      Jmsg3(jcr, M_WARNING, 0, _("Volume \"%s\" not loaded on %s device %s.\n"),
         VolumeName, dev->print_type(), dev->print_name());
      Dmsg3(40, "Volume \"%s\" not loaded on %s device %s.\n",
         VolumeName, dev->print_type(), dev->print_name());

      mark_volume_in_error();
      return try_next_vol;
   }
#endif
   return try_default;
}


/*
 * Mark volume in error in catalog
 */
void DCR::mark_volume_in_error()
{
   Jmsg(jcr, M_INFO, 0, _("Marking Volume \"%s\" in Error in Catalog.\n"),
        VolumeName);
   dev->VolCatInfo = VolCatInfo;       /* structure assignment */
   dev->setVolCatStatus("Error");
   Dmsg0(150, "dir_update_vol_info. Set Error.\n");
   dir_update_volume_info(this, false, false);
   volume_unused(this);
   Dmsg0(50, "set_unload\n");
   dev->set_unload();                 /* must get a new volume */
}

/*
 * Mark volume read_only in catalog
 */
void DCR::mark_volume_read_only()
{
   Jmsg(jcr, M_INFO, 0, _("Marking Volume \"%s\" Read-Only in Catalog.\n"),
        VolumeName);
   dev->VolCatInfo = VolCatInfo;       /* structure assignment */
   dev->setVolCatStatus("Read-Only");
   Dmsg0(150, "dir_update_vol_info. Set Read-Only.\n");
   dir_update_volume_info(this, false, false);
   volume_unused(this);
   Dmsg0(50, "set_unload\n");
   dev->set_unload();                 /* must get a new volume */
}


/*
 * The Volume is not in the correct slot, so mark this
 *   Volume as not being in the Changer.
 */
void DCR::mark_volume_not_inchanger()
{
   Jmsg(jcr, M_ERROR, 0, _("Autochanger Volume \"%s\" not found in slot %d.\n"
"    Setting InChanger to zero in catalog.\n"),
        getVolCatName(), VolCatInfo.Slot);
   dev->VolCatInfo = VolCatInfo;    /* structure assignment */
   VolCatInfo.InChanger = false;
   dev->VolCatInfo.InChanger = false;
   Dmsg0(400, "update vol info in mount\n");
   dir_update_volume_info(this, true, false);  /* set new status */
}

/*
 * Either because we are going to hang a new volume, or because
 *  of explicit user request, we release the current volume.
 */
void DCR::release_volume()
{
   unload_autochanger(this, -1);

   if (WroteVol) {
      Jmsg0(jcr, M_ERROR, 0, _("Hey!!!!! WroteVol non-zero !!!!!\n"));
      Pmsg0(190, "Hey!!!!! WroteVol non-zero !!!!!\n");
   }

   if (dev->is_open() && (!dev->is_tape() || !dev->has_cap(CAP_ALWAYSOPEN))) {
      generate_plugin_event(jcr, bsdEventDeviceClose, this);
      dev->close(this);
   }

   /* If we have not closed the device, then at least rewind the tape */
   if (dev->is_open()) {
      dev->offline_or_rewind(this);
   }

   /*
    * Erase all memory of the current volume
    */
   free_volume(dev);
   dev->block_num = dev->file = 0;
   dev->EndBlock = dev->EndFile = 0;
   memset(&dev->VolCatInfo, 0, sizeof(dev->VolCatInfo));
   dev->clear_volhdr();
   /* Force re-read of label */
   dev->clear_labeled();
   dev->clear_read();
   dev->clear_append();
   dev->label_type = B_BACULA_LABEL;
   VolumeName[0] = 0;

   Dmsg0(190, "release_volume\n");
}

/*
 *      Insanity check
 *
 * Check to see if the tape position as defined by the OS is
 *  the same as our concept.  If it is not,
 *  it means the user has probably manually rewound the tape.
 * Note, we check only if num_writers == 0, but this code will
 *  also work fine for any number of writers. If num_writers > 0,
 *  we probably should cancel all jobs using this device, or
 *  perhaps even abort the SD, or at a minimum, mark the tape
 *  in error.  Another strategy with num_writers == 0, would be
 *  to rewind the tape and do a new eod() request.
 */
bool DCR::is_tape_position_ok()
{
   if (dev->is_tape() && dev->num_writers == 0) {
      int32_t file = dev->get_os_tape_file();
      if (file >= 0 && file != (int32_t)dev->get_file()) {
         Jmsg(jcr, M_ERROR, 0, _("Invalid tape position on volume \"%s\""
              " on device %s. Expected %d, got %d\n"),
              dev->VolHdr.VolumeName, dev->print_name(), dev->get_file(), file);
         /*
          * If the current file is greater than zero, it means we probably
          *  have some bad count of EOF marks, so mark tape in error.  Otherwise
          *  the operator might have moved the tape, so we just release it
          *  and try again.
          */
         if (file > 0) {
            mark_volume_in_error();
         }
         release_volume();
         return false;
      }
   }
   return true;
}


/*
 * If we are reading, we come here at the end of the tape
 *  and see if there are more volumes to be mounted.
 */
bool mount_next_read_volume(DCR *dcr)
{
   DEVICE *dev = dcr->dev;
   JCR *jcr = dcr->jcr;
   Dmsg2(90, "NumReadVolumes=%d CurReadVolume=%d\n", jcr->NumReadVolumes, jcr->CurReadVolume);

   volume_unused(dcr);                /* release current volume */
   /*
    * End Of Tape -- mount next Volume (if another specified)
    */
   if (jcr->NumReadVolumes > 1 && jcr->CurReadVolume < jcr->NumReadVolumes) {
      dev->Lock();
      dev->close(dcr);
      dev->set_read();
      dcr->set_reserved_for_read();
      dev->Unlock();
      if (!acquire_device_for_read(dcr)) {
         Jmsg3(jcr, M_FATAL, 0, _("Cannot open %s Dev=%s, Vol=%s for reading.\n"),
            dev->print_type(), dev->print_name(), dcr->VolumeName);
         jcr->setJobStatus(JS_FatalError); /* Jmsg is not working for *SystemJob* */
         return false;
      }
      return true;                    /* next volume mounted */
   }
   Dmsg0(90, "End of Device reached.\n");
   return false;
}
