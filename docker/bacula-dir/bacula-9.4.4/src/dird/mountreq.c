/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2015 Kern Sibbald

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
 *   Bacula Director -- mountreq.c -- handles the message channel
 *    Mount request from the Storage daemon.
 *
 *     Kern Sibbald, March MMI
 *
 *    This routine runs as a thread and must be thread reentrant.
 *
 *  Basic tasks done here:
 *      Handle Mount services.
 *
 */

#include "bacula.h"
#include "dird.h"

/*
 * Handle mount request
 *  For now, we put the bsock in the UA's queue
 */

/* Requests from the Storage daemon */


/* Responses  sent to Storage daemon */
#ifdef xxx
static char OK_mount[]  = "1000 OK MountVolume\n";
#endif

static BQUEUE mountq = {&mountq, &mountq};
static int num_reqs = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct mnt_req_s {
   BQUEUE bq;
   BSOCK *bs;
   JCR *jcr;
} MNT_REQ;


void mount_request(JCR *jcr, BSOCK *bs, char *buf)
{
   MNT_REQ *mreq;

   mreq = (MNT_REQ *) malloc(sizeof(MNT_REQ));
   memset(mreq, 0, sizeof(MNT_REQ));
   mreq->jcr = jcr;
   mreq->bs = bs;
   P(mutex);
   num_reqs++;
   qinsert(&mountq, &mreq->bq);
   V(mutex);
   return;
}
