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
 * Hello routines for Storage daemon.
 *
 * This file contains all the code relating to reading and writing of
 *  all Hello commands between the daemons.
 *
 *   Written by Kern Sibbald, June 2014
 *
 */


#include "bacula.h"
#include "stored.h"

extern STORES *me;               /* our Global resource */

const int dbglvl = 50;

/*
 * SD_VERSION history
 * Note: Enterprise versions now numbered in 30000
 *       and community is at SD version 3
 *     None prior to 06Aug13
 *      1         - Skipped
 *      2          - Skipped
 *      3 22Feb14 - Added SD->SD with SD_Calls_Client
 *      4 22Jun14 - Skipped
 *    305 04Jun15 - Added JobMedia queueing
 *    306 20Mar15 - Added comm line compression
 */

#define SD_VERSION 306     /* Community SD version */
#define FD_VERSION 214     /* Community FD version */

static char hello_sd[]  = "Hello Bacula SD: Start Job %s %d %d\n";

static char Sorry[]     = "3999 No go\n";
static char OK_hello[]  = "3000 OK Hello %d\n";


/*********************************************************************
 *
 *  Validate hello from the Director.
 *
 * Returns: true  if Hello is good.
 *          false if Hello is bad.
 */
bool validate_dir_hello(JCR* jcr)
{
   POOLMEM *dirname;
   DIRRES *director = NULL;
   int dir_version = 0;
   BSOCK *dir = jcr->dir_bsock;

   if (dir->msglen < 25 || dir->msglen > 500) {
      Dmsg2(dbglvl, "Bad Hello command from Director at %s. Len=%d.\n",
            dir->who(), dir->msglen);
      Qmsg2(jcr, M_SECURITY, 0, _("Bad Hello command from Director at %s. Len=%d.\n"),
            dir->who(), dir->msglen);
      sleep(5);
      return false;
   }
   dirname = get_pool_memory(PM_MESSAGE);
   dirname = check_pool_memory_size(dirname, dir->msglen);

   if (sscanf(dir->msg, "Hello SD: Bacula Director %127s calling %d",
          dirname, &dir_version) != 2 &&
       sscanf(dir->msg, "Hello SD: Bacula Director %127s calling",
          dirname) != 1) {
      dir->msg[100] = 0;
      Dmsg2(dbglvl, "Bad Hello command from Director at %s: %s\n",
            dir->who(), dir->msg);
      Qmsg2(jcr, M_SECURITY, 0, _("Bad Hello command from Director at %s: %s\n"),
            dir->who(), dir->msg);
      free_pool_memory(dirname);
      sleep(5);
      return false;
   }

   if (dir_version >= 1 && me->comm_compression) {
      dir->set_compress();
   } else {
      dir->clear_compress();
      Dmsg0(050, "**** No SD compression to Dir\n");
   }
   director = NULL;
   unbash_spaces(dirname);
   foreach_res(director, R_DIRECTOR) {
      if (strcasecmp(director->hdr.name, dirname) == 0) {
         break;
      }
   }
   if (!director) {
      Dmsg2(dbglvl, "Connection from unknown Director %s at %s rejected.\n",
            dirname, dir->who());
      Qmsg2(jcr, M_SECURITY, 0, _("Connection from unknown Director %s at %s rejected.\n"
            "Please see " MANUAL_AUTH_URL " for help.\n"),
            dirname, dir->who());
      free_pool_memory(dirname);
      sleep(5);
      return false;
   }
   jcr->director = director;
   free_pool_memory(dirname);
   return true;
}

/*
 * After receiving a connection (in dircmd.c) if it is
 *   from the File daemon, this routine is called.
 */
void handle_client_connection(BSOCK *fd)
{
   JCR *jcr;
   int fd_version = 0;
   int sd_version = 0;
   char job_name[500];
   /*
    * Do a sanity check on the message received
    */
   if (fd->msglen < 25 || fd->msglen > (int)sizeof(job_name)) {
      Pmsg1(000, "<filed: %s", fd->msg);
      Qmsg2(NULL, M_SECURITY, 0, _("Invalid connection from %s. Len=%d\n"), fd->who(), fd->msglen);
      bmicrosleep(5, 0);   /* make user wait 5 seconds */
      fd->destroy();
      return;
   }

   Dmsg1(100, "Conn: %s", fd->msg);
   /*
    * See if this is a File daemon connection. If so
    *   call FD handler.
    */
   if (sscanf(fd->msg, "Hello Bacula SD: Start Job %127s %d %d", job_name, &fd_version, &sd_version) != 3 &&
       sscanf(fd->msg, "Hello FD: Bacula Storage calling Start Job %127s %d", job_name, &sd_version) != 2 &&
       sscanf(fd->msg, "Hello Start Job %127s", job_name) != 1) {
      Qmsg2(NULL, M_SECURITY, 0, _("Invalid Hello from %s. Len=%d\n"), fd->who(), fd->msglen);
      sleep(5);
      fd->destroy();
      return;
   }

   if (!(jcr=get_jcr_by_full_name(job_name))) {
      Qmsg1(NULL, M_SECURITY, 0, _("Client connect failed: Job name not found: %s\n"), job_name);
      Dmsg1(3, "**** Job \"%s\" not found.\n", job_name);
      sleep(5);
      fd->destroy();
      return;
   }

   /* After this point, we can use bail_out */
   Dmsg1(100, "Found Client Job %s\n", job_name);
   if (jcr->authenticated) {
      Jmsg3(jcr, M_SECURITY, 0, _("A Client \"%s\" tried to authenticate for Job %s, "
                                 "but the Job is already authenticated with \"%s\".\n"),
            fd->who(), jcr->Job, jcr->file_bsock?jcr->file_bsock->who():"N/A");
      Dmsg2(050, "Hey!!!! JobId %u Job %s already authenticated.\n",
         (uint32_t)jcr->JobId, jcr->Job);
      goto bail_out;
   }

   fd->set_jcr(jcr);
   Dmsg2(050, "fd_version=%d sd_version=%d\n", fd_version, sd_version);

   /* Turn on compression for newer FDs */
   if (fd_version >= 214 || sd_version >= 306) {
      fd->set_compress();             /* set compression allowed */
   } else {
      fd->clear_compress();
      Dmsg0(050, "*** No SD compression to FD\n");
   }

   /*
    * Authenticate the Client (FD or SD)
    */
   jcr->lock_auth();     /* Ensure that only one thread is dealing with auth */
   if (jcr->authenticated) {
      Jmsg2(jcr, M_SECURITY, 0, _("A Client \"%s\" tried to authenticate for Job %s, "
                                 "but the job is already authenticated.\n"),
            fd->who(), jcr->Job);

   } else if (!authenticate_filed(jcr, fd, fd_version)) {
      Dmsg1(50, "Authentication failed Job %s\n", jcr->Job);
      /* Job not yet started, we can cancel */
      Jmsg(jcr, M_SECURITY, 0, _("Unable to authenticate File daemon\n"));

   } else {
      Dmsg2(050, "OK Authentication jid=%u Job %s\n", (uint32_t)jcr->JobId, jcr->Job);
      jcr->file_bsock = fd;
      jcr->FDVersion = fd_version;
      jcr->SDVersion = sd_version;
      jcr->authenticated = true;

      if (sd_version > 0) {
         jcr->sd_client = true;
      }
   }
   jcr->unlock_auth();

   if (!jcr->authenticated) {
      jcr->setJobStatus(JS_ErrorTerminated);
   }

   Dmsg4(050, "=== Auth %s, unblock Job %s jid=%d sd_ver=%d\n",
         jcr->authenticated?"OK":"KO", job_name, jcr->JobId, sd_version);

bail_out:
   /* file_bsock might be NULL or a previous BSOCK */
   if (jcr->file_bsock != fd) {
      fd->destroy();
   }
   pthread_cond_signal(&jcr->job_start_wait); /* wake waiting job */
   free_jcr(jcr);
   if (!jcr->authenticated) {
      sleep(5);
   }
   return;
}


bool is_client_connection(BSOCK *bs)
{
   return
      sscanf(bs->msg, "Hello Bacula SD: Start Job ") == 0 ||
      sscanf(bs->msg, "Hello FD: Bacula Storage calling Start Job ") == 0 ||
      sscanf(bs->msg, "Hello Start Job ") == 0;
}

/*
 * If sd_calls_client, we must read the client's response to
 *   the hello we previously sent.
 */
bool read_client_hello(JCR *jcr)
{
   int i;
   int stat;
   int fd_version = 0;
   int sd_version = 0;
   BSOCK *cl = jcr->file_bsock;
   char job_name[500];

   /* We connected to Client, so finish work */
   if (!cl) {
      Jmsg0(jcr, M_FATAL, 0, _("Client socket not open. Could not connect to Client.\n"));
      Dmsg0(050, "Client socket not open. Could not connect to Client.\n");
      return false;
   }
   /* Get response to Hello command sent earlier */
   Dmsg0(050, "Read Hello command from Client\n");
   for (i=0; i<60; i++) {
      stat = cl->recv();
      if (stat <= 0) {
         bmicrosleep(1, 0);
      } else {
         break;
      }
   }
   if (stat <= 0) {
      berrno be;
      Jmsg1(jcr, M_FATAL, 0, _("Recv request to Client failed. ERR=%s\n"),
         be.bstrerror());
      Dmsg1(050, _("Recv request to Client failed. ERR=%s\n"), be.bstrerror());
      return false;
   }
   Dmsg1(050, ">filed: %s\n", cl->msg);
   if (sscanf(cl->msg, "Hello Bacula SD: Start Job %127s %d %d", job_name, &fd_version, &sd_version) != 3) {
      Jmsg1(jcr, M_FATAL, 0, _("Bad Hello from Client: %s.\n"), cl->msg);
      Dmsg1(050, _("Bad Hello from Client: %s.\n"), cl->msg);
      return false;
   }
   unbash_spaces(job_name);
   jcr->FDVersion = fd_version;
   jcr->SDVersion = sd_version;
   Dmsg1(050, "FDVersion=%d\n", fd_version);
   /* Turn on compression for newer FDs, except for Community version */
   if (jcr->FDVersion >= 214 && me->comm_compression) {
      cl->set_compress();             /* set compression allowed */
   } else {
      cl->clear_compress();
      Dmsg0(050, "*** No SD compression to FD\n");
   }

   return true;
}

/*
 * Send Hello OK to DIR or FD
 */
bool send_hello_ok(BSOCK *bs)
{
   return bs->fsend(OK_hello, SD_VERSION);
}

bool send_sorry(BSOCK *bs)
{
   return bs->fsend(Sorry);
}

/*
 * We are acting as a client, so send Hello to the SD.
 */
bool send_hello_sd(JCR *jcr, char *Job)
{
   bool rtn;
   BSOCK *sd = jcr->store_bsock;

   bash_spaces(Job);
   rtn = sd->fsend(hello_sd, Job, FD_VERSION, SD_VERSION);
   unbash_spaces(Job);
   Dmsg1(100, "Send to SD: %s\n", sd->msg);
   if (!rtn) {
      return false;
   }
   return true;
}

/*
 * We are SD so send Hello to client
 *  Note: later the Client will send us a Hello.
 */
bool send_hello_client(JCR *jcr, char *Job)
{
   bool rtn;
   BSOCK *cl = jcr->file_bsock;

   bash_spaces(Job);
   rtn = cl->fsend("Hello FD: Bacula Storage calling Start Job %s %d\n", Job, SD_VERSION);
   unbash_spaces(Job);
   if (!rtn) {
      return false;
   }
   return rtn;
}
