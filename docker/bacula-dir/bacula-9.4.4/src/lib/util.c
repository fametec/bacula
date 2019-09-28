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
 *   util.c  miscellaneous utility subroutines for Bacula
 *
 *    Kern Sibbald, MM
 */

#include "bacula.h"
#include "jcr.h"
#include "findlib/find.h"

/*
 * Various Bacula Utility subroutines
 *
 */

bool is_null(const void *ptr)
{
   return ptr == NULL;
}

/* Return true of buffer has all zero bytes */
bool is_buf_zero(const char *buf, int len)
{
   uint64_t *ip;
   const char *p;
   int i, len64, done, rem;

   if (buf[0] != 0) {
      return false;
   }
   ip = (uint64_t *)buf;
   /* Optimize by checking uint64_t for zero */
   len64 = len / sizeof(uint64_t);
   for (i=0; i < len64; i++) {
      if (ip[i] != 0) {
         return false;
      }
   }
   done = len64 * sizeof(uint64_t);  /* bytes already checked */
   p = buf + done;
   rem = len - done;
   for (i = 0; i < rem; i++) {
      if (p[i] != 0) {
         return false;
      }
   }
   return true;
}

/*
 * Subroutine that cannot be suppressed by GCC 6.0
 */
void bmemzero(void *buf, size_t size)
{
   memset(buf, 0, size);
   return;
}


/* Convert a string in place to lower case */
void lcase(char *str)
{
   while (*str) {
      if (B_ISUPPER(*str)) {
         *str = tolower((int)(*str));
       }
       str++;
   }
}

/* Convert spaces to non-space character.
 * This makes scanf of fields containing spaces easier.
 */
void
bash_spaces(char *str)
{
   while (*str) {
      if (*str == ' ')
         *str = 0x1;
      str++;
   }
}

/* Convert spaces to non-space character.
 * This makes scanf of fields containing spaces easier.
 */
void
bash_spaces(POOL_MEM &pm)
{
   char *str = pm.c_str();
   while (*str) {
      if (*str == ' ')
         *str = 0x1;
      str++;
   }
}


/* Convert non-space characters (0x1) back into spaces */
void
unbash_spaces(char *str)
{
   while (*str) {
     if (*str == 0x1)
        *str = ' ';
     str++;
   }
}

/* Convert non-space characters (0x1) back into spaces */
void
unbash_spaces(POOL_MEM &pm)
{
   char *str = pm.c_str();
   while (*str) {
     if (*str == 0x1)
        *str = ' ';
     str++;
   }
}

char *encode_time(utime_t utime, char *buf)
{
   struct tm tm;
   int n = 0;
   time_t time = utime;

#if defined(HAVE_WIN32)
   /*
    * Avoid a seg fault in Microsoft's CRT localtime_r(),
    *  which incorrectly references a NULL returned from gmtime() if
    *  time is negative before or after the timezone adjustment.
    */
   struct tm *gtm;

   if ((gtm = gmtime(&time)) == NULL) {
      return buf;
   }

   if (gtm->tm_year == 1970 && gtm->tm_mon == 1 && gtm->tm_mday < 3) {
      return buf;
   }
#endif

   if (localtime_r(&time, &tm)) {
      n = sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d",
                   tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                   tm.tm_hour, tm.tm_min, tm.tm_sec);
   }
   return buf+n;
}



static char hexatable[]="0123456789abcdef";

/*
 * do an hexadump of data[0:len] into buf[0:capacity]
 * a space is inserted between every 4 bytes
 * usage:
 *    char buf[10];
 *    Dmsg2("msglen=%d msg=%s", fd->msglen, hexdump(fd->msg, fd->msglen, buf, sizeof(buf));
 * ==>
 *    msglen=36 msg=12345678 12345678
 */
char *hexdump(const char *data, int len, char *buf, int capacity, bool add_spaces)
{
   char *b=buf;
   int i=0;
   while (i<len && capacity>2) {
      if (add_spaces && i>0 && i%4==0 ) {
         *(b++)=' ';
         capacity--;
      }
      if (capacity>2) {
         *(b++)=hexatable[(data[i]&0xF0)>>4];
         *(b++)=hexatable[data[i++]&0x0F];
      }
      capacity-=2;
   }
   *b='\0';
   return buf;
}

/*
 * do an ASCII dump of data[0:len] into buf[0:capacity]
 * non printable chars are replaced by hexa "\xx"
 * usage:
 *    char buf[10];
 *    Dmsg2("msglen=%d msg=%s", fd->msglen, asciidump(fd->msg, fd->msglen, buf, sizeof(buf));
 * ==>
 *    msglen=5 msg=abcd\10
 */
char *asciidump(const char *data, int len, char *buf, int capacity)
{
   char *b=buf;
   const unsigned char *p=(const unsigned char *)data;
   if (!data) {
      strncpy(buf, "<NULL>", capacity);
      return buf;
   }
   while (len>0 && capacity>1) {
      if (isprint(*p)) {
         *(b++)=*(p++);
         capacity--;
      } else {
         if (capacity>3) {
            *(b++)='\\';
            *(b++)=hexatable[((*p)&0xF0)>>4];
            *(b++)=hexatable[(*(p++))&0x0F];
         }
         capacity-=3;
      }
      len--;
   }
   *b='\0';
   return buf;
}

char *smartdump(const char *data, int len, char *buf, int capacity, bool *is_ascii)
{
   char *b=buf;
   int l=len;
   int c=capacity;
   const unsigned char *p=(const unsigned char *)data;
   if (!data) {
      strncpy(buf, "<NULL>", capacity);
      return buf;
   }
   if (is_ascii != NULL) {
      *is_ascii = false;
   }
   while (l>0 && c>1) {
      if (isprint(*p)) {
         *(b++)=*(p++);
      } else if (isspace(*p) || *p=='\0') {
         *(b++)=' ';
         p++;
      } else {
         return hexdump(data, len, buf, capacity);
      }
      c--;
      l--;
   }
   *b='\0';
   if (is_ascii != NULL) {
      *is_ascii = true;
   }
   return buf;
}

/*
 * check if x is a power  two
 */
int is_power_of_two(uint64_t x)
{
   while ( x%2 == 0 && x > 1) {
      x /= 2;
   }
   return (x == 1);
}

/*
 * Convert a JobStatus code into a human readable form
 */
void jobstatus_to_ascii(int JobStatus, char *msg, int maxlen)
{
   const char *jobstat;
   char buf[100];

   switch (JobStatus) {
   case JS_Created:
      jobstat = _("Created");
      break;
   case JS_Running:
      jobstat = _("Running");
      break;
   case JS_Blocked:
      jobstat = _("Blocked");
      break;
   case JS_Terminated:
      jobstat = _("OK");
      break;
   case JS_Incomplete:
      jobstat = _("Incomplete job");
      break;
   case JS_FatalError:
   case JS_ErrorTerminated:
      jobstat = _("Error");
      break;
   case JS_Error:
      jobstat = _("Non-fatal error");
      break;
   case JS_Warnings:
      jobstat = _("OK -- with warnings");
      break;
   case JS_Canceled:
      jobstat = _("Canceled");
      break;
   case JS_Differences:
      jobstat = _("Verify differences");
      break;
   case JS_WaitFD:
      jobstat = _("Waiting on FD");
      break;
   case JS_WaitSD:
      jobstat = _("Wait on SD");
      break;
   case JS_WaitMedia:
      jobstat = _("Wait for new Volume");
      break;
   case JS_WaitMount:
      jobstat = _("Waiting for mount");
      break;
   case JS_WaitStoreRes:
      jobstat = _("Waiting for Storage resource");
      break;
   case JS_WaitJobRes:
      jobstat = _("Waiting for Job resource");
      break;
   case JS_WaitClientRes:
      jobstat = _("Waiting for Client resource");
      break;
   case JS_WaitMaxJobs:
      jobstat = _("Waiting on Max Jobs");
      break;
   case JS_WaitStartTime:
      jobstat = _("Waiting for Start Time");
      break;
   case JS_WaitPriority:
      jobstat = _("Waiting on Priority");
      break;
   case JS_DataCommitting:
      jobstat = _("SD committing Data");
      break;
   case JS_DataDespooling:
      jobstat = _("SD despooling Data");
      break;
   case JS_AttrDespooling:
      jobstat = _("SD despooling Attributes");
      break;
   case JS_AttrInserting:
      jobstat = _("Dir inserting Attributes");
      break;

   default:
      if (JobStatus == 0) {
         buf[0] = 0;
      } else {
         bsnprintf(buf, sizeof(buf), _("Unknown Job termination status=%d"), JobStatus);
      }
      jobstat = buf;
      break;
   }
   bstrncpy(msg, jobstat, maxlen);
}

/*
 * Convert a JobStatus code into a human readable form - gui version
 */
void jobstatus_to_ascii_gui(int JobStatus, char *msg, int maxlen)
{
   const char *cnv = NULL;
   switch (JobStatus) {
   case JS_Terminated:
      cnv = _("Completed successfully");
      break;
   case JS_Warnings:
      cnv = _("Completed with warnings");
      break;
   case JS_ErrorTerminated:
      cnv = _("Terminated with errors");
      break;
   case JS_FatalError:
      cnv = _("Fatal error");
      break;
   case JS_Created:
      cnv = _("Created, not yet running");
      break;
   case JS_Canceled:
      cnv = _("Canceled by user");
      break;
   case JS_Differences:
      cnv = _("Verify found differences");
      break;
   case JS_WaitFD:
      cnv = _("Waiting for File daemon");
      break;
   case JS_WaitSD:
      cnv = _("Waiting for Storage daemon");
      break;
   case JS_WaitPriority:
      cnv = _("Waiting for higher priority jobs");
      break;
   case JS_AttrInserting:
      cnv = _("Batch inserting file records");
      break;
   };

   if (cnv) {
      bstrncpy(msg, cnv, maxlen);
   } else {
     jobstatus_to_ascii(JobStatus, msg, maxlen);
   }
}

/*
 * Convert Job Termination Status into a string
 */
const char *job_status_to_str(int status, int errors)
{
   const char *str;

   switch (status) {
   case JS_Terminated:
      if (errors > 0) {
         str = _("OK -- with warnings");
      } else {
         str = _("OK");
      }
      break;
   case JS_Warnings:
      str = _("OK -- with warnings");
      break;
   case JS_ErrorTerminated:
   case JS_Error:
      str = _("Error");
      break;
   case JS_FatalError:
      str = _("Fatal Error");
      break;
   case JS_Canceled:
      str = _("Canceled");
      break;
   case JS_Differences:
      str = _("Differences");
      break;
   case JS_Created:
      str = _("Created");
      break;
   case JS_Incomplete:
      str = _("Incomplete");
      break;
   default:
      str = _("Unknown term code");
      break;
   }
   return str;
}


/*
 * Convert Job Type into a string
 */
const char *job_type_to_str(int type)
{
   const char *str = NULL;

   switch (type) {
   case JT_BACKUP:
      str = _("Backup");
      break;
   case JT_MIGRATED_JOB:
      str = _("Migrated Job");
      break;
   case JT_VERIFY:
      str = _("Verify");
      break;
   case JT_RESTORE:
      str = _("Restore");
      break;
   case JT_CONSOLE:
      str = _("Console");
      break;
   case JT_SYSTEM:
      str = _("System or Console");
      break;
   case JT_ADMIN:
      str = _("Admin");
      break;
   case JT_ARCHIVE:
      str = _("Archive");
      break;
   case JT_JOB_COPY:
      str = _("Job Copy");
      break;
   case JT_COPY:
      str = _("Copy");
      break;
   case JT_MIGRATE:
      str = _("Migrate");
      break;
   case JT_SCAN:
      str = _("Scan");
      break;
   }
   if (!str) {
      str = _("Unknown Type");
   }
   return str;
}

/* Convert ActionOnPurge to string (Truncate, Erase, Destroy)
 */
char *action_on_purge_to_string(int aop, POOL_MEM &ret)
{
   if (aop & ON_PURGE_TRUNCATE) {
      pm_strcpy(ret, _("Truncate"));
   }
   if (!aop) {
      pm_strcpy(ret, _("None"));
   }
   return ret.c_str();
}

/*
 * Convert Job Level into a string
 */
const char *job_level_to_str(int level)
{
   const char *str;

   switch (level) {
   case L_BASE:
      str = _("Base");
      break;
   case L_FULL:
      str = _("Full");
      break;
   case L_INCREMENTAL:
      str = _("Incremental");
      break;
   case L_DIFFERENTIAL:
      str = _("Differential");
      break;
   case L_SINCE:
      str = _("Since");
      break;
   case L_VERIFY_CATALOG:
      str = _("Verify Catalog");
      break;
   case L_VERIFY_INIT:
      str = _("Verify Init Catalog");
      break;
   case L_VERIFY_VOLUME_TO_CATALOG:
      str = _("Verify Volume to Catalog");
      break;
   case L_VERIFY_DISK_TO_CATALOG:
      str = _("Verify Disk to Catalog");
      break;
   case L_VERIFY_DATA:
      str = _("Verify Data");
      break;
   case L_VIRTUAL_FULL:
      str = _("Virtual Full");
      break;
   case L_NONE:
      str = " ";
      break;
   default:
      str = _("Unknown Job Level");
      break;
   }
   return str;
}

const char *volume_status_to_str(const char *status)
{
   int pos;
   const char *vs[] = {
      NT_("Append"),    _("Append"),
      NT_("Archive"),   _("Archive"),
      NT_("Disabled"),  _("Disabled"),
      NT_("Full"),      _("Full"),
      NT_("Used"),      _("Used"),
      NT_("Cleaning"),  _("Cleaning"),
      NT_("Purged"),    _("Purged"),
      NT_("Recycle"),   _("Recycle"),
      NT_("Read-Only"), _("Read-Only"),
      NT_("Error"),     _("Error"),
      NULL,             NULL};

   if (status) {
     for (pos = 0 ; vs[pos] ; pos += 2) {
       if ( !strcmp(vs[pos],status) ) {
         return vs[pos+1];
       }
     }
   }

   return _("Invalid volume status");
}


/***********************************************************************
 * Encode the mode bits into a 10 character string like LS does
 ***********************************************************************/

char *encode_mode(mode_t mode, char *buf)
{
  char *cp = buf;

  *cp++ = S_ISDIR(mode) ? 'd' : S_ISBLK(mode)  ? 'b' : S_ISCHR(mode)  ? 'c' :
          S_ISLNK(mode) ? 'l' : S_ISFIFO(mode) ? 'f' : S_ISSOCK(mode) ? 's' : '-';
  *cp++ = mode & S_IRUSR ? 'r' : '-';
  *cp++ = mode & S_IWUSR ? 'w' : '-';
  *cp++ = (mode & S_ISUID
               ? (mode & S_IXUSR ? 's' : 'S')
               : (mode & S_IXUSR ? 'x' : '-'));
  *cp++ = mode & S_IRGRP ? 'r' : '-';
  *cp++ = mode & S_IWGRP ? 'w' : '-';
  *cp++ = (mode & S_ISGID
               ? (mode & S_IXGRP ? 's' : 'S')
               : (mode & S_IXGRP ? 'x' : '-'));
  *cp++ = mode & S_IROTH ? 'r' : '-';
  *cp++ = mode & S_IWOTH ? 'w' : '-';
  *cp++ = (mode & S_ISVTX
               ? (mode & S_IXOTH ? 't' : 'T')
               : (mode & S_IXOTH ? 'x' : '-'));
  *cp = '\0';
  return cp;
}

#if defined(HAVE_WIN32)
int do_shell_expansion(char *name, int name_len)
{
   char *src = bstrdup(name);

   ExpandEnvironmentStrings(src, name, name_len);

   free(src);

   return 1;
}
#else
int do_shell_expansion(char *name, int name_len)
{
   static char meta[] = "~\\$[]*?`'<>\"";
   bool found = false;
   int len, i, stat;
   POOLMEM *cmd;
   BPIPE *bpipe;
   char line[MAXSTRING];
   const char *shellcmd;

   /* Check if any meta characters are present */
   len = strlen(meta);
   for (i = 0; i < len; i++) {
      if (strchr(name, meta[i])) {
         found = true;
         break;
      }
   }
   if (found) {
      cmd =  get_pool_memory(PM_FNAME);
      /* look for shell */
      if ((shellcmd = getenv("SHELL")) == NULL) {
         shellcmd = "/bin/sh";
      }
      pm_strcpy(&cmd, shellcmd);
      pm_strcat(&cmd, " -c \"echo ");
      pm_strcat(&cmd, name);
      pm_strcat(&cmd, "\"");
      Dmsg1(400, "Send: %s\n", cmd);
      if ((bpipe = open_bpipe(cmd, 0, "r"))) {
         *line = 0;
         fgets(line, sizeof(line), bpipe->rfd);
         strip_trailing_junk(line);
         stat = close_bpipe(bpipe);
         Dmsg2(400, "stat=%d got: %s\n", stat, line);
      } else {
         stat = 1;                    /* error */
      }
      free_pool_memory(cmd);
      if (stat == 0) {
         bstrncpy(name, line, name_len);
      }
   }
   return 1;
}
#endif


/*  MAKESESSIONKEY  --  Generate session key with optional start
                        key.  If mode is TRUE, the key will be
                        translated to a string, otherwise it is
                        returned as 16 binary bytes.

    from SpeakFreely by John Walker */

void make_session_key(char *key, char *seed, int mode)
{
   int j, k;
   struct MD5Context md5c;
   unsigned char md5key[16], md5key1[16];
   char s[1024];

#define ss sizeof(s)

   s[0] = 0;
   if (seed != NULL) {
     bstrncat(s, seed, sizeof(s));
   }

   /* The following creates a seed for the session key generator
     based on a collection of volatile and environment-specific
     information unlikely to be vulnerable (as a whole) to an
     exhaustive search attack.  If one of these items isn't
     available on your machine, replace it with something
     equivalent or, if you like, just delete it. */

#if defined(HAVE_WIN32)
   {
      LARGE_INTEGER     li;
      DWORD             length;
      FILETIME          ft;

      bsnprintf(s + strlen(s), ss, "%lu", (uint32_t)GetCurrentProcessId());
      (void)getcwd(s + strlen(s), 256);
      bsnprintf(s + strlen(s), ss, "%lu", (uint32_t)GetTickCount());
      QueryPerformanceCounter(&li);
      bsnprintf(s + strlen(s), ss, "%lu", (uint32_t)li.LowPart);
      GetSystemTimeAsFileTime(&ft);
      bsnprintf(s + strlen(s), ss, "%lu", (uint32_t)ft.dwLowDateTime);
      bsnprintf(s + strlen(s), ss, "%lu", (uint32_t)ft.dwHighDateTime);
      length = 256;
      GetComputerName(s + strlen(s), &length);
      length = 256;
      GetUserName(s + strlen(s), &length);
   }
#else
   bsnprintf(s + strlen(s), ss, "%lu", (uint32_t)getpid());
   bsnprintf(s + strlen(s), ss, "%lu", (uint32_t)getppid());
   (void)getcwd(s + strlen(s), 256);
   bsnprintf(s + strlen(s), ss, "%lu", (uint32_t)clock());
   bsnprintf(s + strlen(s), ss, "%lu", (uint32_t)time(NULL));
#if defined(Solaris)
   sysinfo(SI_HW_SERIAL,s + strlen(s), 12);
#endif
#if defined(HAVE_GETHOSTID)
   bsnprintf(s + strlen(s), ss, "%lu", (uint32_t) gethostid());
#endif
   gethostname(s + strlen(s), 256);
   bsnprintf(s + strlen(s), ss, "%lu", (uint32_t)getuid());
   bsnprintf(s + strlen(s), ss, "%lu", (uint32_t)getgid());
#endif
   MD5Init(&md5c);
   MD5Update(&md5c, (uint8_t *)s, strlen(s));
   MD5Final(md5key, &md5c);
   bsnprintf(s + strlen(s), ss, "%lu", (uint32_t)((time(NULL) + 65121) ^ 0x375F));
   MD5Init(&md5c);
   MD5Update(&md5c, (uint8_t *)s, strlen(s));
   MD5Final(md5key1, &md5c);
#define nextrand    (md5key[j] ^ md5key1[j])
   if (mode) {
     for (j = k = 0; j < 16; j++) {
        unsigned char rb = nextrand;

#define Rad16(x) ((x) + 'A')
        key[k++] = Rad16((rb >> 4) & 0xF);
        key[k++] = Rad16(rb & 0xF);
#undef Rad16
        if (j & 1) {
           key[k++] = '-';
        }
     }
     key[--k] = 0;
   } else {
     for (j = 0; j < 16; j++) {
        key[j] = nextrand;
     }
   }
}
#undef nextrand

void encode_session_key(char *encode, char *session, char *key, int maxlen)
{
   int i;
   for (i=0; (i < maxlen-1) && session[i]; i++) {
      if (session[i] == '-') {
         encode[i] = '-';
      } else {
         encode[i] = ((session[i] - 'A' + key[i]) & 0xF) + 'A';
      }
   }
   encode[i] = 0;
   Dmsg3(000, "Session=%s key=%s encode=%s\n", session, key, encode);
}

void decode_session_key(char *decode, char *session, char *key, int maxlen)
{
   int i, x;

   for (i=0; (i < maxlen-1) && session[i]; i++) {
      if (session[i] == '-') {
         decode[i] = '-';
      } else {
         x = (session[i] - 'A' - key[i]) & 0xF;
         if (x < 0) {
            x += 16;
         }
         decode[i] = x + 'A';
      }
   }
   decode[i] = 0;
   Dmsg3(000, "Session=%s key=%s decode=%s\n", session, key, decode);
}



/*
 * Edit job codes into main command line
 *  %% = %
 *  %b = Job Bytes
 *  %c = Client's name
 *  %C = If the job is a Cloned job (Only on director side)
 *  %d = Director's name (also valid on file daemon)
 *  %e = Job Exit code
 *  %E = Non-fatal Job Errors
 *  %f = Job FileSet (Only on director side)
 *  %F = Job Files
 *  %h = Client address (Only on director side)
 *  %i = JobId
 *  %j = Unique Job id
 *  %l = job level
 *  %n = Unadorned Job name
 *  %o = Job Priority
 *  %p = Pool name (Director)
 *  %P = Process PID
 *  %w = Write Store (Director)
 *  %x = Spool Data (Director)
 *  %D = Director name (Director/FileDaemon)
 *  %C = Cloned (Director)
 *  %I = wjcr->JobId (Director)
 *  %s = Since time
 *  %S = Previous Job name (FileDaemon) for Incremental/Differential
 *  %t = Job type (Backup, ...)
 *  %r = Recipients
 *  %v = Volume name
 *  %R = Job ReadBytes
 *
 *  omsg = edited output message
 *  imsg = input string containing edit codes (%x)
 *  to = recepients list
 *
 */
POOLMEM *edit_job_codes(JCR *jcr, char *omsg, char *imsg, const char *to, job_code_callback_t callback)
{
   char *p, *q;
   const char *str;
   char add[50];
   char name[MAX_ESCAPE_NAME_LENGTH];
   int i;

   *omsg = 0;
   Dmsg1(200, "edit_job_codes: %s\n", imsg);
   for (p=imsg; *p; p++) {
      if (*p == '%') {
         switch (*++p) {
         case '%':
            str = "%";
            break;
         case 'c':
            if (jcr) {
               str = jcr->client_name;
            } else {
               str = _("*none*");
            }
            break;
         case 'd':
            str = my_name;            /* Director's name */
            break;
         case 'e':
            if (jcr) {
               str = job_status_to_str(jcr->JobStatus, jcr->getErrors());
            } else {
               str = _("*none*");
            }
            break;
         case 'E':                    /* Job Errors */
            str = edit_uint64(jcr->getErrors(), add);
            break;
         case 'i':
            if (jcr) {
               bsnprintf(add, sizeof(add), "%d", jcr->JobId);
               str = add;
            } else {
               str = _("*none*");
            }
            break;
         case 'j':                    /* Job name */
            if (jcr) {
               str = jcr->Job;
            } else {
               str = _("*none*");
            }
            break;
         case 'l':
            if (jcr) {
               str = job_level_to_str(jcr->getJobLevel());
            } else {
               str = _("*none*");
            }
            break;
         case 'n':
             if (jcr) {
                bstrncpy(name, jcr->Job, sizeof(name));
                /* There are three periods after the Job name */
                for (i=0; i<3; i++) {
                   if ((q=strrchr(name, '.')) != NULL) {
                       *q = 0;
                   }
                }
                str = name;
             } else {
                str = _("*none*");
             }
             break;
         case 'r':
            str = to;
            break;
         case 's':                    /* since time */
            if (jcr && jcr->stime) {
               str = jcr->stime;
            } else {
               str = _("*none*");
            }
            break;
         case 'F':                    /* Job Files */
            str = edit_uint64(jcr->JobFiles, add);
            break;
         case 'b':                    /* Job Bytes */
            str = edit_uint64(jcr->JobBytes, add);
            break;
         case 't':
            if (jcr) {
               str = job_type_to_str(jcr->getJobType());
            } else {
               str = _("*none*");
            }
            break;
         case 'v':
            if (jcr) {
               if (jcr->VolumeName && jcr->VolumeName[0]) {
                  str = jcr->VolumeName;
               } else {
                  str = "";
               }
            } else {
               str = _("*none*");
            }
            break;
         case 'o':
            edit_uint64(jcr->JobPriority, add);
            str = add;
            break;
         case 'P':
            edit_uint64(getpid(), add);
            str = add;
            break;
         case 'R':                    /* Job ReadBytes */
            str = edit_uint64(jcr->ReadBytes, add);
            break;
         default:
            str = NULL;
            if (callback != NULL) {
               str = callback(jcr, p, name, sizeof(name));
            }

            if (!str) {
                add[0] = '%';
                add[1] = *p;
                add[2] = 0;
                str = add;
            }
            break;
         }
      } else {
         add[0] = *p;
         add[1] = 0;
         str = add;
      }
      Dmsg1(1200, "add_str %s\n", str);
      pm_strcat(&omsg, str);
      Dmsg1(1200, "omsg=%s\n", omsg);
   }
   return omsg;
}

void set_working_directory(char *wd)
{
   struct stat stat_buf;

   if (wd == NULL) {
      Emsg0(M_ERROR_TERM, 0, _("Working directory not defined. Cannot continue.\n"));
   }
   if (stat(wd, &stat_buf) != 0) {
      Emsg1(M_ERROR_TERM, 0, _("Working Directory: \"%s\" not found. Cannot continue.\n"),
         wd);
   }
   if (!S_ISDIR(stat_buf.st_mode)) {
      Emsg1(M_ERROR_TERM, 0, _("Working Directory: \"%s\" is not a directory. Cannot continue.\n"),
         wd);
   }
   working_directory = wd;            /* set global */
}

const char *last_path_separator(const char *str)
{
   if (*str != '\0') {
      for (const char *p = &str[strlen(str) - 1]; p >= str; p--) {
         if (IsPathSeparator(*p)) {
            return p;
         }
      }
   }
   return NULL;
}
