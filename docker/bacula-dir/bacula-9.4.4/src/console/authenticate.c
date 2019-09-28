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
 *   Bacula UA authentication. Provides authentication with
 *     the Director.
 *
 *     Kern Sibbald, June MMI
 *
 *    This routine runs as a thread and must be thread reentrant.
 *
 *  Basic tasks done here:
 *
 */

#include "bacula.h"
#include "console_conf.h"

/*
 * Version at end of Hello Enterprise:
 *   prior to 06Aug13 no version
 *   1 06Aug13 - added comm line compression
 * Community
 *   prior to 06Aug13 no version
 *   100  14Feb17 - added comm line compression
 */
#define UA_VERSION 100

void senditf(const char *fmt, ...);
void sendit(const char *buf);

/* Commands sent to Director */
static char hello[]    = "Hello %s calling %d\n";

/* Response from Director */
static char oldOKhello[]   = "1000 OK:";
static char newOKhello[]   = "1000 OK: %d";
static char FDOKhello[]    = "2000 OK Hello %d";

/* Forward referenced functions */

/*
 * Authenticate Director
 */
int authenticate_director(BSOCK *dir, DIRRES *director, CONRES *cons)
{
   int tls_local_need = BNET_TLS_NONE;
   int tls_remote_need = BNET_TLS_NONE;
   bool tls_authenticate;
   int compatible = true;
   int dir_version = 0;
   int fd_version = 0;
   char bashed_name[MAX_NAME_LENGTH];
   char *password;
   TLS_CONTEXT *tls_ctx = NULL;
            
   /*
    * Send my name to the Director then do authentication
    */
   if (cons) {
      bstrncpy(bashed_name, cons->hdr.name, sizeof(bashed_name));
      bash_spaces(bashed_name);
      password = cons->password;
      /* TLS Requirement */
      if (cons->tls_enable) {
         if (cons->tls_require) {
            tls_local_need = BNET_TLS_REQUIRED;
         } else {
            tls_local_need = BNET_TLS_OK;
         }
      }
      if (cons->tls_authenticate) {
         tls_local_need = BNET_TLS_REQUIRED;
      }
      tls_authenticate = cons->tls_authenticate;
      tls_ctx = cons->tls_ctx;
   } else {
      bstrncpy(bashed_name, "*UserAgent*", sizeof(bashed_name));
      password = director->password;
      /* TLS Requirement */
      if (director->tls_enable) {
         if (director->tls_require) {
            tls_local_need = BNET_TLS_REQUIRED;
         } else {
            tls_local_need = BNET_TLS_OK;
         }
      }

      if (director->tls_authenticate) {
         tls_local_need = BNET_TLS_REQUIRED;
      }
      tls_authenticate = director->tls_authenticate;
      tls_ctx = director->tls_ctx;
   }


   /* Timeout Hello after 15 secs */
   btimer_t *tid = start_bsock_timer(dir, 15);
   dir->fsend(hello, bashed_name, UA_VERSION);

   if (!cram_md5_respond(dir, password, &tls_remote_need, &compatible) ||
       !cram_md5_challenge(dir, password, tls_local_need, compatible)) {
      goto bail_out;
   }

   /* Verify that the remote host is willing to meet our TLS requirements */
   if (tls_remote_need < tls_local_need && tls_local_need != BNET_TLS_OK && tls_remote_need != BNET_TLS_OK) {
      sendit(_("Authorization problem:"
             " Remote server did not advertise required TLS support.\n"));
      goto bail_out;
   }

   /* Verify that we are willing to meet the remote host's requirements */
   if (tls_remote_need > tls_local_need && tls_local_need != BNET_TLS_OK && tls_remote_need != BNET_TLS_OK) {
      sendit(_("Authorization problem:"
             " Remote server requires TLS.\n"));
      goto bail_out;
   }

   /* Is TLS Enabled? */
   if (tls_local_need >= BNET_TLS_OK && tls_remote_need >= BNET_TLS_OK) {
      /* Engage TLS! Full Speed Ahead! */
      if (!bnet_tls_client(tls_ctx, dir, NULL)) {
         sendit(_("TLS negotiation failed\n"));
         goto bail_out;
      }
      if (tls_authenticate) {           /* Authenticate only? */
         dir->free_tls();               /* yes, shutdown tls */
      }
   }

   /*
    * It's possible that the TLS connection will
    * be dropped here if an invalid client certificate was presented
    */
   Dmsg1(6, ">dird: %s", dir->msg);
   if (dir->recv() <= 0) {
      senditf(_("Bad response to Hello command: ERR=%s\n"),
         dir->bstrerror());
      goto bail_out;
   }

   Dmsg1(10, "<dird: %s", dir->msg);
   if (strncmp(dir->msg, oldOKhello, sizeof(oldOKhello)-1) == 0) {
      /* If Dir version exists, get it */
      sscanf(dir->msg, newOKhello, &dir_version);
      sendit(dir->msg);
   /* Check for hello from FD */
   } else if (sscanf(dir->msg, FDOKhello, &fd_version) == 1) {
      sendit(dir->msg);
   } else {
      sendit(_("Director rejected Hello command\n"));
      goto bail_out;
   }
   /* Turn on compression for newer Directors */
   if (dir_version >= 103 && (!cons || cons->comm_compression)) {
      dir->set_compress();
   } else {
      dir->clear_compress();
   }
   /* ***FIXME*** should turn on compression for FD if possible */
   stop_bsock_timer(tid);
   return 1;

bail_out:
   stop_bsock_timer(tid);
   sendit( _("Director authorization problem.\n"
             "Most likely the passwords do not agree.\n"
             "If you are using TLS, there may have been a certificate validation error during the TLS handshake.\n"
             "For help, please see " MANUAL_AUTH_URL "\n"));
   return 0;
}
