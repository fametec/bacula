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

#include "tray-monitor.h"
#include <QInputDialog>
#include <QDir>

/* Static variables */
char *configfile = NULL;
static MONITOR *monitor = NULL;
static CONFIG *config = NULL;
static TrayUI *mainwidget = NULL;
static TSched *scheduler = NULL;

#define CONFIG_FILE "./bacula-tray-monitor.conf"     /* default configuration file */

#ifdef HAVE_WIN32
#define HOME_VAR "APPDATA"
#define CONFIG_FILE_HOME "bacula-tray-monitor.conf" /* In $HOME */
#else
#define HOME_VAR "HOME"
#define CONFIG_FILE_HOME ".bacula-tray-monitor.conf" /* In $HOME */
#endif

static void usage()
{
   fprintf(stderr, _(
PROG_COPYRIGHT
"\n%sVersion: %s (%s) %s %s %s\n\n"
"Usage: tray-monitor [-c config_file] [-d debug_level]\n"
"       -c <file>     set configuration file to file\n"
"       -d <nn>       set debug level to <nn>\n"
"       -dt           print timestamp in debug output\n"
"       -t            test - read configuration and exit\n"
"       -W 0/1        force the detection of the systray\n"
"       -?            print this message.\n"
"\n"), 2004, BDEMO, VERSION, BDATE, HOST_OS, DISTNAME, DISTVER);
}

void refresh_tray(TrayUI *t)
{
   RESMON *r;
   MONITOR *mon;

   if (!t) {
      return;
   }

   t->clearTabs();
   if (!config) {
      return;
   }

   mon = (MONITOR *) GetNextRes(R_MONITOR, NULL);
   t->spinRefresh->setValue(mon?mon->RefreshInterval:60);

   foreach_res(r, R_CLIENT) {
      t->addTab(r);
   }
   foreach_res(r, R_DIRECTOR) {
      t->addTab(r);
   }
   foreach_res(r, R_STORAGE) {
      t->addTab(r);
   }
}

void display_error(const char *fmt, ...)
{
   va_list  arg_ptr;
   POOL_MEM tmp(PM_MESSAGE);
   QMessageBox msgBox;
   int maxlen;

   if (!fmt || !*fmt) {
      return;
   }

   maxlen = tmp.size() - 1;
   va_start(arg_ptr, fmt);
   bvsnprintf(tmp.c_str(), maxlen, fmt, arg_ptr);
   va_end(arg_ptr);

   msgBox.setIcon(QMessageBox::Critical);
   msgBox.setText(tmp.c_str());
   msgBox.exec();
}

void error_handler(const char *file, int line, LEX */* lc */, const char *msg, ...)
{
   POOL_MEM tmp;
   va_list arg_ptr;
   va_start(arg_ptr, msg);
   vsnprintf(tmp.c_str(), tmp.size(), msg, arg_ptr);
   va_end(arg_ptr);
   display_error("Error %s:%d %s\n", file, line, tmp.c_str());
}

int tls_pem_callback(char *buf, int size, const void * /*userdata*/)
{
   bool ok;
   QString text = QInputDialog::getText(mainwidget, _("TLS PassPhrase"),
                                        buf, QLineEdit::Normal,
                                        QDir::home().dirName(), &ok);
   if (ok) {
      bstrncpy(buf, text.toUtf8().data(), size);
      return 1;
   } else {
      return 0;
   }
}

bool reload()
{
   bool displaycfg=false;
   int  nitems = 0;
   struct stat sp;

   Dmsg0(50, "reload the configuration!\n");
   scheduler->stop();
   if (config) {
      delete config;
   }
   config = NULL;
   monitor = NULL;

   if (stat(configfile, &sp) != 0) {
      berrno be;
      Dmsg2(50, "Unable to find %s. ERR=%s\n", configfile, be.bstrerror());
      displaycfg = true;
      goto bail_out;
   }

   config = New(CONFIG());
   if (!parse_tmon_config(config, configfile, M_ERROR)) {
      Dmsg1(50, "Error while parsing %s\n", configfile);
      // TODO: Display a warning message an open the configuration
      //       window
      displaycfg = true;
   }

   LockRes();
   foreach_res(monitor, R_MONITOR) {
      nitems++;
   }
   if (!displaycfg && nitems != 1) {
      Mmsg(config->m_errmsg,
           _("Error: %d Monitor resources defined in %s. "
             "You must define one Monitor resource.\n"),
           nitems, configfile);
      displaycfg = true;
   }
   monitor = (MONITOR*)GetNextRes(R_MONITOR, (RES *)NULL);
   UnlockRes();
   if (displaycfg) {
      display_error(config->m_errmsg);
   }
   refresh_tray(mainwidget);
   if (monitor && monitor->command_dir) {
      scheduler->init(monitor->command_dir);
      scheduler->start();
   } else {
      Dmsg0(50, "Do not start the scheduler\n");
   }
bail_out:
   return displaycfg;
}

/*********************************************************************
 *
 *         Main Bacula Tray Monitor -- User Interface Program
 *
 */
int main(int argc, char *argv[])
{   
   QApplication    app(argc, argv);
   int ch;
   bool test_config = false, display_cfg = false;
   TrayUI tray;
   TSched sched;
   
   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");

   init_stack_dump();
   my_name_is(argc, argv, "tray-monitor");
   lmgr_init_thread();
   init_msg(NULL, NULL, NULL);
#ifdef HAVE_WIN32
   working_directory = getenv("TMP");
#endif
   if (working_directory == NULL) {
      working_directory = "/tmp";
   }
   start_watchdog();

#ifndef HAVE_WIN32
   struct sigaction sigignore;
   sigignore.sa_flags = 0;
   sigignore.sa_handler = SIG_IGN;
   sigfillset(&sigignore.sa_mask);
   sigaction(SIGPIPE, &sigignore, NULL);
#endif

   while ((ch = getopt(argc, argv, "c:d:th?TW:")) != -1) {
      switch (ch) {
      case 'c':                    /* configuration file */
         if (configfile != NULL) {
            free(configfile);
         }
         configfile = bstrdup(optarg);
         break;

      case 'W':
         tray.have_systray = (atoi(optarg) != 0);
         break;

      case 'T':
         set_trace(true);
         break;

      case 'd':
         if (*optarg == 't') {
            dbg_timestamp = true;
         } else {
            debug_level = atoi(optarg);
            if (debug_level <= 0) {
               debug_level = 1;
            }
         }
         break;

      case 't':
         test_config = true;
         break;

      case 'h':
      case '?':
      default:
         usage();
         exit(1);
      }
   }
   argc -= optind;
   //argv += optind;

   if (argc) {
      usage();
      exit(1);
   }

   /* Keep generated files for ourself */
   umask(0077);

   if (configfile == NULL) {
      if (getenv(HOME_VAR) != NULL) {
         int len = strlen(getenv(HOME_VAR)) + strlen(CONFIG_FILE_HOME) + 5;
         configfile = (char *) malloc(len);
         bsnprintf(configfile, len, "%s/%s", getenv(HOME_VAR), CONFIG_FILE_HOME);

      } else {
         configfile = bstrdup(CONFIG_FILE);
      }
   }
   Dmsg1(50, "configfile=%s\n", configfile);

   // We need to initialize the scheduler before the reload() command
   scheduler = &sched;

   OSDependentInit();               /* Initialize Windows path handling */ 
   (void)WSA_Init();                /* Initialize Windows sockets */

   display_cfg = reload();

   if (test_config) {
      exit(0);
   }
   /* If we have a systray, we always keep the application*/
   if (tray.have_systray) {
      app.setQuitOnLastWindowClosed(false);

   } else {  /* Without a systray, we quit when we close */
      app.setQuitOnLastWindowClosed(true);
   }
   tray.setupUi(&tray, monitor);
   refresh_tray(&tray);
   mainwidget = &tray;
   if (display_cfg) {
      new Conf();
   }
   app.exec();
   sched.stop();
   stop_watchdog();
   (void)WSACleanup();               /* Cleanup Windows sockets */

   if (config) {
      delete config;
   }
   config = NULL;
   bfree_and_null(configfile);
   term_msg();
   return 0;
}
