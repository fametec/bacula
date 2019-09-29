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
 *   block_util.c -- tape block utility functions
 *
 *              Kern Sibbald, split from block.c March MMXII
 */


#include "bacula.h"
#include "stored.h"

static const int dbglvl = 160;

#ifdef DEBUG_BLOCK_CHECKSUM
static const bool debug_block_checksum = true;
#else
static const bool debug_block_checksum = false;
#endif

#ifdef NO_TAPE_WRITE_TEST
static const bool no_tape_write_test = true;
#else
static const bool no_tape_write_test = false;
#endif

/*
 * Dump the block header, then walk through
 * the block printing out the record headers.
 */
void dump_block(DEVICE *dev, DEV_BLOCK *b, const char *msg, bool force)
{
   ser_declare;
   char *p;
   char *bufp;
   char Id[BLKHDR_ID_LENGTH+1];
   uint32_t CheckSum, BlockCheckSum;
   uint32_t block_len, reclen;
   uint32_t BlockNumber;
   uint32_t VolSessionId, VolSessionTime, data_len;
   int32_t  FileIndex;
   int32_t  Stream;
   int bhl, rhl;
   char buf1[100], buf2[100];

   if (!force && ((debug_level & ~DT_ALL) < 250)) {
      return;
   }
   if (b->adata) {
      Dmsg0(20, "Dump block: adata=1 cannot dump.\n");
      return;
   }
   bufp = b->bufp;
   if (dev) {
      if (dev->can_read()) {
         bufp = b->buf + b->block_len;
      }
   }
   unser_begin(b->buf, BLKHDR1_LENGTH);
   unser_uint32(CheckSum);
   unser_uint32(block_len);
   unser_uint32(BlockNumber);
   unser_bytes(Id, BLKHDR_ID_LENGTH);
   ASSERT(unser_length(b->buf) == BLKHDR1_LENGTH);
   Id[BLKHDR_ID_LENGTH] = 0;
   if (Id[3] == '2') {
      unser_uint32(VolSessionId);
      unser_uint32(VolSessionTime);
      bhl = BLKHDR2_LENGTH;
      rhl = RECHDR2_LENGTH;
   } else {
      VolSessionId = VolSessionTime = 0;
      bhl = BLKHDR1_LENGTH;
      rhl = RECHDR1_LENGTH;
   }

   if (block_len > 4000000 || block_len < BLKHDR_CS_LENGTH) {
      Dmsg3(20, "Will not dump blocksize too %s %lu msg: %s\n",
            (block_len < BLKHDR_CS_LENGTH)?"small":"big",
            block_len, msg);
      return;
   }

   BlockCheckSum = bcrc32((uint8_t *)b->buf+BLKHDR_CS_LENGTH,
                         block_len-BLKHDR_CS_LENGTH);
   Pmsg7(000, _("Dump block %s %p: adata=%d size=%d BlkNum=%d\n"
"                           Hdrcksum=%x cksum=%x\n"),
      msg, b, b->adata, block_len, BlockNumber, CheckSum, BlockCheckSum);
   p = b->buf + bhl;
   while (p < bufp) {
      unser_begin(p, WRITE_RECHDR_LENGTH);
      if (rhl == RECHDR1_LENGTH) {
         unser_uint32(VolSessionId);
         unser_uint32(VolSessionTime);
      }
      unser_int32(FileIndex);
      unser_int32(Stream);
      unser_uint32(data_len);
      if (Stream == STREAM_ADATA_BLOCK_HEADER) {
         reclen = 0;
         p += WRITE_ADATA_BLKHDR_LENGTH;
      } else if (Stream == STREAM_ADATA_RECORD_HEADER ||
                 Stream == -STREAM_ADATA_RECORD_HEADER) {
         unser_uint32(reclen);
         unser_int32(Stream);
         p += WRITE_ADATA_RECHDR_LENGTH;
      } else {
         reclen = 0;
         p += data_len + rhl;
      }
      Pmsg6(000, _("   Rec: VId=%u VT=%u FI=%s Strm=%s len=%d reclen=%d\n"),
           VolSessionId, VolSessionTime, FI_to_ascii(buf1, FileIndex),
           stream_to_ascii(buf2, Stream, FileIndex), data_len, reclen);
  }
}

/*
 * Create a new block structure.
 * We pass device so that the block can inherit the
 * min and max block sizes.
 */
void DEVICE::new_dcr_blocks(DCR *dcr)
{
   dcr->block = dcr->ameta_block = new_block(dcr);
}

DEV_BLOCK *DEVICE::new_block(DCR *dcr, int size)
{
   DEV_BLOCK *block = (DEV_BLOCK *)get_memory(sizeof(DEV_BLOCK));
   int len;

   memset(block, 0, sizeof(DEV_BLOCK));

   /* If the user has specified a max_block_size, use it as the default */
   if (max_block_size == 0) {
      len = DEFAULT_BLOCK_SIZE;
   } else {
      len = max_block_size;
   }
   block->dev = this;
   /* special size */
   if (size) {
      len = size;
   }
   block->buf_len = len;
   block->buf = get_memory(block->buf_len);
   block->rechdr_queue = get_memory(block->buf_len);
   block->rechdr_items = 0;
   Dmsg2(510, "Rechdr len=%d max_items=%d\n", sizeof_pool_memory(block->rechdr_queue),
      sizeof_pool_memory(block->rechdr_queue)/WRITE_ADATA_RECHDR_LENGTH);
   empty_block(block);
   block->BlockVer = BLOCK_VER;       /* default write version */
   Dmsg3(150, "New block adata=%d len=%d block=%p\n", block->adata, len, block);
   return block;
}


/*
 * Duplicate an existing block (eblock)
 */
DEV_BLOCK *dup_block(DEV_BLOCK *eblock)
{
   DEV_BLOCK *block = (DEV_BLOCK *)get_memory(sizeof(DEV_BLOCK));
   int buf_len = sizeof_pool_memory(eblock->buf);
   int rechdr_len = sizeof_pool_memory(eblock->rechdr_queue);

   memcpy(block, eblock, sizeof(DEV_BLOCK));
   block->buf = get_memory(buf_len);
   memcpy(block->buf, eblock->buf, buf_len);

   block->rechdr_queue = get_memory(rechdr_len);
   memcpy(block->rechdr_queue, eblock->rechdr_queue, rechdr_len);

   /* bufp might point inside buf */
   if (eblock->bufp &&
       eblock->bufp >= eblock->buf &&
       eblock->bufp < (eblock->buf + buf_len))
   {
      block->bufp = (eblock->bufp - eblock->buf) + block->buf;

   } else {
      block->bufp = NULL;
   }
   return block;
}

/*
 * Flush block to disk
 */
bool DEVICE::flush_block(DCR *dcr)
{
   if (!is_block_empty(dcr->block)) {
      Dmsg0(dbglvl, "=== wpath 53 flush_ameta\n");
      Dmsg4(190, "Call flush_ameta_block BlockAddr=%lld nbytes=%d adata=%d block=%x\n",
         dcr->block->BlockAddr, dcr->block->binbuf, dcr->adata_block->adata, dcr->adata_block);
      dump_block(dcr->dev, dcr->block, "Flush_ameta_block");
      if (dcr->jcr->is_canceled() || !dcr->write_block_to_device()) {
         Dmsg0(dbglvl, "=== wpath 54 flush_ameta\n");
         Dmsg0(190, "Failed to write ameta block to device, return false.\n");
         return false;
      }
      empty_block(dcr->block);
   }
   return true;
}


/*
 * Only the first block checksum error was reported.
 *   If there are more, report it now.
 */
void print_block_read_errors(JCR *jcr, DEV_BLOCK *block)
{
   if (block->read_errors > 1) {
      Jmsg(jcr, M_ERROR, 0, _("%d block read errors not printed.\n"),
         block->read_errors);
   }
}

void DEVICE::free_dcr_blocks(DCR *dcr)
{
   if (dcr->block == dcr->ameta_block) {
      dcr->ameta_block = NULL;      /* do not free twice */
   }
   free_block(dcr->block);
   dcr->block = NULL;
   free_block(dcr->ameta_block);
   dcr->ameta_block = NULL;
}

/*
 * Free block
 */
void free_block(DEV_BLOCK *block)
{
   if (block) {
      Dmsg1(999, "free_block buffer=%p\n", block->buf);
      if (block->buf) {
         free_memory(block->buf);
      }
      if (block->rechdr_queue) {
         free_memory(block->rechdr_queue);
      }
      Dmsg1(999, "=== free_block block %p\n", block);
      free_memory((POOLMEM *)block);
   }
}

bool is_block_empty(DEV_BLOCK *block)
{
   if (block->adata) {
      Dmsg1(200, "=== adata=1 binbuf=%d\n", block->binbuf);
      return block->binbuf <= 0;
   } else {
      Dmsg1(200, "=== adata=0 binbuf=%d\n", block->binbuf-WRITE_BLKHDR_LENGTH);
      return block->binbuf <= WRITE_BLKHDR_LENGTH;
   }
}

/* Empty the block -- for writing */
void empty_block(DEV_BLOCK *block)
{
   if (block->adata) {
      block->binbuf = 0;
   } else {
      block->binbuf = WRITE_BLKHDR_LENGTH;
   }
   Dmsg3(250, "empty_block: adata=%d len=%d set binbuf=%d\n",
         block->adata, block->buf_len, block->binbuf);
   block->bufp = block->buf + block->binbuf;
   block->read_len = 0;
   block->write_failed = false;
   block->block_read = false;
   block->needs_write = false;
   block->FirstIndex = block->LastIndex = 0;
   block->RecNum = 0;
   block->BlockAddr = 0;
}

/*
 * Create block header just before write. The space
 * in the buffer should have already been reserved by
 * init_block.
 */
uint32_t ser_block_header(DEV_BLOCK *block, bool do_checksum)
{
   ser_declare;
   uint32_t block_len = block->binbuf;

   block->CheckSum = 0;
   if (block->adata) {
      /* Checksum whole block */
      if (do_checksum) {
         block->CheckSum = bcrc32((uint8_t *)block->buf, block_len);
      }
   } else {
      Dmsg1(160, "block_header: block_len=%d\n", block_len);
      ser_begin(block->buf, BLKHDR2_LENGTH);
      ser_uint32(block->CheckSum);
      ser_uint32(block_len);
      ser_uint32(block->BlockNumber);
      ser_bytes(WRITE_BLKHDR_ID, BLKHDR_ID_LENGTH);
      if (BLOCK_VER >= 2) {
         ser_uint32(block->VolSessionId);
         ser_uint32(block->VolSessionTime);
      }

      /* Checksum whole block except for the checksum */
      if (do_checksum) {
         block->CheckSum = bcrc32((uint8_t *)block->buf+BLKHDR_CS_LENGTH,
                       block_len-BLKHDR_CS_LENGTH);
      }
      Dmsg2(160, "ser_block_header: adata=%d checksum=%x\n", block->adata, block->CheckSum);
      ser_begin(block->buf, BLKHDR2_LENGTH);
      ser_uint32(block->CheckSum);    /* now add checksum to block header */
   }
   return block->CheckSum;
}

/*
 * Unserialize the block header for reading block.
 *  This includes setting all the buffer pointers correctly.
 *
 *  Returns: false on failure (not a block)
 *           true  on success
 */
bool unser_block_header(DCR *dcr, DEVICE *dev, DEV_BLOCK *block)
{
   ser_declare;
   char Id[BLKHDR_ID_LENGTH+1];
   uint32_t BlockCheckSum;
   uint32_t block_len;
   uint32_t block_end;
   uint32_t BlockNumber;
   JCR *jcr = dcr->jcr;
   int bhl;

   if (block->adata) {
      /* Checksum the whole block */
      if (block->block_len <= block->read_len && dev->do_checksum()) {
         BlockCheckSum = bcrc32((uint8_t *)block->buf, block->block_len);
         if (BlockCheckSum != block->CheckSum) {
            dev->dev_errno = EIO;
            Mmsg5(dev->errmsg, _("Volume data error at %lld!\n"
               "Adata block checksum mismatch in block=%u len=%d: calc=%x blk=%x\n"),
               block->BlockAddr, block->BlockNumber,
               block->block_len, BlockCheckSum, block->CheckSum);
            if (block->read_errors == 0 || verbose >= 2) {
               Jmsg(jcr, M_ERROR, 0, "%s", dev->errmsg);
               dump_block(dev, block, "with checksum error");
            }
            block->read_errors++;
            if (!forge_on) {
               return false;
            }
         }
      }
      return true;
   }

   if (block->no_header) {
      return true;
   }
   unser_begin(block->buf, BLKHDR_LENGTH);
   unser_uint32(block->CheckSum);
   unser_uint32(block_len);
   unser_uint32(BlockNumber);
   unser_bytes(Id, BLKHDR_ID_LENGTH);
   ASSERT(unser_length(block->buf) == BLKHDR1_LENGTH);
   Id[BLKHDR_ID_LENGTH] = 0;

   if (Id[3] == '1') {
      bhl = BLKHDR1_LENGTH;
      block->BlockVer = 1;
      block->bufp = block->buf + bhl;
      //Dmsg3(100, "Block=%p buf=%p bufp=%p\n", block, block->buf, block->bufp);
      if (strncmp(Id, BLKHDR1_ID, BLKHDR_ID_LENGTH) != 0) {
         dev->dev_errno = EIO;
         Mmsg4(dev->errmsg, _("Volume data error at %u:%u! Wanted ID: \"%s\", got \"%s\". Buffer discarded.\n"),
            dev->file, dev->block_num, BLKHDR1_ID, Id);
         if (block->read_errors == 0 || verbose >= 2) {
            Jmsg(jcr, M_ERROR, 0, "%s", dev->errmsg);
         }
         block->read_errors++;
         return false;
      }
   } else if (Id[3] == '2') {
      unser_uint32(block->VolSessionId);
      unser_uint32(block->VolSessionTime);
      bhl = BLKHDR2_LENGTH;
      block->BlockVer = 2;
      block->bufp = block->buf + bhl;
      //Dmsg5(100, "Read-blkhdr Block=%p adata=%d buf=%p bufp=%p off=%d\n", block, block->adata,
      //   block->buf, block->bufp, block->bufp-block->buf);
      if (strncmp(Id, BLKHDR2_ID, BLKHDR_ID_LENGTH) != 0) {
         dev->dev_errno = EIO;
         Mmsg4(dev->errmsg, _("Volume data error at %u:%u! Wanted ID: \"%s\", got \"%s\". Buffer discarded.\n"),
            dev->file, dev->block_num, BLKHDR2_ID, Id);
         if (block->read_errors == 0 || verbose >= 2) {
            Jmsg(jcr, M_ERROR, 0, "%s", dev->errmsg);
         }
         block->read_errors++;
         return false;
      }
   } else {
      dev->dev_errno = EIO;
      Mmsg4(dev->errmsg, _("Volume data error at %u:%u! Wanted ID: \"%s\", got \"%s\". Buffer discarded.\n"),
          dev->file, dev->block_num, BLKHDR2_ID, Id);
      Dmsg1(50, "%s", dev->errmsg);
      if (block->read_errors == 0 || verbose >= 2) {
         Jmsg(jcr, M_FATAL, 0, "%s", dev->errmsg);
      }
      block->read_errors++;
      unser_uint32(block->VolSessionId);
      unser_uint32(block->VolSessionTime);
      return false;
   }

   /* Sanity check */
   if (block_len > MAX_BLOCK_SIZE) {
      dev->dev_errno = EIO;
      Mmsg3(dev->errmsg,  _("Volume data error at %u:%u! Block length %u is insane (too large), probably due to a bad archive.\n"),
         dev->file, dev->block_num, block_len);
      if (block->read_errors == 0 || verbose >= 2) {
         Jmsg(jcr, M_ERROR, 0, "%s", dev->errmsg);
      }
      block->read_errors++;
      return false;
   }

   Dmsg1(390, "unser_block_header block_len=%d\n", block_len);
   /* Find end of block or end of buffer whichever is smaller */
   if (block_len > block->read_len) {
      block_end = block->read_len;
   } else {
      block_end = block_len;
   }
   block->binbuf = block_end - bhl;
   Dmsg3(200, "set block=%p adata=%d binbuf=%d\n", block, block->adata, block->binbuf);
   block->block_len = block_len;
   block->BlockNumber = BlockNumber;
   Dmsg3(390, "Read binbuf = %d %d block_len=%d\n", block->binbuf,
      bhl, block_len);
   if (block_len <= block->read_len && dev->do_checksum()) {
      BlockCheckSum = bcrc32((uint8_t *)block->buf+BLKHDR_CS_LENGTH,
                                 block_len-BLKHDR_CS_LENGTH);

      if (BlockCheckSum != block->CheckSum) {
         dev->dev_errno = EIO;
         Mmsg6(dev->errmsg, _("Volume data error at %u:%u!\n"
            "Block checksum mismatch in block=%u len=%d: calc=%x blk=%x\n"),
            dev->file, dev->block_num, (unsigned)BlockNumber,
            block_len, BlockCheckSum, block->CheckSum);
         if (block->read_errors == 0 || verbose >= 2) {
            Jmsg(jcr, M_ERROR, 0, "%s", dev->errmsg);
            dump_block(dev, block, "with checksum error");
         }
         block->read_errors++;
         if (!forge_on) {
            return false;
         }
      }
   }
   return true;
}

/*
 * Calculate how many bytes to write and then clear to end
 *  of block.
 */
uint32_t get_len_and_clear_block(DEV_BLOCK *block, DEVICE *dev, uint32_t &pad)
{
   uint32_t wlen;
   /*
    * Clear to the end of the buffer if it is not full,
    *  and on tape devices, apply min and fixed blocking.
    */
   wlen = block->binbuf;
   if (wlen != block->buf_len) {
      Dmsg2(250, "binbuf=%d buf_len=%d\n", block->binbuf, block->buf_len);

      /* Adjust write size to min/max for tapes and aligned only */
      if (dev->is_tape() || block->adata) {
         /* check for fixed block size */
         if (dev->min_block_size == dev->max_block_size) {
            wlen = block->buf_len;    /* fixed block size already rounded */
         /* Check for min block size */
         } else if (wlen < dev->min_block_size) {
            wlen =  ((dev->min_block_size + TAPE_BSIZE - 1) / TAPE_BSIZE) * TAPE_BSIZE;
         /* Ensure size is rounded */
         } else {
            wlen = ((wlen + TAPE_BSIZE - 1) / TAPE_BSIZE) * TAPE_BSIZE;
         }
      }
      if (block->adata && dev->padding_size > 0) {
         /* Write to next aligned boundry */
         wlen = ((wlen + dev->padding_size - 1) / dev->padding_size) * dev->padding_size;
      }
      ASSERT(wlen <= block->buf_len);
      /* Clear from end of data to end of block */
      if (wlen-block->binbuf > 0) {
         memset(block->bufp, 0, wlen-block->binbuf); /* clear garbage */
      }
      pad = wlen - block->binbuf;        /* padding or zeros written */
      Dmsg5(150, "Zero end blk: adata=%d cleared=%d buf_len=%d wlen=%d binbuf=%d\n",
         block->adata, pad, block->buf_len, wlen, block->binbuf);
   } else {
      pad = 0;
   }

   return wlen;              /* bytes to write */
}

/*
 * Determine if user defined volume size has been
 *  reached, and if so, return true, otherwise
 *  return false.
 */
bool is_user_volume_size_reached(DCR *dcr, bool quiet)
{
   bool hit_max1, hit_max2;
   uint64_t size, max_size;
   DEVICE *dev = dcr->ameta_dev;
   char ed1[50];
   bool rtn = false;

   Enter(dbglvl);
   if (dev->is_aligned()) {
      /* Note, we reserve space for one ameta and one adata block */
      size = dev->VolCatInfo.VolCatBytes + dcr->ameta_block->buf_len +
         dcr->adata_block->buf_len;
   } else {
      size = dev->VolCatInfo.VolCatBytes + dcr->ameta_block->binbuf;
   }
   /* Limit maximum Volume size to value specified by user */
   hit_max1 = (dev->max_volume_size > 0) && (size >= dev->max_volume_size);
   hit_max2 = (dev->VolCatInfo.VolCatMaxBytes > 0) &&
       (size >= dev->VolCatInfo.VolCatMaxBytes);
   if (hit_max1) {
      max_size = dev->max_volume_size;
   } else {
      max_size = dev->VolCatInfo.VolCatMaxBytes;
   }
   if (hit_max1 || hit_max2) {
      if (!quiet) {
         Jmsg(dcr->jcr, M_INFO, 0, _("User defined maximum volume size %s will be exceeded on device %s.\n"
            "   Marking Volume \"%s\" as Full.\n"),
            edit_uint64_with_commas(max_size, ed1),  dev->print_name(),
            dev->getVolCatName());
      }
      Dmsg4(100, "Maximum volume size %s exceeded Vol=%s device=%s.\n"
         "Marking Volume \"%s\" as Full.\n",
         edit_uint64_with_commas(max_size, ed1), dev->getVolCatName(),
         dev->print_name(), dev->getVolCatName());
      rtn = true;
   }
   Dmsg1(dbglvl, "Return from is_user_volume_size_reached=%d\n", rtn);
   Leave(dbglvl);
   return rtn;
}


void reread_last_block(DCR *dcr)
{
#define CHECK_LAST_BLOCK
#ifdef  CHECK_LAST_BLOCK
   bool ok = true;
   DEVICE *dev = dcr->dev;
   JCR *jcr = dcr->jcr;
   DEV_BLOCK *ameta_block = dcr->ameta_block;
   DEV_BLOCK *adata_block = dcr->adata_block;
   DEV_BLOCK *block = dcr->block;
   /*
    * If the device is a tape and it supports backspace record,
    *   we backspace over one or two eof marks depending on
    *   how many we just wrote, then over the last record,
    *   then re-read it and verify that the block number is
    *   correct.
    */
   if (dev->is_tape() && dev->has_cap(CAP_BSR)) {
      /* Now back up over what we wrote and read the last block */
      if (!dev->bsf(1)) {
         berrno be;
         ok = false;
         Jmsg(jcr, M_ERROR, 0, _("Backspace file at EOT failed. ERR=%s\n"),
              be.bstrerror(dev->dev_errno));
      }
      if (ok && dev->has_cap(CAP_TWOEOF) && !dev->bsf(1)) {
         berrno be;
         ok = false;
         Jmsg(jcr, M_ERROR, 0, _("Backspace file at EOT failed. ERR=%s\n"),
              be.bstrerror(dev->dev_errno));
      }
      /* Backspace over record */
      if (ok && !dev->bsr(1)) {
         berrno be;
         ok = false;
         Jmsg(jcr, M_ERROR, 0, _("Backspace record at EOT failed. ERR=%s\n"),
              be.bstrerror(dev->dev_errno));
         /*
          *  On FreeBSD systems, if the user got here, it is likely that his/her
          *    tape drive is "frozen".  The correct thing to do is a
          *    rewind(), but if we do that, higher levels in cleaning up, will
          *    most likely write the EOS record over the beginning of the
          *    tape.  The rewind *is* done later in mount.c when another
          *    tape is requested. Note, the clrerror() call in bsr()
          *    calls ioctl(MTCERRSTAT), which *should* fix the problem.
          */
      }
      if (ok) {
         dev->new_dcr_blocks(dcr);
         /* Note, this can destroy dev->errmsg */
         if (!dcr->read_block_from_dev(NO_BLOCK_NUMBER_CHECK)) {
            Jmsg(jcr, M_ERROR, 0, _("Re-read last block at EOT failed. ERR=%s"),
                 dev->errmsg);
         } else {
            /*
             * If we wrote block and the block numbers don't agree
             *  we have a possible problem.
             */
            if (dcr->block->BlockNumber != dev->LastBlock) {
                if (dev->LastBlock > (dcr->block->BlockNumber + 1)) {
                   Jmsg(jcr, M_FATAL, 0, _(
"Re-read of last block: block numbers differ by more than one.\n"
"Probable tape misconfiguration and data loss. Read block=%u Want block=%u.\n"),
                       dcr->block->BlockNumber, dev->LastBlock);
                 } else {
                   Jmsg(jcr, M_ERROR, 0, _(
"Re-read of last block OK, but block numbers differ. Read block=%u Want block=%u.\n"),
                       dcr->block->BlockNumber, dev->LastBlock);
                 }
            } else {
               Jmsg(jcr, M_INFO, 0, _("Re-read of last block succeeded.\n"));
            }
         }
         dev->free_dcr_blocks(dcr);
         dcr->ameta_block = ameta_block;
         dcr->block = block;
         dcr->adata_block = adata_block;
      }
   }
#endif
}

/*
 * If this routine is called, we do our bookkeeping and
 *   then assure that the volume will not be written any
 *   more.
 */
bool terminate_writing_volume(DCR *dcr)
{
   DEVICE *dev = dcr->dev;
   bool ok = true;
   bool was_adata = false;

   Enter(dbglvl);

   if (dev->is_ateot()) {
      return ok;          /* already been here return now */
   }

   /* Work with ameta device */
   if (dev->adata) {
      dev->set_ateot();       /* no more writing this Volume */
      dcr->adata_block->write_failed = true;
      dcr->set_ameta();
      dev = dcr->ameta_dev;
      was_adata = true;
   }

   /* Create a JobMedia record to indicated end of medium */
   dev->VolCatInfo.VolCatFiles = dev->get_file();
   dev->VolCatInfo.VolLastPartBytes = dev->part_size;
   dev->VolCatInfo.VolCatParts = dev->part;
   if (!dir_create_jobmedia_record(dcr)) {
      Dmsg0(50, "Error from create JobMedia\n");
      dev->dev_errno = EIO;
        Mmsg2(dev->errmsg, _("Could not create JobMedia record for Volume=\"%s\" Job=%s\n"),
            dev->getVolCatName(), dcr->jcr->Job);
       Jmsg(dcr->jcr, M_FATAL, 0, "%s", dev->errmsg);
       ok = false;
   }
   flush_jobmedia_queue(dcr->jcr);
   bstrncpy(dev->LoadedVolName, dev->VolCatInfo.VolCatName, sizeof(dev->LoadedVolName));
   dcr->block->write_failed = true;
   if (dev->can_append() && !dev->weof(dcr, 1)) {     /* end the tape */
      dev->VolCatInfo.VolCatErrors++;
      Jmsg(dcr->jcr, M_ERROR, 0, _("Error writing final EOF to tape. Volume %s may not be readable.\n"
           "%s"), dev->VolCatInfo.VolCatName, dev->errmsg);
      ok = false;
      Dmsg0(50, "Error writing final EOF to volume.\n");
   }
   if (ok) {
      ok = dev->end_of_volume(dcr);
   }

   Dmsg3(100, "Set VolCatStatus Full adata=%d size=%lld vol=%s\n", dev->adata,
      dev->VolCatInfo.VolCatBytes, dev->VolCatInfo.VolCatName);

   /* If still in append mode mark volume Full */
   if (bstrcmp(dev->VolCatInfo.VolCatStatus, "Append")) {
      dev->setVolCatStatus("Full");
   }

   if (!dir_update_volume_info(dcr, false, true)) {
      Mmsg(dev->errmsg, _("Error sending Volume info to Director.\n"));
      ok = false;
      Dmsg0(50, "Error updating volume info.\n");
   }
   Dmsg2(150, "dir_update_volume_info vol=%s to terminate writing -- %s\n",
      dev->getVolCatName(), ok?"OK":"ERROR");

   dev->notify_newvol_in_attached_dcrs(NULL);

   /* Set new file/block parameters for current dcr */
   set_new_file_parameters(dcr);

   if (ok && dev->has_cap(CAP_TWOEOF) && dev->can_append() && !dev->weof(dcr, 1)) {  /* end the tape */
      dev->VolCatInfo.VolCatErrors++;
      /* This may not be fatal since we already wrote an EOF */
      if (dev->errmsg[0]) {
         Jmsg(dcr->jcr, M_ERROR, 0, "%s", dev->errmsg);
      }
      Dmsg0(50, "Writing second EOF failed.\n");
   }

   dev->set_ateot();                  /* no more writing this tape */
   Dmsg2(150, "Leave terminate_writing_volume=%s -- %s\n",
      dev->getVolCatName(), ok?"OK":"ERROR");
   if (was_adata) {
      dcr->set_adata();
   }
   Leave(dbglvl);
   return ok;
}

/*
 * If a new volume has been mounted since our last write
 *   Create a JobMedia record for the previous volume written,
 *   and set new parameters to write this volume
 * The same applies for if we are in a new file.
 */
bool check_for_newvol_or_newfile(DCR *dcr)
{
   JCR *jcr = dcr->jcr;

   if (dcr->NewVol || dcr->NewFile) {
      if (job_canceled(jcr)) {
         Dmsg0(100, "Canceled\n");
         return false;
      }
      /* If we wrote on Volume create a last jobmedia record for this job */
      if (!dcr->VolFirstIndex) {
         Dmsg7(100, "Skip JobMedia Vol=%s wrote=%d MediaId=%lld FI=%lu LI=%lu StartAddr=%lld EndAddr=%lld\n",
            dcr->VolumeName, dcr->WroteVol, dcr->VolMediaId,
            dcr->VolFirstIndex, dcr->VolLastIndex, dcr->StartAddr, dcr->EndAddr);
      }
      if (dcr->VolFirstIndex && !dir_create_jobmedia_record(dcr)) {
         dcr->dev->dev_errno = EIO;
         Jmsg2(jcr, M_FATAL, 0, _("Could not create JobMedia record for Volume=\"%s\" Job=%s\n"),
            dcr->getVolCatName(), jcr->Job);
         set_new_volume_parameters(dcr);
         Dmsg0(100, "cannot create media record\n");
         return false;
      }
      if (dcr->NewVol) {
         Dmsg0(250, "Process NewVol\n");
         flush_jobmedia_queue(jcr);
         /* Note, setting a new volume also handles any pending new file */
         set_new_volume_parameters(dcr);
      } else {
         set_new_file_parameters(dcr);
      }
   }
   return true;
}

/*
 * Do bookkeeping when a new file is created on a Volume. This is
 *  also done for disk files to generate the jobmedia records for
 *  quick seeking.
 */
bool do_new_file_bookkeeping(DCR *dcr)
{
   DEVICE *dev = dcr->dev;
   JCR *jcr = dcr->jcr;

   /* Create a JobMedia record so restore can seek */
   if (!dir_create_jobmedia_record(dcr)) {
      Dmsg0(40, "Error from create_job_media.\n");
      dev->dev_errno = EIO;
      Jmsg2(jcr, M_FATAL, 0, _("Could not create JobMedia record for Volume=\"%s\" Job=%s\n"),
           dcr->getVolCatName(), jcr->Job);
      Dmsg0(40, "Call terminate_writing_volume\n");
      terminate_writing_volume(dcr);
      dev->dev_errno = EIO;
      return false;
   }
   dev->VolCatInfo.VolCatFiles = dev->get_file();
   dev->VolCatInfo.VolLastPartBytes = dev->part_size;
   dev->VolCatInfo.VolCatParts = dev->part;
   if (!dir_update_volume_info(dcr, false, false)) {
      Dmsg0(50, "Error from update_vol_info.\n");
      Dmsg0(40, "Call terminate_writing_volume\n");
      terminate_writing_volume(dcr);
      dev->dev_errno = EIO;
      return false;
   }
   Dmsg0(100, "dir_update_volume_info max file size -- OK\n");

   dev->notify_newfile_in_attached_dcrs();

   /* Set new file/block parameters for current dcr */
   set_new_file_parameters(dcr);
   return true;
}
