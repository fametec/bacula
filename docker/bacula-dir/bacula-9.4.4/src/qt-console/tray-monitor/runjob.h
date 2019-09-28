/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2017 Kern Sibbald

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

#ifndef RUN_H
#define RUN_H

#include "common.h"
#include "ui_run.h"
#include "tray_conf.h"
#include "task.h"
#include <QDialog>

class RunJob: public QDialog
{
   Q_OBJECT

public:
   RESMON *res;
   QWidget *tabAdvanced;
   POOL_MEM command;
   POOL_MEM info;
   POOL_MEM level;
   POOL_MEM curjob;
   Ui::runForm ui;
   RunJob(RESMON *r);
   ~RunJob();

public slots:
   void jobChanged(int);
   void levelChanged(int);
   void jobStarted(task *);
   void jobInfo(task *);
   void fill_defaults(task *);
   void tabChange(int idx);
   void runjob();
   /* close the window properly */
   void close_cb(task *t);
   void close_cb();
};

/* Object that can scan a directory to find jobs */
class TSched: public QObject
{
   Q_OBJECT
private:
   char *command_dir;
   bool read_command_file(const char *file, alist *lst, btime_t mtime);
   int   timer;

public:
   TSched();
   ~TSched();
   void init(const char *cmd_dir);
   bool scan_for_commands(alist *lst);
   void start() {
      timer = startTimer(60000);  // 1-minute timer
   };
   void stop() {
      if (timer >= 0) {
         killTimer(timer);
         timer = -1;
      }
   };
public slots:
   void jobStarted(task *t);
protected:
   void timerEvent(QTimerEvent *event);

};


/* Job found in the command directory */
class TSchedJob: public QObject
{
   Q_OBJECT

public:
   char *component;             // Name of the daemon
   char *command;               // job command
   btime_t create_date;         // When the command file was created
   TSchedJob() : component(NULL), command(NULL) {};

   TSchedJob(const char *comp, const char *cmd, btime_t cd) {
         component = bstrdup(comp);
         command = bstrdup(cmd);
         create_date = cd;
   };

   ~TSchedJob() {
      clear();
   };
   void clear() {
      if (component) {
         bfree_and_null(component);
      }
      if (command) {
         bfree_and_null(command);
      }
      create_date = 0;
   };
};

#endif
