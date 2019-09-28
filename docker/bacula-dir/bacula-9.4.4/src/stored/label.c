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
 *
 *  label.c  Bacula routines to handle labels
 *
 *   Kern Sibbald, MM
 *
 */

#include "bacula.h"                   /* pull in global headers */
#include "stored.h"                   /* pull in Storage Deamon headers */

static const int dbglvl = 100;

/* Forward referenced functions */
static void create_volume_label_record(DCR *dcr, DEVICE *dev, DEV_RECORD *rec, bool adata);

/*
 * Read the volume label
 *
 *  If dcr->VolumeName == NULL, we accept any Bacula Volume
 *  If dcr->VolumeName[0] == 0, we accept any Bacula Volume
 *  otherwise dcr->VolumeName must match the Volume.
 *
 *  If VolName given, ensure that it matches
 *
 *  Returns VOL_  code as defined in record.h
 *    VOL_NOT_READ
 *    VOL_OK                          good label found
 *    VOL_NO_LABEL                    volume not labeled
 *    VOL_IO_ERROR                    I/O error reading tape
 *    VOL_NAME_ERROR                  label has wrong name
 *    VOL_CREATE_ERROR                Error creating label
 *    VOL_VERSION_ERROR               label has wrong version
 *    VOL_LABEL_ERROR                 bad label type
 *    VOL_NO_MEDIA                    no media in drive
 *    VOL_TYPE_ERROR                  aligned/non-aligned/dedup error
 *
 *  The dcr block is emptied on return, and the Volume is
 *    rewound.
 *
 *  Handle both the ameta and adata volumes.
 */
int DEVICE::read_dev_volume_label(DCR *dcr)
{
   JCR *jcr = dcr->jcr;
   char *VolName = dcr->VolumeName;
   DEV_RECORD *record;
   bool ok = false;
   DEV_BLOCK *block = dcr->block;
   int stat;
   bool want_ansi_label;
   bool have_ansi_label = false;

   Enter(dbglvl);
   Dmsg5(dbglvl, "Enter read_volume_label adata=%d res=%d device=%s vol=%s dev_Vol=%s\n",
      block->adata, num_reserved(), print_name(), VolName,
      VolHdr.VolumeName[0]?VolHdr.VolumeName:"*NULL*");


   if (!is_open()) {
      if (!open_device(dcr, OPEN_READ_ONLY)) {
         Leave(dbglvl);
         return VOL_IO_ERROR;
      }
   }

   clear_labeled();
   clear_append();
   clear_read();
   label_type = B_BACULA_LABEL;
   set_worm(get_tape_worm(dcr));
   Dmsg1(dbglvl, "==== worm=%d ====\n", is_worm());

   if (!rewind(dcr)) {
      Mmsg(jcr->errmsg, _("Couldn't rewind %s device %s: ERR=%s\n"),
         print_type(), print_name(), print_errmsg());
      Dmsg1(dbglvl, "return VOL_NO_MEDIA: %s", jcr->errmsg);
      Leave(dbglvl);
      return VOL_NO_MEDIA;
   }
   bstrncpy(VolHdr.Id, "**error**", sizeof(VolHdr.Id));

  /* Read ANSI/IBM label if so requested */
  want_ansi_label = dcr->VolCatInfo.LabelType != B_BACULA_LABEL ||
                    dcr->device->label_type != B_BACULA_LABEL;
  if (want_ansi_label || has_cap(CAP_CHECKLABELS)) {
      stat = read_ansi_ibm_label(dcr);
      /* If we want a label and didn't find it, return error */
      if (want_ansi_label && stat != VOL_OK) {
         goto bail_out;
      }
      if (stat == VOL_NAME_ERROR || stat == VOL_LABEL_ERROR) {
         Mmsg(jcr->errmsg, _("Wrong Volume mounted on %s device %s: Wanted %s have %s\n"),
              print_type(), print_name(), VolName, VolHdr.VolumeName);
         if (!poll && jcr->label_errors++ > 100) {
            Jmsg(jcr, M_FATAL, 0, _("Too many tries: %s"), jcr->errmsg);
         }
         goto bail_out;
      }
      if (stat != VOL_OK) {           /* Not an ANSI/IBM label, so re-read */
         rewind(dcr);
      } else {
         have_ansi_label = true;
      }
   }

   /* Read the Bacula Volume label block */
   record = new_record();
   empty_block(block);

   Dmsg0(130, "Big if statement in read_volume_label\n");
   dcr->reading_label = true;
   if (!dcr->read_block_from_dev(NO_BLOCK_NUMBER_CHECK)) {
      Mmsg(jcr->errmsg, _("Read label block failed: requested Volume \"%s\" on %s device %s is not a Bacula "
           "labeled Volume, because: ERR=%s"), NPRT(VolName),
           print_type(), print_name(), print_errmsg());
      Dmsg1(dbglvl, "%s", jcr->errmsg);
   } else if (!read_record_from_block(dcr, record)) {
      Mmsg(jcr->errmsg, _("Could not read Volume label from block.\n"));
      Dmsg1(dbglvl, "%s", jcr->errmsg);
   } else if (!unser_volume_label(this, record)) {
      Mmsg(jcr->errmsg, _("Could not unserialize Volume label: ERR=%s\n"),
         print_errmsg());
      Dmsg1(dbglvl, "%s", jcr->errmsg);
   } else if (strcmp(VolHdr.Id, BaculaId) != 0 &&
              strcmp(VolHdr.Id, OldBaculaId) != 0 &&
              strcmp(VolHdr.Id, BaculaMetaDataId) != 0 &&
              strcmp(VolHdr.Id, BaculaAlignedDataId) != 0 &&
              strcmp(VolHdr.Id, BaculaS3CloudId) != 0) {
      Mmsg(jcr->errmsg, _("Volume Header Id bad: %s\n"), VolHdr.Id);
      Dmsg1(dbglvl, "%s", jcr->errmsg);
   } else {
      ok = true;
      Dmsg1(dbglvl, "VolHdr.Id OK: %s\n", VolHdr.Id);
   }
   dcr->reading_label = false;
   free_record(record);               /* finished reading Volume record */

   if (!is_volume_to_unload()) {
      clear_unload();
   }

   if (!ok) {
      if (jcr->ignore_label_errors) {
         set_labeled();         /* set has Bacula label */
         if (jcr->errmsg[0]) {
            Jmsg(jcr, M_ERROR, 0, "%s", jcr->errmsg);
         }
         empty_block(block);
         Leave(dbglvl);
         return VOL_OK;
      }
      Dmsg0(dbglvl, "No volume label - bailing out\n");
      stat = VOL_NO_LABEL;
      goto bail_out;
   }

   /* At this point, we have read the first Bacula block, and
    * then read the Bacula Volume label. Now we need to
    * make sure we have the right Volume.
    */
   if (VolHdr.VerNum != BaculaTapeVersion &&
       VolHdr.VerNum != BaculaMetaDataVersion &&
       VolHdr.VerNum != BaculaS3CloudVersion &&
       VolHdr.VerNum != OldCompatibleBaculaTapeVersion1 &&
       VolHdr.VerNum != OldCompatibleBaculaTapeVersion2) {
      Mmsg(jcr->errmsg, _("Volume on %s device %s has wrong Bacula version. Wanted %d got %d\n"),
         print_type(), print_name(), BaculaTapeVersion, VolHdr.VerNum);
      Dmsg1(dbglvl, "VOL_VERSION_ERROR: %s", jcr->errmsg);
      stat = VOL_VERSION_ERROR;
      goto bail_out;
   }
   Dmsg1(dbglvl, "VolHdr.VerNum=%ld OK.\n", VolHdr.VerNum);

   /* We are looking for either an unused Bacula tape (PRE_LABEL) or
    * a Bacula volume label (VOL_LABEL)
    */
   if (VolHdr.LabelType != PRE_LABEL && VolHdr.LabelType != VOL_LABEL) {
      Mmsg(jcr->errmsg, _("Volume on %s device %s has bad Bacula label type: %ld\n"),
          print_type(), print_name(), VolHdr.LabelType);
      Dmsg1(dbglvl, "%s", jcr->errmsg);
      if (!poll && jcr->label_errors++ > 100) {
         Jmsg(jcr, M_FATAL, 0, _("Too many tries: %s"), jcr->errmsg);
      }
      Dmsg0(dbglvl, "return VOL_LABEL_ERROR\n");
      stat = VOL_LABEL_ERROR;
      goto bail_out;
   }

   set_labeled();               /* set has Bacula label */

   /* Compare Volume Names */
   Dmsg2(130, "Compare Vol names: VolName=%s hdr=%s\n", VolName?VolName:"*", VolHdr.VolumeName);
   if (VolName && *VolName && *VolName != '*' && strcmp(VolHdr.VolumeName, VolName) != 0) {
      Mmsg(jcr->errmsg, _("Wrong Volume mounted on %s device %s: Wanted %s have %s\n"),
           print_type(), print_name(), VolName, VolHdr.VolumeName);
      Dmsg1(dbglvl, "%s", jcr->errmsg);
      /*
       * Cancel Job if too many label errors
       *  => we are in a loop
       */
      if (!poll && jcr->label_errors++ > 100) {
         Jmsg(jcr, M_FATAL, 0, "Too many tries: %s", jcr->errmsg);
      }
      Dmsg0(dbglvl, "return VOL_NAME_ERROR\n");
      stat = VOL_NAME_ERROR;
      goto bail_out;
   }

   /* Compare VolType to Device Type */
   switch (dev_type) {
   case B_FILE_DEV:
      if (strcmp(VolHdr.Id, BaculaId) != 0) {
         Mmsg(jcr->errmsg, _("Wrong Volume Type. Wanted a File or Tape Volume %s on device %s, but got: %s\n"),
            VolHdr.VolumeName, print_name(), VolHdr.Id);
         stat = VOL_TYPE_ERROR;
         goto bail_out;
      }
      break;
   case B_ALIGNED_DEV:
   case B_ADATA_DEV:
      if (strcmp(VolHdr.Id, BaculaMetaDataId) != 0) {
         Mmsg(jcr->errmsg, _("Wrong Volume Type. Wanted an Aligned Volume %s on device %s, but got: %s\n"),
            VolHdr.VolumeName, print_name(), VolHdr.Id);
         stat = VOL_TYPE_ERROR;
         goto bail_out;
      }
      break;
   case B_CLOUD_DEV:
      if (strcmp(VolHdr.Id, BaculaS3CloudId) != 0) {
         Mmsg(jcr->errmsg, _("Wrong Volume Type. Wanted a Cloud Volume %s on device %s, but got: %s\n"),
            VolHdr.VolumeName, print_name(), VolHdr.Id);
         stat = VOL_TYPE_ERROR;
         goto bail_out;
      }
   default:
      break;
   }

   if (chk_dbglvl(100)) {
      dump_volume_label();
   }
   Dmsg0(dbglvl, "Leave read_volume_label() VOL_OK\n");
   /* If we are a streaming device, we only get one chance to read */
   if (!has_cap(CAP_STREAM)) {
      rewind(dcr);
      if (have_ansi_label) {
         stat = read_ansi_ibm_label(dcr);
         /* If we want a label and didn't find it, return error */
         if (stat != VOL_OK) {
            goto bail_out;
         }
      }
   }

   Dmsg1(100, "Call reserve_volume=%s\n", VolHdr.VolumeName);
   if (reserve_volume(dcr, VolHdr.VolumeName) == NULL) {
      if (!jcr->errmsg[0]) {
         Mmsg3(jcr->errmsg, _("Could not reserve volume %s on %s device %s\n"),
              VolHdr.VolumeName, print_type(), print_name());
      }
      Dmsg2(dbglvl, "Could not reserve volume %s on %s\n", VolHdr.VolumeName, print_name());
      stat = VOL_NAME_ERROR;
      goto bail_out;
   }

   if (dcr->is_writing()) {
       empty_block(block);
   }

   Leave(dbglvl);
   return VOL_OK;

bail_out:
   empty_block(block);
   rewind(dcr);
   Dmsg2(dbglvl, "return stat=%d %s", stat, jcr->errmsg);
   Leave(dbglvl);
   return stat;
}


/*
 * Create and put a volume label into the block
 *
 *  Returns: false on failure
 *           true  on success
 *
 *  Handle both the ameta and adata volumes.
 */
bool DEVICE::write_volume_label_to_block(DCR *dcr)
{
   DEVICE *dev;
   DEV_BLOCK *block;
   DEV_RECORD rec;
   JCR *jcr = dcr->jcr;
   bool ok = true;

   Enter(100);
   dev = dcr->dev;
   block = dcr->block;
   memset(&rec, 0, sizeof(rec));
   rec.data = get_memory(SER_LENGTH_Volume_Label);
   memset(rec.data, 0, SER_LENGTH_Volume_Label);
   empty_block(block);                /* Volume label always at beginning */

   create_volume_label_record(dcr, dcr->dev, &rec, dcr->block->adata);

   block->BlockNumber = 0;
   /* Note for adata this also writes to disk */
   Dmsg1(100, "write_record_to_block adata=%d\n", dcr->dev->adata);
   if (!write_record_to_block(dcr, &rec)) {
      free_pool_memory(rec.data);
      Jmsg2(jcr, M_FATAL, 0, _("Cannot write Volume label to block for %s device %s\n"),
         dev->print_type(), dev->print_name());
      ok = false;
      goto get_out;
   } else {
      Dmsg4(100, "Wrote fd=%d adata=%d label of %d bytes to block. Vol=%s\n",
         dev->fd(), block->adata, rec.data_len, dcr->VolumeName);
   }
   free_pool_memory(rec.data);

get_out:
   Leave(100);
   return ok;
}

/*
 * Write a Volume Label
 *  !!! Note, this is ONLY used for writing
 *            a fresh volume label.  Any data
 *            after the label will be destroyed,
 *            in fact, we write the label 5 times !!!!
 *
 *  This routine should be used only when labeling a blank tape or
 *  when recylcing a volume.
 *
 *  Handle both the ameta and adata volumes.
 */
bool DEVICE::write_volume_label(DCR *dcr, const char *VolName,
               const char *PoolName, bool relabel, bool no_prelabel)
{
   DEVICE *dev;

   Enter(100);
   Dmsg4(230, "Write:  block=%p ameta=%p dev=%p ameta_dev=%p\n",
         dcr->block, dcr->ameta_block, dcr->dev, dcr->ameta_dev);
   dcr->set_ameta();
   dev = dcr->dev;

   Dmsg0(150, "write_volume_label()\n");
   if (*VolName == 0) {
      if (dcr->jcr) {
         Mmsg(dcr->jcr->errmsg, "ERROR: new_volume_label_to_dev called with NULL VolName\n");
      }
      Pmsg0(0, "=== ERROR: write_volume_label called with NULL VolName\n");
      goto bail_out;
   }

   if (relabel) {
      volume_unused(dcr);             /* mark current volume unused */
      /* Truncate device */
      if (!dev->truncate(dcr)) {
         goto bail_out;
      }
      dev->close_part(dcr);          /* make sure closed for rename */
   }

   /* Set the new filename for open, ... */
   dev->setVolCatName(VolName);
   dcr->setVolCatName(VolName);
   dev->clearVolCatBytes();

   Dmsg1(100, "New VolName=%s\n", VolName);
   if (!dev->open_device(dcr, OPEN_READ_WRITE)) {
      /* If device is not tape, attempt to create it */
      if (dev->is_tape() || !dev->open_device(dcr, CREATE_READ_WRITE)) {
         Jmsg4(dcr->jcr, M_WARNING, 0, _("Open %s device %s Volume \"%s\" failed: ERR=%s"),
               dev->print_type(), dev->print_name(), dcr->VolumeName, dev->bstrerror());
         goto bail_out;
      }
   }
   Dmsg1(150, "Label type=%d\n", dev->label_type);

   if (!write_volume_label_to_dev(dcr, VolName, PoolName, relabel, no_prelabel)) {
      goto bail_out;
   }

   if (!dev->is_aligned()) {
      /* Not aligned data */
      if (dev->weof(dcr, 1)) {
         dev->set_labeled();
      }

      if (chk_dbglvl(100))  {
         dev->dump_volume_label();
      }
      Dmsg0(50, "Call reserve_volume\n");
      /**** ***FIXME*** if dev changes, dcr must be updated */
      if (reserve_volume(dcr, VolName) == NULL) {
         if (!dcr->jcr->errmsg[0]) {
            Mmsg3(dcr->jcr->errmsg, _("Could not reserve volume %s on %s device %s\n"),
                 dev->VolHdr.VolumeName, dev->print_type(), dev->print_name());
         }
         Dmsg1(50, "%s", dcr->jcr->errmsg);
         goto bail_out;
      }
      dev = dcr->dev;                 /* may have changed in reserve_volume */
   }
   dev->clear_append();               /* remove append since this is PRE_LABEL */
   Leave(100);
   return true;

bail_out:
   dcr->adata_label = false;
   dcr->set_ameta();
   volume_unused(dcr);
   dcr->dev->clear_append();            /* remove append since this is PRE_LABEL */
   Leave(100);
   return false;
}

bool DEVICE::write_volume_label_to_dev(DCR *dcr, const char *VolName,
              const char *PoolName, bool relabel, bool no_prelabel)
{
   DEVICE *dev, *ameta_dev;
   DEV_BLOCK *block;
   DEV_RECORD *rec = new_record();
   bool rtn = false;

   Enter(100);
   dev = dcr->dev;
   ameta_dev = dcr->ameta_dev;
   block = dcr->block;

   empty_block(block);
   if (!dev->rewind(dcr)) {
      Dmsg2(130, "Bad status on %s from rewind: ERR=%s\n", dev->print_name(), dev->print_errmsg());
      goto bail_out;
   }

   /* Temporarily mark in append state to enable writing */
   dev->set_append();

   /* Create PRE_LABEL or VOL_LABEL */
   create_volume_header(dev, VolName, PoolName, no_prelabel);

   /*
    * If we have already detected an ANSI label, re-read it
    *   to skip past it. Otherwise, we write a new one if
    *   so requested.
    */
   if (!block->adata) {
      if (dev->label_type != B_BACULA_LABEL) {
         if (read_ansi_ibm_label(dcr) != VOL_OK) {
            dev->rewind(dcr);
            goto bail_out;
         }
      } else if (!write_ansi_ibm_labels(dcr, ANSI_VOL_LABEL, VolName)) {
         goto bail_out;
      }
   }

   create_volume_label_record(dcr, dev, rec, block->adata);
   rec->Stream = 0;
   rec->maskedStream = 0;

   Dmsg2(100, "write_record_to_block adata=%d FI=%d\n", dcr->dev->adata,
      rec->FileIndex);

   /* For adata label this also writes to disk */
   if (!write_record_to_block(dcr, rec)) {
      Dmsg2(40, "Bad Label write on %s: ERR=%s\n", dev->print_name(), dev->print_errmsg());
      goto bail_out;
   } else {
      Dmsg3(100, "Wrote label=%d bytes adata=%d block: %s\n", rec->data_len, block->adata, dev->print_name());
   }
   Dmsg3(100, "New label adata=%d VolCatBytes=%lld VolCatStatus=%s\n",
      dev->adata, ameta_dev->VolCatInfo.VolCatBytes, ameta_dev->VolCatInfo.VolCatStatus);

   if (block->adata) {
      /* Empty block and set data start address */
      empty_block(dcr->adata_block);
   } else {
      Dmsg4(130, "Call write_block_to_dev() fd=%d adata=%d block=%p Addr=%lld\n",
         dcr->dev->fd(), dcr->block->adata, dcr->block, block->dev->lseek(dcr, 0, SEEK_CUR));
      Dmsg1(100, "write_record_to_dev adata=%d\n", dcr->dev->adata);
      /* Write ameta block to device */
      if (!dcr->write_block_to_dev()) {
         Dmsg2(40, "Bad Label write on %s: ERR=%s\n", dev->print_name(), dev->print_errmsg());
         goto bail_out;
      }
   }
   Dmsg3(100, "Wrote new Vol label adata=%d VolCatBytes=%lld VolCatStatus=%s\n",
      dev->adata, ameta_dev->VolCatInfo.VolCatBytes, ameta_dev->VolCatInfo.VolCatStatus);
   rtn = true;

bail_out:
   free_record(rec);
   Leave(100);
   return rtn;
}

/*
 * Write a volume label. This is ONLY called if we have a valid Bacula
 *   label of type PRE_LABEL or we are recyling an existing Volume.
 *
 * By calling write_volume_label_to_block, both ameta and adata
 *   are updated.
 *
 *  Returns: true if OK
 *           false if unable to write it
 */
bool DEVICE::rewrite_volume_label(DCR *dcr, bool recycle)
{
   char ed1[50];
   JCR *jcr = dcr->jcr;

   Enter(100);
   ASSERT2(dcr->VolumeName[0], "Empty Volume name");
   ASSERT(!dcr->block->adata);
   if (is_worm()) {
      Jmsg3(jcr, M_FATAL, 0,  _("Cannot relabel worm %s device %s Volume \"%s\"\n"),
             print_type(), print_name(), dcr->VolumeName);
      Leave(100);
      return false;
   }
   if (!open_device(dcr, OPEN_READ_WRITE)) {
       Jmsg4(jcr, M_WARNING, 0, _("Open %s device %s Volume \"%s\" failed: ERR=%s\n"),
             print_type(), print_name(), dcr->VolumeName, bstrerror());
      Leave(100);
      return false;
   }
   Dmsg2(190, "set append found freshly labeled volume. fd=%d dev=%x\n", fd(), this);
   VolHdr.LabelType = VOL_LABEL; /* set Volume label */
   set_append();
   Dmsg0(100, "Rewrite_volume_label set volcatbytes=0\n");
   clearVolCatBytes();           /* resets both ameta and adata byte counts */
   setVolCatStatus("Append");    /* set append status */

   if (!has_cap(CAP_STREAM)) {
      if (!rewind(dcr)) {
         Jmsg3(jcr, M_FATAL, 0, _("Rewind error on %s device %s: ERR=%s\n"),
               print_type(), print_name(), print_errmsg());
         Leave(100);
         return false;
      }
      if (recycle) {
         Dmsg1(150, "Doing recycle. Vol=%s\n", dcr->VolumeName);
         if (!truncate(dcr)) {
            Jmsg3(jcr, M_FATAL, 0, _("Truncate error on %s device %s: ERR=%s\n"),
                  print_type(), print_name(), print_errmsg());
            Leave(100);
            return false;
         }
         if (!open_device(dcr, OPEN_READ_WRITE)) {
            Jmsg3(jcr, M_FATAL, 0,
               _("Failed to re-open device after truncate on %s device %s: ERR=%s"),
               print_type(), print_name(), print_errmsg());
            Leave(100);
            return false;
         }
      }
   }

   if (!write_volume_label_to_block(dcr)) {
      Dmsg0(150, "Error from write volume label.\n");
      Leave(100);
      return false;
   }
   Dmsg2(100, "wrote vol label to block. adata=%d Vol=%s\n", dcr->block->adata, dcr->VolumeName);

   ASSERT2(dcr->VolumeName[0], "Empty Volume name");
   setVolCatInfo(false);

   /*
    * If we are not dealing with a streaming device,
    *  write the block now to ensure we have write permission.
    *  It is better to find out now rather than later.
    * We do not write the block now if this is an ANSI label. This
    *  avoids re-writing the ANSI label, which we do not want to do.
    */
   if (!has_cap(CAP_STREAM)) {
      /*
       * If we have already detected an ANSI label, re-read it
       *   to skip past it. Otherwise, we write a new one if
       *   so requested.
       */
      if (label_type != B_BACULA_LABEL) {
         if (read_ansi_ibm_label(dcr) != VOL_OK) {
            rewind(dcr);
            Leave(100);
            return false;
         }
      } else if (!write_ansi_ibm_labels(dcr, ANSI_VOL_LABEL, VolHdr.VolumeName)) {
         Leave(100);
         return false;
      }

      /* Attempt write to check write permission */
      Dmsg1(200, "Attempt to write to device fd=%d.\n", fd());
      if (!dcr->write_block_to_dev()) {
         Jmsg3(jcr, M_ERROR, 0, _("Unable to write %s device %s: ERR=%s\n"),
            print_type(), print_name(), print_errmsg());
         Dmsg0(200, "===ERROR write block to dev\n");
         Leave(100);
         return false;
      }
   }
   ASSERT2(dcr->VolumeName[0], "Empty Volume name");
   setVolCatName(dcr->VolumeName);
   if (!dir_get_volume_info(dcr, dcr->VolumeName, GET_VOL_INFO_FOR_WRITE)) {
      Leave(100);
      return false;
   }
   set_labeled();
   /* Set or reset Volume statistics */
   VolCatInfo.VolCatJobs = 0;
   VolCatInfo.VolCatFiles = 0;
   VolCatInfo.VolCatErrors = 0;
   VolCatInfo.VolCatBlocks = 0;
   VolCatInfo.VolCatRBytes = 0;
   VolCatInfo.VolCatCloudParts = 0;
   VolCatInfo.VolLastPartBytes = 0;
   VolCatInfo.VolCatType = 0; /* Will be set by dir_update_volume_info() */
   if (recycle) {
      VolCatInfo.VolCatMounts++;
      VolCatInfo.VolCatRecycles++;
   } else {
      VolCatInfo.VolCatMounts = 1;
      VolCatInfo.VolCatRecycles = 0;
      VolCatInfo.VolCatWrites = 1;
      VolCatInfo.VolCatReads = 1;
   }
   dcr->VolMediaId = dcr->VolCatInfo.VolMediaId;  /* make create_jobmedia work */
   dir_create_jobmedia_record(dcr, true);
   Dmsg1(100, "dir_update_vol_info. Set Append vol=%s\n", dcr->VolumeName);
   VolCatInfo.VolFirstWritten = time(NULL);
   setVolCatStatus("Append");
   if (!dir_update_volume_info(dcr, true, true)) {  /* indicate relabel */
      Leave(100);
      return false;
   }
   if (recycle) {
      Jmsg(jcr, M_INFO, 0, _("Recycled volume \"%s\" on %s device %s, all previous data lost.\n"),
         dcr->VolumeName, print_type(), print_name());
   } else {
      Jmsg(jcr, M_INFO, 0, _("Wrote label to prelabeled Volume \"%s\" on %s device %s\n"),
         dcr->VolumeName, print_type(), print_name());
   }
   /*
    * End writing real Volume label (from pre-labeled tape), or recycling
    *  the volume.
    */
   Dmsg4(100, "OK rewrite vol label. Addr=%s adata=%d slot=%d Vol=%s\n",
      print_addr(ed1, sizeof(ed1)), dcr->block->adata, VolCatInfo.Slot, dcr->VolumeName);
   Leave(100);
   return true;
}


/*
 *  create_volume_label_record
 *   Note: it is assumed that you have created the volume_header
 *     (label) prior to calling this subroutine.
 *   Serialize label (from dev->VolHdr structure) into device record.
 *   Assumes that the dev->VolHdr structure is properly
 *   initialized.
*/
static void create_volume_label_record(DCR *dcr, DEVICE *dev,
     DEV_RECORD *rec, bool adata)
{
   ser_declare;
   struct date_time dt;
   JCR *jcr = dcr->jcr;
   char buf[100];

   /* Serialize the label into the device record. */

   Enter(100);
   rec->data = check_pool_memory_size(rec->data, SER_LENGTH_Volume_Label);
   memset(rec->data, 0, SER_LENGTH_Volume_Label);
   ser_begin(rec->data, SER_LENGTH_Volume_Label);
   ser_string(dev->VolHdr.Id);

   ser_uint32(dev->VolHdr.VerNum);

   if (dev->VolHdr.VerNum >= 11) {
      ser_btime(dev->VolHdr.label_btime);
      dev->VolHdr.write_btime = get_current_btime();
      ser_btime(dev->VolHdr.write_btime);
      dev->VolHdr.write_date = 0;
      dev->VolHdr.write_time = 0;
   } else {
      /* OLD WAY DEPRECATED */
      ser_float64(dev->VolHdr.label_date);
      ser_float64(dev->VolHdr.label_time);
      get_current_time(&dt);
      dev->VolHdr.write_date = dt.julian_day_number;
      dev->VolHdr.write_time = dt.julian_day_fraction;
   }
   ser_float64(dev->VolHdr.write_date);   /* 0 if VerNum >= 11 */
   ser_float64(dev->VolHdr.write_time);   /* 0  if VerNum >= 11 */

   ser_string(dev->VolHdr.VolumeName);
   ser_string(dev->VolHdr.PrevVolumeName);
   ser_string(dev->VolHdr.PoolName);
   ser_string(dev->VolHdr.PoolType);
   ser_string(dev->VolHdr.MediaType);

   ser_string(dev->VolHdr.HostName);
   ser_string(dev->VolHdr.LabelProg);
   ser_string(dev->VolHdr.ProgVersion);
   ser_string(dev->VolHdr.ProgDate);
   /* ***FIXME*** */
   dev->VolHdr.AlignedVolumeName[0] = 0;
   ser_string(dev->VolHdr.AlignedVolumeName);

   /* This is adata Volume information */
   ser_uint64(dev->VolHdr.FirstData);
   ser_uint32(dev->VolHdr.FileAlignment);
   ser_uint32(dev->VolHdr.PaddingSize);
   /* adata and dedup volumes */
   ser_uint32(dev->VolHdr.BlockSize);

   ser_end(rec->data, SER_LENGTH_Volume_Label);
   if (!adata) {
      bstrncpy(dcr->VolumeName, dev->VolHdr.VolumeName, sizeof(dcr->VolumeName));
   }
   ASSERT2(dcr->VolumeName[0], "Empty Volume name");
   rec->data_len = ser_length(rec->data);
   rec->FileIndex = dev->VolHdr.LabelType;
   Dmsg2(100, "LabelType=%d adata=%d\n", dev->VolHdr.LabelType, dev->adata);
   rec->VolSessionId = jcr->VolSessionId;
   rec->VolSessionTime = jcr->VolSessionTime;
   rec->Stream = jcr->NumWriteVolumes;
   rec->maskedStream = jcr->NumWriteVolumes;
   Dmsg3(100, "Created adata=%d Vol label rec: FI=%s len=%d\n", adata, FI_to_ascii(buf, rec->FileIndex),
      rec->data_len);
   Dmsg2(100, "reclen=%d recdata=%s", rec->data_len, rec->data);
   Leave(100);
}


/*
 * Create a volume header (label) in memory
 *   The volume record is created after this header (label)
 *   is created.
 */
void create_volume_header(DEVICE *dev, const char *VolName,
                         const char *PoolName, bool no_prelabel)
{
   DEVRES *device = (DEVRES *)dev->device;

   Enter(130);

   ASSERT2(dev != NULL, "dev ptr is NULL");

   if (dev->is_aligned()) {
      bstrncpy(dev->VolHdr.Id, BaculaMetaDataId, sizeof(dev->VolHdr.Id));
      dev->VolHdr.VerNum = BaculaMetaDataVersion;
      dev->VolHdr.FirstData = dev->file_alignment;
      dev->VolHdr.FileAlignment = dev->file_alignment;
      dev->VolHdr.PaddingSize = dev->padding_size;
      dev->VolHdr.BlockSize = dev->adata_size;
   } else if (dev->is_adata()) {
      bstrncpy(dev->VolHdr.Id, BaculaAlignedDataId, sizeof(dev->VolHdr.Id));
      dev->VolHdr.VerNum = BaculaAlignedDataVersion;
      dev->VolHdr.FirstData = dev->file_alignment;
      dev->VolHdr.FileAlignment = dev->file_alignment;
      dev->VolHdr.PaddingSize = dev->padding_size;
      dev->VolHdr.BlockSize = dev->adata_size;
   } else if (dev->is_cloud()) {
      bstrncpy(dev->VolHdr.Id, BaculaS3CloudId, sizeof(dev->VolHdr.Id));
      dev->VolHdr.VerNum = BaculaS3CloudVersion;
      dev->VolHdr.BlockSize = dev->max_block_size;
      dev->VolHdr.MaxPartSize = dev->max_part_size;
   } else {
      bstrncpy(dev->VolHdr.Id, BaculaId, sizeof(dev->VolHdr.Id));
      dev->VolHdr.VerNum = BaculaTapeVersion;
      dev->VolHdr.BlockSize = dev->max_block_size;
   }

   if ((dev->has_cap(CAP_STREAM) && no_prelabel) || dev->is_worm()) {
      /* We do not want to re-label so write VOL_LABEL now */
      dev->VolHdr.LabelType = VOL_LABEL;
   } else {
      dev->VolHdr.LabelType = PRE_LABEL;  /* Mark Volume as unused */
   }
   bstrncpy(dev->VolHdr.VolumeName, VolName, sizeof(dev->VolHdr.VolumeName));
   bstrncpy(dev->VolHdr.PoolName, PoolName, sizeof(dev->VolHdr.PoolName));
   bstrncpy(dev->VolHdr.MediaType, device->media_type, sizeof(dev->VolHdr.MediaType));

   bstrncpy(dev->VolHdr.PoolType, "Backup", sizeof(dev->VolHdr.PoolType));

   dev->VolHdr.label_btime = get_current_btime();
   dev->VolHdr.label_date = 0;
   dev->VolHdr.label_time = 0;

   if (gethostname(dev->VolHdr.HostName, sizeof(dev->VolHdr.HostName)) != 0) {
      dev->VolHdr.HostName[0] = 0;
   }
   bstrncpy(dev->VolHdr.LabelProg, my_name, sizeof(dev->VolHdr.LabelProg));
   sprintf(dev->VolHdr.ProgVersion, "Ver. %s %s ", VERSION, BDATE);
   sprintf(dev->VolHdr.ProgDate, "Build %s %s ", __DATE__, __TIME__);
   dev->set_labeled();               /* set has Bacula label */
   if (chk_dbglvl(100)) {
      dev->dump_volume_label();
   }
}

/*
 * Create session (Job) label
 *  The pool memory must be released by the calling program
 */
void create_session_label(DCR *dcr, DEV_RECORD *rec, int label)
{
   JCR *jcr = dcr->jcr;
   ser_declare;

   Enter(100);
   rec->VolSessionId   = jcr->VolSessionId;
   rec->VolSessionTime = jcr->VolSessionTime;
   rec->Stream         = jcr->JobId;
   rec->maskedStream   = jcr->JobId;

   rec->data = check_pool_memory_size(rec->data, SER_LENGTH_Session_Label);
   ser_begin(rec->data, SER_LENGTH_Session_Label);
   ser_string(BaculaId);
   ser_uint32(BaculaTapeVersion);

   ser_uint32(jcr->JobId);

   /* Changed in VerNum 11 */
   ser_btime(get_current_btime());
   ser_float64(0);

   ser_string(dcr->pool_name);
   ser_string(dcr->pool_type);
   ser_string(jcr->job_name);         /* base Job name */
   ser_string(jcr->client_name);

   /* Added in VerNum 10 */
   ser_string(jcr->Job);              /* Unique name of this Job */
   ser_string(jcr->fileset_name);
   ser_uint32(jcr->getJobType());
   ser_uint32(jcr->getJobLevel());
   /* Added in VerNum 11 */
   ser_string(jcr->fileset_md5);

   if (label == EOS_LABEL) {
      ser_uint32(jcr->JobFiles);
      ser_uint64(jcr->JobBytes);
      ser_uint32((uint32_t)dcr->StartAddr);       /* Start Block */
      ser_uint32((uint32_t)dcr->EndAddr);         /* End Block */
      ser_uint32((uint32_t)(dcr->StartAddr>>32)); /* Start File */
      ser_uint32((uint32_t)(dcr->EndAddr>>32));   /* End File */
      ser_uint32(jcr->JobErrors);

      /* Added in VerNum 11 */
      ser_uint32(jcr->JobStatus);
   }
   ser_end(rec->data, SER_LENGTH_Session_Label);
   rec->data_len = ser_length(rec->data);
   Leave(100);
}

/* Write session (Job) label
 *  Returns: false on failure
 *           true  on success
 */
bool write_session_label(DCR *dcr, int label)
{
   JCR *jcr = dcr->jcr;
   DEVICE *dev = dcr->dev;
   DEV_RECORD *rec;
   DEV_BLOCK *block = dcr->block;
   char buf1[100], buf2[100];

   Enter(100);
   dev->Lock();
   Dmsg2(140, "=== write_session_label label=%d Vol=%s.\n", label, dev->getVolCatName());
   if (!check_for_newvol_or_newfile(dcr)) {
      Pmsg0(000, "ERR: !check_for_new_vol_or_newfile\n");
      dev->Unlock();
      return false;
   }

   rec = new_record();
   Dmsg1(130, "session_label record=%x\n", rec);
   switch (label) {
   case SOS_LABEL:
      set_start_vol_position(dcr);
      break;
   case EOS_LABEL:
      dcr->EndAddr = dev->get_full_addr();
      break;
   default:
      Jmsg1(jcr, M_ABORT, 0, _("Bad Volume session label request=%d\n"), label);
      break;
   }

   create_session_label(dcr, rec, label);
   rec->FileIndex = label;
   dev->Unlock();

   /*
    * We guarantee that the session record can totally fit
    *  into a block. If not, write the block, and put it in
    *  the next block. Having the sesssion record totally in
    *  one block makes reading them much easier (no need to
    *  read the next block).
    */
   if (!can_write_record_to_block(block, rec)) {
      Dmsg0(150, "Cannot write session label to block.\n");
      if (!dcr->write_block_to_device()) {
         Dmsg0(130, "Got session label write_block_to_dev error.\n");
         free_record(rec);
         Leave(100);
         return false;
      }
   }
   /*
    * We use write_record() because it handles the case that
    *  the maximum user size has been reached.
    */
   if (!dcr->write_record(rec)) {
      Dmsg0(150, "Bad return from write_record\n");
      free_record(rec);
      Leave(100);
      return false;
   }

   Dmsg6(150, "Write sesson_label record JobId=%d FI=%s SessId=%d Strm=%s len=%d "
             "remainder=%d\n", jcr->JobId,
      FI_to_ascii(buf1, rec->FileIndex), rec->VolSessionId,
      stream_to_ascii(buf2, rec->Stream, rec->FileIndex), rec->data_len,
      rec->remainder);

   free_record(rec);
   Dmsg2(150, "Leave write_session_label Block=%u File=%u\n",
      dev->get_block_num(), dev->get_file());
   Leave(100);
   return true;
}

/*  unser_volume_label
 *
 * Unserialize the Bacula Volume label into the device Volume_Label
 * structure.
 *
 * Assumes that the record is already read.
 *
 * Returns: false on error
 *          true  on success
*/

bool unser_volume_label(DEVICE *dev, DEV_RECORD *rec)
{
   ser_declare;
   char buf1[100], buf2[100];

   Enter(100);
   if (rec->FileIndex != VOL_LABEL && rec->FileIndex != PRE_LABEL) {
      Mmsg3(dev->errmsg, _("Expecting Volume Label, got FI=%s Stream=%s len=%d\n"),
              FI_to_ascii(buf1, rec->FileIndex),
              stream_to_ascii(buf2, rec->Stream, rec->FileIndex),
              rec->data_len);
      if (!forge_on) {
         Leave(100);
         return false;
      }
   }

   dev->VolHdr.LabelType = rec->FileIndex;
   dev->VolHdr.LabelSize = rec->data_len;


   /* Unserialize the record into the Volume Header */
   Dmsg2(100, "reclen=%d recdata=%s", rec->data_len, rec->data);
   rec->data = check_pool_memory_size(rec->data, SER_LENGTH_Volume_Label);
   Dmsg2(100, "reclen=%d recdata=%s", rec->data_len, rec->data);
   ser_begin(rec->data, SER_LENGTH_Volume_Label);
   unser_string(dev->VolHdr.Id);
   unser_uint32(dev->VolHdr.VerNum);

   if (dev->VolHdr.VerNum >= 11) {
      unser_btime(dev->VolHdr.label_btime);
      unser_btime(dev->VolHdr.write_btime);
   } else { /* old way */
      unser_float64(dev->VolHdr.label_date);
      unser_float64(dev->VolHdr.label_time);
   }
   unser_float64(dev->VolHdr.write_date);    /* Unused with VerNum >= 11 */
   unser_float64(dev->VolHdr.write_time);    /* Unused with VerNum >= 11 */

   unser_string(dev->VolHdr.VolumeName);
   unser_string(dev->VolHdr.PrevVolumeName);
   unser_string(dev->VolHdr.PoolName);
   unser_string(dev->VolHdr.PoolType);
   unser_string(dev->VolHdr.MediaType);

   unser_string(dev->VolHdr.HostName);
   unser_string(dev->VolHdr.LabelProg);
   unser_string(dev->VolHdr.ProgVersion);
   unser_string(dev->VolHdr.ProgDate);

//   unser_string(dev->VolHdr.AlignedVolumeName);
   dev->VolHdr.AlignedVolumeName[0] = 0;
   unser_uint64(dev->VolHdr.FirstData);
   unser_uint32(dev->VolHdr.FileAlignment);
   unser_uint32(dev->VolHdr.PaddingSize);
   unser_uint32(dev->VolHdr.BlockSize);

   ser_end(rec->data, SER_LENGTH_Volume_Label);
   Dmsg0(190, "unser_vol_label\n");
   if (chk_dbglvl(100)) {
      dev->dump_volume_label();
   }
   Leave(100);
   return true;
}


bool unser_session_label(SESSION_LABEL *label, DEV_RECORD *rec)
{
   ser_declare;

   Enter(100);
   rec->data = check_pool_memory_size(rec->data, SER_LENGTH_Session_Label);
   unser_begin(rec->data, SER_LENGTH_Session_Label);
   unser_string(label->Id);
   unser_uint32(label->VerNum);
   unser_uint32(label->JobId);
   if (label->VerNum >= 11) {
      unser_btime(label->write_btime);
   } else {
      unser_float64(label->write_date);
   }
   unser_float64(label->write_time);
   unser_string(label->PoolName);
   unser_string(label->PoolType);
   unser_string(label->JobName);
   unser_string(label->ClientName);
   if (label->VerNum >= 10) {
      unser_string(label->Job);          /* Unique name of this Job */
      unser_string(label->FileSetName);
      unser_uint32(label->JobType);
      unser_uint32(label->JobLevel);
   }
   if (label->VerNum >= 11) {
      unser_string(label->FileSetMD5);
   } else {
      label->FileSetMD5[0] = 0;
   }
   if (rec->FileIndex == EOS_LABEL) {
      unser_uint32(label->JobFiles);
      unser_uint64(label->JobBytes);
      unser_uint32(label->StartBlock);
      unser_uint32(label->EndBlock);
      unser_uint32(label->StartFile);
      unser_uint32(label->EndFile);
      unser_uint32(label->JobErrors);
      if (label->VerNum >= 11) {
         unser_uint32(label->JobStatus);
      } else {
         label->JobStatus = JS_Terminated; /* kludge */
      }
   }
   Leave(100);
   return true;
}

void DEVICE::dump_volume_label()
{
   int64_t dbl = debug_level;
   uint32_t File;
   const char *LabelType;
   char buf[30];
   struct tm tm;
   struct date_time dt;

   debug_level = 1;
   File = file;
   switch (VolHdr.LabelType) {
   case PRE_LABEL:
      LabelType = "PRE_LABEL";
      break;
   case VOL_LABEL:
      LabelType = "VOL_LABEL";
      break;
   case EOM_LABEL:
      LabelType = "EOM_LABEL";
      break;
   case SOS_LABEL:
      LabelType = "SOS_LABEL";
      break;
   case EOS_LABEL:
      LabelType = "EOS_LABEL";
      break;
   case EOT_LABEL:
      goto bail_out;
   default:
      LabelType = buf;
      sprintf(buf, _("Unknown %d"), VolHdr.LabelType);
      break;
   }

   Pmsg12(-1, _("\nVolume Label:\n"
"Adata             : %d\n"
"Id                : %s"
"VerNo             : %d\n"
"VolName           : %s\n"
"PrevVolName       : %s\n"
"VolFile           : %d\n"
"LabelType         : %s\n"
"LabelSize         : %d\n"
"PoolName          : %s\n"
"MediaType         : %s\n"
"PoolType          : %s\n"
"HostName          : %s\n"
""),
             adata,
             VolHdr.Id, VolHdr.VerNum,
             VolHdr.VolumeName, VolHdr.PrevVolumeName,
             File, LabelType, VolHdr.LabelSize,
             VolHdr.PoolName, VolHdr.MediaType,
             VolHdr.PoolType, VolHdr.HostName);

   if (VolHdr.VerNum >= 11) {
      char dt[50];
      bstrftime(dt, sizeof(dt), btime_to_utime(VolHdr.label_btime));
      Pmsg1(-1, _("Date label written: %s\n"), dt);
   } else {
      dt.julian_day_number   = VolHdr.label_date;
      dt.julian_day_fraction = VolHdr.label_time;
      tm_decode(&dt, &tm);
      Pmsg5(-1,
            _("Date label written: %04d-%02d-%02d at %02d:%02d\n"),
              tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min);
   }

bail_out:
   debug_level = dbl;
}


static void dump_session_label(DEV_RECORD *rec, const char *type)
{
   int64_t dbl;
   struct date_time dt;
   struct tm tm;
   SESSION_LABEL label;
   char ec1[30], ec2[30], ec3[30], ec4[30], ec5[30], ec6[30], ec7[30];

   unser_session_label(&label, rec);
   dbl = debug_level;
   debug_level = 1;
   Pmsg7(-1, _("\n%s Record:\n"
"JobId             : %d\n"
"VerNum            : %d\n"
"PoolName          : %s\n"
"PoolType          : %s\n"
"JobName           : %s\n"
"ClientName        : %s\n"
""),    type, label.JobId, label.VerNum,
      label.PoolName, label.PoolType,
      label.JobName, label.ClientName);

   if (label.VerNum >= 10) {
      Pmsg4(-1, _(
"Job (unique name) : %s\n"
"FileSet           : %s\n"
"JobType           : %c\n"
"JobLevel          : %c\n"
""), label.Job, label.FileSetName, label.JobType, label.JobLevel);
   }

   if (rec->FileIndex == EOS_LABEL) {
      Pmsg8(-1, _(
"JobFiles          : %s\n"
"JobBytes          : %s\n"
"StartBlock        : %s\n"
"EndBlock          : %s\n"
"StartFile         : %s\n"
"EndFile           : %s\n"
"JobErrors         : %s\n"
"JobStatus         : %c\n"
""),
         edit_uint64_with_commas(label.JobFiles, ec1),
         edit_uint64_with_commas(label.JobBytes, ec2),
         edit_uint64_with_commas(label.StartBlock, ec3),
         edit_uint64_with_commas(label.EndBlock, ec4),
         edit_uint64_with_commas(label.StartFile, ec5),
         edit_uint64_with_commas(label.EndFile, ec6),
         edit_uint64_with_commas(label.JobErrors, ec7),
         label.JobStatus);
   }
   if (label.VerNum >= 11) {
      char dt[50];
      bstrftime(dt, sizeof(dt), btime_to_utime(label.write_btime));
      Pmsg1(-1, _("Date written      : %s\n"), dt);
   } else {
      dt.julian_day_number   = label.write_date;
      dt.julian_day_fraction = label.write_time;
      tm_decode(&dt, &tm);
      Pmsg5(-1, _("Date written      : %04d-%02d-%02d at %02d:%02d\n"),
      tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min);
   }

   debug_level = dbl;
}

static int check_label(SESSION_LABEL *label)
{
   int  errors = 0;

   if (label->JobId > 10000000 || label->JobId < 0) {
      Pmsg0(-1, _("***** ERROR ****** : Found error with the JobId\n"));
      errors++;
   }

   if (!errors) {
      switch (label->JobLevel) {
      case L_FULL:
      case L_INCREMENTAL:
      case L_DIFFERENTIAL:
      case L_SINCE:
      case L_VERIFY_CATALOG:
      case L_VERIFY_INIT:
      case L_VERIFY_VOLUME_TO_CATALOG:
      case L_VERIFY_DISK_TO_CATALOG:
      case L_VERIFY_DATA:
      case L_BASE:
      case L_NONE:
      case L_VIRTUAL_FULL:
         break;
      default:
         Pmsg0(-1, _("***** ERROR ****** : Found error with the JobLevel\n"));
         errors++;
      }
   }
   if (!errors) {
      switch (label->JobType) {
      case JT_BACKUP:
            case JT_MIGRATED_JOB:
      case JT_VERIFY:
      case JT_RESTORE:
      case JT_CONSOLE:
      case JT_SYSTEM:
      case JT_ADMIN:
      case JT_ARCHIVE:
      case JT_JOB_COPY:
      case JT_COPY:
      case JT_MIGRATE:
      case JT_SCAN:
               break;
      default:
         Pmsg0(-1, _("***** ERROR ****** : Found error with the JobType\n"));
         errors++;
      }
   }
   if (!errors) {
      POOLMEM *err = get_pool_memory(PM_EMSG);
      if (!is_name_valid(label->Job, &err)) {
         Pmsg1(-1, _("***** ERROR ****** : Found error with the Job name %s\n"), err);
         errors++;
      }
      free_pool_memory(err);
   }
   return errors;
}

int dump_label_record(DEVICE *dev, DEV_RECORD *rec, int verbose, bool check_err)
{
   const char *type;
   int64_t dbl;
   int errors = 0;

   if (rec->FileIndex == 0 && rec->VolSessionId == 0 && rec->VolSessionTime == 0) {
      return 0;
   }
   dbl = debug_level;
   debug_level = 1;
   switch (rec->FileIndex) {
   case PRE_LABEL:
      type = _("Fresh Volume");
      break;
   case VOL_LABEL:
      type = _("Volume");
      break;
   case SOS_LABEL:
      type = _("Begin Job Session");
      break;
   case EOS_LABEL:
      type = _("End Job Session");
      break;
   case EOM_LABEL:
      type = _("End of Media");
      break;
   case EOT_LABEL:
      type = _("End of Tape");
      break;
   default:
      type = _("Unknown");
      break;
   }
   if (verbose) {
      switch (rec->FileIndex) {
      case PRE_LABEL:
      case VOL_LABEL:
         unser_volume_label(dev, rec);
         dev->dump_volume_label();
         break;

      case EOS_LABEL:
      case SOS_LABEL:
         dump_session_label(rec, type);
         break;
      case EOM_LABEL:
         Pmsg7(-1, _("%s Record: File:blk=%u:%u SessId=%d SessTime=%d JobId=%d DataLen=%d\n"),
            type, dev->file, dev->block_num, rec->VolSessionId,
            rec->VolSessionTime, rec->Stream, rec->data_len);
         break;
      case EOT_LABEL:
         Pmsg0(-1, _("Bacula \"End of Tape\" label found.\n"));
         break;
      default:
         Pmsg7(-1, _("%s Record: File:blk=%u:%u SessId=%d SessTime=%d JobId=%d DataLen=%d\n"),
            type, dev->file, dev->block_num, rec->VolSessionId,
            rec->VolSessionTime, rec->Stream, rec->data_len);
         break;
      }
   } else {
      SESSION_LABEL label;
      char dt[50];
      switch (rec->FileIndex) {
      case SOS_LABEL:
         unser_session_label(&label, rec);
         bstrftimes(dt, sizeof(dt), btime_to_utime(label.write_btime));
         Pmsg6(-1, _("%s Record: File:blk=%u:%u SessId=%d SessTime=%d JobId=%d\n"),
            type, dev->file, dev->block_num, rec->VolSessionId, rec->VolSessionTime, label.JobId);
         Pmsg4(-1, _("   Job=%s Date=%s Level=%c Type=%c\n"),
            label.Job, dt, label.JobLevel, label.JobType);
         if (check_err) {
            errors += check_label(&label);
         }
         break;
      case EOS_LABEL:
         char ed1[30], ed2[30];
         unser_session_label(&label, rec);
         bstrftimes(dt, sizeof(dt), btime_to_utime(label.write_btime));
         Pmsg6(-1, _("%s Record: File:blk=%u:%u SessId=%d SessTime=%d JobId=%d\n"),
            type, dev->file, dev->block_num, rec->VolSessionId, rec->VolSessionTime, label.JobId);
         Pmsg7(-1, _("   Date=%s Level=%c Type=%c Files=%s Bytes=%s Errors=%d Status=%c\n"),
            dt, label.JobLevel, label.JobType,
            edit_uint64_with_commas(label.JobFiles, ed1),
            edit_uint64_with_commas(label.JobBytes, ed2),
            label.JobErrors, (char)label.JobStatus);
         if (check_err) {
            errors += check_label(&label);
         }
         break;
      case EOM_LABEL:
      case PRE_LABEL:
      case VOL_LABEL:
      default:
         Pmsg7(-1, _("%s Record: File:blk=%u:%u SessId=%d SessTime=%d JobId=%d DataLen=%d\n"),
            type, dev->file, dev->block_num, rec->VolSessionId, rec->VolSessionTime,
            rec->Stream, rec->data_len);
         break;
      case EOT_LABEL:
         break;
      }
   }
   debug_level = dbl;
   return errors;
}
