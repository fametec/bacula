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
 *  Status packet definition that is used in both the SD and FD. It
 *    permits Win32 to call output_status() and get the output back
 *    at the callback address line by line, and for Linux code,
 *    the output can be sent directly to a BSOCK.
 *
 *     Kern Sibbald, March MMVII
 *
 */

#ifndef __STATUS_H_
#define __STATUS_H_

#ifdef HAVE_GETRLIMIT
   #include <sys/time.h>
   #include <sys/resource.h>
#endif

/*
 * Packet to send to output_status()
 */
class STATUS_PKT {
public:
  BSOCK *bs;                       /* used on Unix machines */
  void *context;                   /* Win32 */
  void (*callback)(const char *msg, int len, void *context);  /* Win32 */
  char api_opts[MAX_NAME_LENGTH];
  int  api;                        /* set if we want API output, with api level */

  /* Methods */
  STATUS_PKT() { memset(this, 0, sizeof(STATUS_PKT)); };
  ~STATUS_PKT() { };
};

extern void output_status(STATUS_PKT *sp);

/*
 * Send to bsock (Director or Console)
 */
static void sendit(const char *msg, int len, STATUS_PKT *sp)
{
   if (sp->bs) {
      BSOCK *user = sp->bs;
      user->msg = check_pool_memory_size(user->msg, len+1);
      memcpy(user->msg, msg, len+1);
      user->msglen = len+1;
      user->send();
   } else {
      sp->callback(msg, len, sp->context);
   }
}

#ifndef STATUS_FUNCTIONS
#define STATUS_FUNCTIONS

/* common to SD/FD */
static void list_terminated_jobs(STATUS_PKT *sp)
{
   OutputWriter ow(sp->api_opts);
   char dt[MAX_TIME_LENGTH], b1[30], b2[30];
   char level[10];
   struct s_last_job *je;
   const char *msg;
   char *p;

   msg =  _("\nTerminated Jobs:\n");
   if (!sp->api) sendit(msg, strlen(msg), sp);
   if (last_jobs->size() == 0) {
      if (!sp->api) sendit("====\n", 5, sp);
      return;
   }
   lock_last_jobs_list();
   msg =  _(" JobId  Level    Files      Bytes   Status   Finished        Name \n");
   if (!sp->api) sendit(msg, strlen(msg), sp);
   msg =  _("===================================================================\n");
   if (!sp->api) sendit(msg, strlen(msg), sp);
   if (sp->api > 1) {
      p = ow.start_group("terminated");
      sendit(p, strlen(p), sp);
   }
   foreach_dlist(je, last_jobs) {
      char JobName[MAX_NAME_LENGTH];
      const char *termstat;
      char buf[1000];

      bstrftime_nc(dt, sizeof(dt), je->end_time);
      switch (je->JobType) {
      case JT_ADMIN:
         bstrncpy(level, "Admn", sizeof(level));
         break;
      case JT_RESTORE:
         bstrncpy(level, "Rest", sizeof(level));
         break;
      default:
         bstrncpy(level, job_level_to_str(je->JobLevel), sizeof(level));
         level[4] = 0;
         break;
      }
      switch (je->JobStatus) {
      case JS_Created:
         termstat = _("Created");
         break;
      case JS_FatalError:
      case JS_ErrorTerminated:
         termstat = _("Error");
         break;
      case JS_Differences:
         termstat = _("Diffs");
         break;
      case JS_Canceled:
         termstat = _("Cancel");
         break;
      case JS_Terminated:
         termstat = _("OK");
         break;
      case JS_Warnings:
         termstat = _("OK -- with warnings");
         break;
      case JS_Incomplete:
         termstat = _("Incomplete");
         break;
      default:
         termstat = _("Other");
         break;
      }
      bstrncpy(JobName, je->Job, sizeof(JobName));
      /* There are three periods after the Job name */
      char *p;
      for (int i=0; i<3; i++) {
         if ((p=strrchr(JobName, '.')) != NULL) {
            *p = 0;
         }
      }
      if (sp->api == 1) {
         bsnprintf(buf, sizeof(buf), _("%6d\t%-6s\t%8s\t%10s\t%-7s\t%-8s\t%s\n"),
            je->JobId,
            level,
            edit_uint64_with_commas(je->JobFiles, b1),
            edit_uint64_with_suffix(je->JobBytes, b2),
            termstat,
            dt, JobName);

      } else if (sp->api > 1) {
         p = ow.get_output(OT_CLEAR,
                           OT_START_OBJ,
                           OT_INT,     "jobid",     je->JobId,
                           OT_JOBLEVEL,"level",     je->JobLevel,
                           OT_JOBTYPE, "type",      je->JobType,
                           OT_JOBSTATUS,"status",    je->JobStatus,
                           OT_STRING,  "status_desc",termstat,
                           OT_SIZE,    "jobbytes",  je->JobBytes,
                           OT_INT32,   "jobfiles",  je->JobFiles,
                           OT_STRING,  "job",       je->Job,
                           OT_STRING,  "name",      JobName,
                           OT_UTIME,   "starttime", je->start_time,
                           OT_UTIME,   "endtime",   je->end_time,
                           OT_INT,     "errors",    je->Errors,
                           OT_END_OBJ,
                           OT_END);
         sendit(p, strlen(p), sp);


      } else {
         bsnprintf(buf, sizeof(buf), _("%6d  %-6s %8s %10s  %-7s  %-8s %s\n"),
            je->JobId,
            level,
            edit_uint64_with_commas(je->JobFiles, b1),
            edit_uint64_with_suffix(je->JobBytes, b2),
            termstat,
            dt, JobName);
      }
      sendit(buf, strlen(buf), sp);
   }
   unlock_last_jobs_list();
   if (!sp->api) {
      sendit("====\n", 5, sp);
   } else if (sp->api > 1) {
      p = ow.end_group(false);
      sendit(p, strlen(p), sp);
   }
}

#if defined(HAVE_WIN32)
int bacstat = 0;

#ifdef FILE_DAEMON
# define BAC_COMPONENT "Client"
#else
# define BAC_COMPONENT "Storage"
#endif

/* Return a one line status for the tray monitor */
char *bac_status(char *buf, int buf_len)
{
   JCR *njcr;
   const char *termstat = _("Bacula " BAC_COMPONENT ": Idle");
   struct s_last_job *job;
   int stat = 0;                      /* Idle */

   if (!last_jobs) {
      goto done;
   }
   Dmsg0(1000, "Begin bac_status jcr loop.\n");
   foreach_jcr(njcr) {
      if (njcr->JobId != 0) {
         stat = JS_Running;
         termstat = _("Bacula " BAC_COMPONENT ": Running");
         break;
      }
   }
   endeach_jcr(njcr);

   if (stat != 0) {
      goto done;
   }
   if (last_jobs->size() > 0) {
      job = (struct s_last_job *)last_jobs->last();
      stat = job->JobStatus;
      switch (job->JobStatus) {
      case JS_Canceled:
         termstat = _("Bacula " BAC_COMPONENT ": Last Job Canceled");
         break;
      case JS_ErrorTerminated:
      case JS_FatalError:
         termstat = _("Bacula " BAC_COMPONENT ": Last Job Failed");
         break;
      default:
         if (job->Errors) {
            termstat = _("Bacula " BAC_COMPONENT ": Last Job had Warnings");
         }
         break;
      }
   }
   Dmsg0(1000, "End bac_status jcr loop.\n");
done:
   bacstat = stat;
   if (buf) {
      bstrncpy(buf, termstat, buf_len);
   }
   return buf;
}

#endif /* HAVE_WIN32 */

#endif  /* ! STATUS_FUNCTIONS */

#endif
