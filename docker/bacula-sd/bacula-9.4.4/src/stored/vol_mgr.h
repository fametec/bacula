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
 * Pulled out of dev.h
 *
 * Kern Sibbald, MMXIII
 *
 */

/*
 * Some details of how volume reservations work
 *
 * class VOLRES:
 *   set_in_use()     volume being used on current drive
 *   clear_in_use()   no longer being used.  Can be re-used or moved.
 *   set_swapping()   set volume being moved to another drive
 *   is_swapping()    volume is being moved to another drive
 *   clear_swapping() volume normal
 *
 */

#ifndef __VOL_MGR_H
#define __VOL_MGR_H 1

class VOLRES;
VOLRES *vol_walk_start();
VOLRES *vol_walk_next(VOLRES *prev_vol);
void vol_walk_end(VOLRES *vol);

/*
 * Volume reservation class -- see vol_mgr.c and reserve.c
 */
class VOLRES {
   bool m_swapping;                   /* set when swapping to another drive */
   bool m_in_use;                     /* set when volume reserved or in use */
   bool m_reading;                    /* set when reading */
   int32_t m_slot;                    /* slot of swapping volume */
   uint32_t m_JobId;                  /* JobId for read volumes */
   volatile int32_t m_use_count;      /* Use count */
   pthread_mutex_t m_mutex;           /* Vol muntex */
public:
   dlink link;
   char *vol_name;                    /* Volume name */
   DEVICE *dev;                       /* Pointer to device to which we are attached */

   void init_mutex() { pthread_mutex_init(&m_mutex, NULL); };
   void destroy_mutex() { pthread_mutex_destroy(&m_mutex); };
   void vLock() { P(m_mutex); };
   void vUnlock() { V(m_mutex); };
   void inc_use_count(void) {P(m_mutex); m_use_count++; V(m_mutex); };
   void dec_use_count(void) {P(m_mutex); m_use_count--; V(m_mutex); };
   int32_t use_count() const { return m_use_count; };
   bool is_swapping() const { return m_swapping; };
   bool is_reading() const { return m_reading; };
   bool is_writing() const { return !m_reading; };
   void set_reading() { m_reading = true; };
   void clear_reading() { m_reading = false; };
   void set_swapping() { m_swapping = true; };
   void clear_swapping() { m_swapping = false; };
   bool is_in_use() const { return m_in_use; };
   void set_in_use() { m_in_use = true; };
   void clear_in_use() { m_in_use = false; };
   void set_slot(int32_t slot) { m_slot = slot; };
   void clear_slot() { m_slot = -1; };
   int32_t get_slot() const { return m_slot; };
   uint32_t get_jobid() const { return m_JobId; };
   void set_jobid(uint32_t JobId) { m_JobId = JobId; };
};

#define foreach_vol(vol) \
   for (vol=vol_walk_start(); vol; (vol=vol_walk_next(vol)) )

#define endeach_vol(vol) vol_walk_end(vol)

#endif
