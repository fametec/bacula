/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

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
 *  Challenge Response Authentication Method using MD5 (CRAM-MD5)
 *
 * cram-md5 is based on RFC2104.
 *
 * Written for Bacula by Kern E. Sibbald, May MMI.
 *
 */

#include "bacula.h"

const int dbglvl = 50;


/* Authorize other end
 * Codes that tls_local_need and tls_remote_need can take:
 *   BNET_TLS_NONE     I cannot do tls
 *   BNET_TLS_OK       I can do tls, but it is not required on my end
 *   BNET_TLS_REQUIRED  tls is required on my end
 *
 *   Returns: false if authentication failed
 *            true if OK
 */
bool cram_md5_challenge(BSOCK *bs, const char *password, int tls_local_need, int compatible)
{
   struct timeval t1;
   struct timeval t2;
   struct timezone tz;
   int i;
   bool ok;
   char chal[MAXSTRING];
   char host[MAXSTRING];
   uint8_t hmac[20];

   if (!bs) {
      Dmsg0(dbglvl, "Invalid bsock\n");
      return false;
   }

   gettimeofday(&t1, &tz);
   for (i=0; i<4; i++) {
      gettimeofday(&t2, &tz);
   }
   srandom((t1.tv_sec&0xffff) * (t2.tv_usec&0xff));
   if (!gethostname(host, sizeof(host))) {
      bstrncpy(host, my_name, sizeof(host));
   }
   /* Send challenge -- no hashing yet */
   bsnprintf(chal, sizeof(chal), "<%u.%u@%s>", (uint32_t)random(), (uint32_t)time(NULL), host);
   if (compatible) {
      Dmsg2(dbglvl, "send: auth cram-md5 challenge %s ssl=%d\n", chal, tls_local_need);
      if (!bs->fsend("auth cram-md5 %s ssl=%d\n", chal, tls_local_need)) {
         Dmsg1(dbglvl, "Send challenge comm error. ERR=%s\n", bs->bstrerror());
         return false;
      }
   } else {
      /* Old non-compatible system */
      Dmsg2(dbglvl, "send: auth cram-md5 challenge %s ssl=%d\n", chal, tls_local_need);
      if (!bs->fsend("auth cram-md5 %s ssl=%d\n", chal, tls_local_need)) {
         Dmsg1(dbglvl, "Send challenge comm error. ERR=%s\n", bs->bstrerror());
         return false;
      }
   }

   /* Read hashed response to challenge */
   if (bs->wait_data(180) <= 0 || bs->recv() <= 0) {
      Dmsg1(dbglvl, "Receive cram-md5 response comm error. ERR=%s\n", bs->bstrerror());
      bmicrosleep(5, 0);
      return false;
   }

   /* Attempt to duplicate hash with our password */
   hmac_md5((uint8_t *)chal, strlen(chal), (uint8_t *)password, strlen(password), hmac);
   bin_to_base64(host, sizeof(host), (char *)hmac, 16, compatible);
   ok = strcmp(bs->msg, host) == 0;
   if (ok) {
      Dmsg1(dbglvl, "Authenticate OK %s\n", host);
   } else {
      bin_to_base64(host, sizeof(host), (char *)hmac, 16, false);
      ok = strcmp(bs->msg, host) == 0;
      if (!ok) {
         Dmsg2(dbglvl, "Authenticate NOT OK: wanted %s, got %s\n", host, bs->msg);
      }
   }
   if (ok) {
      bs->fsend("1000 OK auth\n");
   } else {
      bs->fsend(_("1999 Authorization failed.\n"));
      bmicrosleep(5, 0);
   }
   return ok;
}

/* Respond to challenge from other end */
bool cram_md5_respond(BSOCK *bs, const char *password, int *tls_remote_need, int *compatible)
{
   char chal[MAXSTRING];
   uint8_t hmac[20];

   if (!bs) {
      Dmsg0(dbglvl, "Invalid bsock\n");
      return false;
   }

   *compatible = false;
   if (bs->recv() <= 0) {
      bmicrosleep(5, 0);
      return false;
   }
   if (bs->msglen >= MAXSTRING) {
      Dmsg1(dbglvl, "Msg too long wanted auth cram... Got: %s", bs->msg);
      bmicrosleep(5, 0);
      return false;
   }
   Dmsg1(100, "cram-get received: %s", bs->msg);
   /*
    * Note that the next call is only to keep compatibility with very
    *  old versions of Bacula that used a non-compatible base64 algorithm.
    */
   if (sscanf(bs->msg, "auth cram-md5c %s ssl=%d", chal, tls_remote_need) == 2) {
      *compatible = true;
   } else if (sscanf(bs->msg, "auth cram-md5 %s ssl=%d", chal, tls_remote_need) != 2) {
      if (sscanf(bs->msg, "auth cram-md5 %s\n", chal) != 1) {
         Dmsg1(dbglvl, "Cannot scan received response to challenge: %s", bs->msg);
         bs->fsend(_("1999 Authorization failed.\n"));
         bmicrosleep(5, 0);
         return false;
      }
   }

   hmac_md5((uint8_t *)chal, strlen(chal), (uint8_t *)password, strlen(password), hmac);
   bs->msglen = bin_to_base64(bs->msg, 50, (char *)hmac, 16, *compatible) + 1;
// Don't turn the following on except for local debugging -- security
// Dmsg3(100, "get_auth: chal=%s pw=%s hmac=%s\n", chal, password, bs->msg);
   if (!bs->send()) {
      Dmsg1(dbglvl, "Send challenge failed. ERR=%s\n", bs->bstrerror());
      return false;
   }
   Dmsg1(99, "sending resp to challenge: %s\n", bs->msg);
   if (bs->wait_data(180) <= 0 || bs->recv() <= 0) {
      Dmsg1(dbglvl, "Receive cram-md5 response failed. ERR=%s\n", bs->bstrerror());
      bmicrosleep(5, 0);
      return false;
   }
   if (strcmp(bs->msg, "1000 OK auth\n") == 0) {
      return true;
   }
   Dmsg1(dbglvl, "Received bad response: %s\n", bs->msg);
   bmicrosleep(5, 0);
   return false;
}
