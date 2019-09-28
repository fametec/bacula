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
 *   Bacula authentication. Provides authentication with
 *     File and Storage daemons.
 *
 *     Nicolas Boichat, August MMIV
 *
 *    This routine runs as a thread and must be thread reentrant.
 *
 *  Basic tasks done here:
 *
 */

#include "tray-monitor.h"

/* Commands sent to Director */
static char DIRhello[]    = "Hello %s calling\n";

static char SDhello[] = "Hello SD: Bacula Director %s calling\n";

/* Response from Director */
static char DIROKhello[]   = "1000 OK:";

/* Commands sent to File daemon and received
 *  from the User Agent */
static char FDhello[]    = "Hello Director %s calling\n";

/* Response from SD */
static char SDOKhello[]   = "3000 OK Hello";
/* Response from FD */
static char FDOKhello[] = "2000 OK Hello";

/* Forward referenced functions */

int authenticate_daemon(JCR *jcr, MONITOR *mon, RESMON *res)
{
   BSOCK *bs = res->bs;
   int tls_local_need = BNET_TLS_NONE;
   int tls_remote_need = BNET_TLS_NONE;
   int compatible = true;
   char bashed_name[MAX_NAME_LENGTH];
   char *password, *p;
   int ret = 0;

   bstrncpy(bashed_name, mon->hdr.name, sizeof(bashed_name));
   bash_spaces(bashed_name);
   password = res->password;

   /* TLS Requirement */
   if (res->tls_enable) {
      tls_local_need = BNET_TLS_REQUIRED;
   }

   /* Timeout Hello after 5 mins */
   btimer_t *tid = start_bsock_timer(bs, 60 * 5);
   if (res->type == R_DIRECTOR) {
      p = DIRhello;
   } else if (res->type == R_STORAGE) {
      p = SDhello;
   } else {
      p = FDhello;
   }

   bs->fsend(p, bashed_name);

   if (!cram_md5_respond(bs, password, &tls_remote_need, &compatible) ||
       !cram_md5_challenge(bs, password, tls_local_need, compatible)) {
      Jmsg(jcr, M_FATAL, 0, _("Authorization problem.\n"
                              "Most likely the passwords do not agree.\n"
                              "For help, please see " MANUAL_AUTH_URL "\n"));
      goto bail_out;
   }

   /* Verify that the remote host is willing to meet our TLS requirements */
   if (tls_remote_need < tls_local_need && tls_local_need != BNET_TLS_OK && tls_remote_need != BNET_TLS_OK) {
      Jmsg(jcr, M_FATAL, 0, _("Authorization problem:"
                            " Remote server did not advertise required TLS support.\n"));
      goto bail_out;
   }

   /* Verify that we are willing to meet the remote host's requirements */
   if (tls_remote_need > tls_local_need && tls_local_need != BNET_TLS_OK && tls_remote_need != BNET_TLS_OK) {
      Jmsg(jcr, M_FATAL, 0, ("Authorization problem:"
                             " Remote server requires TLS.\n"));
      goto bail_out;
   }

   /* Is TLS Enabled? */
   if (tls_local_need >= BNET_TLS_OK && tls_remote_need >= BNET_TLS_OK) {
      /* Engage TLS! Full Speed Ahead! */
      if (!bnet_tls_client(res->tls_ctx, bs, NULL)) {
         Jmsg(jcr, M_FATAL, 0, _("TLS negotiation failed\n"));
         goto bail_out;
      }
   }

   Dmsg1(6, "> %s", bs->msg);
   if (bs->recv() <= 0) {
      Jmsg1(jcr, M_FATAL, 0, _("Bad response to Hello command: ERR=%s\n"),
         bs->bstrerror());
      goto bail_out;
   }
   Dmsg1(10, "< %s", bs->msg);
   switch(res->type) {
   case R_DIRECTOR:
      p = DIROKhello;
      break;
   case R_CLIENT:
      p = FDOKhello;
      break;
   case R_STORAGE:
      p = SDOKhello;
      break;
   }
   if (strncmp(bs->msg, p, strlen(p)) != 0) {
      Jmsg(jcr, M_FATAL, 0, _("Daemon rejected Hello command\n"));
      goto bail_out;
   } else {
      //Jmsg0(jcr, M_INFO, 0, dir->msg);
   }
   ret = 1;
bail_out:
   if (tid) {
      stop_bsock_timer(tid);
   }
   return ret;
}
