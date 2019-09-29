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
 * Program for determining file system type
 *
 *   Written by Preben 'Peppe' Guldberg, December MMIV
 */

#include "bacula.h"
#include "findlib/find.h"

static void usage()
{
   fprintf(stderr, _(
"\n"
"Usage: fstype [-v] path ...\n"
"\n"
"       Print the file system type for each file/directory argument given.\n"
"       The following options are supported:\n"
"\n"
"       -l     print all file system types in mtab.\n"
"       -m     print full entries in mtab.\n"
"       -v     print both path and file system type of each argument.\n"
"       -?     print this message.\n"
"\n"));

   exit(1);
}

struct mtab_item {
   rblink link;
   uint64_t dev;
   char fstype[1];
};

/* Compare two device types */
static int compare_mtab_items(void *item1, void *item2)
{
   mtab_item *mtab1, *mtab2;
   mtab1 = (mtab_item *)item1;
   mtab2 = (mtab_item *)item2;
   if (mtab1->dev < mtab2->dev) return -1;
   if (mtab1->dev > mtab2->dev) return 1;
   return 0;
}

void print_mtab_item(void *user_ctx, struct stat *st, const char *fstype,
                      const char *mountpoint, const char *mntopts,
                      const char *fsname)
{
   fprintf(stderr, "dev=%p fstype=%s mountpoint=%s mntopts=%s\n",
      ((void *)st->st_dev), fstype, mountpoint, mntopts);
}

static void add_mtab_item(void *user_ctx, struct stat *st, const char *fstype,
               const char *mountpoint, const char *mntopts,
               const char *fsname)
{
   rblist *mtab_list = (rblist *)user_ctx;
   mtab_item *item, *ritem;
   int len = strlen(fstype) + 1;
   
   item = (mtab_item *)malloc(sizeof(mtab_item) + len);
   item->dev = (uint64_t)st->st_dev;
   bstrncpy(item->fstype, fstype, len);
   //fprintf(stderr, "Add dev=%lx fstype=%s\n", item->dev, item->fstype);
   ritem = (mtab_item *)mtab_list->insert((void *)item, compare_mtab_items);
   if (ritem != item) {
      fprintf(stderr, "Problem!! Returned item not equal added item\n");
   }
   //fprintf(stderr, "dev=%p fstype=%s mountpoint=%s mntopts=%s\n",
   //   ((void *)st->st_dev), fstype, mountpoint, mntopts);
}


int main (int argc, char *const *argv)
{
   char fs[1000];
   bool verbose = false;
   bool list = false;
   bool mtab = false;
   int status = 0;
   int ch, i;

   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");

   while ((ch = getopt(argc, argv, "lmv?")) != -1) {
      switch (ch) {
         case 'l':
            list = true;
            break;
         case 'm':
            mtab = true; /* list mtab */
            break;
         case 'v':
            verbose = true;
            break;
         case '?':
         default:
            usage();

      }
   }
   argc -= optind;
   argv += optind;


   OSDependentInit();

   if (mtab) {
      read_mtab(print_mtab_item, NULL);
      status = 1;
      goto get_out;
   }
   if (list) {
      rblist *mtab_list;
      mtab_item *item;
      mtab_list = New(rblist());
      read_mtab(add_mtab_item, mtab_list);
      fprintf(stderr, "Size of mtab=%d\n", mtab_list->size());
      foreach_rblist(item, mtab_list) {
         fprintf(stderr, "Found dev=%lx fstype=%s\n", item->dev, item->fstype);
      }
      delete mtab_list;
      goto get_out;
   }

   if (argc < 1) {
      usage();
   }
   for (i = 0; i < argc; --argc, ++argv) {
      FF_PKT ff_pkt;
      memset(&ff_pkt, 0, sizeof(ff_pkt));
      ff_pkt.fname = ff_pkt.link = *argv;
      if (lstat(ff_pkt.fname, &ff_pkt.statp) != 0) {
         fprintf(stderr, "lstat of %s failed.\n", ff_pkt.fname);
         status = 1;
         break;
      }
      if (fstype(&ff_pkt, fs, sizeof(fs))) {
         if (verbose) {
            printf("%s: %s\n", *argv, fs);
         } else {
            puts(fs);
         }
      } else {
         fprintf(stderr, _("%s: unknown file system type\n"), *argv);
         status = 1;
      }
   }

get_out:
   exit(status);
}
