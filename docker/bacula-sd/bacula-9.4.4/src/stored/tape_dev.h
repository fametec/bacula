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
 * Inspired by vtape.h
 */

#ifndef __TAPE_DEV_
#define __TAPE_DEV_

struct ALERT {
   char *Volume;
   utime_t alert_time;
   char alerts[10];
};

class tape_dev : public DEVICE {
public:

   tape_dev() { };
   ~tape_dev() { };

   /* DEVICE virtual functions that we redefine with our tape code */
   bool fsf(int num);
   bool offline(DCR *dcr);
   bool rewind(DCR *dcr);
   bool bsf(int num);
   void lock_door();
   void unlock_door();
   bool reposition(DCR *dcr, uint64_t raddr);
   bool mount(int timeout);
   bool unmount(int timeout);
   bool mount_tape(int mount, int dotimeout);
   bool weof(DCR *dcr, int num);
   bool eod(DCR *dcr);
   bool is_eod_valid(DCR *dcr);
   void set_ateof();
   bool open_device(DCR *dcr, int omode);
   void term(DCR *dcr);
   const char *print_type();
   DEVICE *get_dev(DCR *dcr);
   uint32_t get_hi_addr();
   uint32_t get_low_addr();
   uint64_t get_full_addr();
   bool end_of_volume(DCR *dcr);
   char *print_addr(char *buf, int32_t buf_len);
   char *print_addr(char *buf, int32_t maxlen, boffset_t addr);
   bool get_tape_worm(DCR *dcr);
   bool get_tape_alerts(DCR *dcr);
   void show_tape_alerts(DCR *dcr, alert_list_type type,
      alert_list_which which, alert_cb alert_callback);
   int delete_alerts();

   alist *alert_list;
};

#endif /* __TAPE_DEV_ */
