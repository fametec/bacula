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

#include "bacula.h"
#include "bwlimit.h"

#define ONE_SEC 1000000L /* number of microseconds in a second */

void bwlimit::reset_sample()
{
   memset(samples_time, '\0', sizeof(samples_time));
   memset(samples_byte, '\0', sizeof(samples_byte));
   memset(samples_sleep, '\0', sizeof(samples_sleep));
   total_time=total_byte=total_sleep=0;
   current_sample=0;
   current_byte=0;
}

void bwlimit::push_sample(int64_t t, int64_t bytes, int64_t sleep)
{
   // accumulate data in current sample
   current_time+=t;
   current_byte+=bytes;
   current_sleep+=sleep;
   if (current_time>ONE_SEC) {
      // we have accumulated enough data for this sample go to the next one
      // First "pop" the data from the total
      total_time-=samples_time[current_sample];
      total_byte-=samples_byte[current_sample];
      total_sleep-=samples_sleep[current_sample];
      // Push the new one
      total_time+=current_time;
      total_byte+=current_byte;
      total_sleep+=current_sleep;
      // record the data in the table
      samples_time[current_sample]=current_time;
      samples_byte[current_sample]=current_byte;
      samples_sleep[current_sample]=current_sleep;
      // be ready for next sample
      current_time=0;
      current_byte=0;
      current_sleep=0;
      current_sample=(current_sample+1)%sample_capacity;
   }
}

void bwlimit::get_total(int64_t *t, int64_t *bytes, int64_t *sleep)
{
   pthread_mutex_lock(&m_bw_mutex);
   *t=total_time+current_time;
   *bytes=total_byte+current_byte;
   *sleep=total_sleep+current_sleep;
   pthread_mutex_unlock(&m_bw_mutex);
}

int64_t bwlimit::get_bw()
{
   int64_t bw = 0;
   btime_t temp = get_current_btime() - m_last_tick;
   if (temp < 0) {
      temp = 0;
   }
   pthread_mutex_lock(&m_bw_mutex);
   if (total_time+current_time>0) {
      bw=(total_byte+current_byte)*ONE_SEC/(total_time+current_time+temp);
   }
   pthread_mutex_unlock(&m_bw_mutex);
   return bw;
}

void bwlimit::control_bwlimit(int bytes)
{
   btime_t now, temp;
   if (bytes == 0 || m_bwlimit == 0) {
      return;
   }

   lock_guard lg(m_bw_mutex);     /* Release the mutex automatically when we quit the function*/
   now = get_current_btime();          /* microseconds */
   temp = now - m_last_tick;           /* microseconds */

   if (temp < 0 || temp > m_backlog_limit) { /* Take care of clock problems (>10s) or back in time */
      m_nb_bytes = bytes;
      m_last_tick = now;
      reset_sample();
      return;
   }

   /* remove what as been consumed */
   m_nb_bytes -= bytes;

   /* Less than 0.1ms since the last call, see the next time */
   if (temp < 100) {
      push_sample(temp, bytes, 0);
      return;
   }

   /* Add what is authorized to be written in temp us */
   m_nb_bytes += (int64_t)(temp * ((double)m_bwlimit / ONE_SEC));
   m_last_tick = now;

   /* limit the backlog */
   if (m_nb_bytes > m_backlog_limit*m_bwlimit) {
      m_nb_bytes = m_backlog_limit*m_bwlimit;
      push_sample(temp, bytes, 0);
   } else if (m_nb_bytes < 0) {
      /* What exceed should be converted in sleep time */
      int64_t usec_sleep = (int64_t)(-m_nb_bytes /((double)m_bwlimit / ONE_SEC));
      if (usec_sleep > 100) {
         pthread_mutex_unlock(&m_bw_mutex);
         bmicrosleep(usec_sleep / ONE_SEC, usec_sleep % ONE_SEC);
         pthread_mutex_lock(&m_bw_mutex);
      }
      push_sample(temp, bytes, usec_sleep>100?usec_sleep:0);
      /* m_nb_bytes & m_last_tick will be updated at next iteration */
   }
}
