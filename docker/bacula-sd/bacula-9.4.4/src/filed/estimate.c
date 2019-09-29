/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2015 Kern Sibbald

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
 *  Bacula File Daemon estimate.c
 *   Make and estimate of the number of files and size to be saved.
 *
 *    Kern Sibbald, September MMI
 *
 *   Version $Id$
 *
 */

#include "bacula.h"
#include "filed.h"

static int tally_file(JCR *jcr, FF_PKT *ff_pkt, bool);

/*
 * Find all the requested files and count them.
 */
int make_estimate(JCR *jcr)
{
   int stat;

   jcr->setJobStatus(JS_Running);

   set_find_options((FF_PKT *)jcr->ff, jcr->incremental, jcr->mtime);
   /* in accurate mode, we overwrite the find_one check function */
   if (jcr->accurate) {
      set_find_changed_function((FF_PKT *)jcr->ff, accurate_check_file);
   }

   stat = find_files(jcr, (FF_PKT *)jcr->ff, tally_file, plugin_estimate);
   accurate_free(jcr);
   return stat;
}

/*
 * Called here by find() for each file included.
 *
 */
static int tally_file(JCR *jcr, FF_PKT *ff_pkt, bool top_level)
{
   ATTR attr;

   if (job_canceled(jcr)) {
      return 0;
   }
   switch (ff_pkt->type) {
   case FT_LNKSAVED:                  /* Hard linked, file already saved */
   case FT_REGE:
   case FT_REG:
   case FT_LNK:
   case FT_NORECURSE:
   case FT_NOFSCHG:
   case FT_INVALIDFS:
   case FT_INVALIDDT:
   case FT_REPARSE:
   case FT_JUNCTION:
   case FT_DIREND:
   case FT_SPEC:
   case FT_RAW:
   case FT_FIFO:
      break;
   case FT_DIRBEGIN:
   case FT_NOACCESS:
   case FT_NOFOLLOW:
   case FT_NOSTAT:
   case FT_DIRNOCHG:
   case FT_NOCHG:
   case FT_ISARCH:
   case FT_NOOPEN:
   default:
      return 1;
   }

   if (ff_pkt->type != FT_LNKSAVED && S_ISREG(ff_pkt->statp.st_mode)) {
      if (ff_pkt->statp.st_size > 0) {
         jcr->JobBytes += ff_pkt->statp.st_size;
      }
#ifdef HAVE_DARWIN_OS
      if (ff_pkt->flags & FO_HFSPLUS) {
         if (ff_pkt->hfsinfo.rsrclength > 0) {
            jcr->JobBytes += ff_pkt->hfsinfo.rsrclength;
         }
         jcr->JobBytes += 32;    /* Finder info */
      }
#endif
   }
   jcr->num_files_examined++;
   jcr->JobFiles++;                  /* increment number of files seen */
   if (jcr->listing) {
      memcpy(&attr.statp, &ff_pkt->statp, sizeof(struct stat));
      attr.type = ff_pkt->type;
      attr.ofname = (POOLMEM *)ff_pkt->fname;
      attr.olname = (POOLMEM *)ff_pkt->link;
      print_ls_output(jcr, &attr);
   }
   /* TODO: Add loop over jcr->file_list to get Accurate deleted files*/
   return 1;
}
