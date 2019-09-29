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
 * This file handles commands from the File daemon.
 *
 *   Written by Kern Sibbald, MM
 *
 * We get here because the Director has initiated a Job with
 *  the Storage daemon, then done the same with the File daemon,
 *  then when the Storage daemon receives a proper connection from
 *  the File daemon, control is passed here to handle the
 *  subsequent File daemon commands.
 */

#include "bacula.h"
#include "stored.h"

/* Forward referenced functions */
static bool response(JCR *jcr, BSOCK *bs, const char *resp, const char *cmd);

/* Imported variables */
extern STORES *me;

/* Static variables */
static char ferrmsg[]      = "3900 Invalid command\n";
static char OK_data[]      = "3000 OK data\n";

/* Imported functions */
extern bool do_append_data(JCR *jcr);
extern bool do_read_data(JCR *jcr);
extern bool do_backup_job(JCR *jcr);

/* Forward referenced FD commands */
static bool append_open_session(JCR *jcr);
static bool append_close_session(JCR *jcr);
static bool append_data_cmd(JCR *jcr);
static bool append_end_session(JCR *jcr);
static bool read_open_session(JCR *jcr);
static bool read_data_cmd(JCR *jcr);
static bool read_close_session(JCR *jcr);
static bool read_control_cmd(JCR *jcr);
static bool sd_testnetwork_cmd(JCR *jcr);

/* Exported function */
bool get_bootstrap_file(JCR *jcr, BSOCK *bs);

struct s_cmds {
   const char *cmd;
   bool (*func)(JCR *jcr);
};

/*
 * The following are the recognized commands from the File daemon
 */
static struct s_cmds fd_cmds[] = {
   {"append open",  append_open_session},
   {"append data",  append_data_cmd},
   {"append end",   append_end_session},
   {"append close", append_close_session},
   {"read open",    read_open_session},
   {"read data",    read_data_cmd},
   {"read close",   read_close_session},
   {"read control", read_control_cmd},
   {"testnetwork",  sd_testnetwork_cmd},
   {NULL,           NULL}                  /* list terminator */
};

/* Commands from the File daemon that require additional scanning */
static char read_open[]       = "read open session = %127s %ld %ld %ld %ld %ld %ld\n";

/* Responses sent to the File daemon */
static char NO_open[]         = "3901 Error session already open\n";
static char NOT_opened[]      = "3902 Error session not opened\n";
static char ERROR_open[]      = "3904 Error open session, bad parameters\n";
static char OK_end[]          = "3000 OK end\n";
static char OK_close[]        = "3000 OK close Status = %d\n";
static char OK_open[]         = "3000 OK open ticket = %d\n";
static char ERROR_append[]    = "3903 Error append data: %s\n";

/* Information sent to the Director */
static char Job_start[] = "3010 Job %s start\n";
char Job_end[] =
   "3099 Job %s end JobStatus=%d JobFiles=%d JobBytes=%s JobErrors=%u ErrMsg=%s\n";

/*
 * Run a Client Job -- Client already authorized
 *  Note: this can be either a backup or restore or
 *    migrate/copy job.
 *
 * Basic task here is:
 * - Read a command from the Client -- FD or SD
 * - Execute it
 *
 */
void run_job(JCR *jcr)
{
   BSOCK *dir = jcr->dir_bsock;
   char ec1[30];

   dir->set_jcr(jcr);
   Dmsg1(120, "Start run Job=%s\n", jcr->Job);
   dir->fsend(Job_start, jcr->Job);
   jcr->start_time = time(NULL);
   jcr->run_time = jcr->start_time;
   jcr->sendJobStatus(JS_Running);

   /* TODO: Remove when the new match_all is well tested */
   jcr->use_new_match_all = use_new_match_all;
   /*
    * A migrate or copy job does both a restore (read_data) and
    *   a backup (append_data).
    * Otherwise we do the commands that the client sends
    *   which are for normal backup or restore jobs.
    */
   Dmsg3(050, "==== JobType=%c run_job=%d sd_client=%d\n", jcr->getJobType(), jcr->JobId, jcr->sd_client);
   if (jcr->is_JobType(JT_BACKUP) && jcr->sd_client) {
      jcr->session_opened = true;
      Dmsg0(050, "Do: receive for 3000 OK data then append\n");
      if (!response(jcr, jcr->file_bsock, "3000 OK data\n", "Append data")) {
         Dmsg1(050, "Expect: 3000 OK data, got: %s", jcr->file_bsock->msg);
         Jmsg0(jcr, M_FATAL, 0, "Append data not accepted\n");
         goto bail_out;
      }
      append_data_cmd(jcr);
      append_end_session(jcr);
   } else if (jcr->is_JobType(JT_MIGRATE) || jcr->is_JobType(JT_COPY)) {
      jcr->session_opened = true;
      /*
       * Send "3000 OK data" now to avoid a dead lock, the other side is also
       * waiting for one. The old code was reading the "3000 OK" reply
       * at the end of the backup (not really appropriate).
       * dedup needs duplex communication with the other side and needs the
       * "3000 OK" to be read, which is handled here by the code below.
       */
      Dmsg0(215, "send OK_data\n");
      jcr->file_bsock->fsend(OK_data);
      jcr->is_ok_data_sent = true;
      Dmsg1(050, "Do: read_data_cmd file_bsock=%p\n", jcr->file_bsock);
      Dmsg0(050, "Do: receive for 3000 OK data then read\n");
      if (!response(jcr, jcr->file_bsock, "3000 OK data\n", "Data received")) {
         Dmsg1(050, "Expect 3000 OK data, got: %s", jcr->file_bsock->msg);
         Jmsg0(jcr, M_FATAL, 0, "Read data not accepted\n");
         jcr->file_bsock->signal(BNET_EOD);
         goto bail_out;
      }
      read_data_cmd(jcr);
      jcr->file_bsock->signal(BNET_EOD);
   } else {
      /* Either a Backup or Restore job */
      Dmsg0(050, "Do: do_client_commands\n");
      do_client_commands(jcr);
   }
bail_out:
   jcr->end_time = time(NULL);
   flush_jobmedia_queue(jcr);
   dequeue_messages(jcr);             /* send any queued messages */
   jcr->setJobStatus(JS_Terminated);
   generate_daemon_event(jcr, "JobEnd");
   generate_plugin_event(jcr, bsdEventJobEnd);
   bash_spaces(jcr->StatusErrMsg);
   dir->fsend(Job_end, jcr->Job, jcr->JobStatus, jcr->JobFiles,
              edit_uint64(jcr->JobBytes, ec1), jcr->JobErrors, jcr->StatusErrMsg);
   Dmsg1(100, "==== %s", dir->msg);
   unbash_spaces(jcr->StatusErrMsg);
   dequeue_daemon_messages(jcr);
   dir->signal(BNET_EOD);             /* send EOD to Director daemon */
   free_plugins(jcr);                 /* release instantiated plugins */
   garbage_collect_memory_pool();
   return;
}

/*
 * Now talk to the Client (FD/SD) and do what he says
 */
void do_client_commands(JCR *jcr)
{
   int i;
   bool found, quit;
   BSOCK *fd = jcr->file_bsock;

   if (!fd) {
      return;
   }
   fd->set_jcr(jcr);
   for (quit=false; !quit;) {
      int stat;

      /* Read command coming from the File daemon */
      stat = fd->recv();
      if (fd->is_stop()) {            /* hard eof or error */
         break;                       /* connection terminated */
      }
      if (stat <= 0) {
         continue;                    /* ignore signals and zero length msgs */
      }
      Dmsg1(110, "<filed: %s", fd->msg);
      found = false;
      for (i=0; fd_cmds[i].cmd; i++) {
         if (strncmp(fd_cmds[i].cmd, fd->msg, strlen(fd_cmds[i].cmd)) == 0) {
            found = true;               /* indicate command found */
            jcr->errmsg[0] = 0;
            if (!fd_cmds[i].func(jcr)) {    /* do command */
               /* Note fd->msg command may be destroyed by comm activity */
               if (!job_canceled(jcr)) {
                  strip_trailing_junk(fd->msg);
                  if (jcr->errmsg[0]) {
                     strip_trailing_junk(jcr->errmsg);
                     Jmsg2(jcr, M_FATAL, 0, _("Command error with FD msg=\"%s\", SD hanging up. ERR=%s\n"),
                           fd->msg, jcr->errmsg);
                  } else {
                     Jmsg1(jcr, M_FATAL, 0, _("Command error with FD msg=\"%s\", SD hanging up.\n"),
                        fd->msg);
                  }
                  jcr->setJobStatus(JS_ErrorTerminated);
               }
               quit = true;
            }
            break;
         }
      }
      if (!found) {                   /* command not found */
         if (!job_canceled(jcr)) {
            Jmsg1(jcr, M_FATAL, 0, _("FD command not found: %s\n"), fd->msg);
            Dmsg1(110, "<filed: Command not found: %s\n", fd->msg);
         }
         fd->fsend(ferrmsg);
         break;
      }
   }
   fd->signal(BNET_TERMINATE);        /* signal to FD job is done */
}

/*
 *   Append Data command
 *     Open Data Channel and receive Data for archiving
 *     Write the Data to the archive device
 */
static bool append_data_cmd(JCR *jcr)
{
   BSOCK *fd = jcr->file_bsock;

   Dmsg1(120, "Append data: %s", fd->msg);
   if (jcr->session_opened) {
      Dmsg1(110, "<bfiled: %s", fd->msg);
      jcr->setJobType(JT_BACKUP);
      jcr->errmsg[0] = 0;
      if (do_append_data(jcr)) {
         return true;
      } else {
         fd->suppress_error_messages(true); /* ignore errors at this point */
         fd->fsend(ERROR_append, jcr->errmsg);
      }
   } else {
      pm_strcpy(jcr->errmsg, _("Attempt to append on non-open session.\n"));
      fd->fsend(NOT_opened);
   }
   return false;
}

static bool append_end_session(JCR *jcr)
{
   BSOCK *fd = jcr->file_bsock;

   Dmsg1(120, "store<file: %s", fd->msg);
   if (!jcr->session_opened) {
      pm_strcpy(jcr->errmsg, _("Attempt to close non-open session.\n"));
      fd->fsend(NOT_opened);
      return false;
   }
   return fd->fsend(OK_end);
}

/*
 * Test the FD/SD connectivity
 */
static bool sd_testnetwork_cmd(JCR *jcr)
{
   BSOCK *fd = jcr->file_bsock;
   int64_t nb=0;
   bool can_compress, ok=true;

   if (sscanf(fd->msg, "testnetwork bytes=%lld", &nb) != 1) {
      return false;
   }
   /* We disable the comline compression for this test */
   can_compress = fd->can_compress();
   fd->clear_compress();

   /* First, get data from the FD */
   while (fd->recv() > 0) { }

   /* Then, send back data to the FD */
   memset(fd->msg, 0xBB, sizeof_pool_memory(fd->msg));
   fd->msglen = sizeof_pool_memory(fd->msg);

   while(nb > 0 && ok) {
      if (nb < fd->msglen) {
         fd->msglen = nb;
      }
      ok = fd->send();
      nb -= fd->msglen;
   }
   fd->signal(BNET_EOD);

   if (can_compress) {
      fd->set_compress();
   }
   return true;
}

/*
 * Append Open session command
 *
 */
static bool append_open_session(JCR *jcr)
{
   BSOCK *fd = jcr->file_bsock;

   Dmsg1(120, "Append open session: %s", fd->msg);
   if (jcr->session_opened) {
      pm_strcpy(jcr->errmsg, _("Attempt to open already open session.\n"));
      fd->fsend(NO_open);
      return false;
   }

   jcr->session_opened = true;

   /* Send "Ticket" to File Daemon */
   fd->fsend(OK_open, jcr->VolSessionId);
   Dmsg1(110, ">filed: %s", fd->msg);

   return true;
}

/*
 *   Append Close session command
 *      Close the append session and send back Statistics
 *         (need to fix statistics)
 */
static bool append_close_session(JCR *jcr)
{
   BSOCK *fd = jcr->file_bsock;

   Dmsg1(120, "<filed: %s", fd->msg);
   if (!jcr->session_opened) {
      pm_strcpy(jcr->errmsg, _("Attempt to close non-open session.\n"));
      fd->fsend(NOT_opened);
      return false;
   }
   /* Send final statistics to File daemon */
   fd->fsend(OK_close, jcr->JobStatus);
   Dmsg1(120, ">filed: %s", fd->msg);

   fd->signal(BNET_EOD);              /* send EOD to File daemon */

   jcr->session_opened = false;
   return true;
}

/*
 *   Read Data command
 *     Open Data Channel, read the data from
 *     the archive device and send to File
 *     daemon.
 */
static bool read_data_cmd(JCR *jcr)
{
   BSOCK *fd = jcr->file_bsock;

   Dmsg1(120, "Read data: %s", fd->msg);
   if (jcr->session_opened) {
      Dmsg1(120, "<bfiled: %s", fd->msg);
      return do_read_data(jcr);
   } else {
      pm_strcpy(jcr->errmsg, _("Attempt to read on non-open session.\n"));
      fd->fsend(NOT_opened);
      return false;
   }
}

/*
 * Read Open session command
 *
 *  We need to scan for the parameters of the job
 *    to be restored.
 */
static bool read_open_session(JCR *jcr)
{
   BSOCK *fd = jcr->file_bsock;

   Dmsg1(120, "%s", fd->msg);
   if (jcr->session_opened) {
      pm_strcpy(jcr->errmsg, _("Attempt to open an already open session.\n"));
      fd->fsend(NO_open);
      return false;
   }

   if (sscanf(fd->msg, read_open, jcr->read_dcr->VolumeName, &jcr->read_VolSessionId,
         &jcr->read_VolSessionTime, &jcr->read_StartFile, &jcr->read_EndFile,
         &jcr->read_StartBlock, &jcr->read_EndBlock) == 7) {
      Dmsg4(100, "read_open_session got: JobId=%d Vol=%s VolSessId=%ld VolSessT=%ld\n",
         jcr->JobId, jcr->read_dcr->VolumeName, jcr->read_VolSessionId,
         jcr->read_VolSessionTime);
      Dmsg4(100, "  StartF=%ld EndF=%ld StartB=%ld EndB=%ld\n",
         jcr->read_StartFile, jcr->read_EndFile, jcr->read_StartBlock,
         jcr->read_EndBlock);

   } else {
      pm_strcpy(jcr->errmsg, _("Cannot open session, received bad parameters.\n"));
      fd->fsend(ERROR_open);
      return false;
   }

   jcr->session_opened = true;
   jcr->setJobType(JT_RESTORE);

   /* Send "Ticket" to File Daemon */
   fd->fsend(OK_open, jcr->VolSessionId);
   Dmsg1(110, ">filed: %s", fd->msg);

   return true;
}

static bool read_control_cmd(JCR *jcr)
{
   BSOCK *fd = jcr->file_bsock;

   Dmsg1(120, "Read control: %s\n", fd->msg);
   if (!jcr->session_opened) {
      fd->fsend(NOT_opened);
      return false;
   }
   jcr->interactive_session = true;
   return true;
}

/*
 *   Read Close session command
 *      Close the read session
 */
static bool read_close_session(JCR *jcr)
{
   BSOCK *fd = jcr->file_bsock;

   Dmsg1(120, "Read close session: %s\n", fd->msg);
   if (!jcr->session_opened) {
      fd->fsend(NOT_opened);
      return false;
   }
   /* Send final close msg to File daemon */
   fd->fsend(OK_close, jcr->JobStatus);
   Dmsg1(160, ">filed: %s\n", fd->msg);

   fd->signal(BNET_EOD);            /* send EOD to File daemon */

   jcr->session_opened = false;
   return true;
}

/*
 * Get response from FD or SD
 * sent. Check that the response agrees with what we expect.
 *
 *  Returns: false on failure
 *           true  on success
 */
static bool response(JCR *jcr, BSOCK *bs, const char *resp, const char *cmd)
{
   int n;

   if (bs->is_error()) {
      return false;
   }
   if ((n = bs->recv()) >= 0) {
      if (strcmp(bs->msg, resp) == 0) {
         return true;
      }
      Jmsg(jcr, M_FATAL, 0, _("Bad response to %s command: wanted %s, got %s\n"),
            cmd, resp, bs->msg);
      return false;
   }
   Jmsg(jcr, M_FATAL, 0, _("Socket error on %s command: ERR=%s\n"),
         cmd, bs->bstrerror());
   return false;
}
