/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

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
 * Program for determining drive type
 *
 *   Written by Robert Nelson, June 2006
 */

#include "bacula.h"
#include "findlib/find.h"

static void usage()
{
   fprintf(stderr, _(
"\n"
"Usage: drivetype [-v] path ...\n"
"\n"
"       Print the drive type a given file/directory is on.\n"
"       The following options are supported:\n"
"\n"
"       -l     print local fixed hard drive\n"
"       -a     display information on all drives\n"
"       -v     print both path and file system type.\n"
"       -?     print this message.\n"
"\n"));

   exit(1);
}

int display_drive(char *drive, bool display_local, int verbose)
{
   char dt[100];
   int status = 0;

   if (drivetype(drive, dt, sizeof(dt))) {
      if (display_local) {      /* in local mode, display only harddrive */
         if (strcmp(dt, "fixed") == 0) {
            printf("%s\n", drive);
         }
      } else if (verbose) {
         printf("%s: %s\n", drive, dt);
      } else {
         puts(dt);
      }
   } else if (!display_local) { /* local mode is used by FileSet scripts */
      fprintf(stderr, _("%s: unknown\n"), drive);
      status = 1;
   }
   return status;
}

int
main (int argc, char *const *argv)
{
   int verbose = 0;
   int status = 0;
   int ch, i;
   bool display_local = false;
   bool display_all = false;
   char drive='A';
   char buf[16];

   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");

   while ((ch = getopt(argc, argv, "alv?")) != -1) {
      switch (ch) {
         case 'v':
            verbose = 1;
            break;
         case 'l':
            display_local = true;
            break;
         case 'a':
            display_all = true;
            break;
         case '?':
         default:
            usage();

      }
   }
   argc -= optind;
   argv += optind;

   OSDependentInit();
 
   if (argc < 1 && display_all) {
      /* Try all letters */
      for (drive = 'A'; drive <= 'Z'; drive++) {
         bsnprintf(buf, sizeof(buf), "%c:/", drive);
         display_drive(buf, display_local, verbose);
      }
      exit(status);
   }

   if (argc < 1) {
      usage();
   }

   for (i = 0; i < argc; --argc, ++argv) {
      status += display_drive(*argv, display_local, verbose);
   }
   exit(status);
}
