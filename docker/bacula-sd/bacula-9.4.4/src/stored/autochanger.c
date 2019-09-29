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
 *  Routines for handling the autochanger.
 *
 *   Written by Kern Sibbald, August MMII
 */

#include "bacula.h"                   /* pull in global headers */
#include "stored.h"                   /* pull in Storage Deamon headers */

static const int dbglvl = 60;

/* Forward referenced functions */
static void lock_changer(DCR *dcr);
static void unlock_changer(DCR *dcr);
static bool unload_other_drive(DCR *dcr, int slot, bool writing);

bool DCR::is_virtual_autochanger()
{
   return device->changer_command &&
      (device->changer_command[0] == 0 ||
       strcmp(device->changer_command, "/dev/null") == 0);
}

/* Init all the autochanger resources found */
bool init_autochangers()
{
   bool OK = true;
   AUTOCHANGER *changer;
   /* Ensure that the media_type for each device is the same */
   foreach_res(changer, R_AUTOCHANGER) {
      DEVRES *device;
      foreach_alist(device, changer->device) {
         /*
          * If the device does not have a changer name or changer command
          *   defined, use the one from the Autochanger resource
          */
         if (!device->changer_name && changer->changer_name) {
            device->changer_name = bstrdup(changer->changer_name);
         }
         if (!device->changer_command && changer->changer_command) {
            device->changer_command = bstrdup(changer->changer_command);
         }
         if (!device->changer_name) {
            Jmsg(NULL, M_ERROR, 0,
               _("No Changer Name given for device %s. Cannot continue.\n"),
               device->hdr.name);
            OK = false;
         }
         if (!device->changer_command) {
            Jmsg(NULL, M_ERROR, 0,
               _("No Changer Command given for device %s. Cannot continue.\n"),
               device->hdr.name);
            OK = false;
         }
      }
   }
   return OK;
}


/*
 * Called here to do an autoload using the autochanger, if
 *  configured, and if a Slot has been defined for this Volume.
 *  On success this routine loads the indicated tape, but the
 *  label is not read, so it must be verified.
 *
 *  Note if dir is not NULL, it is the console requesting the
 *   autoload for labeling, so we respond directly to the
 *   dir bsock.
 *
 *  Returns: 1 on success
 *           0 on failure (no changer available)
 *          -1 on error on autochanger
 */
int autoload_device(DCR *dcr, bool writing, BSOCK *dir)
{
   JCR *jcr = dcr->jcr;
   DEVICE * volatile dev = dcr->dev;
   char *new_vol_name = dcr->VolumeName;
   int slot;
   int drive = dev->drive_index;
   int rtn_stat = -1;                 /* error status */
   POOLMEM *changer;

   if (!dev->is_autochanger()) {
      Dmsg1(dbglvl, "Device %s is not an autochanger\n", dev->print_name());
      return 0;
   }

   /* An empty ChangerCommand => virtual disk autochanger */
   if (dcr->is_virtual_autochanger()) {
      Dmsg0(dbglvl, "ChangerCommand=0, virtual disk changer\n");
      return 1;                       /* nothing to load */
   }

   slot = dcr->VolCatInfo.InChanger ? dcr->VolCatInfo.Slot : 0;
   /*
    * Handle autoloaders here.  If we cannot autoload it, we
    *  will return 0 so that the sysop will be asked to load it.
    */
   if (writing && slot <= 0) {
      if (dir) {
         return 0;                    /* For user, bail out right now */
      }
      /* ***FIXME*** this really should not be here */
      if (dir_find_next_appendable_volume(dcr)) {
         slot = dcr->VolCatInfo.InChanger ? dcr->VolCatInfo.Slot : 0;
      } else {
         slot = 0;
         dev->clear_wait();
      }
   }
   Dmsg4(dbglvl, "Want slot=%d drive=%d InChgr=%d Vol=%s\n",
         dcr->VolCatInfo.Slot, drive,
         dcr->VolCatInfo.InChanger, dcr->getVolCatName());

   changer = get_pool_memory(PM_FNAME);
   if (slot <= 0) {
      /* Suppress info when polling */
      if (!dev->poll) {
         Jmsg(jcr, M_INFO, 0, _("No slot defined in catalog (slot=%d) for Volume \"%s\" on %s.\n"),
              slot, dcr->getVolCatName(), dev->print_name());
         Jmsg(jcr, M_INFO, 0, _("Cartridge change or \"update slots\" may be required.\n"));
      }
      rtn_stat = 0;
   } else if (!dcr->device->changer_name) {
      /* Suppress info when polling */
      if (!dev->poll) {
         Jmsg(jcr, M_INFO, 0, _("No \"Changer Device\" for %s. Manual load of Volume may be required.\n"),
              dev->print_name());
      }
      rtn_stat = 0;
  } else if (!dcr->device->changer_command) {
      /* Suppress info when polling */
      if (!dev->poll) {
         Jmsg(jcr, M_INFO, 0, _("No \"Changer Command\" for %s. Manual load of Volume may be requird.\n"),
              dev->print_name());
      }
      rtn_stat = 0;
  } else {
      /* Attempt to load the Volume */
      uint32_t timeout = dcr->device->max_changer_wait;
      int loaded, status;

      loaded = get_autochanger_loaded_slot(dcr);
      if (loaded < 0) {   /* Autochanger error, try again */
         loaded = get_autochanger_loaded_slot(dcr);
      }
      Dmsg2(dbglvl, "Found loaded=%d drive=%d\n", loaded, drive);

      if (loaded <= 0 || loaded != slot) {
         POOL_MEM results(PM_MESSAGE);

         /* Unload anything in our drive */
         if (!unload_autochanger(dcr, loaded)) {
            goto bail_out;
         }

         /* Make sure desired slot is unloaded */
         if (!unload_other_drive(dcr, slot, writing)) {
            goto bail_out;
         }

         /*
          * Load the desired cassette
          */
         lock_changer(dcr);
         Dmsg2(dbglvl, "Doing changer load slot %d %s\n", slot, dev->print_name());
         Jmsg(jcr, M_INFO, 0,
            _("3304 Issuing autochanger \"load Volume %s, Slot %d, Drive %d\" command.\n"),
            new_vol_name, slot, drive);
         Dmsg3(dbglvl,
            "3304 Issuing autochanger \"load Volume %s, Slot %d, Drive %d\" command.\n",
            new_vol_name, slot, drive);

         dcr->VolCatInfo.Slot = slot;    /* slot to be loaded */
         changer = edit_device_codes(dcr, changer, dcr->device->changer_command, "load");
         dev->close(dcr);
         Dmsg1(dbglvl, "Run program=%s\n", changer);
         status = run_program_full_output(changer, timeout, results.addr());
         if (status == 0) {
            Jmsg(jcr, M_INFO, 0, _("3305 Autochanger \"load Volume %s, Slot %d, Drive %d\", status is OK.\n"),
                    new_vol_name, slot, drive);
            Dmsg3(dbglvl, "OK: load volume %s, slot %d, drive %d.\n", new_vol_name,  slot, drive);
            bstrncpy(dev->LoadedVolName, new_vol_name, sizeof(dev->LoadedVolName));
            dev->set_slot(slot);      /* set currently loaded slot */
            if (dev->vol) {
               /* We just swapped this Volume so it cannot be swapping any more */
               dev->vol->clear_swapping();
            }
         } else {
            berrno be;
            be.set_errno(status);
            Dmsg5(dbglvl, "Error: load Volume %s, Slot %d, Drive %d, bad stats=%s.\nResults=%s\n",
                new_vol_name, slot, drive,
               be.bstrerror(), results.c_str());
            Jmsg(jcr, M_FATAL, 0, _("3992 Bad autochanger \"load Volume %s Slot %d, Drive %d\": "
                 "ERR=%s.\nResults=%s\n"),
                    new_vol_name, slot, drive, be.bstrerror(), results.c_str());
            rtn_stat = -1;            /* hard error */
            dev->clear_slot();        /* mark unknown */
         }
         unlock_changer(dcr);
      } else {
         status = 0;                  /* we got what we want */
         dev->set_slot(slot);         /* set currently loaded slot */
         bstrncpy(dev->LoadedVolName, new_vol_name, sizeof(dev->LoadedVolName));
      }
      Dmsg1(dbglvl, "After changer, status=%d\n", status);
      if (status == 0) {              /* did we succeed? */
         rtn_stat = 1;                /* tape loaded by changer */
      }
   }
   free_pool_memory(changer);
   return rtn_stat;

bail_out:
   free_pool_memory(changer);
   return -1;

}

/*
 * Returns: -1 if error from changer command
 *          slot otherwise
 *  Note, this is safe to do without releasing the drive
 *   since it does not attempt load/unload a slot.
 */
int get_autochanger_loaded_slot(DCR *dcr)
{
   JCR *jcr = dcr->jcr;
   DEVICE *dev = dcr->dev;
   int status, loaded;
   uint32_t timeout = dcr->device->max_changer_wait;
   int drive = dcr->dev->drive_index;
   POOL_MEM results(PM_MESSAGE);
   POOLMEM *changer;

   if (!dev->is_autochanger()) {
      return -1;
   }
   if (!dcr->device->changer_command) {
      return -1;
   }

   if (dev->get_slot() > 0 && dev->has_cap(CAP_ALWAYSOPEN)) {
      Dmsg1(dbglvl, "Return cached slot=%d\n", dev->get_slot());
      return dev->get_slot();
   }

   /* Virtual disk autochanger */
   if (dcr->is_virtual_autochanger()) {
      return 1;
   }

   /* Find out what is loaded, zero means device is unloaded */
   changer = get_pool_memory(PM_FNAME);
   lock_changer(dcr);
   /* Suppress info when polling */
   if (!dev->poll && chk_dbglvl(1)) {
      Jmsg(jcr, M_INFO, 0, _("3301 Issuing autochanger \"loaded? drive %d\" command.\n"),
           drive);
   }
   changer = edit_device_codes(dcr, changer, dcr->device->changer_command, "loaded");
   Dmsg1(dbglvl, "Run program=%s\n", changer);
   status = run_program_full_output(changer, timeout, results.addr());
   Dmsg3(dbglvl, "run_prog: %s stat=%d result=%s", changer, status, results.c_str());
   if (status == 0) {
      loaded = str_to_int32(results.c_str());
      if (loaded > 0) {
         /* Suppress info when polling */
         if (!dev->poll && chk_dbglvl(1)) {
            Jmsg(jcr, M_INFO, 0, _("3302 Autochanger \"loaded? drive %d\", result is Slot %d.\n"),
                 drive, loaded);
         }
         dev->set_slot(loaded);
      } else {
         /* Suppress info when polling */
         if (!dev->poll && chk_dbglvl(1)) {
            Jmsg(jcr, M_INFO, 0, _("3302 Autochanger \"loaded? drive %d\", result: nothing loaded.\n"),
                 drive);
         }
         if (loaded == 0) {      /* no slot loaded */
            dev->set_slot(0);
         } else {                /* probably some error */
            dev->clear_slot();   /* unknown */
         }
      }
   } else {
      berrno be;
      be.set_errno(status);
      Jmsg(jcr, M_INFO, 0, _("3991 Bad autochanger \"loaded? drive %d\" command: "
           "ERR=%s.\nResults=%s\n"), drive, be.bstrerror(), results.c_str());
      Dmsg3(dbglvl, "Error: autochanger loaded? drive %d "
           "ERR=%s.\nResults=%s\n", drive, be.bstrerror(), results.c_str());
      loaded = -1;              /* force unload */
      dev->clear_slot();        /* slot unknown */
   }
   unlock_changer(dcr);
   free_pool_memory(changer);
   return loaded;
}

static void lock_changer(DCR *dcr)
{
   AUTOCHANGER *changer_res = dcr->device->changer_res;
   if (changer_res) {
      int errstat;
      Dmsg1(dbglvl, "Locking changer %s\n", changer_res->hdr.name);
      if ((errstat=rwl_writelock(&changer_res->changer_lock)) != 0) {
         berrno be;
         Jmsg(dcr->jcr, M_ERROR_TERM, 0, _("Lock failure on autochanger. ERR=%s\n"),
              be.bstrerror(errstat));
      }
   }
}

static void unlock_changer(DCR *dcr)
{
   AUTOCHANGER *changer_res = dcr->device->changer_res;
   if (changer_res) {
      int errstat;
      Dmsg1(dbglvl, "Unlocking changer %s\n", changer_res->hdr.name);
      if ((errstat=rwl_writeunlock(&changer_res->changer_lock)) != 0) {
         berrno be;
         Jmsg(dcr->jcr, M_ERROR_TERM, 0, _("Unlock failure on autochanger. ERR=%s\n"),
              be.bstrerror(errstat));
      }
   }
}

/*
 * Unload the volume, if any, in this drive
 *  On entry: loaded == 0 -- nothing to do
 *            loaded  < 0 -- check if anything to do
 *            loaded  > 0 -- load slot == loaded
 */
bool unload_autochanger(DCR *dcr, int loaded)
{
   DEVICE *dev = dcr->dev;
   JCR *jcr = dcr->jcr;
   const char *old_vol_name;
   int slot;
   uint32_t timeout = dcr->device->max_changer_wait;
   bool ok = true;

   if (loaded == 0) {
      return true;
   }

   if (!dev->is_autochanger() || !dcr->device->changer_name ||
       !dcr->device->changer_command) {
      return false;
   }

   /* Virtual disk autochanger */
   if (dcr->is_virtual_autochanger()) {
      dev->clear_unload();
      return true;
   }

   lock_changer(dcr);
   if (dev->LoadedVolName[0]) {
      old_vol_name = dev->LoadedVolName;
   } else {
      old_vol_name = "*Unknown*";
   }
   if (loaded < 0) {
      loaded = get_autochanger_loaded_slot(dcr);
      if (loaded < 0) {   /* try again, maybe autochanger error */
         loaded = get_autochanger_loaded_slot(dcr);
      }
   }

   if (loaded > 0) {
      POOL_MEM results(PM_MESSAGE);
      POOLMEM *changer = get_pool_memory(PM_FNAME);
      Jmsg(jcr, M_INFO, 0,
         _("3307 Issuing autochanger \"unload Volume %s, Slot %d, Drive %d\" command.\n"),
         old_vol_name, loaded, dev->drive_index);
      Dmsg3(dbglvl,
         "3307 Issuing autochanger \"unload Volume %s, Slot %d, Drive %d\" command.\n",
         old_vol_name, loaded, dev->drive_index);
      slot = dcr->VolCatInfo.Slot;
      dcr->VolCatInfo.Slot = loaded;
      changer = edit_device_codes(dcr, changer,
                   dcr->device->changer_command, "unload");
      dev->close(dcr);
      Dmsg1(dbglvl, "Run program=%s\n", changer);
      int stat = run_program_full_output(changer, timeout, results.addr());
      dcr->VolCatInfo.Slot = slot;
      if (stat != 0) {
         berrno be;
         be.set_errno(stat);
         Jmsg(jcr, M_INFO, 0, _("3995 Bad autochanger \"unload Volume %s, Slot %d, Drive %d\": "
              "ERR=%s\nResults=%s\n"),
              old_vol_name, loaded, dev->drive_index, be.bstrerror(), results.c_str());
         Dmsg5(dbglvl, "Error: unload Volume %s, Slot %d, Drive %d, bad stats=%s.\nResults=%s\n",
               old_vol_name, loaded, dev->drive_index,
               be.bstrerror(), results.c_str());
         ok = false;
         dev->clear_slot();        /* unknown */
      } else {
         dev->set_slot(0);         /* unload is OK, mark nothing loaded */
         dev->clear_unload();
         dev->LoadedVolName[0] = 0; /* clear loaded volume name */
      }
      free_pool_memory(changer);
   }
   unlock_changer(dcr);

   if (ok) {
      free_volume(dev);
   }
   return ok;
}

/*
 * Unload the slot if mounted in a different drive
 */
static bool unload_other_drive(DCR *dcr, int slot, bool writing)
{
   DEVICE *dev = NULL;
   DEVICE *dev_save;
   bool found = false;
   AUTOCHANGER *changer = dcr->dev->device->changer_res;
   DEVRES *device;
   int retries = 0;                /* wait for device retries */
   int loaded;
   int i;

   if (!changer || !changer->device) {
      return false;
   }
   if (changer->device->size() == 1) {
      return true;
   }

   /*
    * We look for the slot number corresponding to the tape
    *   we want in other drives, and if possible, unload
    *   it.
    */
   Dmsg1(dbglvl, "Begin wiffle through devices looking for slot=%d\n", slot);
   /*
    *  foreach_alist(device, changer->device) {
    *
    * The above fails to loop through all devices. It is
    * probably a compiler bug.
    */
   for (i=0; i < changer->device->size(); i++) {
      device = (DEVRES *)changer->device->get(i);
      dev = device->dev;
      if (!dev) {
         Dmsg0(dbglvl, "No dev attached to device\n");
         continue;
      }

      dev_save = dcr->dev;
      dcr->set_dev(dev);
      loaded = get_autochanger_loaded_slot(dcr);
      dcr->set_dev(dev_save);

      if (loaded > 0) {
         Dmsg4(dbglvl, "Want slot=%d, drive=%d loaded=%d dev=%s\n",
            slot, dev->drive_index, loaded, dev->print_name());
         if (loaded == slot) {
            found = true;
            break;
         }
      } else {
         Dmsg4(dbglvl, "After slot=%d drive=%d loaded=%d dev=%s\n",
            slot, dev->drive_index, loaded, dev->print_name());
      }
   }
   Dmsg1(dbglvl, "End wiffle through devices looking for slot=%d\n", slot);
   if (!found) {
      Dmsg1(dbglvl, "Slot=%d not found in another device\n", slot);
      return true;
   } else {
      Dmsg3(dbglvl, "Slot=%d drive=%d found in dev=%s\n", slot, dev->drive_index, dev->print_name());
   }

   /*
    * The Volume we want is on another device.
    * If we want the Volume to read it, and the other device where the
    *   Volume is currently is not open, we simply unload the Volume then
    *   then the subsequent code will load it in the desired drive.
    * If want to write or  the device is open, we attempt to wait for
    *   the Volume to become available.
    */
   if (writing || dev->is_open()) {
      if (dev->is_busy()) {
         Dmsg4(dbglvl, "Vol %s for dev=%s in use dev=%s slot=%d\n",
              dcr->VolumeName, dcr->dev->print_name(),
              dev->print_name(), slot);
      }
      for (int i=0; i < 3; i++) {
         if (dev->is_busy()) {
            Dmsg0(40, "Device is busy. Calling wait_for_device()\n");
            wait_for_device(dcr, retries);
            continue;
         }
         break;
      }
      if (dev->is_busy()) {
         Jmsg(dcr->jcr, M_WARNING, 0, _("Volume \"%s\" wanted on %s is in use by device %s\n"),
              dcr->VolumeName, dcr->dev->print_name(), dev->print_name());
         Dmsg4(dbglvl, "Vol %s for dev=%s is busy dev=%s slot=%d\n",
              dcr->VolumeName, dcr->dev->print_name(), dev->print_name(), dev->get_slot());
         Dmsg2(dbglvl, "num_writ=%d reserv=%d\n", dev->num_writers, dev->num_reserved());
         volume_unused(dcr);
         return false;
      }
   }
   return unload_dev(dcr, dev);
}

/*
 * Unconditionally unload a specified drive
 */
bool unload_dev(DCR *dcr, DEVICE *dev)
{
   JCR *jcr = dcr->jcr;
   bool ok = true;
   uint32_t timeout = dcr->device->max_changer_wait;
   AUTOCHANGER *changer = dcr->dev->device->changer_res;
   const char *old_vol_name = dcr->VolumeName;
   DEVICE *save_dev;
   int save_slot;

   if (!changer) {
      return false;
   }

   save_dev = dcr->dev;               /* save dcr device */
   dcr->set_dev(dev);                 /* temporarily point dcr at other device */

   get_autochanger_loaded_slot(dcr);

   /* Fail if we have no slot to unload */
   if (dev->get_slot() <= 0) {
      if (dev->get_slot() < 0) {
         Dmsg1(dbglvl, "Cannot unload, slot not defined. dev=%s\n",
            dev->print_name());
      }
      dcr->set_dev(save_dev);
      return false;
   }

   save_slot = dcr->VolCatInfo.Slot;
   dcr->VolCatInfo.Slot = dev->get_slot();

   POOLMEM *changer_cmd = get_pool_memory(PM_FNAME);
   POOL_MEM results(PM_MESSAGE);
   if (old_vol_name[0] == 0) {
      if (dev->LoadedVolName[0]) {
         old_vol_name = dev->LoadedVolName;
      } else {
         old_vol_name = "*Unknown*";
      }
   }
   lock_changer(dcr);
   Jmsg(jcr, M_INFO, 0,
      _("3307 Issuing autochanger \"unload Volume %s, Slot %d, Drive %d\" command.\n"),
      old_vol_name, dev->get_slot(), dev->drive_index);
   Dmsg3(0/*dbglvl*/, "Issuing autochanger \"unload Volume %s, Slot %d, Drive %d\" command.\n",
        old_vol_name, dev->get_slot(), dev->drive_index);

   changer_cmd = edit_device_codes(dcr, changer_cmd,
                dcr->device->changer_command, "unload");
   dev->close(dcr);
   Dmsg2(dbglvl, "close dev=%s reserve=%d\n", dev->print_name(),
      dev->num_reserved());
   Dmsg1(dbglvl, "Run program=%s\n", changer_cmd);
   int stat = run_program_full_output(changer_cmd, timeout, results.addr());
   dcr->VolCatInfo.Slot = save_slot;
   if (stat != 0) {
      berrno be;
      be.set_errno(stat);
      Jmsg(jcr, M_INFO, 0, _("3997 Bad autochanger \"unload Volume %s, Slot %d, Drive %d\": ERR=%s.\n"),
           old_vol_name, dev->get_slot(), dev->drive_index, be.bstrerror());
      Dmsg5(dbglvl, "Error: unload Volume %s, Slot %d, Drive %d bad stats=%s.\nResults=%s\n",
            old_vol_name, dev->get_slot(), dev->drive_index,
            be.bstrerror(), results.c_str());
      ok = false;
      dev->clear_slot();          /* unknown */
   } else {
      Dmsg3(dbglvl, "Volume %s, Slot %d unloaded %s\n",
            old_vol_name, dev->get_slot(), dev->print_name());
      dev->set_slot(0);           /* unload OK, mark nothing loaded */
      dev->clear_unload();
      dev->LoadedVolName[0] = 0;
   }
   unlock_changer(dcr);

   if (ok) {
      free_volume(dev);
   }
   dcr->set_dev(save_dev);
   free_pool_memory(changer_cmd);
   return ok;
}



/*
 * List the Volumes that are in the autoloader possibly
 *   with their barcodes.
 *   We assume that it is always the Console that is calling us.
 */
bool autochanger_cmd(DCR *dcr, BSOCK *dir, const char *cmd)
{
   DEVICE *dev = dcr->dev;
   uint32_t timeout = dcr->device->max_changer_wait;
   POOLMEM *changer;
   BPIPE *bpipe;
   int len = sizeof_pool_memory(dir->msg) - 1;
   int stat;

   if (!dev->is_autochanger() || !dcr->device->changer_name ||
       !dcr->device->changer_command) {
      if (strcasecmp(cmd, "drives") == 0) {
         dir->fsend("drives=1\n");
      }
      dir->fsend(_("3993 Device %s not an autochanger device.\n"),
         dev->print_name());
      return false;
   }

   if (strcasecmp(cmd, "drives") == 0) {
      AUTOCHANGER *changer_res = dcr->device->changer_res;
      int drives = 1;
      if (changer_res && changer_res->device) {
         drives = changer_res->device->size();
      }
      dir->fsend("drives=%d\n", drives);
      Dmsg1(dbglvl, "drives=%d\n", drives);
      return true;
   }

   /* If listing, reprobe changer */
   if (bstrcasecmp(cmd, "list") || bstrcasecmp(cmd, "listall")) {
      dcr->dev->set_slot(0);
      get_autochanger_loaded_slot(dcr);
   }

   changer = get_pool_memory(PM_FNAME);
   lock_changer(dcr);
   /* Now issue the command */
   changer = edit_device_codes(dcr, changer,
                 dcr->device->changer_command, cmd);
   dir->fsend(_("3306 Issuing autochanger \"%s\" command.\n"), cmd);
   bpipe = open_bpipe(changer, timeout, "r");
   if (!bpipe) {
      dir->fsend(_("3996 Open bpipe failed.\n"));
      goto bail_out;            /* TODO: check if we need to return false */
   }
   if (bstrcasecmp(cmd, "list") || bstrcasecmp(cmd, "listall")) {
      /* Get output from changer */
      while (fgets(dir->msg, len, bpipe->rfd)) {
         dir->msglen = strlen(dir->msg);
         Dmsg1(dbglvl, "<stored: %s\n", dir->msg);
         dir->send();
      }
   } else if (strcasecmp(cmd, "slots") == 0 ) {
      char buf[100], *p;
      /* For slots command, read a single line */
      buf[0] = 0;
      fgets(buf, sizeof(buf)-1, bpipe->rfd);
      buf[sizeof(buf)-1] = 0;
      /* Strip any leading space in front of # of slots */
      for (p=buf; B_ISSPACE(*p); p++)
        { }
      dir->fsend("slots=%s", p);
      Dmsg1(dbglvl, "<stored: %s", dir->msg);
   }

   stat = close_bpipe(bpipe);
   if (stat != 0) {
      berrno be;
      be.set_errno(stat);
      dir->fsend(_("Autochanger error: ERR=%s\n"), be.bstrerror());
   }

bail_out:
   unlock_changer(dcr);
   free_pool_memory(changer);
   return true;
}


/*
 * Edit codes into ChangerCommand
 *  %% = %
 *  %a = archive device name
 *  %c = changer device name
 *  %d = changer drive index
 *  %f = Client's name
 *  %j = Job name
 *  %l = archive control channel name
 *  %o = command
 *  %s = Slot base 0
 *  %S = Slot base 1
 *  %v = Volume name
 *
 *
 *  omsg = edited output message
 *  imsg = input string containing edit codes (%x)
 *  cmd = command string (load, unload, ...)
 *
 */
char *edit_device_codes(DCR *dcr, char *omsg, const char *imsg, const char *cmd)
{
   const char *p;
   const char *str;
   char add[20];

   *omsg = 0;
   Dmsg1(1800, "edit_device_codes: %s\n", imsg);
   for (p=imsg; *p; p++) {
      if (*p == '%') {
         switch (*++p) {
         case '%':
            str = "%";
            break;
         case 'a':
            str = dcr->dev->archive_name();
            break;
         case 'c':
            str = NPRT(dcr->device->changer_name);
            break;
         case 'l':
            str = NPRT(dcr->device->control_name);
            break;
         case 'd':
            sprintf(add, "%d", dcr->dev->drive_index);
            str = add;
            break;
         case 'o':
            str = NPRT(cmd);
            break;
         case 's':
            sprintf(add, "%d", dcr->VolCatInfo.Slot - 1);
            str = add;
            break;
         case 'S':
            sprintf(add, "%d", dcr->VolCatInfo.Slot);
            str = add;
            break;
         case 'j':                    /* Job name */
            str = dcr->jcr->Job;
            break;
         case 'v':
            if (dcr->dev->LoadedVolName[0]) {
               str = dcr->dev->LoadedVolName;
            } else if (dcr->VolCatInfo.VolCatName[0]) {
               str = dcr->VolCatInfo.VolCatName;
            } else if (dcr->VolumeName[0]) {
               str = dcr->VolumeName;
            } else if (dcr->dev->vol && dcr->dev->vol->vol_name) {
               str = dcr->dev->vol->vol_name;
            } else {
               str = dcr->dev->VolHdr.VolumeName;
            }
            break;
         case 'f':
            str = NPRT(dcr->jcr->client_name);
            break;

         default:
            add[0] = '%';
            add[1] = *p;
            add[2] = 0;
            str = add;
            break;
         }
      } else {
         add[0] = *p;
         add[1] = 0;
         str = add;
      }
      Dmsg1(1900, "add_str %s\n", str);
      pm_strcat(&omsg, (char *)str);
      Dmsg1(1800, "omsg=%s\n", omsg);
   }
   Dmsg1(800, "omsg=%s\n", omsg);
   return omsg;
}
