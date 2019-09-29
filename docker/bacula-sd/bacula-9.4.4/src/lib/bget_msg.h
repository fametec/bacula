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

#ifndef __BGET_MSG_H_
#define __BGET_MSG_H_

#include "bacula.h"

typedef uint64_t blockaddr;
typedef int64_t  blockidx;

#ifdef COMMUNITY
#define BLOCK_HEAD_SIZE               4       // only one int
#define GETMSG_MAX_BLOCK_SIZE    (65*1024-BLOCK_HEAD_SIZE)
#define GETMSG_MAX_HASH_SIZE     64  /* SHA512 */
#define GETMSG_MAX_MSG_SIZE      (GETMSG_MAX_BLOCK_SIZE+GETMSG_MAX_HASH_SIZE+sizeof(uint32_t)+OFFSET_FADDR_SIZE+100)
#endif


class bmessage: public SMARTALLOC
{
public:
   enum { bm_none, bm_ready, bm_busy, bm_ref };

   POOLMEM *msg;    // exchanged with BSOCK
   int32_t msglen;  // length from BSOCK
   int32_t origlen; // length before rehydration, to be compared with length in header
   char *rbuf;      // adjusted to point to data inside *msg
   int32_t rbuflen; // adjusted from msglen
   int status;
   int ret;         // return value from bget_msg()
   int jobbytes;    // must be added to jcr->JobBytes if block is downloaded

   bmessage(int bufsize);
   virtual ~bmessage();
   void swap(BSOCK *sock);
};

class GetMsg: public SMARTALLOC
{
public:
   JCR *jcr;
   BSOCK *bsock;
   const char *rec_header;        /* Format of a header */
   int32_t bufsize;               /* "ideal" bufsize from JCR */

   bool m_is_stop;                /* set by the read thread when bsock->is_stop() */
   bool m_is_done;                /* set when the read thread finish (no more record will be pushed) */
   bool m_is_error;               /* set when the read thread get an error */

   int32_t m_use_count;

   pthread_mutex_t mutex;
   pthread_cond_t cond;
   bmessage *bmsg_aux;
   bmessage *bmsg;   // local bmsg used by bget_msg(NULL)
   int32_t msglen;   // used to mimic BSOCK, updated by bget_msg()
   POOLMEM *msg;     // used to mimic BSOCK, updated by bget_msg()

   void inc_use_count(void) {P(mutex); m_use_count++; V(mutex); };
   void dec_use_count(void) {P(mutex); m_use_count--; V(mutex); };
   int32_t use_count() { int32_t v; P(mutex); v = m_use_count; V(mutex); return v;};


   GetMsg(JCR *a_jcr, BSOCK *a_bsock, const char *a_rec_header, int32_t a_bufsize);
   virtual ~GetMsg();

   virtual int bget_msg(bmessage **pbmsg=NULL);
   inline virtual void *do_read_sock_thread(void) { return NULL; };
   inline virtual int start_read_sock() { return 0; };
   inline virtual void *wait_read_sock(int /*emergency_quit*/) { return NULL;};

   virtual bool is_stop() { return (m_is_stop!=false); };
   virtual bool is_done() { return (m_is_done!=false); };
   virtual bool is_error(){ return (m_is_error!=false); };

   bmessage *new_msg() { return New(bmessage(bufsize)); };

};

/* Call this function to release the memory associated with the message queue
 * The reading thread is using the BufferedMsgBase to work, so we need to free
 * the memory only when the main thread and the reading thread agree
 */
inline void free_GetMsg(GetMsg *b)
{
   b->dec_use_count();
   ASSERT2(b->use_count() >= 0, "GetMsg use_count too low");
   if (b->use_count() == 0) {
      delete b;
   }
}

#endif
