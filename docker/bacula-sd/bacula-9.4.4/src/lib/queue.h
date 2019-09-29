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
 *  Written by John Walker MM
 */

/*  General purpose queue  */

struct b_queue {
        struct b_queue *qnext,     /* Next item in queue */
                     *qprev;       /* Previous item in queue */
};

typedef struct b_queue BQUEUE;

/*  Queue functions  */

void    qinsert(BQUEUE *qhead, BQUEUE *object);
BQUEUE *qnext(BQUEUE *qhead, BQUEUE *qitem);
BQUEUE *qdchain(BQUEUE *qitem);
BQUEUE *qremove(BQUEUE *qhead);
