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
 *  This routine provides a routine that will handle all
 *    the gory little details of reading a record from a Bacula
 *    archive. It uses a callback to pass you each record in turn,
 *    as well as a callback for mounting the next tape.  It takes
 *    care of reading blocks, applying the bsr, ...
 *    Note, this routine is really the heart of the restore routines,
 *    and we are *really* bit pushing here so be careful about making
 *    any modifications.
 *
 *    Kern E. Sibbald, August MMII
 *
 */

#include "bacula.h"
#include "stored.h"

/* Forward referenced functions */
static BSR *position_to_first_file(JCR *jcr, DCR *dcr, BSR *bsr);
static void handle_session_record(DEVICE *dev, DEV_RECORD *rec, SESSION_LABEL *sessrec);
static bool try_repositioning(JCR *jcr, DEV_RECORD *rec, DCR *dcr);
#ifdef DEBUG
static char *rec_state_bits_to_str(DEV_RECORD *rec);
#endif

static const int dbglvl = 150;
static const int no_FileIndex = -999999;
static bool mount_next_vol(JCR *jcr, DCR *dcr, BSR *bsr,
                           SESSION_LABEL *sessrec, bool *should_stop,
                           bool record_cb(DCR *dcr, DEV_RECORD *rec),
                           bool mount_cb(DCR *dcr))
{
   bool    ok = true;
   DEVICE *dev = dcr->dev;
   *should_stop = false;

   /* We need an other volume */
   volume_unused(dcr);       /* mark volume unused */
   if (!mount_cb(dcr)) {
      *should_stop = true;
      /*
       * Create EOT Label so that Media record may
       *  be properly updated because this is the last
       *  tape.
       */
      DEV_RECORD *trec = new_record();
      trec->FileIndex = EOT_LABEL;
      trec->Addr = dev->get_full_addr();
      ok = record_cb(dcr, trec);
      free_record(trec);
      if (jcr->mount_next_volume) {
         jcr->mount_next_volume = false;
         dev->clear_eot();
      }
      return ok;
   }
   jcr->mount_next_volume = false;
   /*
    * The Device can change at the end of a tape, so refresh it
    *   from the dcr.
    */
   dev = dcr->dev;
   /*
    * We just have a new tape up, now read the label (first record)
    *  and pass it off to the callback routine, then continue
    *  most likely reading the previous record.
    */
   dcr->read_block_from_device(NO_BLOCK_NUMBER_CHECK);

   DEV_RECORD *trec = new_record();
   read_record_from_block(dcr, trec);
   handle_session_record(dev, trec, sessrec);
   ok = record_cb(dcr, trec);
   free_record(trec);
   position_to_first_file(jcr, dcr, bsr); /* We jump to the specified position */
   return ok;
}

/*
 * This subroutine reads all the records and passes them back to your
 *  callback routine (also mount routine at EOM).
 * You must not change any values in the DEV_RECORD packet
 */
bool read_records(DCR *dcr,
       bool record_cb(DCR *dcr, DEV_RECORD *rec),
       bool mount_cb(DCR *dcr))
{
   JCR *jcr = dcr->jcr;
   DEVICE *dev = dcr->dev;
   DEV_BLOCK *block = dcr->block;
   DEV_RECORD *rec = NULL;
   uint32_t record;
   int32_t lastFileIndex;
   bool ok = true;
   bool done = false;
   bool should_stop;
   SESSION_LABEL sessrec;
   dlist *recs;                         /* linked list of rec packets open */
   char ed1[50];
   bool first_block = true;

   recs = New(dlist(rec, &rec->link));
   /* We go to the first_file unless we need to reposition during an
    * interactive restore session (the reposition will be done with a different
    * BSR in the for loop */
   position_to_first_file(jcr, dcr, jcr->bsr);
   jcr->mount_next_volume = false;

   for ( ; ok && !done; ) {
      if (job_canceled(jcr)) {
         ok = false;
         break;
      }
      ASSERT2(!dcr->dev->adata, "Called with adata block. Wrong!");


      if (! first_block || dev->dev_type != B_FIFO_DEV ) {
         if (dev->at_eot() || !dcr->read_block_from_device(CHECK_BLOCK_NUMBERS)) {
            if (dev->at_eot()) {
               Jmsg(jcr, M_INFO, 0,
                    _("End of Volume \"%s\" at addr=%s on device %s.\n"),
                    dcr->VolumeName,
                    dev->print_addr(ed1, sizeof(ed1), dev->EndAddr),
                    dev->print_name());
               ok = mount_next_vol(jcr, dcr, jcr->bsr, &sessrec, &should_stop,
                                   record_cb, mount_cb);
               /* Might have changed after the mount request */
               dev = dcr->dev;
               block = dcr->block;
               if (should_stop) {
                  break;
               }
               continue;

            } else if (dev->at_eof()) {
               Dmsg3(200, "EOF at addr=%s on device %s, Volume \"%s\"\n",
                     dev->print_addr(ed1, sizeof(ed1), dev->EndAddr),
                     dev->print_name(), dcr->VolumeName);
               continue;
            } else if (dev->is_short_block()) {
               Jmsg1(jcr, M_ERROR, 0, "%s", dev->errmsg);
               continue;
            } else {
               /* I/O error or strange end of tape */
               display_tape_error_status(jcr, dev);
               if (forge_on || jcr->ignore_label_errors) {
                  dev->fsr(1);       /* try skipping bad record */
                  Pmsg0(000, _("Did fsr in attempt to skip bad record.\n"));
                  continue;              /* try to continue */
               }
               ok = false;               /* stop everything */
               break;
            }
         }
             Dmsg1(dbglvl, "Read new block at pos=%s\n", dev->print_addr(ed1, sizeof(ed1)));
      }
      first_block = false;
#ifdef if_and_when_FAST_BLOCK_REJECTION_is_working
      /* this does not stop when file/block are too big */
      if (!match_bsr_block(jcr->bsr, block)) {
         if (try_repositioning(jcr, rec, dcr)) {
            break;                    /* get next volume */
         }
         continue;                    /* skip this record */
      }
#endif
      /*
       * Get a new record for each Job as defined by
       *   VolSessionId and VolSessionTime
       */
      bool found = false;
      foreach_dlist(rec, recs) {
         if (rec->VolSessionId == block->VolSessionId &&
             rec->VolSessionTime == block->VolSessionTime) {
            /* When the previous Block of the current record is not correctly ordered,
             * if we concat the previous record to the next one, the result is probably
             * incorrect. At least the vacuum command should not use this kind of record
             */
            if (rec->remainder) {
               if (rec->BlockNumber != (block->BlockNumber - 1)
                   &&
                   rec->BlockNumber != block->BlockNumber)
               {
                  Dmsg3(0, "invalid: rec=%ld block=%ld state=%s\n",
                        rec->BlockNumber, block->BlockNumber, rec_state_bits_to_str(rec));
                  rec->invalid = true;
                  /* We can discard the current data if needed. The code is very
                   * tricky in the read_records loop, so it's better to not
                   * introduce new subtle errors.
                   */
                  if (dcr->discard_invalid_records) {
                     empty_record(rec);
                  }
               }
            }
            found = true;
            break;
          }
      }
      if (!found) {
         rec = new_record();
         recs->prepend(rec);
         Dmsg3(dbglvl, "New record for state=%s SI=%d ST=%d\n",
             rec_state_bits_to_str(rec),
             block->VolSessionId, block->VolSessionTime);
      }
      Dmsg4(dbglvl, "Before read rec loop. stat=%s blk=%d rem=%d invalid=%d\n",
            rec_state_bits_to_str(rec), block->BlockNumber, rec->remainder, rec->invalid);
      record = 0;
      rec->state_bits = 0;
      rec->BlockNumber = block->BlockNumber;
      lastFileIndex = no_FileIndex;
      Dmsg1(dbglvl, "Block %s empty\n", is_block_marked_empty(rec)?"is":"NOT");
      for (rec->state_bits=0; ok && !is_block_marked_empty(rec); ) {
         if (!read_record_from_block(dcr, rec)) {
            Dmsg3(200, "!read-break. state_bits=%s blk=%d rem=%d\n", rec_state_bits_to_str(rec),
                  block->BlockNumber, rec->remainder);
            break;
         }
         Dmsg5(dbglvl, "read-OK. state_bits=%s blk=%d rem=%d volume:addr=%s:%llu\n",
               rec_state_bits_to_str(rec), block->BlockNumber, rec->remainder,
               NPRT(rec->VolumeName), rec->Addr);
         /*
          * At this point, we have at least a record header.
          *  Now decide if we want this record or not, but remember
          *  before accessing the record, we may need to read again to
          *  get all the data.
          */
         record++;
         Dmsg6(dbglvl, "recno=%d state_bits=%s blk=%d SI=%d ST=%d FI=%d\n", record,
            rec_state_bits_to_str(rec), block->BlockNumber,
            rec->VolSessionId, rec->VolSessionTime, rec->FileIndex);

         if (rec->FileIndex == EOM_LABEL) { /* end of tape? */
            Dmsg0(40, "Get EOM LABEL\n");
            break;                         /* yes, get out */
         }

         /* Some sort of label? */
         if (rec->FileIndex < 0) {
            handle_session_record(dev, rec, &sessrec);
            if (jcr->bsr) {
               /* We just check block FI and FT not FileIndex */
               rec->match_stat = match_bsr_block(jcr->bsr, block);
            } else {
               rec->match_stat = 0;
            }
            if (rec->invalid) {
               Dmsg5(0, "The record %d in block %ld SI=%ld ST=%ld FI=%ld was marked as invalid\n",
                     rec->RecNum, rec->BlockNumber, rec->VolSessionId, rec->VolSessionTime, rec->FileIndex);
            }
            //ASSERTD(!rec->invalid || dcr->discard_invalid_records, "Got an invalid record");
            /*
             * Note, we pass *all* labels to the callback routine. If
             *  he wants to know if they matched the bsr, then he must
             *  check the match_stat in the record */
            ok = record_cb(dcr, rec);
            rec->invalid = false;  /* The record was maybe invalid, but the next one is probably good */
#ifdef xxx
            /*
             * If this is the end of the Session (EOS) for this record
             *  we can remove the record.  Note, there is a separate
             *  record to read each session. If a new session is seen
             *  a new record will be created at approx line 157 above.
             * However, it seg faults in the for line at lineno 196.
             */
            if (rec->FileIndex == EOS_LABEL) {
               Dmsg2(dbglvl, "Remove EOS rec. SI=%d ST=%d\n", rec->VolSessionId,
                  rec->VolSessionTime);
               recs->remove(rec);
               free_record(rec);
            }
#endif
            continue;
         } /* end if label record */

         /*
          * Apply BSR filter
          */
         if (jcr->bsr) {
            rec->match_stat = match_bsr(jcr->bsr, rec, &dev->VolHdr, &sessrec, jcr);
            Dmsg2(dbglvl, "match_bsr=%d bsr->reposition=%d\n", rec->match_stat,
               jcr->bsr->reposition);
            if (rec->match_stat == -1) { /* no more possible matches */
               done = true;   /* all items found, stop */
               Dmsg1(dbglvl, "All done Addr=%s\n", dev->print_addr(ed1, sizeof(ed1)));
               break;
            } else if (rec->match_stat == 0) {  /* no match */
               Dmsg3(dbglvl, "BSR no match: clear rem=%d FI=%d before set_eof pos %s\n",
                  rec->remainder, rec->FileIndex, dev->print_addr(ed1, sizeof(ed1)));
               rec->remainder = 0;
               rec->state_bits &= ~REC_PARTIAL_RECORD;
               if (try_repositioning(jcr, rec, dcr)) {
                  break;              /* We moved on the volume, read next block */
               }
               continue;              /* we don't want record, read next one */
            }
         }
         dcr->VolLastIndex = rec->FileIndex;  /* let caller know where we are */
         if (is_partial_record(rec)) {
            Dmsg6(dbglvl, "Partial, break. recno=%d state_bits=%s blk=%d SI=%d ST=%d FI=%d\n", record,
               rec_state_bits_to_str(rec), block->BlockNumber,
               rec->VolSessionId, rec->VolSessionTime, rec->FileIndex);
            break;                    /* read second part of record */
         }

         Dmsg6(dbglvl, "OK callback. recno=%d state_bits=%s blk=%d SI=%d ST=%d FI=%d\n", record,
               rec_state_bits_to_str(rec), block->BlockNumber,
               rec->VolSessionId, rec->VolSessionTime, rec->FileIndex);
         if (lastFileIndex != no_FileIndex && lastFileIndex != rec->FileIndex) {
            if (is_this_bsr_done(jcr, jcr->bsr, rec) && try_repositioning(jcr, rec, dcr)) {
               Dmsg1(dbglvl, "This bsr done, break pos %s\n",
                     dev->print_addr(ed1, sizeof(ed1)));
               break;
            }
            Dmsg2(dbglvl, "==== inside LastIndex=%d FileIndex=%d\n", lastFileIndex, rec->FileIndex);
         }
         Dmsg2(dbglvl, "==== LastIndex=%d FileIndex=%d\n", lastFileIndex, rec->FileIndex);
         lastFileIndex = rec->FileIndex;
         if (rec->invalid) {
            Dmsg5(0, "The record %d in block %ld SI=%ld ST=%ld FI=%ld was marked as invalid\n",
                  rec->RecNum, rec->BlockNumber, rec->VolSessionId, rec->VolSessionTime, rec->FileIndex);
         }
         //ASSERTD(!rec->invalid || dcr->discard_invalid_records, "Got an invalid record");
         ok = record_cb(dcr, rec);
         rec->invalid = false;  /* The record was maybe invalid, but the next one is probably good */
#if 0
         /*
          * If we have a digest stream, we check to see if we have
          *  finished the current bsr, and if so, repositioning will
          *  be turned on.
          */
         if (crypto_digest_stream_type(rec->Stream) != CRYPTO_DIGEST_NONE) {
            Dmsg3(dbglvl, "=== Have digest FI=%u before bsr check pos %s\n", rec->FileIndex,
                  dev->print_addr(ed1, sizeof(ed1));
            if (is_this_bsr_done(jcr, jcr->bsr, rec) && try_repositioning(jcr, rec, dcr)) {
               Dmsg1(dbglvl, "==== BSR done at FI=%d\n", rec->FileIndex);
               Dmsg1(dbglvl, "This bsr done, break pos=%s\n",
                     dev->print_addr(ed1, sizeof(ed1)));
               break;
            }
            Dmsg2(900, "After is_bsr_done pos %s\n", dev->print_addr(ed1, sizeof(ed1));
         }
#endif
      } /* end for loop over records */
      Dmsg1(dbglvl, "After end recs in block. pos=%s\n", dev->print_addr(ed1, sizeof(ed1)));
   } /* end for loop over blocks */

   /* Walk down list and free all remaining allocated recs */
   while (!recs->empty()) {
      rec = (DEV_RECORD *)recs->first();
      recs->remove(rec);
      free_record(rec);
   }
   delete recs;
   print_block_read_errors(jcr, block);
   return ok;
}

/*
 * See if we can reposition.
 *   Returns:  true  if at end of volume
 *             false otherwise
 */
static bool try_repositioning(JCR *jcr, DEV_RECORD *rec, DCR *dcr)
{
   BSR *bsr;
   DEVICE *dev = dcr->dev;
   char ed1[50];

   bsr = find_next_bsr(jcr->bsr, dev);
   Dmsg2(dbglvl, "nextbsr=%p mount_next_volume=%d\n", bsr, jcr->bsr->mount_next_volume);
   if (bsr == NULL && jcr->bsr->mount_next_volume) {
      Dmsg0(dbglvl, "Would mount next volume here\n");
      Dmsg1(dbglvl, "Current position Addr=%s\n",
         dev->print_addr(ed1, sizeof(ed1)));
      jcr->bsr->mount_next_volume = false;
      if (!dev->at_eot()) {
         /* Set EOT flag to force mount of next Volume */
         jcr->mount_next_volume = true;
         dev->set_eot();
      }
      rec->Addr = 0;
      return true;
   }
   if (bsr) {
      /*
       * ***FIXME*** gross kludge to make disk seeking work.  Remove
       *   when find_next_bsr() is fixed not to return a bsr already
       *   completed.
       */
      uint64_t dev_addr = dev->get_full_addr();
      uint64_t bsr_addr = get_bsr_start_addr(bsr);

      /* Do not position backwards */
      if (dev_addr > bsr_addr) {
         return false;
      }
      Dmsg2(dbglvl, "Try_Reposition from addr=%llu to %llu\n",
            dev_addr, bsr_addr);
      dev->reposition(dcr, bsr_addr);
      rec->Addr = 0;
      return true;              /* We want the next block */
   }
   return false;
}

/*
 * Position to the first file on this volume
 */
static BSR *position_to_first_file(JCR *jcr, DCR *dcr, BSR *bsr)
{
   DEVICE *dev = dcr->dev;
   uint64_t bsr_addr;
   char ed1[50], ed2[50];

   Enter(150);
   /*
    * Now find and position to first file and block
    *   on this tape.
    */
   if (bsr) {
      bsr->reposition = true;    /* force repositioning */
      bsr = find_next_bsr(bsr, dev);

      if ((bsr_addr=get_bsr_start_addr(bsr)) > 0) {
         Jmsg(jcr, M_INFO, 0, _("Forward spacing Volume \"%s\" to addr=%s\n"),
              dev->VolHdr.VolumeName, dev->print_addr(ed1, sizeof(ed1), bsr_addr));
         dev->clear_eot();      /* TODO: See where to put this clear() exactly */
         Dmsg2(dbglvl, "pos_to_first_file from addr=%s to %s\n",
               dev->print_addr(ed1, sizeof(ed1)),
               dev->print_addr(ed2, sizeof(ed2), bsr_addr));
         dev->reposition(dcr, bsr_addr);
      }
   }
   Leave(150);
   return bsr;
}


static void handle_session_record(DEVICE *dev, DEV_RECORD *rec, SESSION_LABEL *sessrec)
{
   const char *rtype;
   char buf[100];

   memset(sessrec, 0, sizeof(SESSION_LABEL));
   switch (rec->FileIndex) {
   case PRE_LABEL:
      rtype = _("Fresh Volume Label");
      break;
   case VOL_LABEL:
      rtype = _("Volume Label");
      unser_volume_label(dev, rec);
      break;
   case SOS_LABEL:
      rtype = _("Begin Session");
      unser_session_label(sessrec, rec);
      break;
   case EOS_LABEL:
      rtype = _("End Session");
      break;
   case EOM_LABEL:
      rtype = _("End of Media");
      break;
   default:
      bsnprintf(buf, sizeof(buf), _("Unknown code %d\n"), rec->FileIndex);
      rtype = buf;
      break;
   }
   Dmsg5(dbglvl, _("%s Record: VolSessionId=%d VolSessionTime=%d JobId=%d DataLen=%d\n"),
         rtype, rec->VolSessionId, rec->VolSessionTime, rec->Stream, rec->data_len);
}

#ifdef DEBUG
static char *rec_state_bits_to_str(DEV_RECORD *rec)
{
   static char buf[200];
   buf[0] = 0;
   if (rec->state_bits & REC_NO_HEADER) {
      bstrncat(buf, "Nohdr,", sizeof(buf));
   }
   if (is_partial_record(rec)) {
      bstrncat(buf, "partial,", sizeof(buf));
   }
   if (rec->state_bits & REC_BLOCK_EMPTY) {
      bstrncat(buf, "empty,", sizeof(buf));
   }
   if (rec->state_bits & REC_NO_MATCH) {
      bstrncat(buf, "Nomatch,", sizeof(buf));
   }
   if (rec->state_bits & REC_CONTINUATION) {
      bstrncat(buf, "cont,", sizeof(buf));
   }
   if (buf[0]) {
      buf[strlen(buf)-1] = 0;
   }
   return buf;
}
#endif
