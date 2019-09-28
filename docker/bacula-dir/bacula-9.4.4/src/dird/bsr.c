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
 *   Bacula Director -- Bootstrap Record routines.
 *
 *      BSR (bootstrap record) handling routines split from
 *        ua_restore.c July MMIII
 *
 *     Kern Sibbald, July MMII
 *
 */

#include "bacula.h"
#include "dird.h"

/* Forward referenced functions */
static uint32_t write_bsr(UAContext *ua, RESTORE_CTX &rx, FILE *fd);

/*
 * Create new FileIndex entry for BSR
 */
RBSR_FINDEX *new_findex()
{
   RBSR_FINDEX *fi = (RBSR_FINDEX *)bmalloc(sizeof(RBSR_FINDEX));
   memset(fi, 0, sizeof(RBSR_FINDEX));
   return fi;
}

/*
 * Get storage device name from Storage resource
 */
static bool get_storage_device(char *device, char *storage)
{
   STORE *store;
   if (storage[0] == 0) {
      return false;
   }
   store = (STORE *)GetResWithName(R_STORAGE, storage);
   if (!store) {
      return false;
   }
   DEVICE *dev = (DEVICE *)(store->device->first());
   if (!dev) {
      return false;
   }
   bstrncpy(device, dev->hdr.name, MAX_NAME_LENGTH);
   return true;
}

/*
 * Our data structures were not designed completely
 *  correctly, so the file indexes cover the full
 *  range regardless of volume. The FirstIndex and LastIndex
 *  passed in here are for the current volume, so when
 *  writing out the fi, constrain them to those values.
 *
 * We are called here once for each JobMedia record
 *  for each Volume.
 */
static uint32_t write_findex(rblist *fi_list,
              int32_t FirstIndex, int32_t LastIndex, FILE *fd)
{
   RBSR_FINDEX *fi;
   uint32_t count = 0;

   fi = (RBSR_FINDEX *) fi_list->first();
   while (fi) {
      int32_t findex, findex2;

      /* fi points to the first item of the list, or the next item that is not
       * contigous to the previous group
       */
      findex = fi->findex;
      findex2 = fi->findex2;

      /* Sometime (with the restore command for example), the fi_list can
       * contain false gaps (1-10, 11-11, 12-20 instead of 1-20). The for loop
       * is here to merge blocks and reduce the bsr output. The next while(fi)
       * iteration will use the next_fi that points to the last merged element.
       */
      RBSR_FINDEX *next_fi;
      for (next_fi = (RBSR_FINDEX*) fi_list->next(fi);
           next_fi && next_fi->findex == (findex2+1);
           next_fi = (RBSR_FINDEX *) fi_list->next(next_fi))
      {
         findex2 = next_fi->findex2;
      }

      /* next_fi points after the current block (or to the end of the list), so
       * the next while() iteration will use the next value
       */
      fi = next_fi;

      /* We look if the current FI block match the volume information */
      if ((findex >= FirstIndex && findex <= LastIndex) ||
          (findex2 >= FirstIndex && findex2 <= LastIndex) ||
          (findex < FirstIndex && findex2 > LastIndex)) {

         findex = findex < FirstIndex ? FirstIndex : findex;
         findex2 = findex2 > LastIndex ? LastIndex : findex2;

         if (findex == findex2) {
            fprintf(fd, "FileIndex=%d\n", findex);
            count++;
         } else {
            fprintf(fd, "FileIndex=%d-%d\n", findex, findex2);
            count += findex2 - findex + 1;
         }
      }
   }

   return count;
}

/*
 * Find out if Volume defined with FirstIndex and LastIndex
 *   falls within the range of selected files in the bsr.
 */
static bool is_volume_selected(rblist *fi_list,
              int32_t FirstIndex, int32_t LastIndex)
{
   RBSR_FINDEX *fi;
   foreach_rblist(fi, fi_list) {
      if ((fi->findex >= FirstIndex && fi->findex <= LastIndex) ||
          (fi->findex2 >= FirstIndex && fi->findex2 <= LastIndex) ||
          (fi->findex < FirstIndex && fi->findex2 > LastIndex)) {
         return true;
      }
   }
   return false;
}


/* Create a new bootstrap record */
RBSR *new_bsr()
{
   RBSR_FINDEX *fi=NULL;
   RBSR *bsr = (RBSR *)bmalloc(sizeof(RBSR));
   memset(bsr, 0, sizeof(RBSR));
   bsr->fi_list = New(rblist(fi, &fi->link));
   return bsr;
}

/* Free the entire BSR */
void free_bsr(rblist *bsr_list)
{
   RBSR *bsr;
   foreach_rblist(bsr, bsr_list) {
      delete bsr->fi_list;
      if (bsr->VolParams) {
         free(bsr->VolParams);
      }
      if (bsr->fileregex) {
         free(bsr->fileregex);
      }
      if (bsr->m_fi) {
         free(bsr->m_fi);
      }
   }
   delete bsr_list;
}

/*
 * Complete the BSR by filling in the VolumeName and
 *  VolSessionId and VolSessionTime using the JobId
 */
bool complete_bsr(UAContext *ua, rblist *bsr_list)
{
   RBSR *bsr;
   foreach_rblist(bsr, bsr_list) {
      JOB_DBR jr;
      memset(&jr, 0, sizeof(jr));
      jr.JobId = bsr->JobId;
      if (!db_get_job_record(ua->jcr, ua->db, &jr)) {
         ua->error_msg(_("Unable to get Job record. ERR=%s\n"), db_strerror(ua->db));
         return false;
      }
      bsr->VolSessionId = jr.VolSessionId;
      bsr->VolSessionTime = jr.VolSessionTime;
      if (jr.JobFiles == 0) {      /* zero files is OK, not an error, but */
         bsr->VolCount = 0;        /*   there are no volumes */
         continue;
      }
      if ((bsr->VolCount=db_get_job_volume_parameters(ua->jcr, ua->db, bsr->JobId,
           &(bsr->VolParams))) == 0) {
         ua->error_msg(_("Unable to get Job Volume Parameters. ERR=%s\n"), db_strerror(ua->db));
         if (bsr->VolParams) {
            free(bsr->VolParams);
            bsr->VolParams = NULL;
         }
         return false;
      }
   }
   return true;
}

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t uniq = 0;

static void make_unique_restore_filename(UAContext *ua, POOL_MEM &fname)
{
   JCR *jcr = ua->jcr;
   int i = find_arg_with_value(ua, "bootstrap");
   if (i >= 0) {
      Mmsg(fname, "%s", ua->argv[i]);
      jcr->unlink_bsr = false;
   } else {
      P(mutex);
      uniq++;
      V(mutex);
      Mmsg(fname, "%s/%s.restore.%u.bsr", working_directory, my_name, uniq);
      jcr->unlink_bsr = true;
   }
   if (jcr->RestoreBootstrap) {
      free(jcr->RestoreBootstrap);
   }
   jcr->RestoreBootstrap = bstrdup(fname.c_str());
}

/*
 * Write the bootstrap records to file
 */
uint32_t write_bsr_file(UAContext *ua, RESTORE_CTX &rx)
{
   FILE *fd;
   POOL_MEM fname(PM_MESSAGE);
   uint32_t count = 0;;
   bool err;

   make_unique_restore_filename(ua, fname);
   fd = bfopen(fname.c_str(), "w+b");
   if (!fd) {
      berrno be;
      ua->error_msg(_("Unable to create bootstrap file %s. ERR=%s\n"),
         fname.c_str(), be.bstrerror());
      goto bail_out;
   }
   /* Write them to file */
   count = write_bsr(ua, rx, fd);
   err = ferror(fd);
   fclose(fd);
   if (count == 0) {
      ua->info_msg(_("No files found to read. No bootstrap file written.\n"));
      goto bail_out;
   }
   if (err) {
      ua->error_msg(_("Error writing bsr file.\n"));
      count = 0;
      goto bail_out;
   }

   if (chk_dbglvl(10)) {
      print_bsr(ua, rx);
   }

bail_out:
   return count;
}

static void display_vol_info(UAContext *ua, RESTORE_CTX &rx, JobId_t JobId)
{
   POOL_MEM volmsg(PM_MESSAGE);
   char Device[MAX_NAME_LENGTH];
   char online;
   RBSR *bsr;

   foreach_rblist(bsr, rx.bsr_list) {
      if (JobId && JobId != bsr->JobId) {
         continue;
      }

      for (int i=0; i < bsr->VolCount; i++) {
         if (bsr->VolParams[i].VolumeName[0]) {
            if (!get_storage_device(Device, bsr->VolParams[i].Storage)) {
               Device[0] = 0;
            }
            if (bsr->VolParams[i].InChanger && bsr->VolParams[i].Slot) {
               online = '*';
            } else {
               online = ' ';
            }
            Mmsg(volmsg, "%c%-25s %-25s %-25s",
                 online, bsr->VolParams[i].VolumeName,
                 bsr->VolParams[i].Storage, Device);
            add_prompt(ua, volmsg.c_str());
         }
      }
   }
}

void display_bsr_info(UAContext *ua, RESTORE_CTX &rx)
{
   char *p;
   JobId_t JobId;

   /* Tell the user what he will need to mount */
   ua->send_msg("\n");
   ua->send_msg(_("The Job will require the following (*=>InChanger):\n"
                  "   Volume(s)                 Storage(s)                SD Device(s)\n"
                  "===========================================================================\n"));
   /* Create Unique list of Volumes using prompt list */
   start_prompt(ua, "");
   if (*rx.JobIds == 0) {
      /* Print Volumes in any order */
      display_vol_info(ua, rx, 0);
   } else {
      /* Ensure that the volumes are printed in JobId order */
      for (p=rx.JobIds; get_next_jobid_from_list(&p, &JobId) > 0; ) {
         display_vol_info(ua, rx, JobId);
      }
   }
   for (int i=0; i < ua->num_prompts; i++) {
      ua->send_msg("   %s\n", ua->prompt[i]);
      free(ua->prompt[i]);
      if (ua->unique[i]) free(ua->unique[i]);
   }
   if (ua->num_prompts == 0) {
      ua->send_msg(_("No Volumes found to restore.\n"));
   } else {
      ua->send_msg(_("\nVolumes marked with \"*\" are in the Autochanger.\n"));
   }
   ua->num_prompts = 0;
   ua->send_msg("\n");

   return;
}

/*
 * Write bsr data for a single bsr record
 */
static uint32_t write_bsr_item(RBSR *bsr, UAContext *ua,
                   RESTORE_CTX &rx, FILE *fd, bool &first, uint32_t &LastIndex)
{
   char ed1[50], ed2[50];
   uint32_t count = 0;
   uint32_t total_count = 0;
   char device[MAX_NAME_LENGTH];

   /*
    * For a given volume, loop over all the JobMedia records.
    *   VolCount is the number of JobMedia records.
    */
   for (int i=0; i < bsr->VolCount; i++) {
      if (!is_volume_selected(bsr->fi_list, bsr->VolParams[i].FirstIndex,
           bsr->VolParams[i].LastIndex)) {
         bsr->VolParams[i].VolumeName[0] = 0;  /* zap VolumeName */
         continue;
      }
      if (!rx.store) {
         find_storage_resource(ua, rx, bsr->VolParams[i].Storage,
                                       bsr->VolParams[i].MediaType);
      }
      fprintf(fd, "Storage=\"%s\"\n", bsr->VolParams[i].Storage);
      fprintf(fd, "Volume=\"%s\"\n", bsr->VolParams[i].VolumeName);
      fprintf(fd, "MediaType=\"%s\"\n", bsr->VolParams[i].MediaType);
      if (bsr->fileregex) {
         fprintf(fd, "FileRegex=%s\n", bsr->fileregex);
      }
      if (get_storage_device(device, bsr->VolParams[i].Storage)) {
         fprintf(fd, "Device=\"%s\"\n", device);
      }
      if (bsr->VolParams[i].Slot > 0) {
         fprintf(fd, "Slot=%d\n", bsr->VolParams[i].Slot);
      }
      fprintf(fd, "VolSessionId=%u\n", bsr->VolSessionId);
      fprintf(fd, "VolSessionTime=%u\n", bsr->VolSessionTime);
      fprintf(fd, "VolAddr=%s-%s\n", edit_uint64(bsr->VolParams[i].StartAddr, ed1),
              edit_uint64(bsr->VolParams[i].EndAddr, ed2));
      Dmsg2(100, "bsr VolParam FI=%u LI=%u\n",
            bsr->VolParams[i].FirstIndex, bsr->VolParams[i].LastIndex);

      count = write_findex(bsr->fi_list, bsr->VolParams[i].FirstIndex,
                           bsr->VolParams[i].LastIndex, fd);
      if (count) {
         fprintf(fd, "Count=%u\n", count);
      }
      total_count += count;
      /* If the same file is present on two tapes or in two files
       *   on a tape, it is a continuation, and should not be treated
       *   twice in the totals.
       */
      if (!first && LastIndex == bsr->VolParams[i].FirstIndex) {
         total_count--;
      }
      first = false;
      LastIndex = bsr->VolParams[i].LastIndex;
   }
   return total_count;
}


/*
 * Here we actually write out the details of the bsr file.
 *  Note, there is one bsr for each JobId, but the bsr may
 *  have multiple volumes, which have been entered in the
 *  order they were written.
 * The bsrs must be written out in the order the JobIds
 *  are found in the jobid list.
 */
static uint32_t write_bsr(UAContext *ua, RESTORE_CTX &rx, FILE *fd)
{
   bool first = true;
   uint32_t LastIndex = 0;
   uint32_t total_count = 0;
   char *p;
   JobId_t JobId;
   RBSR *bsr;
   if (*rx.JobIds == 0) {
      foreach_rblist(bsr, rx.bsr_list) {
         total_count += write_bsr_item(bsr, ua, rx, fd, first, LastIndex);
      }
      return total_count;
   }
   for (p=rx.JobIds; get_next_jobid_from_list(&p, &JobId) > 0; ) {
      foreach_rblist(bsr, rx.bsr_list) {
         if (JobId == bsr->JobId) {
            total_count += write_bsr_item(bsr, ua, rx, fd, first, LastIndex);
         }
      }
   }
   return total_count;
}

void print_bsr(UAContext *ua, RESTORE_CTX &rx)
{
   write_bsr(ua, rx, stdout);
}

static int search_rbsr(void *elt1, void *elt2)
{
   RBSR *bsr1 = (RBSR *)elt1;
   RBSR *bsr = (RBSR *)elt2;

   /* We might replace by a simple JobId - JobId */
   if (bsr->JobId == bsr1->JobId) {
      return 0;

   } else if (bsr->JobId < bsr1->JobId) {
      return 1;
   }

   return -1;
}

static int search_fi(void *elt1, void *elt2)
{
   RBSR_FINDEX *f1 = (RBSR_FINDEX *) elt1;
   RBSR_FINDEX *f2 = (RBSR_FINDEX *) elt2;

   if (f1->findex == (f2->findex - 1)) {
      return 0;

   } else if (f1->findex2 == (f2->findex2 + 1)) {
      return 0;

   } else if (f1->findex >= f2->findex && f1->findex2 <= f2->findex2) {
      return 0;
   }

   return (f1->findex > f2->findex) ? 1 : -1;
}

rblist *create_bsr_list(uint32_t JobId, int findex, int findex2)
{
   RBSR *bsr = NULL;
   RBSR_FINDEX *fi = NULL;
   rblist *bsr_list = New(rblist(bsr, &bsr->link));

   bsr = new_bsr();
   bsr->JobId = JobId;

   bsr_list->insert(bsr, search_rbsr);

   fi = new_findex();
   fi->findex = findex;
   fi->findex2 = findex2;

   bsr->fi_list->insert(fi, search_fi);

   return bsr_list;
}

/*
 * Add a FileIndex to the list of BootStrap records.
 *  Here we are only dealing with JobId's and the FileIndexes
 *  associated with those JobIds.
 * We expect that JobId, FileIndex are sorted ascending.
 *
 * When doing restore from tree, FileIndex are not sorted, so it can
 * create gaps.
 */
void add_findex(rblist *bsr_list, uint32_t JobId, int32_t findex)
{
   RBSR *bsr, bsr2;
   RBSR_FINDEX *fi, *nfi;

   if (findex == 0) {
      return;                         /* probably a dummy directory */
   }
   if (findex < 0) findex = -findex;

   bsr2.JobId = JobId;
   /* Walk down list of bsrs until we find the JobId */
   bsr = (RBSR *)bsr_list->search(&bsr2, search_rbsr);

   /* The list is empty, or the JobId is not already in,
    * Must add new JobId
    */
   if (!bsr) {
      bsr = new_bsr();
      bsr->JobId = JobId;
      bsr_list->insert(bsr, search_rbsr);
   }

   if (bsr->m_fi) {
      fi = bsr->m_fi;

   } else {
      fi = bsr->m_fi = new_findex();
   }

   fi->findex  = findex;
   fi->findex2 = findex;

   Dmsg1(1000, "Trying to insert %ld\n", findex);
   /* try to insert our fi */
   nfi = (RBSR_FINDEX*) bsr->fi_list->insert((void *)fi, search_fi);

   /* We found an existing one, extend it */
   if (nfi != fi) {
      if (findex == (nfi->findex2 + 1)) {
         Dmsg2(1000, "Extend %ld-%ld\n", nfi->findex, findex);
         nfi->findex2 = findex;

      } else if (findex == (nfi->findex - 1)) {
         Dmsg2(1000, "Extend %ld-%ld\n", findex, nfi->findex2);
         nfi->findex = findex;

      } else {
         Dmsg2(1000, "Found the same values? %ld-%ld\n", nfi->findex, nfi->findex2);
      }

   } else {
      Dmsg2(1000, "Inserted %ld-%ld\n", fi->findex, fi->findex2);
      bsr->m_fi = NULL;         /* comsumed */
   }
}

/*
 * Add all possible  FileIndexes to the list of BootStrap records.
 *  Here we are only dealing with JobId's and the FileIndexes
 *  associated with those JobIds.
 */
void add_findex_all(rblist *bsr_list, uint32_t JobId, const char *fileregex)
{
   RBSR *bsr, bsr2;
   RBSR_FINDEX *fi;

   bsr2.JobId = JobId;
   /* Walk down list of bsrs until we find the JobId */
   bsr = (RBSR *)bsr_list->search(&bsr2, search_rbsr);

   if (!bsr) {                    /* Must add new JobId */
      fi = new_findex();
      fi->findex = 1;
      fi->findex2 = INT32_MAX;

      bsr = new_bsr();
      bsr->JobId = JobId;
      bsr->fi_list->insert(fi, search_fi);
      bsr_list->insert(bsr, search_rbsr);

      if (fileregex) {
         /* If we use regexp to restore, set it for each jobid */
         bsr->fileregex = bstrdup(fileregex);
      }
      return;
   }

   /*
    * At this point, bsr points to bsr containing this JobId,
    */
   fi = new_findex();
   fi->findex = 1;
   fi->findex2 = INT32_MAX;
   bsr->fi_list->insert(fi, search_fi);
   return;
}

#ifdef needed
/* Foreach files in currrent list, send "/path/fname\0LStat\0MD5\0Delta" to FD
 *      row[0]=Path, row[1]=Filename, row[2]=FileIndex
 *      row[3]=JobId row[4]=LStat row[5]=DeltaSeq row[6]=MD5
 */
static int sendit(void *arg, int num_fields, char **row)
{
   JCR *jcr = (JCR *)arg;

   if (job_canceled(jcr)) {
      return 1;
   }

   if (row[2][0] == '0') {           /* discard when file_index == 0 */
      return 0;
   }

   /* sending with checksum */
   if (num_fields == 7
       && row[6][0] /* skip checksum = '0' */
       && row[6][1])
   {
      jcr->file_bsock->fsend("%s%s%c%s%c%s%c%s",
                             row[0], row[1], 0, row[4], 0, row[6], 0, row[5]);
   } else {
      jcr->file_bsock->fsend("%s%s%c%s%c%c%s",
                             row[0], row[1], 0, row[4], 0, 0, row[5]);
   }
   return 0;
}
#endif

/* We list all files for a given FI structure */
static void scan_findex(JCR *jcr, RBSR *bsr,
                        int32_t FirstIndex, int32_t LastIndex,
                        int32_t &lastFileIndex, uint32_t &lastJobId)
{
   RBSR_FINDEX *fi;
   FILE_DBR fdbr;
   memset(&fdbr, 0, sizeof(fdbr));

   fi = (RBSR_FINDEX *) bsr->fi_list->first();
   while (fi) {
      int32_t findex, findex2;

      /* fi points to the first item of the list, or the next item that is not
       * contigous to the previous group
       */
      findex = fi->findex;
      findex2 = fi->findex2;

      /* Sometime (with the restore command for example), the fi_list can
       * contain false gaps (1-10, 11-11, 12-20 instead of 1-20). The for loop
       * is here to merge blocks and reduce the bsr output. The next while(fi)
       * iteration will use the next_fi that points to the last merged element.
       */
      RBSR_FINDEX *next_fi;
      for (next_fi = (RBSR_FINDEX*) bsr->fi_list->next(fi);
           next_fi && next_fi->findex == (findex2+1);
           next_fi = (RBSR_FINDEX *) bsr->fi_list->next(next_fi))
      {
         findex2 = next_fi->findex2;
      }

      /* next_fi points after the current block (or to the end of the list), so
       * the next while() iteration will use the next value
       */
      fi = next_fi;

      /* We look if the current FI block match the volume information */
      if ((findex >= FirstIndex && findex <= LastIndex) ||
          (findex2 >= FirstIndex && findex2 <= LastIndex) ||
          (findex < FirstIndex && findex2 > LastIndex)) {

         findex = findex < FirstIndex ? FirstIndex : findex;
         findex2 = findex2 > LastIndex ? LastIndex : findex2;

         bool dolist=false;
         /* Display only new files */
         if (findex != lastFileIndex || bsr->JobId != lastJobId) {
            /* Not the same file, or not the same job */
            fdbr.FileIndex = findex;
            //dolist = true;

         } else if (findex2 != lastFileIndex) {
            /* We are in the same job, and the first index was already generated */
            fdbr.FileIndex = findex + 1;
            //dolist = true;
         }

         /* Keep the current values for the next loop */
         lastJobId = bsr->JobId;
         lastFileIndex = findex2;

         /* Generate if needed the list of files */
         if (dolist) {
            fdbr.FileIndex2 = findex2;
            fdbr.JobId = bsr->JobId;
            /* New code not working */
            //db_list_files(jcr, jcr->db, &fdbr, sendit, jcr);
         }
      }
   }
}

/*
 * Scan bsr data for a single bsr record
 */
static void scan_bsr_item(JCR *jcr, RBSR *bsr)
{
   int32_t lastFileIndex=0;
   uint32_t lastJobId=0;
   /*
    * For a given volume, loop over all the JobMedia records.
    *   VolCount is the number of JobMedia records.
    */
   for (int i=0; i < bsr->VolCount; i++) {
      if (!is_volume_selected(bsr->fi_list,
                              bsr->VolParams[i].FirstIndex,
                              bsr->VolParams[i].LastIndex))
      {
         continue;
      }

      scan_findex(jcr, bsr,
                  bsr->VolParams[i].FirstIndex,
                  bsr->VolParams[i].LastIndex,
                  lastFileIndex, lastJobId);
   }
}

/*
 * We need to find all files from the BSR. All files are listed, this is used
 * to send the list of the files to be restored to a plugin for example.
 */
void scan_bsr(JCR *jcr)
{
   char *p;
   JobId_t JobId;
   RBSR *bsr;
   if (!jcr->JobIds || *jcr->JobIds == 0) {
      foreach_rblist(bsr, jcr->bsr_list) {
         scan_bsr_item(jcr, bsr);
      }
      return;
   }
   for (p=jcr->JobIds; get_next_jobid_from_list(&p, &JobId) > 0; ) {
      foreach_rblist(bsr, jcr->bsr_list) {
         if (JobId == bsr->JobId) {
            scan_bsr_item(jcr, bsr);
         }
      }
   }
   return;
}
