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

#include "bacula.h"
#include "../stored/stored.h"

extern bool parse_sd_config(CONFIG *config, const char *configfile, int exit_code);

static CONFIG *config;

void *start_heap;
#define CONFIG_FILE "bacula-sd.conf"
char *configfile = NULL;
bool detect_errors = false;
int  errors = 0;

static void usage()
{
   fprintf(stderr, _(
PROG_COPYRIGHT
"\n%sVersion: %s (%s)\n\n"
"Usage: cloud_test [options] <device-name>\n"
"       -b <file>       specify a bootstrap file\n"
"       -c <file>       specify a Storage configuration file\n"
"       -d <nn>         set debug level to <nn>\n"
"       -dt             print timestamp in debug output\n"
"       -v              be verbose\n"
"       -V              specify Volume names (separated by |)\n"
"       -?              print this message\n\n"), 2000, "", VERSION, BDATE);
   exit(1);
}

static void get_session_record(JCR *jcr, DEVICE *dev, DEV_RECORD *rec, SESSION_LABEL *sessrec)
{
   const char *rtype;
   memset(sessrec, 0, sizeof(SESSION_LABEL));
   jcr->JobId = 0;
   switch (rec->FileIndex) {
   case PRE_LABEL:
      rtype = _("Fresh Volume Label");
      break;
   case VOL_LABEL:
      rtype = _("Volume Label");
      unser_volume_label(dev, rec);
      break;
   case SOS_LABEL:
      rtype = _("Begin Job Session");
      unser_session_label(sessrec, rec);
      jcr->JobId = sessrec->JobId;
      break;
   case EOS_LABEL:
      rtype = _("End Job Session");
      break;
   case 0:
   case EOM_LABEL:
      rtype = _("End of Medium");
      break;
   case EOT_LABEL:
      rtype = _("End of Physical Medium");
      break;
   case SOB_LABEL:
      rtype = _("Start of object");
      break;
   case EOB_LABEL:
      rtype = _("End of object");
      break;
   default:
      rtype = _("Unknown");
      Dmsg1(10, "FI rtype=%d unknown\n", rec->FileIndex);
      break;
   }
   Dmsg5(10, "%s Record: VolSessionId=%d VolSessionTime=%d JobId=%d DataLen=%d\n",
         rtype, rec->VolSessionId, rec->VolSessionTime, rec->Stream, rec->data_len);
   if (verbose) {
      Pmsg5(-1, _("%s Record: VolSessionId=%d VolSessionTime=%d JobId=%d DataLen=%d\n"),
            rtype, rec->VolSessionId, rec->VolSessionTime, rec->Stream, rec->data_len);
   }
}

/* List just block information */
static void do_blocks(JCR *jcr, DCR *dcr)
{
   DEV_BLOCK *block = dcr->block;
   DEVICE *dev = dcr->dev;
   char buf1[100], buf2[100];
   DEV_RECORD *rec = new_record();
   for ( ;; ) {
      if (!dcr->read_block_from_device(NO_BLOCK_NUMBER_CHECK)) {
         Dmsg1(100, "!read_block(): ERR=%s\n", dev->print_errmsg());
         if (dev->at_eot()) {
            if (!mount_next_read_volume(dcr)) {
               Jmsg(jcr, M_INFO, 0, _("Got EOM at file %u on device %s, Volume \"%s\"\n"),
                  dev->file, dev->print_name(), dcr->VolumeName);
               break;
            }
            /* Read and discard Volume label */
            DEV_RECORD *record;
            SESSION_LABEL sessrec;
            record = new_record();
            dcr->read_block_from_device(NO_BLOCK_NUMBER_CHECK);
            read_record_from_block(dcr, record);
            get_session_record(jcr, dev, record, &sessrec);
            free_record(record);
            Jmsg(jcr, M_INFO, 0, _("Mounted Volume \"%s\".\n"), dcr->VolumeName);
         } else if (dev->at_eof()) {
            Jmsg(jcr, M_INFO, 0, _("End of file %u on device %s, Volume \"%s\"\n"),
               dev->part, dev->print_name(), dcr->VolumeName);
            Dmsg0(20, "read_record got eof. try again\n");
            continue;
         } else if (dev->is_short_block()) {
            Jmsg(jcr, M_INFO, 0, "%s", dev->print_errmsg());
            continue;
         } else {
            /* I/O error */
            errors++;
            display_tape_error_status(jcr, dev);
            break;
         }
      }
      read_record_from_block(dcr, rec);
      printf("Block: %d size=%d\n", block->BlockNumber, block->block_len);
   }
   free_record(rec);
   return;
}

int main (int argc, char *argv[])
{
   int ch;
   DEVICE *dev;
   cloud_dev *cdev;
   cloud_driver *driver;
   char *VolumeName=NULL;
   JCR *jcr=NULL;
   BSR *bsr = NULL;
   char *bsrName = NULL;
   BtoolsAskDirHandler askdir_handler;

   init_askdir_handler(&askdir_handler);
   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");
   init_stack_dump();
   lmgr_init_thread();

   working_directory = "/tmp";
   my_name_is(argc, argv, "cloud_test");
   init_msg(NULL, NULL);              /* initialize message handler */

   OSDependentInit();


   while ((ch = getopt(argc, argv, "b:c:d:vV:?")) != -1) {
      switch (ch) {
      case 'c':                    /* specify config file */
         if (configfile != NULL) {
            free(configfile);
         }
         configfile = bstrdup(optarg);
         break;

      case 'b':
         bsrName = optarg;
         break;

      case 'd':                    /* debug level */
         if (*optarg == 't') {
            dbg_timestamp = true;
         } else {
            char *p;
            /* We probably find a tag list -d 10,sql,bvfs */
            if ((p = strchr(optarg, ',')) != NULL) {
               *p = 0;
            }
            debug_level = atoi(optarg);
            if (debug_level <= 0) {
               debug_level = 1;
            }
            if (p) {
               debug_parse_tags(p+1, &debug_level_tags);
            }
         }
         break;

      case 'v':
         verbose++;
         break;

      case 'V':                    /* Volume name */
         VolumeName = optarg;
         break;

      case '?':
      default:
         usage();

      } /* end switch */
   } /* end while */
   argc -= optind;
   argv += optind;

   if (!argc) {
      Pmsg0(0, _("No archive name specified\n"));
      usage();
   }

   if (configfile == NULL) {
      configfile = bstrdup(CONFIG_FILE);
   }

   config = New(CONFIG());
   parse_sd_config(config, configfile, M_ERROR_TERM);
   setup_me();
   load_sd_plugins(me->plugin_directory);
   if (bsrName) {
      bsr = parse_bsr(NULL, bsrName);
   }
   jcr = setup_jcr("cloud_test", argv[0], bsr, VolumeName, SD_READ);
   dev = jcr->dcr->dev;
   if (!dev || dev->dev_type != B_CLOUD_DEV) {
      Pmsg0(0, "Bad device\n");
      exit(1);
   }
   do_blocks(jcr, jcr->dcr);
   /* Start low level tests */
   cdev = (cloud_dev *)dev;
   driver = cdev->driver;

   /* TODO: Put here low level tests for all drivers */
   if (!cdev->truncate_cache(jcr->dcr)) {
      Pmsg1(0, "Unable to truncate the cache ERR=%s\n", cdev->errmsg);
   }

   if (jcr) {
      release_device(jcr->dcr);
      free_jcr(jcr);
      dev->term(NULL);
   }
   return 0;
}
