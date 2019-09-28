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

#ifndef BCLOUD_TRANSFER_MANAGER_H
#define BCLOUD_TRANSFER_MANAGER_H

#include "bacula.h"
#include "lib/workq.h"

/* forward declarations */
class transfer_manager;
class cloud_driver;
class DCR;
class transfer;
class cloud_proxy;

typedef void *(transfer_engine)(transfer *);


/* possible states of a transfer object */
typedef enum {
/* initial state */
   /* object has been created but not queued yet*/
   TRANS_STATE_CREATED = 0,
/* in the workq states */
   /* object is queued*/
   TRANS_STATE_QUEUED,
   /* object is processed*/
   TRANS_STATE_PROCESSED,
/* completed states */
   /* object processing has completed ok*/
   TRANS_STATE_DONE,
   /* object processing has completed but failed*/
   TRANS_STATE_ERROR,
/* number of states */
   NUM_TRANS_STATE
} transfer_state;

/* each cloud transfer (download, upload, etc.)
  is wrapped into a transfer object */
class transfer : public SMARTALLOC
{
public:
   dlink                link;   /* Used in global manager dlist */

/* m_stat prefixed statistics variables : */
   /* protect access to statistics resources*/
   pthread_mutex_t      m_stat_mutex;
   /* size of the transfer: should be filled asap */
   uint64_t             m_stat_size;
   /* time when process started */
   utime_t              m_stat_start;
   /* duration of the transfer : automatically filled when transfer is completed*/
   utime_t              m_stat_duration;
   /* estimate time to arrival : predictive guest approximation of transfer time*/
   utime_t              m_stat_eta;

/* status variables :*/
   /* protect status changes*/
   pthread_mutex_t      m_mutex;
   /* cond variable to broadcast transfer completion*/
   pthread_cond_t       m_done;
   /* status message */
   POOLMEM            *m_message;
   /* current transfer state*/
   transfer_state       m_state;

/* other variables :*/
   /* the manager that holds this element */
   transfer_manager    *m_mgr;
   /* the function processed by this transfer: contrary to the workq, it can be different for each transfer */
   transfer_engine     *m_funct;

   /* variables */
   const char          *m_cache_fname;
   const char          *m_volume_name;
   uint32_t             m_part;
   cloud_driver        *m_driver;
   DCR                 *m_dcr;
   cloud_proxy         *m_proxy;
   /* size of the transfer result : filled by the processor (driver) */
   uint64_t             m_res_size;
   /* last modification time of the transfer result : filled by the processor (driver) */
   utime_t              m_res_mtime;

   /* the associated workq element */
   workq_ele_t         *m_workq_elem;

   /* reference counter */
   int                  m_use_count;

   /* cancel flag */
   bool                 m_cancel;

   /* truncate cache once transfer is completed (upload)*/
   bool                 m_do_cache_truncate;
/* methods :*/
   /* constructor
   * size         : the size in bytes of the transfer
   * funct        : function to process
   * cache_fname  : cache file name is duplicated in the transfer constructor
   * volume_name  : volume name is duplicated in the transfer constructor
   * part         : part index
   * driver       : pointer to the cloud_driver
   * dcr          : pointer to DCR
   */
   transfer(uint64_t     size,
            transfer_engine *funct,
            const char   *cache_fname,
            const char   *volume_name,
            uint32_t     part,
            cloud_driver *driver,
            DCR          *dcr,
            cloud_proxy  *proxy
      );

   /* destructor*/
   ~transfer();

   /* opaque function that will process m_funct with m_arg as parameter. Called back from the workq.
    * depending on m_funct return value, changes m_state to TRANS_STATE_DONE or TRANS_STATE_ERROR */
   void proceed();

   /* waits for the asynchronous computation to finish (including cancel()ed computations).
    * ret: 0:Ok, errorcode otherwise */
   int wait(); /* no timeout */
   int timedwait(const timeval& tv); /* with timeout */

   /* queue this transfer for processing in the manager workq
    * ret :true if queuing is successful */
   bool queue();

   /* cancel processing
    * ret: true cancel done, false cancel failed */
   bool cancel();

   /* callback fct that checks if transfer has been cancelled */
   bool is_cancelled() const;

   /* append a status message into msg*/
   uint32_t append_status(POOL_MEM& msgs);

   void set_do_cache_truncate(bool do_cache_truncate);

protected:
friend class transfer_manager;

   /* the manager references itselfs thru this function*/
   void set_manager(transfer_manager *mgr);

   /* change the state
    * ret : true if transition is legal, false otherwise */
   bool transition(transfer_state state);

   /* ref counting must lock the element prior to use */
   int                  inc_use_count();
   /* !!dec use count can delete the transfer */
   int                  dec_use_count();
};

/*
   The transfer_manager wraps around the work queue and holds the transfer(s)
*/
class transfer_manager : public SMARTALLOC
{
public:

/* m_stat prefixed statistics variables global for this manager: */
   /* protect access to statistics resources*/
   pthread_mutex_t      m_stat_mutex;
   /* number of workers*/
   uint32_t             m_stat_nb_workers;
   /* current number of transfers in TRANS_STATE_QUEUED state in this manager*/
   uint64_t             m_stat_nb_transfer_queued;
   /* current number of transfers in TRANS_STATE_PROCESSED state in this manager*/
   uint64_t             m_stat_nb_transfer_processed;
   /* current number of transfers in TRANS_STATE_DONE state in this manager*/
   uint64_t             m_stat_nb_transfer_done;
   /* current number of transfers in TRANS_STATE_ERROR state in this manager*/
   uint64_t             m_stat_nb_transfer_error;

   /* size in bytes of transfers in TRANS_STATE_QUEUED state in this manager*/
   uint64_t             m_stat_size_queued;
   /* size in bytes of transfers in TRANS_STATE_PROCESSED state in this manager*/
   uint64_t             m_stat_size_processed;
   /* size in bytes of transfers in TRANS_STATE_DONE state in this manager*/
   uint64_t             m_stat_size_done;
   /* size in bytes of transfers in TRANS_STATE_ERROR state in this manager*/
   uint64_t             m_stat_size_error;
   /* duration of transfers in TRANS_STATE_DONE state in this manager*/
   utime_t              m_stat_duration_done;
   /* computed bytes/sec transfer rate */
   uint64_t             m_stat_average_rate;
   /* computed Estimate Time to Arrival */
   utime_t              m_stat_eta;


/* status variables global for this manager: */
   /* protect status access*/
   pthread_mutex_t      m_mutex;
   /* status message for this manager TBD*/
   POOLMEM              *m_message;
   /* m_state for the manager TBD*/
   int32_t              m_state;

/* others: */
   /* tranfer list*/
   dlist                m_transfer_list;

   /* workq used by this manager*/
   workq_t              m_wq;

/* methods */

   /* constructor */
   transfer_manager(uint32_t n);

   /* destructor */
   ~transfer_manager();

/* transfer functions */

   /* create a new or inc-reference a similar transfer. (factory)
    * ret: transfer* is ref_counted and must be kept, used
    * and eventually released by caller with release() */
   transfer *get_xfer(uint64_t     size,
            transfer_engine *funct,
            POOLMEM      *cache_fname,
            const char   *volume_name,
            uint32_t     part,
            cloud_driver *driver,
            DCR          *dcr,
            cloud_proxy  *proxy);

   /* does the xfer belong to this manager? */
   bool owns(transfer *xfer);

   /* un-ref transfer and delete if ref count falls to zero
    * caller must NOT use xfer anymore after calling release() */
   void release(transfer *xfer);

   /* accessors to xfer->queue */
   bool queue(transfer *xfer);

   /* accessors to xfer->wait */
   int wait(transfer *xfer);

   /* accessors to xfer->timedwait */
   int timedwait(transfer *xfer, const timeval& tv);

   /* accessors to xfer->cancel */
   bool cancel(transfer *xfer);

   /* search the transfer list for similar transfer */
   bool find(const char *VolName, uint32_t index);

   /* call to update manager statistics, before displaying it b.e.*/
   void update_statistics();

   /* append a status message into msg*/
   uint32_t append_status(POOL_MEM& msg, bool verbose);

protected:
friend class transfer;

   /* append a transfer object to this manager */
   int add_work(transfer* t);
   /* remove associated workq_ele_t from this manager workq*/
   int remove_work(workq_ele_t *elem);
};

#endif /*  BCLOUD_TRANSFER_MANAGER_H */
