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
 *  Bacula File Daemon Status routines
 *
 *    Kern Sibbald, August MMI
 *
 */

#include "bacula.h"
#include "filed.h"
#include "lib/status.h"

extern void *start_heap;

extern bool GetWindowsVersionString(char *buf, int maxsiz);


/* Forward referenced functions */
static void  list_running_jobs(STATUS_PKT *sp);
static void  list_status_header(STATUS_PKT *sp);

/* Static variables */
static char qstatus1[] = ".status %127s\n";
static char qstatus2[] = ".status %127s api=%d api_opts=%127s";

static char OKqstatus[]   = "2000 OK .status\n";
static char DotStatusJob[] = "JobId=%d JobStatus=%c JobErrors=%d\n";

#if defined(HAVE_WIN32)
static int privs = 0;
#endif
#ifdef WIN32_VSS
#include "vss.h"
#define VSS " VSS"
#else
#define VSS ""
#endif

/*
 * General status generator
 */
void output_status(STATUS_PKT *sp)
{
   list_status_header(sp);
   list_running_jobs(sp);
   list_terminated_jobs(sp);    /* defined in lib/status.h */
}

#if defined(HAVE_LZO)
static const bool have_lzo = true;
#else
static const bool have_lzo = false;
#endif


static void api_list_status_header(STATUS_PKT *sp)
{
   char *p;
   char buf[300];
   OutputWriter wt(sp->api_opts);
   *buf = 0;

#if defined(HAVE_WIN32)
   if (!GetWindowsVersionString(buf, sizeof(buf))) {
      *buf = 0;
   }
#endif

   wt.start_group("header");
   wt.get_output(
      OT_STRING, "name",        my_name,
      OT_STRING, "version",     VERSION " (" BDATE ")",
      OT_STRING, "uname",       HOST_OS " " DISTNAME " " DISTVER,
      OT_UTIME,  "started",     daemon_start_time,
      OT_INT64,  "pid",         (int64_t)getpid(),
      OT_INT,    "jobs_run",    num_jobs_run,
      OT_INT,    "jobs_running",job_count(),
      OT_STRING, "winver",      buf,
      OT_INT64,  "debug",       debug_level,
      OT_INT,    "trace",       get_trace(),
      OT_INT64,  "bwlimit",     me->max_bandwidth_per_job,
      OT_PLUGINS, "plugins",    b_plugin_list,
      OT_END);
   p = wt.end_group();
   sendit(p, strlen(p), sp);
}

static void  list_status_header(STATUS_PKT *sp)
{
   POOL_MEM msg(PM_MESSAGE);
   char b1[32], b2[32], b3[32], b4[32], b5[35];
   int64_t memused = (char *)sbrk(0)-(char *)start_heap;
   int len;
   char dt[MAX_TIME_LENGTH];

   if (sp->api) {
      api_list_status_header(sp);
      return;
   }

   len = Mmsg(msg, _("%s %sVersion: %s (%s) %s %s %s %s\n"),
              my_name, BDEMO, VERSION, BDATE, VSS, HOST_OS,
              DISTNAME, DISTVER);
   sendit(msg.c_str(), len, sp);
   bstrftime_nc(dt, sizeof(dt), daemon_start_time);
   len = Mmsg(msg, _("Daemon started %s. Jobs: run=%d running=%d.\n"),
        dt, num_jobs_run, job_count());
   sendit(msg.c_str(), len, sp);
#if defined(HAVE_WIN32)
   char buf[300];
   if (GetWindowsVersionString(buf, sizeof(buf))) {
      len = Mmsg(msg, "%s\n", buf);
      sendit(msg.c_str(), len, sp);
   }
   memused = get_memory_info(buf, sizeof(buf));
   if (debug_level > 0) {
      if (!privs) {
         privs = enable_backup_privileges(NULL, 1);
      }
      len = Mmsg(msg, "Priv 0x%x\n", privs);
      sendit(msg.c_str(), len, sp);

      /* Display detailed information that we got from get_memory_info() */
      len = Mmsg(msg, "Memory: %s\n", buf);
      sendit(msg.c_str(), len, sp);

      len = Mmsg(msg, "APIs=%sOPT,%sATP,%sLPV,%sCFA,%sCFW,\n",
                 p_OpenProcessToken?"":"!",
                 p_AdjustTokenPrivileges?"":"!",
                 p_LookupPrivilegeValue?"":"!",
                 p_CreateFileA?"":"!",
                 p_CreateFileW?"":"!");
      sendit(msg.c_str(), len, sp);
      len = Mmsg(msg, " %sWUL,%sWMKD,%sGFAA,%sGFAW,%sGFAEA,%sGFAEW,%sSFAA,%sSFAW,%sBR,%sBW,%sSPSP,\n",
                 p_wunlink?"":"!",
                 p_wmkdir?"":"!",
                 p_GetFileAttributesA?"":"!",
                 p_GetFileAttributesW?"":"!",
                 p_GetFileAttributesExA?"":"!",
                 p_GetFileAttributesExW?"":"!",
                 p_SetFileAttributesA?"":"!",
                 p_SetFileAttributesW?"":"!",
                 p_BackupRead?"":"!",
                 p_BackupWrite?"":"!",
                 p_SetProcessShutdownParameters?"":"!");
      sendit(msg.c_str(), len, sp);
      len = Mmsg(msg, " %sWC2MB,%sMB2WC,%sFFFA,%sFFFW,%sFNFA,%sFNFW,%sSCDA,%sSCDW,\n",
                 p_WideCharToMultiByte?"":"!",
                 p_MultiByteToWideChar?"":"!",
                 p_FindFirstFileA?"":"!",
                 p_FindFirstFileW?"":"!",
                 p_FindNextFileA?"":"!",
                 p_FindNextFileW?"":"!",
                 p_SetCurrentDirectoryA?"":"!",
                 p_SetCurrentDirectoryW?"":"!");
      sendit(msg.c_str(), len, sp);
      len = Mmsg(msg, " %sGCDA,%sGCDW,%sGVPNW,%sGVNFVMPW,%sLZO,%sEFS\n",
                 p_GetCurrentDirectoryA?"":"!",
                 p_GetCurrentDirectoryW?"":"!",
                 p_GetVolumePathNameW?"":"!",
                 p_GetVolumeNameForVolumeMountPointW?"":"!",
                 have_lzo?"":"!",
                 "!");
      sendit(msg.c_str(), len, sp);
   }
#endif
   len = Mmsg(msg, _(" Heap: heap=%s smbytes=%s max_bytes=%s bufs=%s max_bufs=%s\n"),
         edit_uint64_with_commas(memused, b1),
         edit_uint64_with_commas(sm_bytes, b2),
         edit_uint64_with_commas(sm_max_bytes, b3),
         edit_uint64_with_commas(sm_buffers, b4),
         edit_uint64_with_commas(sm_max_buffers, b5));
   sendit(msg.c_str(), len, sp);
   len = Mmsg(msg, _(" Sizes: boffset_t=%d size_t=%d debug=%s trace=%d "
                     "mode=%d,%d bwlimit=%skB/s\n"),
              sizeof(boffset_t), sizeof(size_t),
              edit_uint64(debug_level, b2), get_trace(), (int)DEVELOPER_MODE, 0,
              edit_uint64_with_commas(me->max_bandwidth_per_job/1024, b1));
   sendit(msg.c_str(), len, sp);
   if (b_plugin_list && b_plugin_list->size() > 0) {
      Plugin *plugin;
      int len;
      pm_strcpy(msg, " Plugin: ");
      foreach_alist(plugin, b_plugin_list) {
         len = pm_strcat(msg, plugin->file);
         /* Print plugin version when debug activated */
         if (debug_level > 0 && plugin->pinfo) {
            pInfo *info = (pInfo *)plugin->pinfo;
            pm_strcat(msg, "(");
            pm_strcat(msg, NPRT(info->plugin_version));
            len = pm_strcat(msg, ")");
         }
         if (len > 80) {
            pm_strcat(msg, "\n   ");
         } else {
            pm_strcat(msg, " ");
         }
      }
      len = pm_strcat(msg, "\n");
      sendit(msg.c_str(), len, sp);
   }
}

/*
 * List running jobs in for humans.
 */
static void  list_running_jobs_plain(STATUS_PKT *sp)
{
   int total_sec, inst_sec;
   uint64_t total_bps, inst_bps;
   POOL_MEM msg(PM_MESSAGE);
   char b1[50], b2[50], b3[50], b4[50], b5[50], b6[50];
   int len;
   bool found = false;
   JCR *njcr;
   time_t now = time(NULL);
   char dt[MAX_TIME_LENGTH];

   Dmsg0(1000, "Begin status jcr loop.\n");
   len = Mmsg(msg, _("\nRunning Jobs:\n"));
   sendit(msg.c_str(), len, sp);
   foreach_jcr(njcr) {
      const char *vss = "";
#ifdef WIN32_VSS
      if (njcr->pVSSClient && njcr->pVSSClient->IsInitialized()) {
         vss = "VSS ";
      }
#endif
      bstrftime_nc(dt, sizeof(dt), njcr->start_time);
      if (njcr->JobId == 0) {
         len = Mmsg(msg, _("Director connected %sat: %s\n"),
                    (njcr->dir_bsock && njcr->dir_bsock->tls)?_("using TLS "):"",
                    dt);
      } else {
         len = Mmsg(msg, _("JobId %d Job %s is running.\n"),
                    njcr->JobId, njcr->Job);
         sendit(msg.c_str(), len, sp);
         len = Mmsg(msg, _("    %s%s %s Job started: %s\n"),
                    vss, job_level_to_str(njcr->getJobLevel()),
                    job_type_to_str(njcr->getJobType()), dt);
      }
      sendit(msg.c_str(), len, sp);
      if (njcr->JobId == 0) {
         continue;
      }
      if (njcr->last_time == 0) {
         njcr->last_time = njcr->start_time;
      }
      total_sec = now - njcr->start_time;
      inst_sec = now - njcr->last_time;
      if (total_sec <= 0) {
         total_sec = 1;
      }
      if (inst_sec <= 0) {
         inst_sec = 1;
      }
      /* Instanteous bps not smoothed */
      inst_bps = (njcr->JobBytes - njcr->LastJobBytes) / inst_sec;
      if (njcr->LastRate <= 0) {
         njcr->LastRate = inst_bps;
      }
      /* Smooth the instantaneous bps a bit */
      inst_bps = (2 * njcr->LastRate + inst_bps) / 3;
      /* total bps (AveBytes/sec) since start of job */
      total_bps = njcr->JobBytes / total_sec;
      len = Mmsg(msg,  _("    Files=%s Bytes=%s AveBytes/sec=%s LastBytes/sec=%s Errors=%d\n"
                         "    Bwlimit=%s ReadBytes=%s\n"),
           edit_uint64_with_commas(njcr->JobFiles, b1),
           edit_uint64_with_commas(njcr->JobBytes, b2),
           edit_uint64_with_commas(total_bps, b3),
           edit_uint64_with_commas(inst_bps, b4),
           njcr->JobErrors, edit_uint64_with_commas(njcr->max_bandwidth, b5),
           edit_uint64_with_commas(njcr->ReadBytes, b6));
      sendit(msg.c_str(), len, sp);

      if (njcr->is_JobType(JT_RESTORE)) {
         if (njcr->ExpectedFiles > 0) {
            len = Mmsg(msg, _("    Files: Restored=%s Expected=%s Completed=%d%%\n"),
                       edit_uint64_with_commas(njcr->num_files_examined, b1),
                       edit_uint64_with_commas(njcr->ExpectedFiles, b2),
                       (100*njcr->num_files_examined)/njcr->ExpectedFiles);

         } else {
            len = Mmsg(msg, _("    Files: Restored=%s\n"),
                       edit_uint64_with_commas(njcr->num_files_examined, b1));
         }
      } else {
         len = Mmsg(msg, _("    Files: Examined=%s Backed up=%s\n"),
            edit_uint64_with_commas(njcr->num_files_examined, b1),
            edit_uint64_with_commas(njcr->JobFiles, b2));
      }
      /* Update only every 10 seconds */
      if (now - njcr->last_time > 10) {
         njcr->LastRate = inst_bps;
         njcr->LastJobBytes = njcr->JobBytes;
         njcr->last_time = now;
      }
      sendit(msg.c_str(), len, sp);
      if (njcr->JobFiles > 0) {
         njcr->lock();
         len = Mmsg(msg, _("    Processing file: %s\n"), njcr->last_fname);
         njcr->unlock();
         sendit(msg.c_str(), len, sp);
      }

      found = true;
      if (njcr->store_bsock) {
         len = Mmsg(msg, "    SDReadSeqNo=%" lld " fd=%d SDtls=%d\n",
                    njcr->store_bsock->read_seqno, njcr->store_bsock->m_fd,
                    (njcr->store_bsock->tls)?1:0);
         sendit(msg.c_str(), len, sp);
      } else {
         len = Mmsg(msg, _("    SDSocket closed.\n"));
         sendit(msg.c_str(), len, sp);
      }
   }
   endeach_jcr(njcr);

   if (!found) {
      len = Mmsg(msg, _("No Jobs running.\n"));
      sendit(msg.c_str(), len, sp);
   }
   sendit(_("====\n"), 5, sp);
}

/*
 * List running jobs for Bat or Bweb in a format
 *  simpler to parse. Be careful when changing this
 *  subroutine.
 */
static void  list_running_jobs_api(STATUS_PKT *sp)
{
   OutputWriter ow(sp->api_opts);
   int sec, bps;
   char *p;
   JCR *njcr;

   /* API v1, edit with comma, space before the name, sometime ' ' as separator */

   foreach_jcr(njcr) {
      int vss = 0;
#ifdef WIN32_VSS
      if (njcr->pVSSClient && njcr->pVSSClient->IsInitialized()) {
         vss = 1;
      }
#endif
      p = ow.get_output(OT_CLEAR, OT_START_OBJ, OT_END);

      if (njcr->JobId == 0) {
         int val = (njcr->dir_bsock && njcr->dir_bsock->tls)?1:0;
         ow.get_output(OT_UTIME, "DirectorConnected", njcr->start_time,
                       OT_INT, "DirTLS", val,
                       OT_END);
      } else {
         ow.get_output(OT_INT32,   "JobId", njcr->JobId,
                       OT_STRING,  "Job",   njcr->Job,
                       OT_INT,     "VSS",   vss,
                       OT_JOBLEVEL,"Level", njcr->getJobLevel(),
                       OT_JOBTYPE, "Type",  njcr->getJobType(),
                       OT_JOBSTATUS, "Status", njcr->getJobStatus(),
                       OT_UTIME,   "StartTime", njcr->start_time,
                       OT_END);

      }
      sendit(p, strlen(p), sp);
      if (njcr->JobId == 0) {
         continue;
      }
      sec = time(NULL) - njcr->start_time;
      if (sec <= 0) {
         sec = 1;
      }
      bps = (int)(njcr->JobBytes / sec);
      ow.get_output(OT_CLEAR,
                    OT_INT32,   "JobFiles",  njcr->JobFiles,
                    OT_SIZE,    "JobBytes",  njcr->JobBytes,
                    OT_INT,     "Bytes/sec", bps,
                    OT_INT,     "Errors",    njcr->JobErrors,
                    OT_INT64,   "Bwlimit",   njcr->max_bandwidth,
                    OT_SIZE,    "ReadBytes", njcr->ReadBytes,
                    OT_END);

      ow.get_output(OT_INT32,  "Files Examined",  njcr->num_files_examined, OT_END);

      if (njcr->is_JobType(JT_RESTORE) && njcr->ExpectedFiles > 0) {
         ow.get_output(OT_INT32,  "Expected Files",  njcr->ExpectedFiles,
                       OT_INT32,  "Percent Complete", 100*(njcr->num_files_examined/njcr->ExpectedFiles),
                       OT_END);
      }

      sendit(p, strlen(p), sp);
      ow.get_output(OT_CLEAR, OT_END);

      if (njcr->JobFiles > 0) {
         njcr->lock();
         ow.get_output(OT_STRING,  "Processing file", njcr->last_fname, OT_END);
         njcr->unlock();
      }

      if (njcr->store_bsock) {
         int val = (njcr->store_bsock->tls)?1:0;
         ow.get_output(OT_INT64, "SDReadSeqNo", (int64_t)njcr->store_bsock->read_seqno,
                       OT_INT,   "fd",          njcr->store_bsock->m_fd,
                       OT_INT,   "SDtls",       val,
                       OT_END);
      } else {
         ow.get_output(OT_STRING, "SDSocket", "closed", OT_END);
      }
      ow.get_output(OT_END_OBJ, OT_END);
      sendit(p, strlen(p), sp);
   }
   endeach_jcr(njcr);
}

static void  list_running_jobs(STATUS_PKT *sp)
{
   if (sp->api) {
      list_running_jobs_api(sp);
   } else {
      list_running_jobs_plain(sp);
   }
}

/*
 * Status command from Director
 */
int status_cmd(JCR *jcr)
{
   BSOCK *user = jcr->dir_bsock;
   STATUS_PKT sp;

   user->fsend("\n");
   sp.bs = user;
   sp.api = false;                         /* no API output */
   output_status(&sp);

   user->signal(BNET_EOD);
   return 1;
}

/*
 * .status command from Director
 */
int qstatus_cmd(JCR *jcr)
{
   BSOCK *dir = jcr->dir_bsock;
   POOLMEM *cmd;
   JCR *njcr;
   s_last_job* job;
   STATUS_PKT sp;

   sp.bs = dir;
   cmd = get_memory(dir->msglen+1);

   if (sscanf(dir->msg, qstatus2, cmd, &sp.api, sp.api_opts) != 3) {
      if (sscanf(dir->msg, qstatus1, cmd) != 1) {
         pm_strcpy(&jcr->errmsg, dir->msg);
         Jmsg1(jcr, M_FATAL, 0, _("Bad .status command: %s\n"), jcr->errmsg);
         dir->fsend(_("2900 Bad .status command, missing argument.\n"));
         dir->signal(BNET_EOD);
         free_memory(cmd);
         return 0;
      }
   }
   unbash_spaces(cmd);

   if (strcasecmp(cmd, "current") == 0) {
      dir->fsend(OKqstatus, cmd);
      foreach_jcr(njcr) {
         if (njcr->JobId != 0) {
            dir->fsend(DotStatusJob, njcr->JobId, njcr->JobStatus, njcr->JobErrors);
         }
      }
      endeach_jcr(njcr);
   } else if (strcasecmp(cmd, "last") == 0) {
      dir->fsend(OKqstatus, cmd);
      if ((last_jobs) && (last_jobs->size() > 0)) {
         job = (s_last_job*)last_jobs->last();
         dir->fsend(DotStatusJob, job->JobId, job->JobStatus, job->Errors);
      }
   } else if (strcasecmp(cmd, "header") == 0) {
       sp.api = true;
       list_status_header(&sp);
   } else if (strcasecmp(cmd, "running") == 0) {
       sp.api = true;
       list_running_jobs(&sp);
   } else if (strcasecmp(cmd, "terminated") == 0) {
       sp.api = MAX(sp.api, 1);
       list_terminated_jobs(&sp); /* defined in lib/status.h */
   } else {
      pm_strcpy(&jcr->errmsg, dir->msg);
      Jmsg1(jcr, M_FATAL, 0, _("Bad .status command: %s\n"), jcr->errmsg);
      dir->fsend(_("2900 Bad .status command, wrong argument.\n"));
      dir->signal(BNET_EOD);
      free_memory(cmd);
      return 0;
   }

   dir->signal(BNET_EOD);
   free_memory(cmd);
   return 1;
}
