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
 *  Higher Level Device routines.
 *  Knows about Bacula tape labels and such
 *
 *  NOTE! In general, subroutines that have the word
 *        "device" in the name do locking.  Subroutines
 *        that have the word "dev" in the name do not
 *        do locking.  Thus if xxx_device() calls
 *        yyy_dev(), all is OK, but if xxx_device()
 *        calls yyy_device(), everything will hang.
 *        Obviously, no zzz_dev() is allowed to call
 *        a www_device() or everything falls apart.
 *
 * Concerning the routines dev->rLock()() and block_device()
 *  see the end of this module for details.  In general,
 *  blocking a device leaves it in a state where all threads
 *  other than the current thread block when they attempt to
 *  lock the device. They remain suspended (blocked) until the device
 *  is unblocked. So, a device is blocked during an operation
 *  that takes a long time (initialization, mounting a new
 *  volume, ...) locking a device is done for an operation
 *  that takes a short time such as writing data to the
 *  device.
 *
 *
 *   Kern Sibbald, MM, MMI
 *
 */

#include "bacula.h"                   /* pull in global headers */
#include "stored.h"                   /* pull in Storage Deamon headers */

/* Forward referenced functions */

/*
 * This is the dreaded moment. We either have an end of
 * medium condition or worse, and error condition.
 * Attempt to "recover" by obtaining a new Volume.
 *
 * Here are a few things to know:
 *  dcr->VolCatInfo contains the info on the "current" tape for this job.
 *  dev->VolCatInfo contains the info on the tape in the drive.
 *    The tape in the drive could have changed several times since
 *    the last time the job used it (jcr->VolCatInfo).
 *  dcr->VolumeName is the name of the current/desired tape in the drive.
 *
 * We enter with device locked, and
 *     exit with device locked.
 *
 * Note, we are called only from one place in block.c for the daemons.
 *     The btape utility calls it from btape.c.
 *
 *  Returns: true  on success
 *           false on failure
 */
bool fixup_device_block_write_error(DCR *dcr, int retries)
{
   char PrevVolName[MAX_NAME_LENGTH];
   DEV_BLOCK *block = dcr->block;
   DEV_BLOCK *ameta_block = dcr->ameta_block;
   DEV_BLOCK *adata_block = dcr->adata_block;
   char b1[30], b2[30];
   time_t wait_time;
   char dt[MAX_TIME_LENGTH];
   JCR *jcr = dcr->jcr;
   DEVICE *dev;
   int blocked;              /* save any previous blocked status */
   bool ok = false;
   bool save_adata = dcr->dev->adata;

   Enter(100);
   if (save_adata) {
      dcr->set_ameta();      /* switch to working with ameta */
   }
   dev = dcr->dev;
   blocked = dev->blocked();

   wait_time = time(NULL);

   /*
    * If we are blocked at entry, unblock it, and set our own block status
    */
   if (blocked != BST_NOT_BLOCKED) {
      unblock_device(dev);
   }
   block_device(dev, BST_DOING_ACQUIRE);

   /* Continue unlocked, but leave BLOCKED */
   dev->Unlock();

   bstrncpy(PrevVolName, dev->getVolCatName(), sizeof(PrevVolName));
   bstrncpy(dev->VolHdr.PrevVolumeName, PrevVolName, sizeof(dev->VolHdr.PrevVolumeName));

   /* create temporary block, that will be released at the end, current blocks
    * have been saved in local DEV_BLOCK above and will be restored before to
    * leave the function
    */
   dev->new_dcr_blocks(dcr);

   /* Inform User about end of medium */
   Jmsg(jcr, M_INFO, 0, _("End of medium on Volume \"%s\" Bytes=%s Blocks=%s at %s.\n"),
        PrevVolName, edit_uint64_with_commas(dev->VolCatInfo.VolCatBytes, b1),
        edit_uint64_with_commas(dev->VolCatInfo.VolCatBlocks, b2),
        bstrftime(dt, sizeof(dt), time(NULL)));

   Dmsg1(150, "set_unload dev=%s\n", dev->print_name());
   dev->set_unload();

   /* Clear DCR Start/End Block/File positions */
   dcr->VolFirstIndex = dcr->VolLastIndex = 0;
   dcr->StartAddr = dcr->EndAddr = 0;
   dcr->VolMediaId = 0;
   dcr->WroteVol = false;

   if (!dcr->mount_next_write_volume()) {
      dev->free_dcr_blocks(dcr);
      dcr->block = block;
      dcr->ameta_block = ameta_block;
      dcr->adata_block = adata_block;
      dev->Lock();
      goto bail_out;
   }
   Dmsg2(150, "must_unload=%d dev=%s\n", dev->must_unload(), dev->print_name());

   dev->notify_newvol_in_attached_dcrs(dcr->VolumeName);
   dev->Lock();                    /* lock again */

   dev->VolCatInfo.VolCatJobs++;              /* increment number of jobs on vol */
   if (!dir_update_volume_info(dcr, false, false)) { /* send Volume info to Director */
      goto bail_out;
   }

   Jmsg(jcr, M_INFO, 0, _("New volume \"%s\" mounted on device %s at %s.\n"),
      dcr->VolumeName, dev->print_name(), bstrftime(dt, sizeof(dt), time(NULL)));

   /*
    * If this is a new tape, the label_blk will contain the
    *  label, so write it now. If this is a previously
    *  used tape, mount_next_write_volume() will return an
    *  empty label_blk, and nothing will be written.
    */
   Dmsg0(190, "write label block to dev\n");
   if (!dcr->write_block_to_dev()) {
      berrno be;
      Pmsg1(0, _("write_block_to_device Volume label failed. ERR=%s"),
        be.bstrerror(dev->dev_errno));
      dev->free_dcr_blocks(dcr);
      dcr->block = block;
      dcr->ameta_block = ameta_block;
      dcr->adata_block = adata_block;
      goto bail_out;
   }
   dev->free_dcr_blocks(dcr);
   dcr->block = block;
   dcr->ameta_block = ameta_block;
   dcr->adata_block = adata_block;

   /* Clear NewVol now because dir_get_volume_info() already done */
   jcr->dcr->NewVol = false;
   set_new_volume_parameters(dcr);

   jcr->run_time += time(NULL) - wait_time; /* correct run time for mount wait */

   /* Write overflow block to device */
   Dmsg0(190, "Write overflow block to dev\n");
   if (save_adata) {
      dcr->set_adata();      /* try to write block we entered with */
   }
   if (!dcr->write_block_to_dev()) {
      berrno be;
      Dmsg1(0, _("write_block_to_device overflow block failed. ERR=%s"),
        be.bstrerror(dev->dev_errno));
      /* Note: recursive call */
      if (retries-- <= 0 || !fixup_device_block_write_error(dcr, retries)) {
         Jmsg2(jcr, M_FATAL, 0,
              _("Catastrophic error. Cannot write overflow block to device %s. ERR=%s"),
              dev->print_name(), be.bstrerror(dev->dev_errno));
         goto bail_out;
      }
   }
   ok = true;

bail_out:
   if (save_adata) {
      dcr->set_ameta();   /* Do unblock ... on ameta */
   }
   /*
    * At this point, the device is locked and blocked.
    * Unblock the device, restore any entry blocked condition, then
    *   return leaving the device locked (as it was on entry).
    */
   unblock_device(dev);
   if (blocked != BST_NOT_BLOCKED) {
      block_device(dev, blocked);
   }
   if (save_adata) {
      dcr->set_adata();      /* switch back to what we entered with */
   }
   return ok;                               /* device locked */
}

void set_start_vol_position(DCR *dcr)
{
   DEVICE *dev = dcr->dev;
   /* Set new start position */
   if (dev->is_tape()) {
      dcr->StartAddr = dcr->EndAddr = dev->get_full_addr();
   } else {
      if (dev->adata) {
         dev = dcr->ameta_dev;
      }
      /*
       * Note: we only update the DCR values for ameta blocks
       *  because all the indexing (JobMedia) is done with
       *  ameta blocks/records, which may point to adata.
       */
      dcr->StartAddr = dcr->EndAddr = dev->get_full_addr();
   }
}

/*
 * We have a new Volume mounted, so reset the Volume parameters
 *  concerning this job.  The global changes were made earlier
 *  in the dev structure.
 */
void set_new_volume_parameters(DCR *dcr)
{
   JCR *jcr = dcr->jcr;
   Dmsg1(40, "set_new_volume_parameters dev=%s\n", dcr->dev->print_name());
   if (dcr->NewVol) {
      while (dcr->VolumeName[0] == 0) {
         int retries = 5;
         wait_for_device(dcr, retries);
      }
      if (dir_get_volume_info(dcr, dcr->VolumeName, GET_VOL_INFO_FOR_WRITE)) {
         dcr->dev->clear_wait();
      } else {
         Dmsg1(40, "getvolinfo failed. No new Vol: %s", jcr->errmsg);
      }
   }
   set_new_file_parameters(dcr);
   jcr->NumWriteVolumes++;
   dcr->NewVol = false;
}

/*
 * We are now in a new Volume file, so reset the Volume parameters
 *  concerning this job.  The global changes were made earlier
 *  in the dev structure.
 */
void set_new_file_parameters(DCR *dcr)
{
   set_start_vol_position(dcr);

   /* Reset indicies */
   Dmsg3(1000, "Reset indices Vol=%s were: FI=%d LI=%d\n", dcr->VolumeName,
      dcr->VolFirstIndex, dcr->VolLastIndex);
   dcr->VolFirstIndex = 0;
   dcr->VolLastIndex = 0;
   dcr->NewFile = false;
   dcr->WroteVol = false;
}



/*
 *   First Open of the device. Expect dev to already be initialized.
 *
 *   This routine is used only when the Storage daemon starts
 *   and always_open is set, and in the stand-alone utility
 *   routines such as bextract.
 *
 *   Note, opening of a normal file is deferred to later so
 *    that we can get the filename; the device_name for
 *    a file is the directory only.
 *
 *   Returns: false on failure
 *            true  on success
 */
bool first_open_device(DCR *dcr)
{
   DEVICE *dev = dcr->dev;
   bool ok = true;

   Dmsg0(120, "start open_output_device()\n");
   if (!dev) {
      return false;
   }

   dev->rLock(false);

   /* Defer opening files */
   if (!dev->is_tape()) {
      Dmsg0(129, "Device is file, deferring open.\n");
      goto bail_out;
   }

   Dmsg0(129, "Opening device.\n");
   if (!dev->open_device(dcr, OPEN_READ_ONLY)) {
      Jmsg1(NULL, M_FATAL, 0, _("dev open failed: %s\n"), dev->errmsg);
      ok = false;
      goto bail_out;
   }
   Dmsg1(129, "open dev %s OK\n", dev->print_name());

bail_out:
   dev->rUnlock();
   return ok;
}
