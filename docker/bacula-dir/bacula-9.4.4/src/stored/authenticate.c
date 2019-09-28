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
 * Authenticate caller
 *
 *   Written by Kern Sibbald, October 2000
 */


#include "bacula.h"
#include "stored.h"

extern STORES *me;               /* our Global resource */

const int dbglvl = 50;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/* Version at end of Hello
 *   prior to 06Aug13 no version
 *   1 06Aug13 - added comm line compression
 *   2 13Dec13 - added api version to status command
 */
#define SD_VERSION 2


/*
 * Authenticate the Director
 */
bool authenticate_director(JCR* jcr)
{
   DIRRES *director = jcr->director;
   int tls_local_need = BNET_TLS_NONE;
   int tls_remote_need = BNET_TLS_NONE;
   int compatible = true;                  /* require md5 compatible DIR */
   bool auth_success = false;
   alist *verify_list = NULL;
   BSOCK *dir = jcr->dir_bsock;

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

   if (director->tls_verify_peer) {
      verify_list = director->tls_allowed_cns;
   }

   /* Timeout authentication after 10 mins */
   btimer_t *tid = start_bsock_timer(dir, AUTH_TIMEOUT);
   auth_success = cram_md5_challenge(dir, director->password, tls_local_need, compatible);
   if (auth_success) {
      auth_success = cram_md5_respond(dir, director->password, &tls_remote_need, &compatible);
      if (!auth_success) {
         Dmsg1(dbglvl, "cram_get_auth respond failed with Director %s\n", dir->who());
      }
   } else {
      Dmsg1(dbglvl, "cram_auth challenge failed with Director %s\n", dir->who());
   }

   if (!auth_success) {
      Jmsg0(jcr, M_FATAL, 0, _("Incorrect password given by Director.\n"
       "For help, please see: " MANUAL_AUTH_URL "\n"));
      auth_success = false;
      goto auth_fatal;
   }

   /* Verify that the remote host is willing to meet our TLS requirements */
   if (tls_remote_need < tls_local_need && tls_local_need != BNET_TLS_OK && tls_remote_need != BNET_TLS_OK) {
      Jmsg0(jcr, M_FATAL, 0, _("Authorization problem: Remote server did not"
           " advertize required TLS support.\n"));
      Dmsg2(dbglvl, "remote_need=%d local_need=%d\n", tls_remote_need, tls_local_need);
      auth_success = false;
      goto auth_fatal;
   }

   /* Verify that we are willing to meet the remote host's requirements */
   if (tls_remote_need > tls_local_need && tls_local_need != BNET_TLS_OK && tls_remote_need != BNET_TLS_OK) {
      Jmsg0(jcr, M_FATAL, 0, _("Authorization problem: Remote server requires TLS.\n"));
      Dmsg2(dbglvl, "remote_need=%d local_need=%d\n", tls_remote_need, tls_local_need);
      auth_success = false;
      goto auth_fatal;
   }

   if (tls_local_need >= BNET_TLS_OK && tls_remote_need >= BNET_TLS_OK) {
      /* Engage TLS! Full Speed Ahead! */
      if (!bnet_tls_server(director->tls_ctx, dir, verify_list)) {
         Jmsg(jcr, M_FATAL, 0, _("TLS negotiation failed with DIR at \"%s:%d\"\n"),
            dir->host(), dir->port());
         auth_success = false;
         goto auth_fatal;
      }
      if (director->tls_authenticate) {     /* authenticate with tls only? */
         dir->free_tls();                   /* yes, shut it down */
      }
   }

auth_fatal:
   stop_bsock_timer(tid);
   jcr->director = director;
   if (auth_success) {
      return send_hello_ok(dir);
   }
   send_sorry(dir);
   Dmsg1(dbglvl, "Unable to authenticate Director at %s.\n", dir->who());
   Jmsg1(jcr, M_ERROR, 0, _("Unable to authenticate Director at %s.\n"), dir->who());
   bmicrosleep(5, 0);
   return false;
}


int authenticate_filed(JCR *jcr, BSOCK *fd, int FDVersion)
{
   int tls_local_need = BNET_TLS_NONE;
   int tls_remote_need = BNET_TLS_NONE;
   int compatible = true;                 /* require md5 compatible FD */
   bool auth_success = false;
   alist *verify_list = NULL;

   /* TLS Requirement */
   if (me->tls_enable) {
      if (me->tls_require) {
         tls_local_need = BNET_TLS_REQUIRED;
      } else {
         tls_local_need = BNET_TLS_OK;
      }
   }

   if (me->tls_authenticate) {
      tls_local_need = BNET_TLS_REQUIRED;
   }

   if (me->tls_verify_peer) {
      verify_list = me->tls_allowed_cns;
   }

   /* Timeout authentication after 5 mins */
   btimer_t *tid = start_bsock_timer(fd, AUTH_TIMEOUT);
   /* Challenge FD */
   Dmsg0(050, "Challenge FD\n");
   auth_success = cram_md5_challenge(fd, jcr->sd_auth_key, tls_local_need, compatible);
   if (auth_success) {
       /* Respond to his challenge */
       Dmsg0(050, "Respond to FD challenge\n");
       auth_success = cram_md5_respond(fd, jcr->sd_auth_key, &tls_remote_need, &compatible);
       if (!auth_success) {
          Dmsg1(dbglvl, "Respond cram-get-auth respond failed with FD: %s\n", fd->who());
       }
   } else {
      Dmsg1(dbglvl, "Challenge cram-auth failed with FD: %s\n", fd->who());
   }

   if (!auth_success) {
      Jmsg(jcr, M_FATAL, 0, _("Incorrect authorization key from File daemon at %s rejected.\n"
       "For help, please see: " MANUAL_AUTH_URL "\n"),
           fd->who());
      auth_success = false;
      goto auth_fatal;
   }

   /* Verify that the remote host is willing to meet our TLS requirements */
   if (tls_remote_need < tls_local_need && tls_local_need != BNET_TLS_OK && tls_remote_need != BNET_TLS_OK) {
      Jmsg(jcr, M_FATAL, 0, _("Authorization problem: Remote server did not"
           " advertize required TLS support.\n"));
      Dmsg2(dbglvl, "remote_need=%d local_need=%d\n", tls_remote_need, tls_local_need);
      auth_success = false;
      goto auth_fatal;
   }

   /* Verify that we are willing to meet the remote host's requirements */
   if (tls_remote_need > tls_local_need && tls_local_need != BNET_TLS_OK && tls_remote_need != BNET_TLS_OK) {
      Jmsg(jcr, M_FATAL, 0, _("Authorization problem: Remote server requires TLS.\n"));
      Dmsg2(dbglvl, "remote_need=%d local_need=%d\n", tls_remote_need, tls_local_need);
      auth_success = false;
      goto auth_fatal;
   }

   if (tls_local_need >= BNET_TLS_OK && tls_remote_need >= BNET_TLS_OK) {
      /* Engage TLS! Full Speed Ahead! */
      if (!bnet_tls_server(me->tls_ctx, fd, verify_list)) {
         Jmsg(jcr, M_FATAL, 0, _("TLS negotiation failed with FD at \"%s:%d\"\n"),
            fd->host(), fd->port());
         auth_success = false;
         goto auth_fatal;
      }
      if (me->tls_authenticate) {          /* tls authenticate only? */
         fd->free_tls();                   /* yes, shut it down */
      }
   }

auth_fatal:
   stop_bsock_timer(tid);
   if (!auth_success) {
      Jmsg(jcr, M_FATAL, 0, _("Incorrect authorization key from File daemon at %s rejected.\n"
       "For help, please see: " MANUAL_AUTH_URL "\n"),
           fd->who());
   }

   /* Version 5 of the protocol is a bit special, it is used by both 6.0.0
    * Enterprise version and 7.0.x Community version, but do not support the
    * same level of features. As nobody is using the 6.0.0 release, we can
    * be pretty sure that the FD in version 5 is a community FD.
    */
   if (auth_success && (FDVersion >= 9 || FDVersion == 5)) {
      send_hello_ok(fd);
   }
   return auth_success;
}

/*
 * First prove our identity to the Storage daemon, then
 * make him prove his identity.
 */
bool authenticate_storagedaemon(JCR *jcr)
{
   BSOCK *sd = jcr->store_bsock;
   int tls_local_need = BNET_TLS_NONE;
   int tls_remote_need = BNET_TLS_NONE;
   int compatible = true;
   bool auth_success = false;
   int sd_version = 0;

   btimer_t *tid = start_bsock_timer(sd, AUTH_TIMEOUT);

   /* TLS Requirement */
   if (have_tls && me->tls_enable) {
      if (me->tls_require) {
         tls_local_need = BNET_TLS_REQUIRED;
      } else {
         tls_local_need = BNET_TLS_OK;
      }
   }

   if (me->tls_authenticate) {
      tls_local_need = BNET_TLS_REQUIRED;
   }

   if (job_canceled(jcr)) {
      auth_success = false;     /* force quick exit */
      goto auth_fatal;
   }

   /* Respond to SD challenge */
   Dmsg0(050, "Respond to SD challenge\n");
   auth_success = cram_md5_respond(sd, jcr->sd_auth_key, &tls_remote_need, &compatible);
   if (job_canceled(jcr)) {
      auth_success = false;     /* force quick exit */
      goto auth_fatal;
   }
   if (!auth_success) {
      Dmsg1(dbglvl, "cram_respond failed for SD: %s\n", sd->who());
   } else {
      /* Now challenge him */
      Dmsg0(050, "Challenge SD\n");
      auth_success = cram_md5_challenge(sd, jcr->sd_auth_key, tls_local_need, compatible);
      if (!auth_success) {
         Dmsg1(dbglvl, "cram_challenge failed for SD: %s\n", sd->who());
      }
   }

   if (!auth_success) {
      Jmsg(jcr, M_FATAL, 0, _("Authorization key rejected by Storage daemon.\n"
       "Please see " MANUAL_AUTH_URL " for help.\n"));
      goto auth_fatal;
   } else {
      Dmsg0(050, "Authorization with SD is OK\n");
   }

   /* Verify that the remote host is willing to meet our TLS requirements */
   if (tls_remote_need < tls_local_need && tls_local_need != BNET_TLS_OK && tls_remote_need != BNET_TLS_OK) {
      Jmsg(jcr, M_FATAL, 0, _("Authorization problem: Remote server did not"
           " advertize required TLS support.\n"));
      Dmsg2(dbglvl, "remote_need=%d local_need=%d\n", tls_remote_need, tls_local_need);
      auth_success = false;
      goto auth_fatal;
   }

   /* Verify that we are willing to meet the remote host's requirements */
   if (tls_remote_need > tls_local_need && tls_local_need != BNET_TLS_OK && tls_remote_need != BNET_TLS_OK) {
      Jmsg(jcr, M_FATAL, 0, _("Authorization problem: Remote server requires TLS.\n"));
      Dmsg2(dbglvl, "remote_need=%d local_need=%d\n", tls_remote_need, tls_local_need);
      auth_success = false;
      goto auth_fatal;
   }

   if (tls_local_need >= BNET_TLS_OK && tls_remote_need >= BNET_TLS_OK) {
      /* Engage TLS! Full Speed Ahead! */
      if (!bnet_tls_client(me->tls_ctx, sd, NULL)) {
         Jmsg(jcr, M_FATAL, 0, _("TLS negotiation failed.\n"));
         auth_success = false;
         goto auth_fatal;
      }
      if (me->tls_authenticate) {           /* tls authentication only? */
         sd->free_tls();                    /* yes, shutdown tls */
      }
   }
   if (sd->recv() <= 0) {
      auth_success = false;
      goto auth_fatal;
   }
   sscanf(sd->msg, "3000 OK Hello %d", &sd_version);
   if (sd_version >= 1 && me->comm_compression) {
      sd->set_compress();
   } else {
      sd->clear_compress();
      Dmsg0(050, "*** No FD compression with SD\n");
   }

   /* At this point, we have successfully connected */

auth_fatal:
   /* Destroy session key */
   memset(jcr->sd_auth_key, 0, strlen(jcr->sd_auth_key));
   stop_bsock_timer(tid);
   /* Single thread all failures to avoid DOS */
   if (!auth_success) {
      P(mutex);
      bmicrosleep(6, 0);
      V(mutex);
   }
   return auth_success;
}
