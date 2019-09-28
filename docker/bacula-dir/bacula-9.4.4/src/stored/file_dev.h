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
/*
 * Inspired by vtape.h
 */

#ifndef __FILE_DEV_
#define __FILE_DEV_

class file_dev : public DEVICE {
public:

   file_dev() { };
   ~file_dev() { m_fd = -1; };
   bool is_eod_valid(DCR *dcr);
   bool eod(DCR *dcr);
   bool open_device(DCR *dcr, int omode);
   const char *print_type();
};

#endif /* __FILE_DEV_ */
