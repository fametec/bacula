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
 * Modified version of tape_dev.h
 */

#ifndef __FIFO_DEV_
#define __FIFO_DEV_

class fifo_dev : public tape_dev {
public:

   fifo_dev() { };
   ~fifo_dev() { };

   /* DEVICE virtual functions that we redefine with our fifo code */
   bool open_device(DCR *dcr, int omode);
   boffset_t lseek(DCR *dcr, boffset_t offset, int whence);
   bool truncate(DCR *dcr);
   const char *print_type();
};

#endif /* __FIFO_DEV_ */
