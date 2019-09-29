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

#ifndef MUTEX_LIST_H
#define MUTEX_LIST_H 1

/*
 * Use this list to manage lock order and protect the Bacula from
 * race conditions and dead locks
 */

#define PRIO_SD_DEV_ACQUIRE    4            /* dev.acquire_mutex */
#define PRIO_SD_DEV_ACCESS     5            /* dev.m_mutex */
#define PRIO_SD_VOL_LIST       0            /* vol_list_lock */
#define PRIO_SD_VOL_INFO       12           /* vol_info_mutex */
#define PRIO_SD_READ_VOL_LIST  13           /* read_vol_list */
#define PRIO_SD_DEV_SPOOL      14           /* dev.spool_mutex */
#define PRIO_SD_ACH_ACCESS     16           /* autochanger lock mutex */

#endif
