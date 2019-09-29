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
 * Test program for testing regular expressions.
 *
 *  Kern Sibbald, MMVI
 *
 */

#include "bacula.h"

/*
 *  If you define BACULA_REGEX, bregex will be built with the
 *  Bacula bregex library, which is the same code that we
 *  use on Win32, thus using Linux, you can test your Win32
 *  expressions. Otherwise, this program will link with the
 *  system library routines.
 */
//#define BACULA_REGEX

#ifdef BACULA_REGEX

#include "lib/bregex.h"

#else
#ifndef HAVE_REGEX_H
#include "lib/bregex.h"
#else
#include <regex.h>
#endif

#endif


static void usage()
{
   fprintf(stderr,
"\n"
"Usage: bregex [-d debug_level] -f <data-file>\n"
"       -f          specify file of data to be matched\n"
"       -l          suppress line numbers\n"
"       -n          print lines that do not match\n"
"       -d <nn>     set debug level to <nn>\n"
"       -dt         print timestamp in debug output\n"
"       -?          print this message.\n"
"\n\n");

   exit(1);
}


int main(int argc, char *const *argv)
{
   regex_t preg;
   char prbuf[500];
   char *fname = NULL;
   int rc, ch;
   char data[1000];
   char pat[500];
   FILE *fd;
   bool match_only = true;
   int lineno;
   bool no_linenos = false;
   

   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");

   while ((ch = getopt(argc, argv, "d:f:ln?")) != -1) {
      switch (ch) {
      case 'd':                       /* set debug level */
         if (*optarg == 't') {
            dbg_timestamp = true;
         } else {
            debug_level = atoi(optarg);
            if (debug_level <= 0) {
               debug_level = 1;
            }
         }
         break;

      case 'f':                       /* data */
         fname = optarg;
         break;

      case 'l':
         no_linenos = true;
         break;

      case 'n':
         match_only = false;
         break;

      case '?':
      default:
         usage();

      }
   }
   argc -= optind;
   argv += optind;

   if (!fname) {
      printf("A data file must be specified.\n");
      usage();
   }

   OSDependentInit();

   for ( ;; ) {
      printf("Enter regex pattern: ");
      if (fgets(pat, sizeof(pat)-1, stdin) == NULL) {
         break;
      }
      strip_trailing_newline(pat);
      if (pat[0] == 0) {
         exit(0);
      }
      rc = regcomp(&preg, pat, REG_EXTENDED);
      if (rc != 0) {
         regerror(rc, &preg, prbuf, sizeof(prbuf));
         printf("Regex compile error: %s\n", prbuf);
         continue;
      }
      fd = fopen(fname, "r");
      if (!fd) {
         printf(_("Could not open data file: %s\n"), fname);
         exit(1);
      }
      lineno = 0;
      while (fgets(data, sizeof(data)-1, fd)) {
         const int nmatch = 30;
         regmatch_t pmatch[nmatch];
         strip_trailing_newline(data);
         lineno++;
         rc = regexec(&preg, data, nmatch, pmatch,  0);
         if ((match_only && rc == 0) || (!match_only && rc != 0)) {
            if (no_linenos) {
               printf("%s\n", data);
            } else {
               printf("%5d: %s\n", lineno, data);
            }
         }
      }
      fclose(fd);
      regfree(&preg);
   }
   exit(0);
}
