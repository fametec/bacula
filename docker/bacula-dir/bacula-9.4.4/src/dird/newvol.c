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
 *   Bacula Director -- newvol.c -- creates new Volumes in
 *    catalog Media table from the LabelFormat specification.
 *
 *     Kern Sibbald, May MMI
 *
 *    This routine runs as a thread and must be thread reentrant.
 *
 *  Basic tasks done here:
 *      If possible create a new Media entry
 *
 */

#include "bacula.h"
#include "dird.h"

/* Forward referenced functions */
static bool create_simple_name(JCR *jcr, MEDIA_DBR *mr, POOL_DBR *pr);
static bool perform_full_name_substitution(JCR *jcr, MEDIA_DBR *mr, POOL_DBR *pr);


/*
 * Automatic Volume name creation using the LabelFormat
 *
 *  The media record must have the PoolId filled in when
 *   calling this routine.
 */
bool newVolume(JCR *jcr, MEDIA_DBR *mr, STORE *store, POOL_MEM &errmsg)
{
   POOL_DBR pr;

   bmemset(&pr, 0, sizeof(pr));

   /* See if we can create a new Volume */
   db_lock(jcr->db);
   pr.PoolId = mr->PoolId;

   if (!db_get_pool_numvols(jcr, jcr->db, &pr)) {
      goto bail_out;
   }

   if (pr.MaxVols > 0 && pr.NumVols >= pr.MaxVols) {
      Mmsg(errmsg, "Maximum Volumes exceeded for Pool %s", pr.Name);
      Dmsg1(90, "Too many volumes for Pool %s\n", pr.Name);
      goto bail_out;
   }

   mr->clear();
   set_pool_dbr_defaults_in_media_dbr(mr, &pr);
   jcr->VolumeName[0] = 0;
   bstrncpy(mr->MediaType, jcr->wstore->media_type, sizeof(mr->MediaType));
   generate_plugin_event(jcr, bDirEventNewVolume); /* return void... */
   if (jcr->VolumeName[0] && is_volume_name_legal(NULL, jcr->VolumeName)) {
      bstrncpy(mr->VolumeName, jcr->VolumeName, sizeof(mr->VolumeName));
      /* Check for special characters */
   } else if (pr.LabelFormat[0] && pr.LabelFormat[0] != '*') {
      if (is_volume_name_legal(NULL, pr.LabelFormat)) {
         /* No special characters, so apply simple algorithm */
         if (!create_simple_name(jcr, mr, &pr)) {
            goto bail_out;
         }
      } else {  /* try full substitution */
         /* Found special characters, so try substitution */
         if (!perform_full_name_substitution(jcr, mr, &pr)) {
            goto bail_out;
         }
         if (!is_volume_name_legal(NULL, mr->VolumeName)) {
            Mmsg(errmsg, _("Illegal character in Volume name"));
            Jmsg(jcr, M_ERROR, 0, _("Illegal character in Volume name \"%s\"\n"),
                 mr->VolumeName);
            goto bail_out;
         }
      }
   } else {
      goto bail_out;
   }
   pr.NumVols++;
   mr->Enabled = 1;
   set_storageid_in_mr(store, mr);
   if (db_create_media_record(jcr, jcr->db, mr) &&
       db_update_pool_record(jcr, jcr->db, &pr)) {
      Jmsg(jcr, M_INFO, 0, _("Created new Volume=\"%s\", Pool=\"%s\", MediaType=\"%s\" in catalog.\n"),
           mr->VolumeName, pr.Name, mr->MediaType);
      Dmsg1(90, "Created new Volume=%s\n", mr->VolumeName);
      db_unlock(jcr->db);
      return true;
   } else {
      Mmsg(errmsg, "%s", db_strerror(jcr->db));
      Jmsg(jcr, M_ERROR, 0, "%s", db_strerror(jcr->db));
   }

bail_out:
   db_unlock(jcr->db);
   return false;
}

static bool create_simple_name(JCR *jcr, MEDIA_DBR *mr, POOL_DBR *pr)
{
   char name[MAXSTRING];
   char num[20];
   db_int64_ctx ctx;
   POOL_MEM query(PM_MESSAGE);
   char ed1[50];

   /* See if volume already exists */
   mr->VolumeName[0] = 0;
   bstrncpy(name, pr->LabelFormat, sizeof(name));
   ctx.value = 0;
   /* TODO: Remove Pool as it is not used in the query */
   Mmsg(query, "SELECT MAX(MediaId) FROM Media,Pool WHERE Pool.PoolId=%s",
        edit_int64(pr->PoolId, ed1));
   if (!db_sql_query(jcr->db, query.c_str(), db_int64_handler, (void *)&ctx)) {
      Jmsg(jcr, M_WARNING, 0, _("SQL failed, but ignored. ERR=%s\n"), db_strerror(jcr->db));
      ctx.value = pr->NumVols+1;
   }
   for (int i=(int)ctx.value+1; i<(int)ctx.value+100; i++) {
      MEDIA_DBR tmr;
      sprintf(num, "%04d", i);
      bstrncpy(tmr.VolumeName, name, sizeof(tmr.VolumeName));
      bstrncat(tmr.VolumeName, num, sizeof(tmr.VolumeName));
      if (db_get_media_record(jcr, jcr->db, &tmr)) {
         Jmsg(jcr, M_WARNING, 0,
             _("Wanted to create Volume \"%s\", but it already exists. Trying again.\n"),
             tmr.VolumeName);
         continue;
      }
      bstrncpy(mr->VolumeName, name, sizeof(mr->VolumeName));
      bstrncat(mr->VolumeName, num, sizeof(mr->VolumeName));
      break;                    /* Got good name */
   }
   if (mr->VolumeName[0] == 0) {
      Jmsg(jcr, M_ERROR, 0, _("Too many failures. Giving up creating Volume name.\n"));
      return false;
   }
   return true;
}

/*
 * Perform full substitution on Label
 */
static bool perform_full_name_substitution(JCR *jcr, MEDIA_DBR *mr, POOL_DBR *pr)
{
   bool ok = false;
   POOLMEM *label = get_pool_memory(PM_FNAME);
   jcr->NumVols = pr->NumVols;
   if (variable_expansion(jcr, pr->LabelFormat, &label)) {
      bstrncpy(mr->VolumeName, label, sizeof(mr->VolumeName));
      ok = true;
   }
   free_pool_memory(label);
   return ok;
}
