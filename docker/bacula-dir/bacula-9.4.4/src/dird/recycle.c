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
 *   Bacula Director -- Automatic Recycling of Volumes
 *      Recycles Volumes that have been purged
 *
 *     Kern Sibbald, May MMII
 */


#include "bacula.h"
#include "dird.h"
#include "ua.h"

/* Forward referenced functions */

bool find_recycled_volume(JCR *jcr, bool InChanger, MEDIA_DBR *mr,
                          STORE *store)
{
   bstrncpy(mr->VolStatus, "Recycle", sizeof(mr->VolStatus));
   set_storageid_in_mr(store, mr);
   if (db_find_next_volume(jcr, jcr->db, 1, InChanger, mr)) {
      jcr->MediaId = mr->MediaId;
      Dmsg1(20, "Find_next_vol MediaId=%lu\n", jcr->MediaId);
      pm_strcpy(jcr->VolumeName, mr->VolumeName);
      set_storageid_in_mr(store, mr);
      return true;
   }
   return false;
}

/*
 *   Look for oldest Purged volume
 */
bool recycle_oldest_purged_volume(JCR *jcr, bool InChanger,
        MEDIA_DBR *mr, STORE *store)
{
   bstrncpy(mr->VolStatus, "Purged", sizeof(mr->VolStatus));
   if (db_find_next_volume(jcr, jcr->db, 1, InChanger, mr)) {
      set_storageid_in_mr(store, mr);
      if (recycle_volume(jcr, mr)) {
         Jmsg(jcr, M_INFO, 0, _("Recycled volume \"%s\"\n"), mr->VolumeName);
         Dmsg1(100, "return 1  recycle_oldest_purged_volume Vol=%s\n", mr->VolumeName);
         return true;
      }
   }
   Dmsg0(100, "return 0  recycle_oldest_purged_volume end\n");
   return false;
}

/*
 * Recycle the specified volume
 */
int recycle_volume(JCR *jcr, MEDIA_DBR *mr)
{
   bstrncpy(mr->VolStatus, "Recycle", sizeof(mr->VolStatus));
   mr->VolJobs = mr->VolFiles = mr->VolBlocks = mr->VolErrors = 0;
   mr->VolBytes = 1;
   mr->FirstWritten = mr->LastWritten = 0;
   mr->RecycleCount++;
   mr->set_first_written = true;
   set_storageid_in_mr(NULL, mr);
   return db_update_media_record(jcr, jcr->db, mr);
}
