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
 *
 *  Routines for getting tape worm status
 *
 *   Written by Kern Sibbald, September MMXVIII
 *
 */

#include "bacula.h"                   /* pull in global headers */
#include "stored.h"                   /* pull in Storage Deamon headers */


static const int dbglvl = 50;


bool tape_dev::get_tape_worm(DCR *dcr)
{
   JCR *jcr = dcr->jcr;

   if (!job_canceled(jcr) && dcr->device->worm_command &&
       dcr->device->control_name) {
      POOLMEM *wormcmd;
      int status = 1;
      bool is_worm = false;
      int worm_val = 0;
      BPIPE *bpipe;
      char line[MAXSTRING];
      const char *fmt = " %d";

      wormcmd = get_pool_memory(PM_FNAME);
      wormcmd = edit_device_codes(dcr, wormcmd, dcr->device->worm_command, "");
      /* Wait maximum 5 minutes */
      bpipe = open_bpipe(wormcmd, 60 * 5, "r");
      if (bpipe) {
         while (fgets(line, (int)sizeof(line), bpipe->rfd)) {
            is_worm = false;
            if (bsscanf(line, fmt, &worm_val) == 1) {
               if (worm_val > 0) {
                  is_worm = true;
               }
            }
         }
         status = close_bpipe(bpipe);
         free_pool_memory(wormcmd);
         return is_worm;
      } else {
         status = errno;
      }
      if (status != 0) {
         berrno be;
         Jmsg(jcr, M_WARNING, 0, _("3997 Bad worm command status: %s: ERR=%s.\n"),
              wormcmd, be.bstrerror(status));
         Dmsg2(dbglvl, _("3997 Bad worm command status: %s: ERR=%s.\n"),
              wormcmd, be.bstrerror(status));
      }

      Dmsg1(400, "worm script status=%d\n", status);
      free_pool_memory(wormcmd);
   } else {
      if (!dcr->device->worm_command) {
         Dmsg1(dbglvl, "Cannot get tape worm status: no Worm Command specified for device %s\n",
            print_name());
         Dmsg1(dbglvl, "Cannot get tape worm status: no Worm Command specified for device %s\n",
            print_name());

      }
      if (!dcr->device->control_name) {
         Dmsg1(dbglvl, "Cannot get tape worm status: no Control Device specified for device %s\n",
            print_name());
         Dmsg1(dbglvl, "Cannot get tape worm status: no Control Device specified for device %s\n",
            print_name());
      }
   }
   return false;
}
