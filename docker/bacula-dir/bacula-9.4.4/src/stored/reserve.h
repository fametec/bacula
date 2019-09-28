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
 * Definitions for reservation system.
 *
 *  Kern Sibbald, February MMVI
 *
 */

/*
 *   Use Device command from Director
 *   The DIR tells us what Device Name to use, the Media Type,
 *      the Pool Name, and the Pool Type.
 *
 *    Ensure that the device exists and is opened, then store
 *      the media and pool info in the JCR.  This class is used
 *      only temporarily in this file.
 */
class DIRSTORE {
public:
   alist *device;
   bool append;
   char name[MAX_NAME_LENGTH];
   char media_type[MAX_NAME_LENGTH];
   char pool_name[MAX_NAME_LENGTH];
   char pool_type[MAX_NAME_LENGTH];
};

/* Reserve context */
class RCTX {
public:
   JCR *jcr;
   char *device_name;
   DIRSTORE *store;
   DEVRES   *device;
   DEVICE *low_use_drive;             /* Low use drive candidate */
   bool try_low_use_drive;            /* see if low use drive available */
   bool any_drive;                    /* Accept any drive if set */
   bool PreferMountedVols;            /* Prefer volumes already mounted */
   bool exact_match;                  /* Want exact volume */
   bool have_volume;                  /* Have DIR suggested vol name */
   bool suitable_device;              /* at least one device is suitable */
   bool autochanger_only;             /* look at autochangers only */
   bool notify_dir;                   /* Notify DIR about device */
   bool append;                       /* set if append device */
   char VolumeName[MAX_NAME_LENGTH];  /* Vol name suggested by DIR */
};
