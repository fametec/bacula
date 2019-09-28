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

#ifndef BWLIMIT_H
#define BWLIMIT_H

class bwlimit: public SMARTALLOC
{
   static const int sample_capacity=10;

private:
   int64_t m_bwlimit;           /* set to limit bandwidth */
   int64_t m_nb_bytes;          /* bytes sent/recv since the last tick */
   btime_t m_last_tick;         /* last tick used by bwlimit */
   btime_t m_backlog_limit;     /* don't handle more backlog thna this us */
   pthread_mutex_t m_bw_mutex;

   int64_t samples_time[sample_capacity];
   int64_t samples_byte[sample_capacity];
   int64_t samples_sleep[sample_capacity];
   int64_t total_time, total_byte, total_sleep;
   int64_t current_time, current_byte, current_sleep;
   int64_t current_sample;

   void push_sample(int64_t t, int64_t bytes, int64_t sleep);

public:
   bwlimit(int64_t speed=0): m_bwlimit(speed), m_nb_bytes(0), m_last_tick(0),
         m_backlog_limit(10*1000*1000),
         total_time(0), total_byte(0), total_sleep(0), current_sample(0)
   {
      pthread_mutex_init(&m_bw_mutex, NULL);
      reset_sample();
   };
   ~bwlimit() {
      pthread_mutex_destroy(&m_bw_mutex);
   };

   void control_bwlimit(int bytes);
   void set_bwlimit(int64_t maxspeed) { m_bwlimit = maxspeed; };
   int64_t get_bwlimit() { return m_bwlimit; };
   bool use_bwlimit() { return m_bwlimit > 0;};
   int64_t get_bw();
   void get_total(int64_t *t, int64_t *bytes, int64_t *sleep);
   void reset_sample();
};
#endif
