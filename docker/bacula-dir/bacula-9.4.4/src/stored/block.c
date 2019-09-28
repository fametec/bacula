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
 *   block.c -- tape block handling functions
 *
 *              Kern Sibbald, March MMI
 *                 added BB02 format October MMII
 */


#include "bacula.h"
#include "stored.h"

#ifdef DEBUG_BLOCK_CHECKSUM
static const bool debug_block_checksum = true;
#else
static const bool debug_block_checksum = false;
#endif

static int debug_io_error = 0;    /* # blocks to write before creating I/O error */

#ifdef NO_TAPE_WRITE_TEST
static const bool no_tape_write_test = true;
#else
static const bool no_tape_write_test = false;
#endif


uint32_t get_len_and_clear_block(DEV_BLOCK *block, DEVICE *dev, uint32_t &pad);
uint32_t ser_block_header(DEV_BLOCK *block, bool do_checksum);
bool unser_block_header(DCR *dcr, DEVICE *dev, DEV_BLOCK *block);

/*
 * Write a block to the device, with locking and unlocking
 *
 * Returns: true  on success
 *        : false on failure
 *
 */
bool DCR::write_block_to_device(bool final)
{
   bool ok = true;
   DCR *dcr = this;

   if (dcr->spooling) {
      Dmsg0(250, "Write to spool\n");
      ok = write_block_to_spool_file(dcr);
      return ok;
   }

   if (!is_dev_locked()) {        /* device already locked? */
      /* note, do not change this to dcr->rLock */
      dev->rLock(false);          /* no, lock it */
   }

   if (!check_for_newvol_or_newfile(dcr)) {
      ok = false;
      goto bail_out;   /* fatal error */
   }

   Dmsg1(500, "Write block to dev=%p\n", dcr->dev);
   if (!write_block_to_dev()) {
      Dmsg2(40, "*** Failed write_block_to_dev adata=%d block=%p\n",
         block->adata, block);
      if (job_canceled(jcr) || jcr->getJobType() == JT_SYSTEM) {
         ok = false;
         Dmsg2(40, "cancel=%d or SYSTEM=%d\n", job_canceled(jcr),
            jcr->getJobType() == JT_SYSTEM);
      } else {
         bool was_adata = false;
         if (was_adata) {
            dcr->set_ameta();
            was_adata = true;
         }
         /* Flush any existing JobMedia info */
         if (!(ok = dir_create_jobmedia_record(dcr))) {
            Jmsg(jcr, M_FATAL, 0, _("[SF0201] Error writing JobMedia record to catalog.\n"));
         } else {
            Dmsg1(40, "Calling fixup_device was_adata=%d...\n", was_adata);
            ok = fixup_device_block_write_error(dcr);
         }
         if (was_adata) {
            dcr->set_adata();
         }
      }
   }
   if (ok && final && !dir_create_jobmedia_record(dcr)) {
      Jmsg(jcr, M_FATAL, 0, _("[SF0202] Error writing final JobMedia record to catalog.\n"));
   }

bail_out:
   if (!dcr->is_dev_locked()) {        /* did we lock dev above? */
      /* note, do not change this to dcr->dunlock */
      dev->Unlock();                  /* unlock it now */
   }
   return ok;
}

/*
 * Write a block to the device
 *
 *  Returns: true  on success or EOT
 *           false on hard error
 */
bool DCR::write_block_to_dev()
{
   ssize_t stat = 0;
   uint32_t wlen;                     /* length to write */
   bool ok = true;
   DCR *dcr = this;
   uint32_t checksum;
   uint32_t pad;                      /* padding or zeros written */
   boffset_t pos;
   char ed1[50];

   if (no_tape_write_test) {
      empty_block(block);
      return true;
   }
   if (job_canceled(jcr)) {
      return false;
   }
   if (!dev->enabled) {
      Jmsg1(jcr, M_FATAL, 0,  _("[SF0203] Cannot write block. Device is disabled. dev=%s\n"), dev->print_name());
      return false;
   }

   ASSERT2(block->adata == dev->adata, "Block and dev adata not same");
   Dmsg4(200, "fd=%d adata=%d bufp-buf=%d binbuf=%d\n", dev->fd(), block->adata,
      block->bufp-block->buf, block->binbuf);
   ASSERT2(block->binbuf == ((uint32_t)(block->bufp - block->buf)), "binbuf badly set");

   if (is_block_empty(block)) {  /* Does block have data in it? */
      Dmsg1(50, "return write_block_to_dev no adata=%d data to write\n", block->adata);
      return true;
   }

   if (dev->at_weot()) {
      Dmsg1(50, "==== FATAL: At EOM with ST_WEOT. adata=%d.\n", dev->adata);
      dev->dev_errno = ENOSPC;
      Jmsg1(jcr, M_FATAL, 0,  _("[SF0204] Cannot write block. Device at EOM. dev=%s\n"), dev->print_name());
      return false;
   }
   if (!dev->can_append()) {
      dev->dev_errno = EIO;
      Jmsg1(jcr, M_FATAL, 0, _("[SF0205] Attempt to write on read-only Volume. dev=%s\n"), dev->print_name());
      Dmsg1(50, "Attempt to write on read-only Volume. dev=%s\n", dev->print_name());
      return false;
   }

   if (!dev->is_open()) {
      Jmsg1(jcr, M_FATAL, 0, _("[SF0206] Attempt to write on closed device=%s\n"), dev->print_name());
      Dmsg1(50, "Attempt to write on closed device=%s\n", dev->print_name());
      return false;
   }

   wlen = get_len_and_clear_block(block, dev, pad);
   block->block_len = wlen;
   dev->updateVolCatPadding(pad);

   checksum = ser_block_header(block, dev->do_checksum());

   if (!dev->do_size_checks(dcr, block)) {
      Dmsg0(50, "Size check triggered.  Cannot write block.\n");
      return false;
   }

   dev->updateVolCatWrites(1);

   dump_block(dev, block, "before write");

#ifdef DEBUG_BLOCK_ZEROING
   uint32_t *bp = (uint32_t *)block->buf;
   if (bp[0] == 0 && bp[1] == 0 && bp[2] == 0 && block->buf[12] == 0) {
      Jmsg0(jcr, M_ABORT, 0, _("[SA0201] Write block header zeroed.\n"));
   }
#endif

   /*
    * If Adata block, we must seek to the correct address
    */
   if (block->adata) {
      ASSERT(dcr->dev->adata);
      uint64_t cur = dev->lseek(dcr, 0, SEEK_CUR);
      /* If we are going to create a hole, record it */
      if (block->BlockAddr != cur) {
         dev->lseek(dcr, block->BlockAddr, SEEK_SET);
         Dmsg4(100, "Adata seek BlockAddr from %lld to %lld = %lld bytes adata_addr=%lld\n",
            cur, block->BlockAddr, block->BlockAddr - cur, dev->adata_addr);
         /* Insanity check */
         if (block->BlockAddr > cur) {
            dev->updateVolCatHoleBytes(block->BlockAddr - cur);
         } else if (block->BlockAddr < cur) {
           Pmsg5(000, "Vol=%s cur=%lld BlockAddr=%lld adata=%d block=%p\n",
              dev->getVolCatName(), cur, block->BlockAddr, block->adata, block);
           Jmsg3(jcr, M_FATAL, 0, "[SF0207] Bad seek on adata Vol=%s BlockAddr=%lld DiskAddr=%lld. Multiple simultaneous Jobs?\n",
              dev->getVolCatName(), block->BlockAddr, cur);
           //Pmsg2(000, "HoleBytes would go negative cur=%lld blkaddr=%lld\n", cur, block->BlockAddr);
         }
      }
   }

   /*
    * Do write here, make a somewhat feeble attempt to recover from
    *  I/O errors, or from the OS telling us it is busy.
    */
   int retry = 0;
   errno = 0;
   stat = 0;
   /* ***FIXME**** remove next line debug */
   pos =  dev->lseek(dcr, 0, SEEK_CUR);
   do {
      if (retry > 0 && stat == -1 && errno == EBUSY) {
         berrno be;
         Dmsg4(100, "===== write retry=%d stat=%d errno=%d: ERR=%s\n",
               retry, stat, errno, be.bstrerror());
         bmicrosleep(5, 0);    /* pause a bit if busy or lots of errors */
         dev->clrerror(-1);
      }
      stat = dev->write(block->buf, (size_t)wlen);
      Dmsg4(100, "%s write() BlockAddr=%lld wlen=%d Vol=%s wlen=%d\n",
         block->adata?"Adata":"Ameta", block->BlockAddr, wlen,
         dev->VolHdr.VolumeName);
   } while (stat == -1 && (errno == EBUSY || errno == EIO) && retry++ < 3);

   /* ***FIXME*** remove 2 lines debug */
   Dmsg2(100, "Wrote %d bytes at %s\n", wlen, dev->print_addr(ed1, sizeof(ed1), pos));
   dump_block(dev, block, "After write");

   if (debug_block_checksum) {
      uint32_t achecksum = ser_block_header(block, dev->do_checksum());
      if (checksum != achecksum) {
         Jmsg2(jcr, M_ERROR, 0, _("[SA0201] Block checksum changed during write: before=%u after=%u\n"),
            checksum, achecksum);
         dump_block(dev, block, "with checksum error");
      }
   }

#ifdef DEBUG_BLOCK_ZEROING
   if (bp[0] == 0 && bp[1] == 0 && bp[2] == 0 && block->buf[12] == 0) {
      Jmsg0(jcr, M_ABORT, 0, _("[SA0202] Write block header zeroed.\n"));
   }
#endif

   if (debug_io_error) {
      debug_io_error--;
      if (debug_io_error == 1) {  /* trigger error */
         stat = -1;
         dev->dev_errno = EIO;
         errno = EIO;
         debug_io_error = 0;      /* turn off trigger */
      }
   }

   if (stat != (ssize_t)wlen) {
      /* Some devices simply report EIO when the volume is full.
       * With a little more thought we may be able to check
       * capacity and distinguish real errors and EOT
       * conditions.  In any case, we probably want to
       * simulate an End of Medium.
       */
      if (stat == -1) {
         berrno be;
         dev->clrerror(-1);                 /* saves errno in dev->dev_errno */
         if (dev->dev_errno == 0) {
            dev->dev_errno = ENOSPC;        /* out of space */
         }
         if (dev->dev_errno != ENOSPC) {
            int etype = M_ERROR;
            if (block->adata) {
               etype = M_FATAL;
            }
            dev->VolCatInfo.VolCatErrors++;
            Jmsg5(jcr, etype, 0, _("%s Write error at %s on device %s Vol=%s. ERR=%s.\n"),
               etype==M_FATAL?"[SF0208]":"[SE0201]",
               dev->print_addr(ed1, sizeof(ed1)), dev->print_name(),
               dev->getVolCatName(), be.bstrerror());
            if (dev->get_tape_alerts(this)) {
               dev->show_tape_alerts(this, list_long, list_last, alert_callback);
            }
         }
      } else {
        dev->dev_errno = ENOSPC;            /* out of space */
      }
      if (dev->dev_errno == ENOSPC) {
         dev->update_freespace();
         if (dev->is_freespace_ok() && dev->free_space < dev->min_free_space) {
            int mtype = M_FATAL;
            dev->set_nospace();
            if (dev->is_removable()) {
               mtype = M_INFO;
            }
            Jmsg(jcr, mtype, 0, _("%s Out of freespace caused End of Volume \"%s\" at %s on device %s. Write of %u bytes got %d.\n"),
               mtype==M_FATAL?"[SF0209]":"[SI0201]",
               dev->getVolCatName(),
               dev->print_addr(ed1, sizeof(ed1)), dev->print_name(), wlen, stat);
         } else {
            dev->clear_nospace();
            Jmsg(jcr, M_INFO, 0, _("[SI0202] End of Volume \"%s\" at %s on device %s. Write of %u bytes got %d.\n"),
               dev->getVolCatName(),
               dev->print_addr(ed1, sizeof(ed1)), dev->print_name(), wlen, stat);
         }
      }
      if (chk_dbglvl(100)) {
         berrno be;
         Dmsg7(90, "==== Write error. fd=%d size=%u rtn=%d dev_blk=%d blk_blk=%d errno=%d: ERR=%s\n",
            dev->fd(), wlen, stat, dev->block_num, block->BlockNumber,
            dev->dev_errno, be.bstrerror(dev->dev_errno));
      }

      Dmsg0(40, "Calling terminate_writing_volume\n");
      ok = terminate_writing_volume(dcr);
      if (ok) {
         reread_last_block(dcr);
      }
      return false;
   }

   /* We successfully wrote the block, now do housekeeping */
   Dmsg2(1300, "VolCatBytes=%lld newVolCatBytes=%lld\n", dev->VolCatInfo.VolCatBytes,
      (dev->VolCatInfo.VolCatBytes+wlen));
   if (!dev->setVolCatAdataBytes(block->BlockAddr + wlen)) {
      dev->updateVolCatBytes(wlen);
      Dmsg3(200, "AmetaBytes=%lld AdataBytes=%lld Bytes=%lld\n",
         dev->VolCatInfo.VolCatAmetaBytes, dev->VolCatInfo.VolCatAdataBytes, dev->VolCatInfo.VolCatBytes);
   }
   dev->updateVolCatBlocks(1);
   dev->LastBlock = block->BlockNumber;
   block->BlockNumber++;

   /* Update dcr values */
   if (dev->is_tape()) {
      dev->EndAddr = dev->get_full_addr();
      if (dcr->EndAddr < dev->EndAddr) {
         dcr->EndAddr = dev->EndAddr;
      }
      dev->block_num++;
   } else {
      /* Save address of byte just written */
      uint64_t addr = dev->file_addr + wlen - 1;
      if (dev->is_indexed()) {
         uint64_t full_addr = dev->get_full_addr(addr);
         if (full_addr < dcr->EndAddr) {
            Pmsg2(000, "Possible incorrect EndAddr oldEndAddr=%llu newEndAddr=%llu\n",
               dcr->EndAddr, full_addr);
         }
         dcr->EndAddr = full_addr;
      }

      if (dev->adata) {
         /* We really should use file_addr, but I am not sure it is correctly set */
         Dmsg3(100, "Set BlockAddr from %lld to %lld adata_addr=%lld\n",
            block->BlockAddr, block->BlockAddr + wlen, dev->adata_addr);
         block->BlockAddr += wlen;
         dev->adata_addr = block->BlockAddr;
      } else {
         block->BlockAddr = dev->get_full_addr() + wlen;
      }
   }
   if (dev->is_indexed()) {
      if (dcr->VolMediaId != dev->VolCatInfo.VolMediaId) {
         Dmsg7(100, "JobMedia Vol=%s wrote=%d MediaId=%lld FI=%lu LI=%lu StartAddr=%lld EndAddr=%lld Wrote\n",
            dcr->VolumeName, dcr->WroteVol, dcr->VolMediaId,
            dcr->VolFirstIndex, dcr->VolLastIndex, dcr->StartAddr, dcr->EndAddr);
      }
      dcr->VolMediaId = dev->VolCatInfo.VolMediaId;
      Dmsg3(150, "VolFirstIndex=%d blockFirstIndex=%d Vol=%s\n",
         dcr->VolFirstIndex, block->FirstIndex, dcr->VolumeName);
      if (dcr->VolFirstIndex == 0 && block->FirstIndex > 0) {
         dcr->VolFirstIndex = block->FirstIndex;
      }
      if (block->LastIndex > (int32_t)dcr->VolLastIndex) {
         dcr->VolLastIndex = block->LastIndex;
      }
      dcr->WroteVol = true;
   }

   dev->file_addr += wlen;            /* update file address */
   dev->file_size += wlen;
   dev->usage += wlen;                /* update usage counter */
   if (dev->part > 0) {
      dev->part_size += wlen;
   }
   dev->setVolCatInfo(false);         /* Needs update */

   Dmsg2(1300, "write_block: wrote block %d bytes=%d\n", dev->block_num, wlen);
   empty_block(block);
   return true;
}


/*
 * Read block with locking
 *
 */
bool DCR::read_block_from_device(bool check_block_numbers)
{
   bool ok;

   Dmsg0(250, "Enter read_block_from_device\n");
   dev->rLock(false);
   ok = read_block_from_dev(check_block_numbers);
   dev->rUnlock();
   Dmsg1(250, "Leave read_block_from_device. ok=%d\n", ok);
   return ok;
}

static void set_block_position(DCR *dcr, DEVICE *dev, DEV_BLOCK *block)
{
   /* Used also by the Single Item Restore code to locate a particular block */
   if (dev->is_tape()) {
      block->BlockAddr = dev->get_full_addr();

   } else if (!dev->adata) {    /* TODO: See if we just use !dev->adata for tapes */
      /* Note: we only update the DCR values for ameta blocks
       * because all the indexing (JobMedia) is done with
       * ameta blocks/records, which may point to adata.
       */
      block->BlockAddr = dev->get_full_addr();
   }
   block->RecNum = 0;
}

/*
 * Read the next block into the block structure and unserialize
 *  the block header.  For a file, the block may be partially
 *  or completely in the current buffer.
 * Note: in order for bscan to generate correct JobMedia records
 *  we must be careful to update the EndAddr of the last byte read.
 */
bool DCR::read_block_from_dev(bool check_block_numbers)
{
   ssize_t stat;
   int looping;
   int retry;
   DCR *dcr = this;
   boffset_t pos;
   char ed1[50];
   uint32_t data_len;

   if (job_canceled(jcr)) {
      Mmsg(dev->errmsg, _("Job failed or canceled.\n"));
      Dmsg1(000, "%s", dev->errmsg);
      block->read_len = 0;
      return false;
   }
   if (!dev->enabled) {
      Mmsg(dev->errmsg, _("[SF0210] Cannot write block. Device is disabled. dev=%s\n"), dev->print_name());
      Jmsg1(jcr, M_FATAL, 0, "%s", dev->errmsg);
      return false;
   }

   if (dev->at_eot()) {
      Mmsg(dev->errmsg, _("[SX0201] At EOT: attempt to read past end of Volume.\n"));
      Dmsg1(000, "%s", dev->errmsg);
      block->read_len = 0;
      return false;
   }
   looping = 0;

   if (!dev->is_open()) {
      Mmsg4(dev->errmsg, _("[SF0211] Attempt to read closed device: fd=%d at file:blk %u:%u on device %s\n"),
         dev->fd(), dev->file, dev->block_num, dev->print_name());
      Jmsg(dcr->jcr, M_FATAL, 0, "%s", dev->errmsg);
      Pmsg4(000, "Fatal: dev=%p dcr=%p adata=%d bytes=%lld\n", dev, dcr, dev->adata,
         VolCatInfo.VolCatAdataRBytes);
      Pmsg1(000, "%s", dev->errmsg);
      block->read_len = 0;
      return false;
   }

   set_block_position(dcr, dev, block);

reread:
   if (looping > 1) {
      dev->dev_errno = EIO;
      Mmsg1(dev->errmsg, _("[SE0202] Block buffer size looping problem on device %s\n"),
         dev->print_name());
      Dmsg1(000, "%s", dev->errmsg);
      Jmsg(jcr, M_ERROR, 0, "%s", dev->errmsg);
      block->read_len = 0;
      return false;
   }

   /* See if we must open another part */
   if (dev->at_eof() && !dev->open_next_part(dcr)) {
      if (dev->at_eof()) {        /* EOF just seen? */
         dev->set_eot();          /* yes, error => EOT */
      }
      return false;
   }

   retry = 0;
   errno = 0;
   stat = 0;

   if (dev->adata) {
      dev->lseek(dcr, dcr->block->BlockAddr, SEEK_SET);
   }
   {  /* debug block */
      pos = dev->lseek(dcr, (boffset_t)0, SEEK_CUR); /* get curr pos */
      Dmsg2(200, "Pos for read=%s %lld\n",
         dev->print_addr(ed1, sizeof(ed1), pos), pos);
   }

   data_len = 0;

   do {
      retry = 0;

      do {
         if ((retry > 0 && stat == -1 && errno == EBUSY)) {
            berrno be;
            Dmsg4(100, "===== read retry=%d stat=%d errno=%d: ERR=%s\n",
                  retry, stat, errno, be.bstrerror());
            bmicrosleep(10, 0);    /* pause a bit if busy or lots of errors */
            dev->clrerror(-1);
         }
         stat = dev->read(block->buf + data_len, (size_t)(block->buf_len - data_len));
         if (stat > 0)
            data_len += stat;

      } while (stat == -1 && (errno == EBUSY || errno == EINTR || errno == EIO) && retry++ < 3);

   } while (data_len < block->buf_len && stat > 0 && dev->dev_type == B_FIFO_DEV);

   Dmsg4(110, "Read() adata=%d vol=%s nbytes=%d pos=%lld\n",
      block->adata, dev->VolHdr.VolumeName, stat < 0 ? stat : data_len, pos);
   if (stat < 0) {
      berrno be;
      dev->clrerror(-1);
      Dmsg2(90, "Read device fd=%d got: ERR=%s\n", dev->fd(), be.bstrerror());
      block->read_len = 0;
      if (reading_label) {      /* Trying to read Volume label */
         Mmsg(dev->errmsg, _("[SE0203] The %sVolume=%s on device=%s appears to be unlabeled.%s\n"),
            dev->adata?"adata ":"", VolumeName, dev->print_name(),
            dev->is_fs_nearly_full(1048576)?" Warning: The filesystem is nearly full.":"");
      } else {
         Mmsg4(dev->errmsg, _("[SE0204] Read error on fd=%d at addr=%s on device %s. ERR=%s.\n"),
            dev->fd(), dev->print_addr(ed1, sizeof(ed1)), dev->print_name(), be.bstrerror());
      }
      Jmsg(jcr, M_ERROR, 0, "%s", dev->errmsg);
      if (dev->get_tape_alerts(this)) {
         dev->show_tape_alerts(this, list_long, list_last, alert_callback);
      }
      if (dev->at_eof()) {        /* EOF just seen? */
         dev->set_eot();          /* yes, error => EOT */
      }
      return false;
   }

   stat = data_len;

   if (stat == 0) {             /* Got EOF ! */
      pos = dev->lseek(dcr, (boffset_t)0, SEEK_CUR); /* get curr pos */
      pos = dev->get_full_addr(pos);
      if (reading_label) {      /* Trying to read Volume label */
         Mmsg4(dev->errmsg, _("The %sVolume=%s on device=%s appears to be unlabeled.%s\n"),
            dev->adata?"adata ":"", VolumeName, dev->print_name(),
            dev->is_fs_nearly_full(1048576)?" Warning: The filesystem is nearly full.":"");
      } else {
         Mmsg4(dev->errmsg, _("Read zero %sbytes Vol=%s at %s on device %s.\n"),
               dev->adata?"adata ":"", dev->VolCatInfo.VolCatName,
               dev->print_addr(ed1, sizeof(ed1), pos), dev->print_name());
      }
      block->read_len = 0;
      Dmsg1(100, "%s", dev->errmsg);
      if (dev->at_eof()) {        /* EOF just seen? */
         dev->set_eot();          /* yes, error => EOT */
      }
      dev->set_ateof();
      dev->file_addr = 0;
      dev->EndAddr = pos;
      if (dcr->EndAddr < dev->EndAddr) {
         dcr->EndAddr = dev->EndAddr;
      }
      Dmsg3(150, "==== Read zero bytes. adata=%d vol=%s at %s\n", dev->adata,
         dev->VolCatInfo.VolCatName, dev->print_addr(ed1, sizeof(ed1), pos));
      return false;             /* return eof */
   }

   /* Continue here for successful read */

   block->read_len = stat;      /* save length read */
   if (block->adata) {
      block->binbuf = block->read_len;
      block->block_len = block->read_len;
   } else {
      if (block->read_len == 80 &&
           (dcr->VolCatInfo.LabelType != B_BACULA_LABEL ||
            dcr->device->label_type != B_BACULA_LABEL)) {
         /* ***FIXME*** should check label */
         Dmsg2(100, "Ignore 80 byte ANSI label at %u:%u\n", dev->file, dev->block_num);
         dev->clear_eof();
         goto reread;             /* skip ANSI/IBM label */
      }

      if (block->read_len < BLKHDR2_LENGTH) {
         dev->dev_errno = EIO;
         Mmsg3(dev->errmsg, _("[SE0205] Volume data error at %s! Very short block of %d bytes on device %s discarded.\n"),
            dev->print_addr(ed1, sizeof(ed1)), block->read_len, dev->print_name());
         Jmsg(jcr, M_ERROR, 0, "%s", dev->errmsg);
         dev->set_short_block();
         block->read_len = block->binbuf = 0;
         Dmsg2(50, "set block=%p binbuf=%d\n", block, block->binbuf);
         return false;             /* return error */
      }

      if (!unser_block_header(this, dev, block)) {
         if (forge_on) {
            dev->file_addr += block->read_len;
            dev->file_size += block->read_len;
            goto reread;
         }
         return false;
      }
   }

   /*
    * If the block is bigger than the buffer, we reposition for
    *  re-reading the block, allocate a buffer of the correct size,
    *  and go re-read.
    */
   Dmsg3(150, "adata=%d block_len=%d buf_len=%d\n", block->adata, block->block_len, block->buf_len);
   if (block->block_len > block->buf_len) {
      dev->dev_errno = EIO;
      Mmsg2(dev->errmsg,  _("[SE0206] Block length %u is greater than buffer %u. Attempting recovery.\n"),
         block->block_len, block->buf_len);
      Jmsg(jcr, M_ERROR, 0, "%s", dev->errmsg);
      Pmsg1(000, "%s", dev->errmsg);
      /* Attempt to reposition to re-read the block */
      if (dev->is_tape()) {
         Dmsg0(250, "BSR for reread; block too big for buffer.\n");
         if (!dev->bsr(1)) {
            Mmsg(dev->errmsg, "%s", dev->bstrerror());
            if (dev->errmsg[0]) {
               Jmsg(jcr, M_ERROR, 0, "[SE0207] %s", dev->errmsg);
            }
            block->read_len = 0;
            return false;
         }
      } else {
         Dmsg0(250, "Seek to beginning of block for reread.\n");
         boffset_t pos = dev->lseek(dcr, (boffset_t)0, SEEK_CUR); /* get curr pos */
         pos -= block->read_len;
         dev->lseek(dcr, pos, SEEK_SET);
         dev->file_addr = pos;
      }
      Mmsg1(dev->errmsg, _("[SI0203] Setting block buffer size to %u bytes.\n"), block->block_len);
      Jmsg(jcr, M_INFO, 0, "%s", dev->errmsg);
      Pmsg1(000, "%s", dev->errmsg);
      /* Set new block length */
      dev->max_block_size = block->block_len;
      block->buf_len = block->block_len;
      free_memory(block->buf);
      block->buf = get_memory(block->buf_len);
      empty_block(block);
      looping++;
      goto reread;                    /* re-read block with correct block size */
   }

   if (block->block_len > block->read_len) {
      dev->dev_errno = EIO;
      Mmsg4(dev->errmsg, _("[SE0208] Volume data error at %u:%u! Short block of %d bytes on device %s discarded.\n"),
         dev->file, dev->block_num, block->read_len, dev->print_name());
      Jmsg(jcr, M_ERROR, 0, "%s", dev->errmsg);
      dev->set_short_block();
      block->read_len = block->binbuf = 0;
      return false;             /* return error */
   }

   dev->clear_short_block();
   dev->clear_eof();
   dev->updateVolCatReads(1);
   dev->updateVolCatReadBytes(block->read_len);

   /* Update dcr values */
   if (dev->is_tape()) {
      dev->EndAddr = dev->get_full_addr();
      if (dcr->EndAddr <  dev->EndAddr) {
         dcr->EndAddr = dev->EndAddr;
      }
      dev->block_num++;
   } else {
      /* We need to take care about a short block in EndBlock/File
       * computation
       */
      uint32_t len = MIN(block->read_len, block->block_len);
      uint64_t addr = dev->get_full_addr() + len - 1;
      if (dev->is_indexed()) {
         if (addr > dcr->EndAddr) {
            dcr->EndAddr = addr;
         }
      }
      dev->EndAddr = addr;
   }
   if (dev->is_indexed()) {
      dcr->VolMediaId = dev->VolCatInfo.VolMediaId;
   }
   dev->file_addr += block->read_len;
   dev->file_size += block->read_len;
   dev->usage     += block->read_len;      /* update usage counter */

   /*
    * If we read a short block on disk,
    * seek to beginning of next block. This saves us
    * from shuffling blocks around in the buffer. Take a
    * look at this from an efficiency stand point later, but
    * it should only happen once at the end of each job.
    *
    * I've been lseek()ing negative relative to SEEK_CUR for 30
    *   years now. However, it seems that with the new off_t definition,
    *   it is not possible to seek negative amounts, so we use two
    *   lseek(). One to get the position, then the second to do an
    *   absolute positioning -- so much for efficiency.  KES Sep 02.
    */
   Dmsg0(250, "At end of read block\n");
   if (block->read_len > block->block_len && !dev->is_tape()) {
      char ed1[50];
      boffset_t pos = dev->lseek(dcr, (boffset_t)0, SEEK_CUR); /* get curr pos */
      Dmsg1(250, "Current lseek pos=%s\n", edit_int64(pos, ed1));
      pos -= (block->read_len - block->block_len);
      dev->lseek(dcr, pos, SEEK_SET);
      Dmsg3(250, "Did lseek pos=%s blk_size=%d rdlen=%d\n",
         edit_int64(pos, ed1), block->block_len,
            block->read_len);
      dev->file_addr = pos;
      dev->file_size = pos;
   }
   Dmsg3(150, "Exit read_block read_len=%d block_len=%d binbuf=%d\n",
      block->read_len, block->block_len, block->binbuf);
   block->block_read = true;
   return true;
}
