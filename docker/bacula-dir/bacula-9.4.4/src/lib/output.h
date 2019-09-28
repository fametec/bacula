/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2019 Kern Sibbald

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

#ifndef OUTPUT_H
#define OUTPUT_H

#include "bacula.h"

enum _OutputType {
   OT_INT,                      /* Integer */
   OT_SIZE,                     /* int64 size */
   OT_PINT32,                   /* Uint32 */
   OT_INT32,
   OT_PINT64,                   /* Uint64 */
   OT_INT64,
   OT_STRING,
   OT_BTIME,                    /* btime_t */
   OT_UTIME,                    /* utime_t */
   OT_JOBTYPE,
   OT_JOBLEVEL,
   OT_JOBSTATUS,
   OT_PLUGINS,                  /* Plugin alist */
   OT_RATIO,                    /* Double %.2f format */
   OT_ALIST_STR,

   OT_END,                      /* Last operator (no extra arg) */
   OT_START_OBJ,                /* Skip a line to start a new object (no extra arg) */
   OT_END_OBJ,                  /* Skip a line to end current object (no extra arg) */
   OT_CLEAR,                    /* truncate current buffer (no extra arg) */
   OT_DURATION                  /* time duration in second */
};
/* Force OutputType to int to avoid compiler default conversion warnings */
typedef int OutputType;

/* Keep the same order for get_options/parse_options */
typedef enum {
   OTT_TIME_ISO  = 0,
   OTT_TIME_UNIX = 1,          /* unix time stamp */
   OTT_TIME_NC   = 2           /* Formatted time for user display: dd-Mon hh:mm */
} OutputTimeType;

#define OW_DEFAULT_SEPARATOR  '\n'
#define OW_DEFAULT_TIMEFORMAT OTT_TIME_ISO

/* If included from output.c, mark the class as export (else, symboles are
 * exported from all files...
 */
#ifdef OUTPUT_C
# define OUTPUT_EXPORT DLL_IMP_EXP
#else
# define OUTPUT_EXPORT
#endif

class OUTPUT_EXPORT OutputWriter: public SMARTALLOC
{
private:
   void init() {
      buf = NULL;
      separator  = OW_DEFAULT_SEPARATOR;
      separator_str[0] = OW_DEFAULT_SEPARATOR;
      separator_str[1] = 0;
      timeformat = OW_DEFAULT_TIMEFORMAT;
      object_separator = 0;
      flags = 0;
   };

protected:
   virtual char   *get_output(va_list ap, POOLMEM **out, OutputType first);
   void            get_buf(bool append); /* Allocate buf if needed */

   int             flags;
   char            separator;
   char            separator_str[2];
   char            object_separator;
   OutputTimeType  timeformat;
   POOLMEM        *buf;

public:
   OutputWriter(const char *opts) {
      init();
      parse_options(opts);
   };

   OutputWriter() {
      init();
   };

   virtual ~OutputWriter() {
      free_and_null_pool_memory(buf);
   };

   /* s[ascii code]t[0-3]
    *      ^^^        ^^
    *    separator  time format
    * "s43"   => + will be used as separator
    * "s43t1" => + as separator and time as unix timestamp
    */
   virtual void  parse_options(const char *opts);
   virtual char *get_options(char *dest_l128); /* MAX_NAME_LENGTH mini  */

   /* Make a clear separation in the output*/
   virtual char *start_group(const char *name, bool append=true);
   virtual char *end_group(bool append=true);

   /* Make a clear separation in the output for list*/
   virtual char *start_list(const char *name, bool append=true);
   virtual char *end_list(bool append=true);

   /* \n by default, can be \t for example */
   void set_separator(char sep) {
      separator = sep;
      separator_str[0] = sep;
   };

   void set_object_separator(char sep) {
      object_separator = sep;
   };

   void set_time_format(OutputTimeType fmt) {
      timeformat = fmt;
   };

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

   /* Use a user supplied buffer */
   char *get_output(POOLMEM **out, OutputType first, ...);

   /* Use the internal buffer */
   char *get_output(OutputType first, ...);
};

#endif
