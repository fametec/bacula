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
 *  Routines to acquire and release a device for read/write
 *
 *   Written by Kern Sibbald, August MMII
 */

#include "bacula.h"                   /* pull in global headers */
#include "stored.h"                   /* pull in Storage Deamon headers */

static int const rdbglvl = 100;

/* Forward referenced functions */
static void set_dcr_from_vol(DCR *dcr, VOL_LIST *vol);

/*********************************************************************
 * Acquire device for reading.
 *  The drive should have previously been reserved by calling
 *  reserve_device_for_read(). We read the Volume label from the block and
 *  leave the block pointers just after the label.
 *
 *  Returns: false if failed for any reason
 *           true  if successful
 */
bool acquire_device_for_read(DCR *dcr)
{
   DEVICE *dev;
   JCR *jcr = dcr->jcr;
   bool ok = false;
   bool tape_previously_mounted;
   VOL_LIST *vol;
   bool try_autochanger = true;
   int i;
   int vol_label_status;
   int retry = 0;

   Enter(rdbglvl);
   dev = dcr->dev;
   ASSERT2(!dev->adata, "Called with adata dev. Wrong!");
   dev->Lock_read_acquire();
   Dmsg2(rdbglvl, "dcr=%p dev=%p\n", dcr, dcr->dev);
   Dmsg2(rdbglvl, "MediaType dcr=%s dev=%s\n", dcr->media_type, dev->device->media_type);
   dev->dblock(BST_DOING_ACQUIRE);

   if (dev->num_writers > 0) {
      Jmsg2(jcr, M_FATAL, 0, _("Acquire read: num_writers=%d not zero. Job %d canceled.\n"),
         dev->num_writers, jcr->JobId);
      goto get_out;
   }

   /* Find next Volume, if any */
   vol = jcr->VolList;
   if (!vol) {
      char ed1[50];
      Jmsg(jcr, M_FATAL, 0, _("No volumes specified for reading. Job %s canceled.\n"),
         edit_int64(jcr->JobId, ed1));
      goto get_out;
   }
   jcr->CurReadVolume++;
   for (i=1; i<jcr->CurReadVolume; i++) {
      vol = vol->next;
   }
   if (!vol) {
      Jmsg(jcr, M_FATAL, 0, _("Logic error: no next volume to read. Numvol=%d Curvol=%d\n"),
         jcr->NumReadVolumes, jcr->CurReadVolume);
      goto get_out;                   /* should not happen */
   }
   set_dcr_from_vol(dcr, vol);

   if (generate_plugin_event(jcr, bsdEventDeviceOpen, dcr) != bRC_OK) {
      Jmsg(jcr, M_FATAL, 0, _("generate_plugin_event(bsdEventDeviceOpen) Failed\n"));
      goto get_out;
   }

   Dmsg2(rdbglvl, "Want Vol=%s Slot=%d\n", vol->VolumeName, vol->Slot);

   /*
    * If the MediaType requested for this volume is not the
    *  same as the current drive, we attempt to find the same
    *  device that was used to write the orginal volume.  If
    *  found, we switch to using that device.
    *
    *  N.B. A lot of routines rely on the dcr pointer not changing
    *    read_records.c even has multiple dcrs cached, so we take care
    *    here to release all important parts of the dcr and re-acquire
    *    them such as the block pointer (size may change), but we do
    *    not release the dcr.
    */
   Dmsg2(rdbglvl, "MediaType dcr=%s dev=%s\n", dcr->media_type, dev->device->media_type);
   if (dcr->media_type[0] && strcmp(dcr->media_type, dev->device->media_type) != 0) {
      RCTX rctx;
      DIRSTORE *store;
      int stat;

      Jmsg4(jcr, M_INFO, 0, _("Changing read device. Want Media Type=\"%s\" have=\"%s\"\n"
                              "  %s device=%s\n"),
            dcr->media_type, dev->device->media_type, dev->print_type(),
            dev->print_name());
      Dmsg4(rdbglvl, "Changing read device. Want Media Type=\"%s\" have=\"%s\"\n"
                              "  %s device=%s\n",
            dcr->media_type, dev->device->media_type,
            dev->print_type(), dev->print_name());

      generate_plugin_event(jcr, bsdEventDeviceClose, dcr);

      dev->dunblock(DEV_UNLOCKED);

      lock_reservations();
      memset(&rctx, 0, sizeof(RCTX));
      rctx.jcr = jcr;
      jcr->read_dcr = dcr;
      jcr->reserve_msgs = New(alist(10, not_owned_by_alist));
      rctx.any_drive = true;
      rctx.device_name = vol->device;
      store = new DIRSTORE;
      memset(store, 0, sizeof(DIRSTORE));
      store->name[0] = 0; /* No storage name */
      bstrncpy(store->media_type, vol->MediaType, sizeof(store->media_type));
      bstrncpy(store->pool_name, dcr->pool_name, sizeof(store->pool_name));
      bstrncpy(store->pool_type, dcr->pool_type, sizeof(store->pool_type));
      store->append = false;
      rctx.store = store;
      clean_device(dcr);                     /* clean up the dcr */

      /*
       * Search for a new device
       */
      stat = search_res_for_device(rctx);
      release_reserve_messages(jcr);         /* release queued messages */
      unlock_reservations();

      if (stat == 1) { /* found new device to use */
         /*
          * Switching devices, so acquire lock on new device,
          *   then release the old one.
          */
         dcr->dev->Lock_read_acquire();      /* lock new one */
         dev->Unlock_read_acquire();         /* release old one */
         dev = dcr->dev;                     /* get new device pointer */
         dev->dblock(BST_DOING_ACQUIRE);

         dcr->VolumeName[0] = 0;
         Jmsg(jcr, M_INFO, 0, _("Media Type change.  New read %s device %s chosen.\n"),
            dev->print_type(), dev->print_name());
         Dmsg2(50, "Media Type change.  New read %s device %s chosen.\n",
            dev->print_type(), dev->print_name());
         if (generate_plugin_event(jcr, bsdEventDeviceOpen, dcr) != bRC_OK) {
            Jmsg(jcr, M_FATAL, 0, _("generate_plugin_event(bsdEventDeviceOpen) Failed\n"));
            goto get_out;
         }
         bstrncpy(dcr->VolumeName, vol->VolumeName, sizeof(dcr->VolumeName));
         dcr->setVolCatName(vol->VolumeName);
         bstrncpy(dcr->media_type, vol->MediaType, sizeof(dcr->media_type));
         dcr->VolCatInfo.Slot = vol->Slot;
         dcr->VolCatInfo.InChanger = vol->Slot > 0;
         bstrncpy(dcr->pool_name, store->pool_name, sizeof(dcr->pool_name));
         bstrncpy(dcr->pool_type, store->pool_type, sizeof(dcr->pool_type));
      } else {
         /* error */
         Jmsg1(jcr, M_FATAL, 0, _("No suitable device found to read Volume \"%s\"\n"),
            vol->VolumeName);
         Dmsg1(rdbglvl, "No suitable device found to read Volume \"%s\"\n", vol->VolumeName);
         goto get_out;
      }
   }
   Dmsg2(rdbglvl, "MediaType dcr=%s dev=%s\n", dcr->media_type, dev->device->media_type);

   dev->clear_unload();

   if (dev->vol && dev->vol->is_swapping()) {
      dev->vol->set_slot(vol->Slot);
      Dmsg3(rdbglvl, "swapping: slot=%d Vol=%s dev=%s\n", dev->vol->get_slot(),
         dev->vol->vol_name, dev->print_name());
   }

   init_device_wait_timers(dcr);

   tape_previously_mounted = dev->can_read() || dev->can_append() ||
                             dev->is_labeled();
// tape_initially_mounted = tape_previously_mounted;

   /* Volume info is always needed because of VolType */
   Dmsg1(rdbglvl, "dir_get_volume_info vol=%s\n", dcr->VolumeName);
   if (!dir_get_volume_info(dcr, dcr->VolumeName, GET_VOL_INFO_FOR_READ)) {
      Dmsg2(rdbglvl, "dir_get_vol_info failed for vol=%s: %s\n",
         dcr->VolumeName, jcr->errmsg);
      Jmsg1(jcr, M_WARNING, 0, "Read acquire: %s", jcr->errmsg);
   }
   dev->set_load();                /* set to load volume */

   for ( ;; ) {
      /* If not polling limit retries */
      if (!dev->poll && retry++ > 10) {
         break;
      }
      dev->clear_labeled();              /* force reread of label */
      if (job_canceled(jcr)) {
         char ed1[50];
         Mmsg1(dev->errmsg, _("Job %s canceled.\n"), edit_int64(jcr->JobId, ed1));
         Jmsg(jcr, M_INFO, 0, dev->errmsg);
         goto get_out;                /* error return */
      }

      dcr->do_unload();
      dcr->do_swapping(SD_READ);
      dcr->do_load(SD_READ);
      set_dcr_from_vol(dcr, vol);          /* refresh dcr with desired volume info */

      /*
       * This code ensures that the device is ready for
       * reading. If it is a file, it opens it.
       * If it is a tape, it checks the volume name
       */
      Dmsg1(rdbglvl, "open vol=%s\n", dcr->VolumeName);
      if (!dev->open_device(dcr, OPEN_READ_ONLY)) {
         if (!dev->poll) {
            Jmsg4(jcr, M_WARNING, 0, _("Read open %s device %s Volume \"%s\" failed: ERR=%s\n"),
                  dev->print_type(), dev->print_name(), dcr->VolumeName, dev->bstrerror());
         }
         goto default_path;
      }
      Dmsg1(rdbglvl, "opened dev %s OK\n", dev->print_name());

      /* Read Volume Label */
      Dmsg0(rdbglvl, "calling read-vol-label\n");
      vol_label_status = dev->read_dev_volume_label(dcr);
      switch (vol_label_status) {
      case VOL_OK:
         Dmsg1(rdbglvl, "Got correct volume. VOL_OK: %s\n", dcr->VolCatInfo.VolCatName);
         ok = true;
         dev->VolCatInfo = dcr->VolCatInfo;     /* structure assignment */
         break;                    /* got it */
      case VOL_IO_ERROR:
         Dmsg0(rdbglvl, "IO Error\n");
         /*
          * Send error message generated by dev->read_dev_volume_label()
          *  only we really had a tape mounted. This supresses superfluous
          *  error messages when nothing is mounted.
          */
         if (tape_previously_mounted) {
            Jmsg(jcr, M_WARNING, 0, "Read acquire: %s", jcr->errmsg);
         }
         goto default_path;
      case VOL_TYPE_ERROR:
         Jmsg(jcr, M_FATAL, 0, "%s", jcr->errmsg);
         goto get_out;
      case VOL_NAME_ERROR:
         Dmsg3(rdbglvl, "Vol name=%s want=%s drv=%s.\n", dev->VolHdr.VolumeName,
               dcr->VolumeName, dev->print_name());
         if (dev->is_volume_to_unload()) {
            goto default_path;
         }
         dev->set_unload();              /* force unload of unwanted tape */
         if (!unload_autochanger(dcr, -1)) {
            /* at least free the device so we can re-open with correct volume */
            dev->close(dcr);
            free_volume(dev);
         }
         dev->set_load();
         /* Fall through */
      default:
         Jmsg1(jcr, M_WARNING, 0, "Read acquire: %s", jcr->errmsg);
default_path:
         Dmsg0(rdbglvl, "default path\n");
         tape_previously_mounted = true;

         /*
          * If the device requires mount, close it, so the device can be ejected.
          */
         if (dev->requires_mount()) {
            dev->close(dcr);
            free_volume(dev);
         }

         /* Call autochanger only once unless ask_sysop called */
         if (try_autochanger) {
            int stat;
            Dmsg2(rdbglvl, "calling autoload Vol=%s Slot=%d\n",
               dcr->VolumeName, dcr->VolCatInfo.Slot);
            stat = autoload_device(dcr, SD_READ, NULL);
            if (stat > 0) {
               try_autochanger = false;
               continue;              /* try reading volume mounted */
            }
         }

         /* Mount a specific volume and no other */
         Dmsg0(rdbglvl, "calling dir_ask_sysop\n");
         if (!dir_ask_sysop_to_mount_volume(dcr, SD_READ)) {
            goto get_out;             /* error return */
         }

         /* Volume info is always needed because of VolType */
         Dmsg1(150, "dir_get_volume_info vol=%s\n", dcr->VolumeName);
         if (!dir_get_volume_info(dcr, dcr->VolumeName, GET_VOL_INFO_FOR_READ)) {
            Dmsg2(150, "dir_get_vol_info failed for vol=%s: %s\n",
                  dcr->VolumeName, jcr->errmsg);
            Jmsg1(jcr, M_WARNING, 0, "Read acquire: %s", jcr->errmsg);
         }
         dev->set_load();                /* set to load volume */

         try_autochanger = true;      /* permit trying the autochanger again */

         continue;                    /* try reading again */
      } /* end switch */
      break;
   } /* end for loop */

   if (!ok) {
      Jmsg2(jcr, M_FATAL, 0, _("Too many errors trying to mount %s device %s for reading.\n"),
            dev->print_type(), dev->print_name());
      goto get_out;
   }

   dev->clear_append();
   dev->set_read();
   jcr->sendJobStatus(JS_Running);
   Jmsg(jcr, M_INFO, 0, _("Ready to read from volume \"%s\" on %s device %s.\n"),
      dcr->VolumeName, dev->print_type(), dev->print_name());

get_out:
   dev->Lock();
   /* If failed and not writing plugin close device */
   if (!ok && dev->num_writers == 0 && dev->num_reserved() == 0) {
      generate_plugin_event(jcr, bsdEventDeviceClose, dcr);
   }
   /*
    * Normally we are blocked, but in at least one error case above
    *   we are not blocked because we unsuccessfully tried changing
    *   devices.
    */
   if (dev->is_blocked()) {
      dev->dunblock(DEV_LOCKED);
   } else {
      dev->Unlock();               /* dunblock() unlock the device too */
   }
   Dmsg2(rdbglvl, "dcr=%p dev=%p\n", dcr, dcr->dev);
   Dmsg2(rdbglvl, "MediaType dcr=%s dev=%s\n", dcr->media_type, dev->device->media_type);
   dev->Unlock_read_acquire();
   Leave(rdbglvl);
   return ok;
}

/*
 * Acquire device for writing. We permit multiple writers.
 *  If this is the first one, we read the label.
 *
 *  Returns: NULL if failed for any reason
 *           dcr if successful.
 *   Note, normally reserve_device_for_append() is called
 *   before this routine.
 */
DCR *acquire_device_for_append(DCR *dcr)
{
   DEVICE *dev = dcr->dev;
   JCR *jcr = dcr->jcr;
   bool ok = false;
   bool have_vol = false;

   Enter(200);
   dcr->set_ameta();
   init_device_wait_timers(dcr);

   dev->Lock_acquire();             /* only one job at a time */
   dev->Lock();
   Dmsg1(100, "acquire_append device is %s\n", dev->print_type());
   /*
    * With the reservation system, this should not happen
    */
   if (dev->can_read()) {
      Mmsg2(jcr->errmsg, "Want to append but %s device %s is busy reading.\n",
         dev->print_type(), dev->print_name());
      Jmsg(jcr, M_FATAL, 0, jcr->errmsg);
      Dmsg0(50, jcr->errmsg);
      goto get_out;
   }

   dev->clear_unload();

   /*
    * have_vol defines whether or not mount_next_write_volume should
    *   ask the Director again about what Volume to use.
    */
   if (dev->can_append() && dcr->is_suitable_volume_mounted() &&
       strcmp(dcr->VolCatInfo.VolCatStatus, "Recycle") != 0) {
      Dmsg0(190, "device already in append.\n");
      /*
       * At this point, the correct tape is already mounted, so
       *   we do not need to do mount_next_write_volume(), unless
       *   we need to recycle the tape.
       */
       if (dev->num_writers == 0) {
          dev->VolCatInfo = dcr->VolCatInfo;   /* structure assignment */
       }
       have_vol = dcr->is_tape_position_ok();
   }

   if (!have_vol) {
      dev->rLock(true);
      block_device(dev, BST_DOING_ACQUIRE);
      dev->Unlock();
      Dmsg1(190, "jid=%u Do mount_next_write_vol\n", (uint32_t)jcr->JobId);
      if (!dcr->mount_next_write_volume()) {
         if (!job_canceled(jcr)) {
            /* Reduce "noise" -- don't print if job canceled */
            Mmsg2(jcr->errmsg, _("Could not ready %s device %s for append.\n"),
               dev->print_type(), dev->print_name());
            Jmsg(jcr, M_FATAL, 0, jcr->errmsg);
            Dmsg0(50, jcr->errmsg);
         }
         dev->Lock();
         unblock_device(dev);
         goto get_out;
      }
      Dmsg2(190, "Output pos=%u:%u\n", dcr->dev->file, dcr->dev->block_num);
      dev->Lock();
      unblock_device(dev);
   }

   if (generate_plugin_event(jcr, bsdEventDeviceOpen, dcr) != bRC_OK) {
      Mmsg0(jcr->errmsg,  _("generate_plugin_event(bsdEventDeviceOpen) Failed\n"));
      Jmsg(jcr, M_FATAL, 0, jcr->errmsg);
      Dmsg0(50, jcr->errmsg);
      goto get_out;
   }

   dev->num_writers++;                /* we are now a writer */
   if (jcr->NumWriteVolumes == 0) {
      jcr->NumWriteVolumes = 1;
   }
   dev->VolCatInfo.VolCatJobs++;              /* increment number of jobs on vol */

   ok = dir_update_volume_info(dcr, false, false); /* send Volume info to Director */
   if (!ok) {                   /* We cannot use this volume/device */
      Jmsg(jcr, M_WARNING, 0, _("Warning cannot use Volume \"%s\", update_volume_info failed.\n"),
         dev->VolCatInfo.VolCatName);
      dev->num_writers--;       /* on fail update_volume do not update num_writers */
      /* TODO: See if we revert the NumWriteVolumes as well */
   }

   Dmsg4(100, "=== nwriters=%d nres=%d vcatjob=%d dev=%s\n",
      dev->num_writers, dev->num_reserved(), dev->VolCatInfo.VolCatJobs,
      dev->print_name());

get_out:
   /* Don't plugin close here, we might have multiple writers */
   dcr->clear_reserved();
   dev->Unlock();
   dev->Unlock_acquire();
   Leave(200);
   return ok ? dcr : NULL;
}

/*
 * This job is done, so release the device. From a Unix standpoint,
 *  the device remains open.
 *
 * Note, if we were spooling, we may enter with the device blocked.
 *   We unblock at the end, only if it was us who blocked the
 *   device.
 *
 */
bool release_device(DCR *dcr)
{
   JCR *jcr = dcr->jcr;
   DEVICE *dev = dcr->dev;
   bool ok = true;
   char tbuf[100];
   bsteal_lock_t holder;

   dev->Lock();
   if (!obtain_device_block(dev,
                            &holder,
                            0,  /* infinite wait */
                            BST_RELEASING)) {
      ASSERT2(0, "unable to obtain device block");
   }

   lock_volumes();
   Dmsg2(100, "release_device device %s is %s\n", dev->print_name(), dev->is_tape()?"tape":"disk");

   /* if device is reserved, job never started, so release the reserve here */
   dcr->clear_reserved();

   if (dev->can_read()) {
      VOLUME_CAT_INFO *vol = &dev->VolCatInfo;
      generate_plugin_event(jcr, bsdEventDeviceClose, dcr);
      dev->clear_read();              /* clear read bit */
      Dmsg2(150, "dir_update_vol_info. label=%d Vol=%s\n",
         dev->is_labeled(), vol->VolCatName);
      if (dev->is_labeled() && vol->VolCatName[0] != 0) {
         dir_update_volume_info(dcr, false, false); /* send Volume info to Director */
         remove_read_volume(jcr, dcr->VolumeName);
         volume_unused(dcr);
      }
   } else if (dev->num_writers > 0) {
      /*
       * Note if WEOT is set, we are at the end of the tape
       *   and may not be positioned correctly, so the
       *   job_media_record and update_vol_info have already been
       *   done, which means we skip them here.
       */
      dev->num_writers--;
      Dmsg1(100, "There are %d writers in release_device\n", dev->num_writers);
      if (dev->is_labeled()) {
         if (!dev->at_weot()) {
            Dmsg2(200, "dir_create_jobmedia. Release vol=%s dev=%s\n",
                  dev->getVolCatName(), dev->print_name());
         }
         if (!dev->at_weot() && !dir_create_jobmedia_record(dcr)) {
            Jmsg2(jcr, M_FATAL, 0, _("Could not create JobMedia record for Volume=\"%s\" Job=%s\n"),
               dcr->getVolCatName(), jcr->Job);
         }
         /* If no more writers, and no errors, and wrote something, write an EOF */
         if (!dev->num_writers && dev->can_write() && dev->block_num > 0) {
            dev->weof(dcr, 1);
            write_ansi_ibm_labels(dcr, ANSI_EOF_LABEL, dev->VolHdr.VolumeName);
         }
         if (!dev->at_weot()) {
            dev->VolCatInfo.VolCatFiles = dev->get_file();   /* set number of files */
            /* Note! do volume update before close, which zaps VolCatInfo */
            dir_update_volume_info(dcr, false, false); /* send Volume info to Director */
            Dmsg2(200, "dir_update_vol_info. Release vol=%s dev=%s\n",
                  dev->getVolCatName(), dev->print_name());
         }
         if (dev->num_writers == 0) {         /* if not being used */
            volume_unused(dcr);               /*  we obviously are not using the volume */
            generate_plugin_event(jcr, bsdEventDeviceClose, dcr);
         }
      }

   } else {
      /*
       * If we reach here, it is most likely because the job
       *   has failed, since the device is not in read mode and
       *   there are no writers. It was probably reserved.
       */
      volume_unused(dcr);
      generate_plugin_event(jcr, bsdEventDeviceClose, dcr);
   }
   Dmsg3(100, "%d writers, %d reserve, dev=%s\n", dev->num_writers, dev->num_reserved(),
         dev->print_name());

   /* If no writers, close if file or !CAP_ALWAYS_OPEN */
   if (dev->num_writers == 0 && (!dev->is_tape() || !dev->has_cap(CAP_ALWAYSOPEN))) {
      generate_plugin_event(jcr, bsdEventDeviceClose, dcr);
      if (!dev->close(dcr) && dev->errmsg[0]) {
         Jmsg(jcr, M_ERROR, 0, "%s", dev->errmsg);
      }
      free_volume(dev);
   }
   unlock_volumes();

   /* Do new tape alert code */
   dev->get_tape_alerts(dcr);
   /* alert_callback is in tape_alert.c -- show only most recent (last) alert */
   dev->show_tape_alerts(dcr, list_long, list_last, alert_callback);

   pthread_cond_broadcast(&dev->wait_next_vol);
   Dmsg2(100, "JobId=%u broadcast wait_device_release at %s\n",
         (uint32_t)jcr->JobId, bstrftimes(tbuf, sizeof(tbuf), (utime_t)time(NULL)));
   pthread_cond_broadcast(&wait_device_release);

   give_back_device_block(dev, &holder);
   /*
    * If we are the thread that blocked the device, then unblock it
    */
   if (pthread_equal(dev->no_wait_id, pthread_self())) {
      dev->dunblock(true);
   } else {
      dev->Unlock();
   }

   dev->end_of_job(dcr);

   if (dcr->keep_dcr) {
      dev->detach_dcr_from_dev(dcr);
   } else {
      free_dcr(dcr);
   }
   Dmsg2(100, "Device %s released by JobId=%u\n", dev->print_name(),
         (uint32_t)jcr->JobId);
   return ok;
}

/*
 * Clean up the device for reuse without freeing the memory
 */
bool clean_device(DCR *dcr)
{
   bool ok;
   dcr->keep_dcr = true;                  /* do not free the dcr */
   ok = release_device(dcr);
   dcr->keep_dcr = false;
   return ok;
}

/*
 * Create a new Device Control Record and attach
 *   it to the device (if this is a real job).
 * Note, this has been updated so that it can be called first
 *   without a DEVICE, then a second or third time with a DEVICE,
 *   and each time, it should cleanup and point to the new device.
 *   This should facilitate switching devices.
 * Note, each dcr must point to the controlling job (jcr).  However,
 *   a job can have multiple dcrs, so we must not store in the jcr's
 *   structure as previously. The higher level routine must store
 *   this dcr in the right place
 *
 */
DCR *new_dcr(JCR *jcr, DCR *dcr, DEVICE *dev, bool writing)
{
   DEVICE *odev;
   if (!dcr) {
      dcr = (DCR *)malloc(sizeof(DCR));
      memset(dcr, 0, sizeof(DCR));
      dcr->tid = pthread_self();
      dcr->uploads = New(alist(100, false));
      dcr->downloads = New(alist(100, false));
      dcr->spool_fd = -1;
   }
   dcr->jcr = jcr;                 /* point back to jcr */
   odev = dcr->dev;
   if (dcr->attached_to_dev && odev) {
      Dmsg2(100, "Detach 0x%x from olddev %s\n", dcr, odev->print_name());
      odev->detach_dcr_from_dev(dcr);
   }
   ASSERT2(!dcr->attached_to_dev, "DCR is attached. Wrong!");
   /* Set device information, possibly change device */
   if (dev) {
      ASSERT2(!dev->adata, "Called with adata dev. Wrong!");
      dev->free_dcr_blocks(dcr);
      dev->new_dcr_blocks(dcr);
      if (dcr->rec) {
         free_record(dcr->rec);
      }
      dcr->rec = new_record();
      /* Use job spoolsize prior to device spoolsize */
      if (jcr && jcr->spool_size) {
         dcr->max_job_spool_size = jcr->spool_size;
      } else {
         dcr->max_job_spool_size = dev->device->max_job_spool_size;
      }
      dcr->device = dev->device;
      dcr->set_dev(dev);
      Dmsg2(100, "Attach 0x%x to dev %s\n", dcr, dev->print_name());
      dev->attach_dcr_to_dev(dcr);
   }
   if (writing) {
      dcr->set_writing();
   } else {
      dcr->clear_writing();
   }
   return dcr;
}

/*
 * Search the dcrs list for the given dcr. If it is found,
 *  as it should be, then remove it. Also zap the jcr pointer
 *  to the dcr if it is the same one.
 *
 * Note, this code will be turned on when we can write to multiple
 *  dcrs at the same time.
 */
#ifdef needed
static void remove_dcr_from_dcrs(DCR *dcr)
{
   JCR *jcr = dcr->jcr;
   if (jcr->dcrs) {
      int i = 0;
      DCR *ldcr;
      int num = jcr->dcrs->size();
      for (i=0; i < num; i++) {
         ldcr = (DCR *)jcr->dcrs->get(i);
         if (ldcr == dcr) {
            jcr->dcrs->remove(i);
            if (jcr->dcr == dcr) {
               jcr->dcr = NULL;
            }
         }
      }
   }
}
#endif

void DEVICE::attach_dcr_to_dev(DCR *dcr)
{
   JCR *jcr = dcr->jcr;

   Lock_dcrs();
   jcr = dcr->jcr;
   if (jcr) Dmsg1(500, "JobId=%u enter attach_dcr_to_dev\n", (uint32_t)jcr->JobId);
   /* ***FIXME*** return error if dev not initiated */
   if (!dcr->attached_to_dev && initiated && jcr && jcr->getJobType() != JT_SYSTEM) {
      ASSERT2(!adata, "Called on adata dev. Wrong!");
      Dmsg4(200, "Attach Jid=%d dcr=%p size=%d dev=%s\n", (uint32_t)jcr->JobId,
         dcr, attached_dcrs->size(), print_name());
      attached_dcrs->append(dcr);  /* attach dcr to device */
      dcr->attached_to_dev = true;
   }
   Unlock_dcrs();
}

/*
 * Note!! Do not enter with dev->Lock() since unreserve_device()
 *   is going to lock it too.
 */
void DEVICE::detach_dcr_from_dev(DCR *dcr)
{
   Dmsg0(500, "Enter detach_dcr_from_dev\n"); /* jcr is NULL in some cases */

   Lock();
   Lock_dcrs();
   /* Detach this dcr only if attached */
   if (dcr->attached_to_dev) {
      ASSERT2(!adata, "Called with adata dev. Wrong!");
      dcr->unreserve_device(true);
      Dmsg4(200, "Detach Jid=%d dcr=%p size=%d to dev=%s\n", (uint32_t)dcr->jcr->JobId,
         dcr, attached_dcrs->size(), print_name());
      dcr->attached_to_dev = false;
      if (attached_dcrs->size()) {
         attached_dcrs->remove(dcr);  /* detach dcr from device */
      }
   }
   /* Check if someone accidentally left a drive reserved, and clear it */
   if (attached_dcrs->size() == 0 && num_reserved() > 0) {
      Pmsg3(000, "Warning!!! Detach %s DCR: dcrs=0 reserved=%d setting reserved==0. dev=%s\n",
         dcr->is_writing() ? "writing" : "reading", num_reserved(), print_name());
      m_num_reserved = 0;
   }
   dcr->attached_to_dev = false;
   Unlock_dcrs();
   Unlock();
}

/*
 * Free up all aspects of the given dcr -- i.e. dechain it,
 *  release allocated memory, zap pointers, ...
 */
void free_dcr(DCR *dcr)
{
   JCR *jcr;

   jcr = dcr->jcr;

   if (dcr->dev) {
      dcr->dev->detach_dcr_from_dev(dcr);
   }

   if (dcr->dev) {
      dcr->dev->free_dcr_blocks(dcr);
   } else {
      dcr->ameta_block = NULL;
      free_block(dcr->block);
   }
   if (dcr->rec) {
      free_record(dcr->rec);
   }
   if (jcr && jcr->dcr == dcr) {
      jcr->dcr = NULL;
   }
   if (jcr && jcr->read_dcr == dcr) {
      jcr->read_dcr = NULL;
   }
   delete dcr->uploads;
   delete dcr->downloads;
   free(dcr);
}

static void set_dcr_from_vol(DCR *dcr, VOL_LIST *vol)
{
   /*
    * Note, if we want to be able to work from a .bsr file only
    *  for disaster recovery, we must "simulate" reading the catalog
    */
   bstrncpy(dcr->VolumeName, vol->VolumeName, sizeof(dcr->VolumeName));
   dcr->setVolCatName(vol->VolumeName);
   bstrncpy(dcr->media_type, vol->MediaType, sizeof(dcr->media_type));
   dcr->VolCatInfo.Slot = vol->Slot;
   dcr->VolCatInfo.InChanger = vol->Slot > 0;
   dcr->CurrentVol = vol;   /* Keep VOL_LIST pointer (freed at the end of the job) */
}
