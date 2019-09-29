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
 *   scan.c scan a directory (on a removable file) for a valid
 *      Volume name. If found, open the file for append.
 *
 *    Kern Sibbald, MMVI
 *
 */

#include "bacula.h"
#include "stored.h"

int breaddir(DIR *dirp, POOLMEM *&d_name);

/* Forward referenced functions */
static bool is_volume_name_legal(char *name);


bool DEVICE::scan_dir_for_volume(DCR *dcr)
{
   DIR* dp;
   int name_max;
   char *mount_point;
   VOLUME_CAT_INFO dcrVolCatInfo, devVolCatInfo;
   char VolumeName[MAX_NAME_LENGTH];
   struct stat statp;
   bool found = false;
   POOL_MEM fname(PM_FNAME);
   POOL_MEM dname(PM_FNAME);
   bool need_slash = false;
   int len;

   dcrVolCatInfo = dcr->VolCatInfo;     /* structure assignment */
   devVolCatInfo = VolCatInfo;          /* structure assignment */
   bstrncpy(VolumeName, dcr->VolumeName, sizeof(VolumeName));

   name_max = pathconf(".", _PC_NAME_MAX);
   if (name_max < 1024) {
      name_max = 1024;
   }

   if (device->mount_point) {
      mount_point = device->mount_point;
   } else {
      mount_point = device->device_name;
   }

   if (!(dp = opendir(mount_point))) {
      berrno be;
      dev_errno = errno;
      Dmsg3(29, "scan_dir_for_vol: failed to open dir %s (dev=%s), ERR=%s\n",
            mount_point, print_name(), be.bstrerror());
      goto get_out;
   }

   len = strlen(mount_point);
   if (len > 0) {
      need_slash = !IsPathSeparator(mount_point[len - 1]);
   }
   for ( ;; ) {
      if (breaddir(dp, dname.addr()) != 0) {
         dev_errno = EIO;
         Dmsg2(129, "scan_dir_for_vol: failed to find suitable file in dir %s (dev=%s)\n",
               mount_point, print_name());
         break;
      }
      if (strcmp(dname.c_str(), ".") == 0 ||
          strcmp(dname.c_str(), "..") == 0) {
         continue;
      }

      if (!is_volume_name_legal(dname.c_str())) {
         continue;
      }
      pm_strcpy(fname, mount_point);
      if (need_slash) {
         pm_strcat(fname, "/");
      }
      pm_strcat(fname, dname);
      if (lstat(fname.c_str(), &statp) != 0 ||
          !S_ISREG(statp.st_mode)) {
         continue;                 /* ignore directories & special files */
      }

      /*
       * OK, we got a different volume mounted. First save the
       *  requested Volume info (dcr) structure, then query if
       *  this volume is really OK. If not, put back the desired
       *  volume name, mark it not in changer and continue.
       */
      bstrncpy(dcr->VolumeName, dname.c_str(), sizeof(dcr->VolumeName));
      /* Check if this is a valid Volume in the pool */
      if (!dir_get_volume_info(dcr, dcr->VolumeName, GET_VOL_INFO_FOR_WRITE)) {
         continue;
      }
      /* This was not the volume we expected, but it is OK with
       * the Director, so use it.
       */
      VolCatInfo = dcr->VolCatInfo;       /* structure assignment */
      found = true;
      break;                /* got a Volume */
   }
   closedir(dp);

get_out:
   if (!found) {
      /* Restore VolumeName we really wanted */
      bstrncpy(dcr->VolumeName, VolumeName, sizeof(dcr->VolumeName));
      dcr->VolCatInfo = dcrVolCatInfo;     /* structure assignment */
      VolCatInfo = devVolCatInfo;          /* structure assignment */
   }
   Dsm_check(100);
   return found;
}

/*
 * Check if the Volume name has legal characters
 * If ua is non-NULL send the message
 */
static bool is_volume_name_legal(char *name)
{
   int len;
   const char *p;
   const char *accept = ":.-_";

   /* Restrict the characters permitted in the Volume name */
   for (p=name; *p; p++) {
      if (B_ISALPHA(*p) || B_ISDIGIT(*p) || strchr(accept, (int)(*p))) {
         continue;
      }
      return false;
   }
   len = strlen(name);
   if (len >= MAX_NAME_LENGTH) {
      return false;
   }
   if (len == 0) {
      return false;
   }
   return true;
}
