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
 *  Written by: Eric Bollengier, December MMXIII
 */

#define OUTPUT_C                /* control dll export in output.h */
#include "output.h"
#include "plugins.h"

/* use new output (lowercase, no special char) */
#define OF_USE_NEW_OUTPUT  1

void OutputWriter::parse_options(const char *options)
{
   int nb=0;
   const char *p = options;
   while (*p) {
      nb=0;

      switch(*p) {
      case 'C':
         flags = 0;
         set_time_format(OW_DEFAULT_TIMEFORMAT);
         set_separator(OW_DEFAULT_SEPARATOR);
         break;

      case 'S':                 /* object separator */
         while(isdigit(*(p+1))) {
            nb = nb*10 + (*(++p) - '0');
         }
         if (isascii(nb)) {
            set_object_separator((char) nb);
         }
         break;

      case 'o':
         flags |= OF_USE_NEW_OUTPUT; /* lowercase and only isalpha */
         break;

      case 't':                 /* Time format */
         if (isdigit(*(p+1))) {
            nb = (*(++p) - '0');
            set_time_format((OutputTimeType) nb);
         }
         break;

      case 's':                 /* Separator */
         while(isdigit(*(p+1))) {
            nb = nb*10 + (*(++p) - '0');
         }
         if (isascii(nb)) {
            set_separator((char) nb);
         }
         break;
      default:
         break;
      }
      p++;
   }
}

char *OutputWriter::get_options(char *dest)
{
   char ed1[50];
   *dest = *ed1 = 0;
   if (separator != OW_DEFAULT_SEPARATOR) {
      snprintf(dest, 50, "s%d", (int)separator);
   }
   if (object_separator) {
      snprintf(ed1, sizeof(ed1), "S%d", (int) object_separator);
      bstrncat(dest, ed1, sizeof(ed1));
   }
   if (timeformat != OW_DEFAULT_TIMEFORMAT) {
      snprintf(ed1, sizeof(ed1), "t%d", (int) timeformat);
      bstrncat(dest, ed1, sizeof(ed1));
   }
   if (flags & OF_USE_NEW_OUTPUT) {
      bstrncat(dest, "o", 1);
   }
   return dest;
}

void OutputWriter::get_buf(bool append)
{
   if (!buf) {
      buf = get_pool_memory(PM_MESSAGE);
      *buf = 0;

   } else if (!append) {
      *buf = 0;
   }
}

char *OutputWriter::start_group(const char *name, bool append)
{
   get_buf(append);
   pm_strcat(buf, name);
   pm_strcat(buf, ":\n");
   return buf;
}

char *OutputWriter::end_group(bool append)
{
   get_buf(append);
   pm_strcat(buf, "\n");

   return buf;
}

char *OutputWriter::start_list(const char *name, bool append)
{
   get_buf(append);
   pm_strcat(buf, name);
   pm_strcat(buf, ": [\n");
   return buf;
}

char *OutputWriter::end_list(bool append)
{
   get_buf(append);
   pm_strcat(buf, "]\n");

   return buf;
}

/* Usage:
 *   get_output(
 *       OT_STRING,   "name",       "value",
 *       OT_PINT32,   "age",        10,
 *       OT_TIME,     "birth-date", 1120202002,
 *       OT_PINT64,   "weight",     100,
 *       OT_END);
 *
 *
 *  "name=value\nage=10\nbirt-date=2012-01-12 10:20:00\nweight=100\n"
 *
 */
char *OutputWriter::get_output(OutputType first, ...)
{
   char    *ret;
   va_list  arg_ptr;

   get_buf(true);               /* Append to the current string */

   va_start(arg_ptr, first);
   ret = get_output(arg_ptr, &buf, first);
   va_end(arg_ptr);

   return ret;
}

/* Usage:
 *   get_output(&out,
 *       OT_STRING,   "name",       "value",
 *       OT_PINT32,   "age",        10,
 *       OT_TIME,     "birth-date", 1120202002,
 *       OT_PINT64,   "weight",     100,
 *       OT_END);
 *
 *
 *  "name=value\nage=10\nbirt-date=2012-01-12 10:20:00\nweight=100\n"
 *
 */
char *OutputWriter::get_output(POOLMEM **out, OutputType first, ...)
{
   va_list arg_ptr;
   char *ret;

   va_start(arg_ptr, first);
   ret = get_output(arg_ptr, out, first);
   va_end(arg_ptr);

   return ret;
}

char *OutputWriter::get_output(va_list ap, POOLMEM **out, OutputType first)
{
   char       ed1[MAX_TIME_LENGTH];
   int        i;
   int64_t    i64;
   uint64_t   u64;
   int32_t    i32;
   double     d;
   btime_t    bt;
   char      *s = NULL, *k = NULL;
   alist     *lst;
   Plugin    *plug;
   POOLMEM   *tmp2 = get_pool_memory(PM_FNAME);
   POOLMEM   *tmp = get_pool_memory(PM_FNAME);
   OutputType val = first;
         
   while (val != OT_END) {

      *tmp = 0;

      /* Some arguments are not using a keyword */
      switch (val) {
      case OT_END:
      case OT_START_OBJ:
      case OT_END_OBJ:
      case OT_CLEAR:
         break;

      default:
         k = va_arg(ap, char *);          /* Get the variable name */

         /* If requested, we can put the keyword in lowercase */
         if (flags & OF_USE_NEW_OUTPUT) {
            tmp2 = check_pool_memory_size(tmp2, strlen(k)+1);
            for (i = 0; k[i] ; i++) {
               if (isalnum(k[i])) {
                  tmp2[i] = tolower(k[i]);
               } else {
                  tmp2[i] = '_';
               }
            }
            tmp2[i] = 0;
            k = tmp2;
         }
      }

      //Dmsg2(000, "%d - %s\n", val, k);

      switch (val) {
      case OT_ALIST_STR:
         lst = va_arg(ap, alist *);
         i = 0;
         Mmsg(tmp, "%s=", k);
         if (lst) {
            foreach_alist(s, lst) {
               if (i++ > 0) {
                  pm_strcat(tmp, ",");
               }
               pm_strcat(tmp, s);
            }
         }
         pm_strcat(tmp, separator_str);
         break;
      case OT_PLUGINS:
         lst = va_arg(ap, alist *);
         i = 0;
         pm_strcpy(tmp, "plugins=");
         if (lst) {
            foreach_alist(plug, lst) {
               if (i++ > 0) {
                  pm_strcat(tmp, ",");
               }
               pm_strcat(tmp, plug->file);
            }
         }
         pm_strcat(tmp, separator_str);
         break;
      case OT_RATIO:
         d = va_arg(ap, double);
         Mmsg(tmp, "%s=%.2f%c", k, d, separator);
         break;

      case OT_STRING:
         s = va_arg(ap, char *);
         Mmsg(tmp, "%s=%s%c", k, NPRTB(s), separator) ;
         break;

      case OT_INT32:
         i32 = va_arg(ap, int32_t);
         Mmsg(tmp, "%s=%d%c", k, i32, separator);
         break;

      case OT_UTIME:
      case OT_BTIME:
         if (val == OT_UTIME) {
            bt = va_arg(ap, utime_t);
         } else {
            bt = va_arg(ap, btime_t);
         }
         switch (timeformat) {
         case OTT_TIME_NC: /* Formatted time for user display: dd-Mon hh:mm */
            bstrftime_ny(ed1, sizeof(ed1), bt);
            break;

         case OTT_TIME_UNIX:         /* unix timestamp */
            bsnprintf(ed1, sizeof(ed1), "%lld", bt);
            break;

         case OTT_TIME_ISO:
            /* wanted fallback */
         default:
            bstrutime(ed1, sizeof(ed1), bt);
         }
         Mmsg(tmp, "%s_epoch=%lld%c%s=%s%c", k, bt, separator, k, ed1, separator);
         break;

      case OT_DURATION:
         bt = va_arg(ap, utime_t);     
         bstrutime(ed1, sizeof(ed1), bt); 
         Mmsg(tmp, "%s=%lld%c%s_str=%s%c", k, bt, separator, k, edit_utime(bt, ed1, sizeof(ed1)), separator);
         break;

      case OT_SIZE:
      case OT_INT64:
         i64 = va_arg(ap, int64_t);
         Mmsg(tmp, "%s=%lld%c", k, i64, separator);
         break;

      case OT_PINT64:
         u64 = va_arg(ap, uint64_t);
         Mmsg(tmp, "%s=%llu%c", k, u64, separator);
         break;

      case OT_INT:
         i64 = va_arg(ap, int);
         Mmsg(tmp, "%s=%lld%c", k, i64, separator);
         break;

      case OT_JOBLEVEL:
      case OT_JOBTYPE:
      case OT_JOBSTATUS:
         i32 = va_arg(ap, int32_t);
         if (i32 == 0) {
            Mmsg(tmp, "%s=%c", k, separator);
         } else {
            Mmsg(tmp, "%s=%c%c", k, (char)i32, separator);
         }
         break;

      case OT_CLEAR:
         **out = 0;
         break;

      case OT_END_OBJ:
         pm_strcpy(tmp, "\n");
         break;

      case OT_START_OBJ:
         i=0;
         if (object_separator) {
            for(; i < 32 ; i++) {
               tmp[i] = object_separator;
            }
         }
         tmp[i++] = '\n';
         tmp[i] = 0;
         break;

      case OT_END:
         /* wanted fallback */
      default:
         val = OT_END;
      }

      if (val != OT_END) {
         pm_strcat(out, tmp);
         val = (OutputType) va_arg(ap, int); /* OutputType is promoted to int when using ... */
      }
   }

   free_pool_memory(tmp);
   free_pool_memory(tmp2);
   //Dmsg1(000, "%s", *out);
   return *out;
}

#ifndef TEST_PROGRAM
#define TEST_PROGRAM_A
#endif

#ifdef TEST_PROGRAM
#include "unittests.h"

int main(int argc, char **argv)
{
   Unittests output_test("output_test");
   char ed1[50];
   OutputWriter wt;
   POOLMEM *tmp = get_pool_memory(PM_FNAME);
   *tmp = 0;

   int         nb   = 10000;
   const char *ptr  = "my value";
   char       *str  = bstrdup("ptr");
   int32_t     nb32 = -1;
   int64_t     nb64 = -1;
   btime_t     t    = time(NULL);

   ok(strcmp(wt.get_options(ed1), "") == 0, "Default options");

   Pmsg1(000, "%s", wt.start_group("test"));

   wt.get_output(&tmp, OT_CLEAR,
                 OT_STRING, "test", "my value",
                 OT_STRING, "test2", ptr,
                 OT_STRING, "test3", str,
                 OT_INT,    "nb",   nb,
                 OT_INT32,  "nb32", nb32,
                 OT_INT64,  "nb64", nb64,
                 OT_BTIME,  "now",  t,
                 OT_END);

   Pmsg1(000, "%s", tmp);

   free_pool_memory(tmp);

   Pmsg1(000, "%s",
         wt.get_output(OT_CLEAR,
                 OT_START_OBJ,
                 OT_STRING, "test", "my value",
                 OT_STRING, "test2", ptr,
                 OT_STRING, "test3", str,
                 OT_INT,    "nb",   nb,
                 OT_INT32,  "nb32", nb32,
                 OT_INT64,  "nb64", nb64,
                 OT_BTIME,  "now",  t,
                 OT_END_OBJ,
                 OT_END));

   wt.set_time_format(OTT_TIME_UNIX);
   ok(strcmp("t1", wt.get_options(ed1)) == 0, "Check unix time format");

   Pmsg1(000, "%s",
         wt.get_output(OT_CLEAR,
                 OT_BTIME,  "now",  t,
                 OT_END));

   wt.set_time_format(OTT_TIME_NC);
   ok(strcmp("t2", wt.get_options(ed1)) == 0, "Check NC time format");

   Pmsg1(000, "%s",
         wt.get_output(OT_CLEAR,
                 OT_BTIME,  "now",  t,
                 OT_END));

   Pmsg1(000, "%s", wt.end_group(false));

   wt.parse_options("s43t1O");
   ok(strcmp(wt.get_options(ed1), "s43t1") == 0, "Check options after parsing");

   ok(strstr(
         wt.get_output(OT_CLEAR,
                 OT_BTIME,  "now",  t,
                 OT_STRING, "brazil", "test",
                 OT_END),
         "+brazil=test+") != NULL,
      "Check separator");

   wt.parse_options("CS35");
   ok(strcmp(wt.get_options(ed1), "S35") == 0, "Check options after parsing");

   Pmsg1(000, "%s",
         wt.get_output(OT_CLEAR,
                 OT_START_OBJ,
                 OT_STRING, "test", "my value",
                 OT_STRING, "test2", ptr,
                 OT_END_OBJ,
                 OT_START_OBJ,
                 OT_STRING, "test", "my value",
                 OT_STRING, "test2", ptr,
                 OT_END_OBJ,
                 OT_END));

   free(str);

   return report();
}
#endif   /* TEST_PROGRAM */
