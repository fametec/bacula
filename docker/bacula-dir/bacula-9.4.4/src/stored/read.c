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
 * Read code for Storage daemon
 *
 *     Kern Sibbald, November MM
 *
 */

#include "bacula.h"
#include "stored.h"

/* Forward referenced subroutines */
static bool read_record_cb(DCR *dcr, DEV_RECORD *rec);
static bool mac_record_cb(DCR *dcr, DEV_RECORD *rec);

/* Responses sent to the File daemon */
static char OK_data[]    = "3000 OK data\n";
static char FD_error[]   = "3000 error\n";
static char rec_header[] = "rechdr %ld %ld %ld %ld %ld";

/*
 *  Read Data and send to File Daemon
 *   Returns: false on failure
 *            true  on success
 */
bool do_read_data(JCR *jcr)
{
   BSOCK *fd = jcr->file_bsock;
   bool ok = true;
   DCR *dcr = jcr->read_dcr;
   char ec[50];

   Dmsg0(100, "Start read data.\n");

   if (!fd->set_buffer_size(dcr->device->max_network_buffer_size, BNET_SETBUF_WRITE)) {
      return false;
   }

   if (jcr->NumReadVolumes == 0) {
      Jmsg(jcr, M_FATAL, 0, _("No Volume names found for restore.\n"));
      fd->fsend(FD_error);
      return false;
   }

   Dmsg2(200, "Found %d volumes names to restore. First=%s\n", jcr->NumReadVolumes,
      jcr->VolList->VolumeName);

   /* Ready device for reading */
   if (!acquire_device_for_read(dcr)) {
      fd->fsend(FD_error);
      return false;
   }
   dcr->dev->start_of_job(dcr);

   /* Tell File daemon we will send data */
   if (!jcr->is_ok_data_sent) {
      fd->fsend(OK_data);
      jcr->is_ok_data_sent = true;
   }

   jcr->sendJobStatus(JS_Running);
   jcr->run_time = time(NULL);
   jcr->JobFiles = 0;

   if (jcr->is_JobType(JT_MIGRATE) || jcr->is_JobType(JT_COPY)) {
      ok = read_records(dcr, mac_record_cb, mount_next_read_volume);
   } else {
      ok = read_records(dcr, read_record_cb, mount_next_read_volume);
   }

   /*
    * Don't use time_t for job_elapsed as time_t can be 32 or 64 bits,
    *   and the subsequent Jmsg() editing will break
    */
   int32_t job_elapsed = time(NULL) - jcr->run_time;

   if (job_elapsed <= 0) {
      job_elapsed = 1;
   }

   Jmsg(dcr->jcr, M_INFO, 0, _("Elapsed time=%02d:%02d:%02d, Transfer rate=%s Bytes/second\n"),
         job_elapsed / 3600, job_elapsed % 3600 / 60, job_elapsed % 60,
         edit_uint64_with_suffix(jcr->JobBytes / job_elapsed, ec));

   /* Send end of data to FD */
   fd->signal(BNET_EOD);

   if (!release_device(jcr->read_dcr)) {
      ok = false;
   }

   Dmsg0(30, "Done reading.\n");
   return ok;
}

static bool read_record_cb(DCR *dcr, DEV_RECORD *rec)
{
   JCR *jcr = dcr->jcr;
   BSOCK *fd = jcr->file_bsock;
   bool ok = true;
   POOLMEM *save_msg;
   char ec1[50], ec2[50];
   POOLMEM *wbuf = rec->data;                 /* send buffer */
   uint32_t wsize = rec->data_len;            /* send size */

   if (rec->FileIndex < 0) {
      return true;
   }

   Dmsg5(400, "Send to FD: SessId=%u SessTim=%u FI=%s Strm=%s, len=%d\n",
      rec->VolSessionId, rec->VolSessionTime,
      FI_to_ascii(ec1, rec->FileIndex),
      stream_to_ascii(ec2, rec->Stream, rec->FileIndex),
      wsize);

   Dmsg2(640, ">filed: send header stream=0x%lx len=%ld\n", rec->Stream, wsize);
   /* Send record header to File daemon */
   if (!fd->fsend(rec_header, rec->VolSessionId, rec->VolSessionTime,
          rec->FileIndex, rec->Stream, wsize)) {
      Pmsg1(000, _(">filed: Error Hdr=%s\n"), fd->msg);
      Jmsg1(jcr, M_FATAL, 0, _("Error sending header to Client. ERR=%s\n"),
         fd->bstrerror());
      return false;
   }
   /*
    * For normal migration jobs, FileIndex values are sequential because
    *  we are dealing with one job.  However, for Vbackup (consolidation),
    *  we will be getting records from multiple jobs and writing them back
    *  out, so we need to ensure that the output FileIndex is sequential.
    *  We do so by detecting a FileIndex change and incrementing the
    *  JobFiles, which we then use as the output FileIndex.
    */
   if (rec->FileIndex >= 0) {
      /* If something changed, increment FileIndex */
      if (rec->VolSessionId != rec->last_VolSessionId ||
          rec->VolSessionTime != rec->last_VolSessionTime ||
          rec->FileIndex != rec->last_FileIndex) {
         jcr->JobFiles++;
         rec->last_VolSessionId = rec->VolSessionId;
         rec->last_VolSessionTime = rec->VolSessionTime;
         rec->last_FileIndex = rec->FileIndex;
      }
   }

   /* Debug code: check if we must hangup or blowup */
   if (handle_hangup_blowup(jcr, jcr->JobFiles, jcr->JobBytes)) {
      return false;
   }

   save_msg = fd->msg;          /* save fd message pointer */
   fd->msg = wbuf;
   fd->msglen = wsize;
   /* Send data record to File daemon */
   jcr->JobBytes += wsize;   /* increment bytes this job */
   Dmsg1(640, ">filed: send %d bytes data.\n", fd->msglen);
   if (!fd->send()) {
      Pmsg1(000, _("Error sending to FD. ERR=%s\n"), fd->bstrerror());
      Jmsg1(jcr, M_FATAL, 0, _("Error sending data to Client. ERR=%s\n"),
         fd->bstrerror());
      ok = false;
   }
   fd->msg = save_msg;
   return ok;
}

/*
 * New routine after to SD->SD implementation
 * Called here for each record from read_records()
 *  Returns: true if OK
 *           false if error
 */
static bool mac_record_cb(DCR *dcr, DEV_RECORD *rec)
{
   JCR *jcr = dcr->jcr;
   BSOCK *fd = jcr->file_bsock;
   char buf1[100], buf2[100];
   bool new_header = false;
   POOLMEM *save_msg;
   char ec1[50], ec2[50];
   bool ok = true;
   POOLMEM *wbuf = rec->data;;                 /* send buffer */
   uint32_t wsize = rec->data_len;             /* send size */

#ifdef xxx
   Pmsg5(000, "on entry     JobId=%d FI=%s SessId=%d Strm=%s len=%d\n",
      jcr->JobId,
      FI_to_ascii(buf1, rec->FileIndex), rec->VolSessionId,
      stream_to_ascii(buf2, rec->Stream, rec->FileIndex), rec->data_len);
#endif

   /* If label and not for us, discard it */
   if (rec->FileIndex < 0) {
      Dmsg1(100, "FileIndex=%d\n", rec->FileIndex);
      return true;
   }

   /*
    * For normal migration jobs, FileIndex values are sequential because
    *  we are dealing with one job.  However, for Vbackup (consolidation),
    *  we will be getting records from multiple jobs and writing them back
    *  out, so we need to ensure that the output FileIndex is sequential.
    *  We do so by detecting a FileIndex change and incrementing the
    *  JobFiles, which we then use as the output FileIndex.
    */
   if (rec->FileIndex >= 0) {
      /* If something changed, increment FileIndex */
      if (rec->VolSessionId != rec->last_VolSessionId ||
          rec->VolSessionTime != rec->last_VolSessionTime ||
          rec->FileIndex != rec->last_FileIndex ||
          rec->Stream != rec->last_Stream) {

         /* Something changed */
         if (rec->last_VolSessionId != 0) {        /* Not first record */
            Dmsg1(200, "Send EOD jobfiles=%d\n", jcr->JobFiles);
            if (!fd->signal(BNET_EOD)) {  /* End of previous stream */
               Jmsg(jcr, M_FATAL, 0, _("Error sending to File daemon. ERR=%s\n"),
                        fd->bstrerror());
               return false;
            }
         }
         new_header = true;
         if (rec->FileIndex != rec->last_FileIndex) {
            jcr->JobFiles++;
         }
         rec->last_VolSessionId = rec->VolSessionId;
         rec->last_VolSessionTime = rec->VolSessionTime;
         rec->last_FileIndex = rec->FileIndex;
         rec->last_Stream = rec->Stream;
      }
      rec->FileIndex = jcr->JobFiles;     /* set sequential output FileIndex */
   }

   if (new_header) {
      new_header = false;
      Dmsg5(400, "Send header to FD: SessId=%u SessTim=%u FI=%s Strm=%s, len=%ld\n",
         rec->VolSessionId, rec->VolSessionTime,
         FI_to_ascii(ec1, rec->FileIndex),
         stream_to_ascii(ec2, rec->Stream, rec->FileIndex),
         wsize);

      /* Send data header to File daemon */
      if (!fd->fsend("%ld %ld %ld", rec->FileIndex, rec->Stream, wsize)) {
         Pmsg1(000, _(">filed: Error Hdr=%s\n"), fd->msg);
         Jmsg1(jcr, M_FATAL, 0, _("Error sending to File daemon. ERR=%s\n"),
            fd->bstrerror());
         return false;
      }
   }

   Dmsg1(400, "FI=%d\n", rec->FileIndex);
   /* Send data record to File daemon */
   save_msg = fd->msg;          /* save fd message pointer */
   fd->msg = wbuf;         /* pass data directly to the FD */
   fd->msglen = wsize;
   jcr->JobBytes += wsize;   /* increment bytes this job */
   Dmsg1(400, ">filed: send %d bytes data.\n", fd->msglen);
   if (!fd->send()) {
      Pmsg1(000, _("Error sending to FD. ERR=%s\n"), fd->bstrerror());
      Jmsg1(jcr, M_FATAL, 0, _("Error sending to File daemon. ERR=%s\n"),
         fd->bstrerror());
      ok = false;
   }
   fd->msg = save_msg;                /* restore fd message pointer */

   Dmsg5(500, "wrote_record JobId=%d FI=%s SessId=%d Strm=%s len=%d\n",
      jcr->JobId,
      FI_to_ascii(buf1, rec->FileIndex), rec->VolSessionId,
      stream_to_ascii(buf2, rec->Stream, rec->FileIndex), rec->data_len);

   return ok;
}
