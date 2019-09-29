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
 * Authenticate Director who is attempting to connect.
 *
 *   Kern Sibbald, October 2000
 *
 */

#include "bacula.h"
#include "filed.h"

extern CLIENT *me;                 /* my resource */

const int dbglvl = 50;

/* Version at end of Hello
 *   prior to 10Mar08 no version
 *   1 10Mar08
 *   2 13Mar09 - added the ability to restore from multiple storages
 *   3 03Sep10 - added the restore object command for vss plugin 4.0
 *   4 25Nov10 - added bandwidth command 5.1
 *   5 24Nov11 - added new restore object command format (pluginname) 6.0
 *   6 15Feb12 - added Component selection information list
 *   7 19Feb12 - added Expected files to restore
 *   8 22Mar13 - added restore options + version for SD
 *   9 06Aug13 - added comm line compression
 *  10 01Jan14 - added SD Calls Client and api version to status command
 */
#define FD_VERSION 10

/* For compatibility with old Community SDs */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Authenticated the Director
 */
bool authenticate_director(JCR *jcr)
{
   DIRRES *director = jcr->director;
   int tls_local_need = BNET_TLS_NONE;
   int tls_remote_need = BNET_TLS_NONE;
   int compatible = true;                 /* Want md5 compatible DIR */
   bool auth_success = false;
   alist *verify_list = NULL;
   btimer_t *tid = NULL;
   BSOCK *dir = jcr->dir_bsock;

   if (have_tls) {
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
   }

   tid = start_bsock_timer(dir, AUTH_TIMEOUT);
   /* Challenge the director */
   auth_success = cram_md5_challenge(dir, director->password, tls_local_need, compatible);
   if (job_canceled(jcr)) {
      auth_success = false;
      goto auth_fatal;                   /* quick exit */
   }
   if (auth_success) {
      auth_success = cram_md5_respond(dir, director->password, &tls_remote_need, &compatible);
      if (!auth_success) {
          char addr[64];
          char *who = dir->get_peer(addr, sizeof(addr)) ? dir->who() : addr;
          Dmsg1(dbglvl, "cram_get_auth respond failed for Director: %s\n", who);
      }
   } else {
       char addr[64];
       char *who = dir->get_peer(addr, sizeof(addr)) ? dir->who() : addr;
       Dmsg1(dbglvl, "cram_auth challenge failed for Director %s\n", who);
   }
   if (!auth_success) {
       Emsg1(M_FATAL, 0, _("Incorrect password given by Director at %s.\n"),
             dir->who());
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
         Jmsg0(jcr, M_FATAL, 0, _("TLS negotiation failed.\n"));
         auth_success = false;
         goto auth_fatal;
      }
      if (director->tls_authenticate) {         /* authentication only? */
         dir->free_tls();                        /* shutodown tls */
      }
   }
   auth_success = true;

auth_fatal:
   if (tid) {
      stop_bsock_timer(tid);
      tid = NULL;
   }
   if (auth_success) {
      return send_hello_ok(dir);
   }
   send_sorry(dir);
   /* Single thread all failures to avoid DOS */
   P(mutex);
   bmicrosleep(6, 0);
   V(mutex);
   return false;
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
   Dmsg0(050, "==== respond to SD challenge\n");
   auth_success = cram_md5_respond(sd, jcr->sd_auth_key, &tls_remote_need, &compatible);
   if (job_canceled(jcr)) {
      auth_success = false;     /* force quick exit */
      goto auth_fatal;
   }
   if (!auth_success) {
      Dmsg1(dbglvl, "cram_respond failed for SD: %s\n", sd->who());
   } else {
      /* Now challenge him */
      Dmsg0(050, "==== Challenge SD\n");
      auth_success = cram_md5_challenge(sd, jcr->sd_auth_key, tls_local_need, compatible);
      if (!auth_success) {
         Dmsg1(dbglvl, "cram_challenge failed for SD: %s\n", sd->who());
      }
   }

   if (!auth_success) {
      Jmsg(jcr, M_FATAL, 0, _("Authorization key rejected by Storage daemon.\n"
       "For help, please see " MANUAL_AUTH_URL "\n"));
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
