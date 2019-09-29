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
 *
 *   fifo_dev.c  -- low level operations on fifo devices
 *
 *     written by, Kern Sibbald, MM
 *     separated from file_dev.c January 2016f4
 *
 */

#include "bacula.h"
#include "stored.h"

bool fifo_dev::open_device(DCR *dcr, int omode)
{
   return tape_dev::open_device(dcr, omode);
}

boffset_t fifo_dev::lseek(DCR *dcr, boffset_t offset, int whence)
{
   /* Cannot seek */
   return 0;
}

bool fifo_dev::truncate(DCR *dcr)
{
   /* Cannot truncate */
   return true;
}

const char *fifo_dev::print_type()
{
   return "FIFO";
}
