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
 *  Routines for getting and displaying tape alerts
 *
 *   Written by Kern Sibbald, October MMXVI
 *
 */

#include "bacula.h"                   /* pull in global headers */
#include "stored.h"                   /* pull in Storage Deamon headers */

#include "tape_alert_msgs.h"

static const int dbglvl = 120;

#define MAX_MSG 54                    /* Maximum alert message number */

void alert_callback(void *ctx, const char *short_msg, const char *long_msg,
       char *Volume, int severity, int flags, int alertno, utime_t alert_time)
{
   DCR *dcr = (DCR *)ctx;
   JCR *jcr = dcr->jcr;
   DEVICE *dev = dcr->dev;
   int type = M_INFO;

   switch (severity) {
   case 'C':
      type = M_FATAL;
      break;
   case 'W':
      type = M_WARNING;
      break;
   case 'I':
      type = M_INFO;
      break;
   }
   if (flags & TA_DISABLE_DRIVE) {
      dev->enabled = false;
      Jmsg(jcr, M_WARNING, 0, _("Disabled Device %s due to tape alert=%d.\n"),
         dev->print_name(), alertno);
      Tmsg2(dbglvl, _("Disabled Device %s due to tape alert=%d.\n"),
         dev->print_name(), alertno);
   }
   if (flags & TA_DISABLE_VOLUME) {
      dev->setVolCatStatus("Disabled");
      dev->VolCatInfo.VolEnabled = false;
      dir_update_volume_info(dcr, false, true);
      Jmsg(jcr, M_WARNING, 0, _("Disabled Volume \"%s\" due to tape alert=%d.\n"),
           Volume, alertno);
      Tmsg2(dbglvl, _("Disabled Volume \"%s\" due to tape alert=%d.\n"),
            Volume, alertno);
   }
   Jmsg(jcr, type, (utime_t)alert_time, _("Alert: Volume=\"%s\" alert=%d: ERR=%s\n"),
      Volume, alertno, long_msg);
}

bool tape_dev::get_tape_alerts(DCR *dcr)
{
   JCR *jcr = dcr->jcr;

   if (!job_canceled(jcr) && dcr->device->alert_command &&
       dcr->device->control_name) {
      POOLMEM *alertcmd;
      int status = 1;
      int nalerts = 0;
      BPIPE *bpipe;
      ALERT *alert, *rmalert;
      char line[MAXSTRING];
      const char *fmt = "TapeAlert[%d]";

      if (!alert_list) {
         alert_list = New(alist(10));
      }
      alertcmd = get_pool_memory(PM_FNAME);
      alertcmd = edit_device_codes(dcr, alertcmd, dcr->device->alert_command, "");
      /* Wait maximum 5 minutes */
      bpipe = open_bpipe(alertcmd, 60 * 5, "r");
      if (bpipe) {
         int alertno;
         alert = (ALERT *)malloc(sizeof(ALERT));
         memset(alert->alerts, 0, sizeof(alert->alerts));
         alert->Volume = bstrdup(getVolCatName());
         alert->alert_time = (utime_t)time(NULL);
         while (fgets(line, (int)sizeof(line), bpipe->rfd)) {
            alertno = 0;
            if (bsscanf(line, fmt, &alertno) == 1) {
               if (alertno > 0) {
                  if (nalerts+1 > (int)sizeof(alert->alerts)) {
                     break;
                  } else {
                     alert->alerts[nalerts++] = alertno;
                  }
               }
            }
         }
         status = close_bpipe(bpipe);
         if (nalerts > 0) {
             /* Maintain First in, last out list */
             if (alert_list->size() > 8) {
                rmalert = (ALERT *)alert_list->last();
                free(rmalert->Volume);
                alert_list->pop();
                free(rmalert);
             }
            alert_list->prepend(alert);
         } else {
            free(alert->Volume);
            free(alert);
         }
         free_pool_memory(alertcmd);
         return true;
      } else {
         status = errno;
      }
      if (status != 0) {
         berrno be;
         Jmsg(jcr, M_ALERT, 0, _("3997 Bad alert command: %s: ERR=%s.\n"),
              alertcmd, be.bstrerror(status));
         Tmsg2(10, _("3997 Bad alert command: %s: ERR=%s.\n"),
              alertcmd, be.bstrerror(status));
      }

      Dmsg1(400, "alert status=%d\n", status);
      free_pool_memory(alertcmd);
   } else {
      if (!dcr->device->alert_command) {
         Dmsg1(dbglvl, "Cannot do tape alerts: no Alert Command specified for device %s\n",
            print_name());
         Tmsg1(dbglvl, "Cannot do tape alerts: no Alert Command specified for device %s\n",
            print_name());

      }
      if (!dcr->device->control_name) {
         Dmsg1(dbglvl, "Cannot do tape alerts: no Control Device specified for device %s\n",
            print_name());
         Tmsg1(dbglvl, "Cannot do tape alerts: no Control Device specified for device %s\n",
            print_name());
      }
   }
   return false;
}


/*
 * Print desired tape alert messages
 */
void tape_dev::show_tape_alerts(DCR *dcr, alert_list_type list_type,
        alert_list_which which, alert_cb alert_callback)
{
   int i;
   ALERT *alert;
   int code;

   if (!alert_list) {
      return;
   }
   Dmsg1(dbglvl, "There are %d alerts.\n", alert_list->size());
   switch (list_type) {
   case list_codes:
      foreach_alist(alert, alert_list) {
         for (i=0; i<(int)sizeof(alert->alerts) && alert->alerts[i]; i++) {
            code = alert->alerts[i];
            Dmsg4(dbglvl, "Volume=%s alert=%d severity=%c flags=0x%x\n", alert->Volume, code,
               ta_errors[code].severity, (int)ta_errors[code].flags);
            alert_callback(dcr, ta_errors[code].short_msg, long_msg[code],
               alert->Volume, ta_errors[code].severity,
               ta_errors[code].flags,  code, (utime_t)alert->alert_time);
         }
         if (which == list_last) {
            break;
         }
      }
      break;
   default:
      foreach_alist(alert, alert_list) {
         for (i=0; i<(int)sizeof(alert->alerts) && alert->alerts[i]; i++) {
            code = alert->alerts[i];
            Dmsg4(dbglvl, "Volume=%s severity=%c flags=0x%x alert=%s\n", alert->Volume,
               ta_errors[code].severity, (int)ta_errors[code].flags,
               ta_errors[code].short_msg);
            alert_callback(dcr, ta_errors[code].short_msg, long_msg[code],
               alert->Volume, ta_errors[code].severity,
               ta_errors[code].flags, code, (utime_t)alert->alert_time);
         }
         if (which == list_last) {
            break;
         }
      }
      break;
   }
   return;
}

/*
 * Delete alert list returning number deleted
 */
int tape_dev::delete_alerts()
{
   ALERT *alert;
   int deleted = 0;

   if (alert_list) {
      foreach_alist(alert, alert_list) {
         free(alert->Volume);
         deleted++;
      }
      alert_list->destroy();
      free(alert_list);
      alert_list = NULL;
   }
   return deleted;
}
