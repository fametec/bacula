/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2018 Kern Sibbald

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
 * Bacula Cloud Transfer Manager:
 * transfer manager wraps around the work queue.
 * Reports transfer status and error
 * Reports statistics about current past and future work
 * Written by Norbert Bizet, May MMXVI
 *
 */
#include "cloud_transfer_mgr.h"
#include "stored.h"

/* constructor
   * size : the size in bytes of the transfer
   * funct : function to process
   * arg : argument passed to the function
   * cache_fname  : cache file name is duplicated in the transfer constructor
   * volume_name  :  volume name is duplicated in the transfer constructor
   * part         : part index
   * driver       : pointer to the cloud_driver
   * dcr          : pointer to DCR
*/
transfer::transfer(uint64_t    size,
                  void *       (*funct)(transfer*),
                  const char   *cache_fname,
                  const char   *volume_name,
                  uint32_t     part,
                  cloud_driver *driver,
                  DCR          *dcr,
                  cloud_proxy  *proxy) :
   m_stat_size(size),
   m_stat_start(0),
   m_stat_duration(0),
   m_stat_eta(0),
   m_message(NULL),
   m_state(TRANS_STATE_CREATED),
   m_mgr(NULL),
   m_funct(funct),
   m_cache_fname(bstrdup(cache_fname)), /* cache fname is duplicated*/
   m_volume_name(bstrdup(volume_name)), /* volume name is duplicated*/
   m_part(part),
   m_driver(driver),
   m_dcr(dcr),
   m_proxy(proxy),
   m_workq_elem(NULL),
   m_use_count(0),
   m_cancel(false),
   m_do_cache_truncate(false)
{
   pthread_mutex_init(&m_stat_mutex, 0);
   pthread_mutex_init(&m_mutex, 0);
   pthread_cond_init(&m_done, NULL);

   m_message = get_pool_memory(PM_MESSAGE);
   *m_message = 0;
}

/* destructor */
transfer::~transfer()
{
   free_pool_memory(m_message);
   pthread_cond_destroy(&m_done);
   pthread_mutex_destroy(&m_mutex);
   pthread_mutex_destroy(&m_stat_mutex);

   free(m_volume_name);
   free(m_cache_fname);
   if (m_use_count > 0) {
      ASSERT(FALSE);
      Dmsg1(0, "!!!m_use_count = %d\n", m_use_count);
   }
}

/* queue this transfer for processing in the manager workq
 * ret :true if queuing is successful */
bool transfer::queue()
{
   if (!transition(TRANS_STATE_QUEUED)) {
      return false;
   }
   return true;
}


/* opaque function that processes m_funct with m_arg as parameter
 * depending on m_funct return value, changes state to TRANS_STATE_DONE
 *  or TRANS_STATE_ERROR
 */
void transfer::proceed()
{
   if (transition(TRANS_STATE_PROCESSED)) {
      if (m_funct(this)) {
         transition(TRANS_STATE_ERROR);
      } else {
         transition(TRANS_STATE_DONE);
      }
   } else {
      Mmsg(m_message, _("wrong transition to TRANS_STATE_PROCESS in proceed review logic\n"));
   }
}

int transfer::wait()
{
   lock_guard lg(m_mutex);

   int stat = 0;
   while (m_state != TRANS_STATE_DONE &&
          m_state != TRANS_STATE_ERROR) {

      if ((stat = pthread_cond_wait(&m_done, &m_mutex)) != 0) {
         return stat;
      }
   }
   return stat;
}

int transfer::timedwait(const timeval& tv)
{
   lock_guard lg(m_mutex);
   struct timespec timeout;
   struct timeval ttv;
   struct timezone tz;
   int stat = 0;
   timeout.tv_sec = tv.tv_sec;
   timeout.tv_nsec = tv.tv_usec * 1000;

   while (m_state != TRANS_STATE_DONE &&
          m_state != TRANS_STATE_ERROR) {

      gettimeofday(&ttv, &tz);
      timeout.tv_nsec += ttv.tv_usec * 1000;
      timeout.tv_sec += ttv.tv_sec;

      if ((stat = pthread_cond_timedwait(&m_done, &m_mutex, &timeout)) != 0) {
         return stat;
      }
   }
   return stat;
}

/* place the cancel flag and wait until processing is done */
bool transfer::cancel()
{
   {
      lock_guard lg(m_mutex);
      m_cancel = true;
   }
   return wait();
}

/* checking the cancel status : doesnt request locking */
bool transfer::is_cancelled() const
{
   return m_cancel;
}

uint32_t transfer::append_status(POOL_MEM& msg)
{
   POOLMEM *tmp_msg = get_pool_memory(PM_MESSAGE);
   char ec[30];
   uint32_t ret=0;
   static const char *state[]  = {"created",  "queued",  "process", "done", "error"};

   if (m_state > TRANS_STATE_PROCESSED) {
      ret =  Mmsg(tmp_msg,_("%s/part.%-5d state=%-7s size=%sB duration=%ds%s%s\n"),
                  m_volume_name, m_part,
                  state[m_state],
                  edit_uint64_with_suffix(m_stat_size, ec),
                  m_stat_duration,
                  (strlen(m_message) != 0)?" msg=":"",
                  (strlen(m_message) != 0)?m_message:"");
      pm_strcat(msg, tmp_msg);
   } else {
      ret = Mmsg(tmp_msg,_("%s/part.%-5d, state=%-7s size=%sB eta=%dss%s%s\n"),
                  m_volume_name, m_part,
                  state[m_state],
                  edit_uint64_with_suffix(m_stat_size, ec),
                  m_stat_eta,
                  (strlen(m_message) != 0)?" msg=":"",
                  (strlen(m_message) != 0)?m_message:"");
      pm_strcat(msg, tmp_msg);
   }
   free_pool_memory(tmp_msg);
   return ret;
}


/* the manager references itself through this function */
void transfer::set_manager(transfer_manager *mgr)
{
   lock_guard lg(m_mutex);
   m_mgr = mgr;
}

/* change the state */
bool transfer::transition(transfer_state state)
{
   /* lock state mutex*/
   lock_guard lg(m_mutex);

   /* transition from current state (m_state) to target state (state)*/
   bool ret = false; /*impossible transition*/
   switch(m_state)
   {
      case TRANS_STATE_CREATED:
         /* CREATED -> QUEUED */
         if (state == TRANS_STATE_QUEUED) {
            /* valid transition*/
            ret = true;
            if (m_mgr) {
               /*lock manager statistics */
               P(m_mgr->m_stat_mutex);
               /*increment the number of queued transfer*/
               m_mgr->m_stat_nb_transfer_queued++;
               /*add the current size into manager queued size*/
               m_mgr->m_stat_size_queued += m_stat_size;
               /*unlock manager statistics */
               V(m_mgr->m_stat_mutex);

               P(m_mgr->m_mutex);
               ++m_use_count;
               m_mgr->add_work(this);
               V(m_mgr->m_mutex);
            }
         }
         break;

      case TRANS_STATE_QUEUED:
         /* QUEUED -> CREATED : back to initial state*/
         if (state == TRANS_STATE_CREATED) {
            /* valid transition*/
            ret = true;
            if (m_mgr) {
               /*lock manager statistics */
               P(m_mgr->m_stat_mutex);
               /*decrement the number of queued transfer*/
               m_mgr->m_stat_nb_transfer_queued--;
               /*remove the current size from the manager queued size*/
               m_mgr->m_stat_size_queued -= m_stat_size;
               /*unlock manager statistics */
               V(m_mgr->m_stat_mutex);

               P(m_mgr->m_mutex);
               m_mgr->remove_work(m_workq_elem);
               --m_use_count;
               V(m_mgr->m_mutex);
            }
         }
         /* QUEUED -> PROCESSED : a worker aquired the transfer*/
         if (state == TRANS_STATE_PROCESSED) {
            /*valid transition*/
            ret = true;
            if (m_mgr) {
               /*lock manager statistics */
               P(m_mgr->m_stat_mutex);
               /*decrement the number of queued transfer*/
               m_mgr->m_stat_nb_transfer_queued--;
               /*increment the number of processed transfer*/
               m_mgr->m_stat_nb_transfer_processed++;
               /*remove the current size from the manager queued size*/
               m_mgr->m_stat_size_queued -= m_stat_size;
               /*... and add it to the manager processed size*/
               m_mgr->m_stat_size_processed += m_stat_size;
               /*unlock manager statistics */
               V(m_mgr->m_stat_mutex);

               /*transfer starts now*/
               P(m_stat_mutex);
               m_stat_start = (utime_t)time(NULL);
               V(m_stat_mutex);
            }
         }
         break;

      case TRANS_STATE_PROCESSED:
         /* PROCESSED -> DONE : Success! */
         if (state == TRANS_STATE_DONE) {
            /*valid transition*/
            ret = true;
            /*transfer stops now : compute transfer duration*/
            P(m_stat_mutex);
            m_stat_duration = (utime_t)time(NULL)-m_stat_start;
            V(m_stat_mutex);

            if (m_mgr) {
               /*lock manager statistics */
               P(m_mgr->m_stat_mutex);
               /* ... from processed to done*/
               m_mgr->m_stat_nb_transfer_processed--;
               m_mgr->m_stat_nb_transfer_done++;
               m_mgr->m_stat_size_processed -= m_stat_size;
               m_mgr->m_stat_size_done += m_stat_size;
               /*add local duration to manager duration */
               m_mgr->m_stat_duration_done += m_stat_duration;
               /*reprocess the manager average rate with it*/
               if (m_mgr->m_stat_duration_done != 0) {
                  m_mgr->m_stat_average_rate =
                        m_mgr->m_stat_size_done /
                        m_mgr->m_stat_duration_done;
               }
               /*unlock manager statistics */
               V(m_mgr->m_stat_mutex);

               /* process is completed, unref the workq reference */
               --m_use_count;
            }

            if (m_proxy) {
               m_proxy->set(m_volume_name, m_part, m_res_mtime, m_res_size);
            }

            /* in both case, success or failure, life keeps going on */
            pthread_cond_broadcast(&m_done);
         }
         /* PROCESSED -> ERROR : Failure! */
         if (state == TRANS_STATE_ERROR) {
            /*valid transition*/
            ret = true;
            /*transfer stops now, even if in error*/
            P(m_stat_mutex);
            m_stat_duration = (utime_t)time(NULL)-m_stat_start;
            V(m_stat_mutex);

            if (m_mgr) {
               /*lock manager statistics */
               P(m_mgr->m_stat_mutex);
               /* ... from processed to error*/
               m_mgr->m_stat_nb_transfer_processed--;
               m_mgr->m_stat_nb_transfer_error++;
               m_mgr->m_stat_size_processed -= m_stat_size;
               m_mgr->m_stat_size_error += m_stat_size;
               /*unlock manager statistics */
               V(m_mgr->m_stat_mutex);

               /* process is completed, unref the workq reference */
               --m_use_count;
            }
            /* in both case, success or failure, life keeps going on */
            pthread_cond_broadcast(&m_done);
         }
         break;

      case TRANS_STATE_DONE:
      case TRANS_STATE_ERROR:
      default:
         ret = false;
         break;
   }

   /* update state when transition is valid*/
   if (ret) {
      m_state = state;
   }

   return ret;
}

void transfer::set_do_cache_truncate(bool do_cache_truncate)
{
   m_do_cache_truncate=do_cache_truncate;
}

int transfer::inc_use_count()
{
   lock_guard lg(m_mutex);
   return ++m_use_count;
}

int transfer::dec_use_count()
{
   lock_guard lg(m_mutex);
   return --m_use_count;
}

void *transfer_launcher(void *arg)
{
   transfer *t = (transfer *)arg;
   if (t) {
      t->proceed();
   }
   return NULL;
}

/* -----------------------------------------------------------
   transfer manager declarations
   -----------------------------------------------------------
 */

/* constructor */
transfer_manager::transfer_manager(uint32_t n)
{
   transfer *item=NULL;
   m_transfer_list.init(item, &item->link);
   pthread_mutex_init(&m_stat_mutex, 0);
   pthread_mutex_init(&m_mutex, 0);
   workq_init(&m_wq, 1, transfer_launcher);
}

/* destructor */
transfer_manager::~transfer_manager()
{
   workq_wait_idle(&m_wq);
   pthread_mutex_destroy(&m_mutex);
   pthread_mutex_destroy(&m_stat_mutex);
}

/* create a new or inc-reference a similar transfer. (factory)
 * ret: transfer* is ref_counted and must be kept, used
 * and eventually released by caller with release() */
transfer *transfer_manager::get_xfer(uint64_t     size,
            transfer_engine *funct,
            POOLMEM      *cache_fname,
            const char   *volume_name,
            uint32_t     part,
            cloud_driver *driver,
            DCR          *dcr,
            cloud_proxy  *proxy)
{
   lock_guard lg (m_mutex);

   /* do we have a similar transfer on tap? */
   transfer *item;
   foreach_dlist(item, (&m_transfer_list)) {
      /* this is where "similar transfer" is defined:
       * same volume_name, same part idx */
      if (strcmp(item->m_volume_name, volume_name) == 0 && item->m_part == part) {
         item->inc_use_count();
         return item;
      }
   }
   /* no existing transfer: create a new one */
   item = New(transfer(size,
                       funct,
                       cache_fname,/* cache_fname is duplicated in the transfer constructor*/
                       volume_name, /* volume_name is duplicated in the transfer constructor*/
                       part,
                       driver,
                       dcr,
                       proxy));

   ASSERT(item->m_state == TRANS_STATE_CREATED);
   item->set_manager(this);
   /* inc use_count once for m_transfer_list insertion */
   item->inc_use_count();
   m_transfer_list.append(item);
   /* inc use_count once for caller ref counting */
   item->inc_use_count();
   return item;
}

/* does the xfer belong to us? */
bool transfer_manager::owns(transfer *xfer)
{
   lock_guard lg(m_mutex);
   transfer *item;
   foreach_dlist(item, (&m_transfer_list)) {
      /* same address */
      if (item == xfer) {
         return true;
      }
   }
   return false;
}

/* un-ref transfer and free if ref count goes to zero
 * caller must NOT use xfer anymore after this has been called */
void transfer_manager::release(transfer *xfer)
{
   if (xfer) {
      ASSERTD(owns(xfer), "Wrong Manager");
      /* wait should have been done already by caller,
       * but we cannot afford deleting the transfer while it's not completed */
      wait(xfer);
      /* decrement the caller reference */
      if (xfer->dec_use_count() == 1) {
         /* the only ref left is the one from m_transfer_list
          * time for deletion */
         lock_guard lg(m_mutex);
         m_transfer_list.remove(xfer);
         xfer->dec_use_count();
         delete xfer;
      }
   }
}

/* accessors to xfer->queue */
bool transfer_manager::queue(transfer *xfer)
{
   if (xfer) {
      ASSERTD(owns(xfer), "Wrong Manager");
      return xfer->queue();
   }
   return false;
}

/* accessors to xfer->wait */
int transfer_manager::wait(transfer *xfer)
{
   if (xfer) {
      ASSERTD(owns(xfer), "Wrong Manager");
      return xfer->wait();
   }
   return 0;
}

/* accessors to xfer->timedwait */
int transfer_manager::timedwait(transfer *xfer, const timeval& tv)
{
   if (xfer) {
      ASSERTD(owns(xfer), "Wrong Manager");
      return xfer->timedwait(tv);
   }
   return 0;
}

/* accessors to xfer->cancel */
bool transfer_manager::cancel(transfer *xfer)
{
   if (xfer) {
      ASSERTD(owns(xfer), "Wrong Manager");
      return xfer->cancel();
   }
   return false;
}

/* append a transfer object to this manager */
int transfer_manager::add_work(transfer* t)
{
   return workq_add(&m_wq, t, t ? &t->m_workq_elem : NULL, 0);
}

/* remove associated workq_ele_t from this manager workq */
int transfer_manager::remove_work(workq_ele_t *elem)
{
   return workq_remove(&m_wq, elem);
}
/* search the transfer list for similar transfer */
bool transfer_manager::find(const char *VolName, uint32_t index)
{
   /* Look in the transfer list if we have a download/upload for the current volume */
   lock_guard lg(m_mutex);
   transfer *item;
   foreach_dlist(item, (&m_transfer_list)) {
      if (strcmp(item->m_volume_name, VolName) == 0 && item->m_part == index) {
         return true;
      }
   }
   return false;
}

/* Call to this function just before displaying global statistics */
void transfer_manager::update_statistics()
{
   /* lock the manager stats */
   P(m_stat_mutex);

   /* ETA naive calculation for each element in the queue =
    * (accumulator(previous elements size) / average_rate) / num_workers;
    */
   uint64_t accumulator=0;

   /* lock the queue so order and chaining cannot be modified */
   P(m_mutex);
   P(m_wq.mutex);
   m_stat_nb_workers = m_wq.max_workers;

   /* parse the queued and processed transfers */
   transfer *t;
   foreach_dlist(t, &m_transfer_list) {
      if ( (t->m_state == TRANS_STATE_QUEUED) ||
            (t->m_state == TRANS_STATE_PROCESSED)) {
         accumulator+=t->m_stat_size;
         P(t->m_stat_mutex);
         if ((m_stat_average_rate != 0) &&  (m_stat_nb_workers != 0)) {
            /*update eta for each transfer*/
            t->m_stat_eta = (accumulator / m_stat_average_rate) / m_stat_nb_workers;
         }
         V(t->m_stat_mutex);
      }
   }

   /* the manager ETA is the ETA of the last transfer in its workq */
   if (m_wq.last) {
      transfer *t = (transfer *)m_wq.last->data;
      if (t) {
         m_stat_eta = t->m_stat_eta;
      }
   }

   V(m_wq.mutex);
   V(m_mutex);
   V(m_stat_mutex);
}

/* short status of the transfers */
uint32_t transfer_manager::append_status(POOL_MEM& msg, bool verbose)
{
   update_statistics();
   char ec0[30],ec1[30],ec2[30],ec3[30],ec4[30];
   POOLMEM *tmp_msg = get_pool_memory(PM_MESSAGE);
   uint32_t ret = Mmsg(tmp_msg, _("(%sB/s) (ETA %d s) "
            "Queued=%d %sB, Processed=%d %sB, Done=%d %sB, Failed=%d %sB\n"),
            edit_uint64_with_suffix(m_stat_average_rate, ec0), m_stat_eta,
            m_stat_nb_transfer_queued,  edit_uint64_with_suffix(m_stat_size_queued, ec1),
            m_stat_nb_transfer_processed,  edit_uint64_with_suffix(m_stat_size_processed, ec2),
            m_stat_nb_transfer_done,  edit_uint64_with_suffix(m_stat_size_done, ec3),
            m_stat_nb_transfer_error, edit_uint64_with_suffix(m_stat_size_error, ec4));
   pm_strcat(msg, tmp_msg);

   if (verbose) {
      P(m_mutex);
      if (!m_transfer_list.empty()) {
         ret += Mmsg(tmp_msg, _("------------------------------------------------------------ details ------------------------------------------------------------\n"));
         pm_strcat(msg, tmp_msg);
      }
      transfer *tpkt;
      foreach_dlist(tpkt, &m_transfer_list) {
         ret += tpkt->append_status(msg);
      }
      V(m_mutex);
   }
   free_pool_memory(tmp_msg);
   return ret;
}
