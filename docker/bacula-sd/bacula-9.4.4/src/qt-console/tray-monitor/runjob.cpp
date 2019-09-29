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

#include "runjob.h"
#include <QMessageBox>

static void fillcombo(QComboBox *cb, alist *lst, bool addempty=true)
{
   if (lst && lst->size() > 0) {
      QStringList list;
      char *str;
      if (addempty) {
         list << QString("");
      }
      foreach_alist(str, lst) {
         list << QString(str);
      }
      cb->addItems(list);
   } else {
      cb->setEnabled(false);
   }
}

RunJob::RunJob(RESMON *r): QDialog(), res(r), tabAdvanced(NULL)
{
   int nbjob;
   if (res->jobs->size() == 0) {
      QMessageBox msgBox;
      msgBox.setText(_("This restricted console does not have access to Backup jobs"));
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.exec();
      deleteLater();
      return;

   }

   ui.setupUi(this);
   setModal(true);
   connect(ui.cancelButton, SIGNAL(clicked()), this, SLOT(close_cb()));
   connect(ui.okButton, SIGNAL(clicked()), this, SLOT(runjob()));
   connect(ui.jobCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(jobChanged(int)));
   connect(ui.levelCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(levelChanged(int)));
   ui.dateTimeEdit->setMinimumDate(QDate::currentDate());
   ui.dateTimeEdit->setMaximumDate(QDate::currentDate().addDays(7));
   ui.dateTimeEdit->setDate(QDate::currentDate());
   ui.dateTimeEdit->setTime(QTime::currentTime());
   ui.boxEstimate->setVisible(false);

   res->mutex->lock();
   nbjob = res->jobs->size();
   fillcombo(ui.jobCombo,    res->jobs, (nbjob > 1));
   fillcombo(ui.clientCombo, res->clients);
   fillcombo(ui.filesetCombo,res->filesets);
   fillcombo(ui.poolCombo,   res->pools);
   fillcombo(ui.storageCombo,res->storages);
   fillcombo(ui.catalogCombo,res->catalogs);
   res->mutex->unlock();
   connect(ui.tabWidget, SIGNAL(currentChanged(int)), this, SLOT(tabChange(int)));
   QStringList levels;
   levels << "" << "Incremental" << "Differential"  << "Full";
   ui.levelCombo->addItems(levels);

   MONITOR *m = (MONITOR*) GetNextRes(R_MONITOR, NULL);
   if (!m->display_advanced_options) {
      tabAdvanced = ui.tabWidget->widget(1);
      ui.tabWidget->removeTab(1);
   }

   show();
}

void RunJob::tabChange(int idx)
{
   QString q = ui.tabWidget->tabText(idx);
   if (q.contains("Advanced")) {
      if (ui.jobCombo->currentText().compare("") == 0) {
         pm_strcpy(curjob, "");
         ui.tab2->setEnabled(false);

      } else if (ui.jobCombo->currentText().compare(curjob.c_str()) != 0) {
         task *t = new task();
         char *job = bstrdup(ui.jobCombo->currentText().toUtf8().data());
         pm_strcpy(curjob, job); // Keep the job name to not refresh the Advanced tab the next time

         Dmsg1(10, "get defaults for %s\n", job);
         res->mutex->lock();
         bfree_and_null(res->defaults.job);
         res->defaults.job = job;
         res->mutex->unlock();

         ui.tab2->setEnabled(false);
         connect(t, SIGNAL(done(task *)), this, SLOT(fill_defaults(task *)), Qt::QueuedConnection);
         t->init(res, TASK_DEFAULTS);
         res->wrk->queue(t);
      }
   }
}

void RunJob::runjob()
{
   POOL_MEM tmp;
   char *p;

   p = ui.jobCombo->currentText().toUtf8().data();
   if (!p || !*p) {
      QMessageBox msgBox;
      msgBox.setText(_("Nothing selected"));
      msgBox.setIcon(QMessageBox::Warning);
      msgBox.exec();
      return;
   }

   Mmsg(command, "run job=\"%s\" yes", p);

   if (strcmp(p, NPRTB(res->defaults.job)) == 0 || strcmp("", NPRTB(res->defaults.job)) == 0) {
      p = ui.storageCombo->currentText().toUtf8().data();
      if (p && *p && strcmp(p, NPRTB(res->defaults.storage)) != 0) {
         Mmsg(tmp, " storage=\"%s\"", p);
         pm_strcat(command, tmp.c_str());
      }

      p = ui.clientCombo->currentText().toUtf8().data();
      if (p && *p && strcmp(p, NPRTB(res->defaults.client)) != 0) {
         Mmsg(tmp, " client=\"%s\"", p);
         pm_strcat(command, tmp.c_str());
      }

      p = ui.levelCombo->currentText().toUtf8().data();
      if (p && *p && strcmp(p, NPRTB(res->defaults.level)) != 0) {
         Mmsg(tmp, " level=\"%s\"", p);
         pm_strcat(command, tmp.c_str());
      }

      p = ui.poolCombo->currentText().toUtf8().data();
      if (p && *p && strcmp(p, NPRTB(res->defaults.pool)) != 0) {
         Mmsg(tmp, " pool=\"%s\"", p);
         pm_strcat(command, tmp.c_str());
      }

      p = ui.filesetCombo->currentText().toUtf8().data();
      if (p && *p && strcmp(p, NPRTB(res->defaults.fileset)) != 0) {
         Mmsg(tmp, " fileset=\"%s\"", p);
         pm_strcat(command, tmp.c_str());
      }

      if (res->defaults.priority && res->defaults.priority != ui.prioritySpin->value()) {
         Mmsg(tmp, " priority=\"%d\"", res->defaults.priority);
         pm_strcat(command, tmp.c_str());
      }
   }

   QDate dnow = QDate::currentDate();
   QTime tnow = QTime::currentTime();
   QDate dval = ui.dateTimeEdit->date();
   QTime tval = ui.dateTimeEdit->time();

   if (dval > dnow || (dval == dnow && tval > tnow)) {
      Mmsg(tmp, " when=\"%s %s\"", dval.toString("yyyy-MM-dd").toUtf8().data(), tval.toString("hh:mm:00").toUtf8().data());
      pm_strcat(command, tmp.c_str());
   }

   if (res->type == R_CLIENT) {
      pm_strcat(command, " fdcalled=1");
   }
   
   // Build the command and run it!
   task *t = new task();
   t->init(res, TASK_RUN);
   connect(t, SIGNAL(done(task *)), this, SLOT(jobStarted(task *)), Qt::QueuedConnection);
   t->arg = command.c_str();
   res->wrk->queue(t);
}

void RunJob::jobStarted(task *t)
{
   Dmsg1(10, "%s\n", command.c_str());
   Dmsg1(10, "-> jobid=%d\n", t->result.i);
   deleteLater();
   delete t;
}

void RunJob::close_cb(task *t)
{
   deleteLater();
   delete t;
}

void RunJob::close_cb()
{
   task *t = new task();
   connect(t, SIGNAL(done(task *)), this, SLOT(close_cb(task *)), Qt::QueuedConnection);
   t->init(res, TASK_DISCONNECT);
   res->wrk->queue(t);
}

void RunJob::jobChanged(int)
{
   char *p;
   ui.levelCombo->setCurrentIndex(0);
   ui.storageCombo->setCurrentIndex(0);
   ui.filesetCombo->setCurrentIndex(0);
   ui.clientCombo->setCurrentIndex(0);
   ui.storageCombo->setCurrentIndex(0);
   ui.poolCombo->setCurrentIndex(0);
   ui.catalogCombo->setCurrentIndex(0);

   p = ui.jobCombo->currentText().toUtf8().data();
   if (p && *p) {
      task *t = new task();
      t->init(res, TASK_INFO);
      pm_strcpy(info, p);
      connect(t, SIGNAL(done(task *)), this, SLOT(jobInfo(task *)), Qt::QueuedConnection);
      t->arg = info.c_str();    // Jobname
      t->arg2 = NULL;           // Level
      res->wrk->queue(t);
   }
}

void RunJob::levelChanged(int)
{
   char *p;
   p = ui.jobCombo->currentText().toUtf8().data();
   if (p && *p) {
      pm_strcpy(info, p);      
      p = ui.levelCombo->currentText().toUtf8().data();
      if (p && *p) {
         task *t = new task();
         pm_strcpy(level, p);
         connect(t, SIGNAL(done(task *)), this, SLOT(jobInfo(task *)), Qt::QueuedConnection);
         t->init(res, TASK_INFO);
         t->arg = info.c_str();    // Jobname
         t->arg2 = level.c_str();  // Level
         res->wrk->queue(t);
      }
   }
}

void RunJob::jobInfo(task *t)
{
   char ed1[50];
   res->mutex->lock();
   if (res->infos.CorrNbJob == 0) {
      ui.boxEstimate->setVisible(false);
   } else {
      QString t;
      edit_uint64_with_suffix(res->infos.JobBytes, ed1);
      strncat(ed1, "B", sizeof(ed1));
      ui.labelJobBytes->setText(QString(ed1));
      ui.labelJobFiles->setText(QString(edit_uint64_with_commas(res->infos.JobFiles, ed1)));
      ui.labelJobLevel->setText(QString(job_level_to_str(res->infos.JobLevel)));
      t = tr("Computed over %1 job%2, the correlation is %3/100.").arg(res->infos.CorrNbJob).arg(res->infos.CorrNbJob>1?"s":"").arg(res->infos.CorrJobBytes);
      ui.labelJobBytes_2->setToolTip(t);
      t = tr("Computed over %1 job%2, The correlation is %3/100.").arg(res->infos.CorrNbJob).arg(res->infos.CorrNbJob>1?"s":"").arg(res->infos.CorrJobFiles);
      ui.labelJobFiles_2->setToolTip(t);
      ui.boxEstimate->setVisible(true);
   }
   res->mutex->unlock();
   t->deleteLater();
}

static void set_combo(QComboBox *dest, char *str)
{
   if (str) {
      int idx = dest->findText(QString(str), Qt::MatchExactly);
      if (idx >= 0) {
         dest->setCurrentIndex(idx);
      }
   }
}

void RunJob::fill_defaults(task *t)
{
   if (t->status == true) {
      res->mutex->lock();
      set_combo(ui.levelCombo, res->defaults.level);
      set_combo(ui.filesetCombo, res->defaults.fileset);
      set_combo(ui.clientCombo, res->defaults.client);
      set_combo(ui.storageCombo, res->defaults.storage);
      set_combo(ui.poolCombo, res->defaults.pool);
      set_combo(ui.catalogCombo, res->defaults.catalog);
      res->mutex->unlock();
   }

   ui.tab2->setEnabled(true);
   t->deleteLater();
}

RunJob::~RunJob()
{
   Dmsg0(10, "~RunJob()\n");
   if (tabAdvanced) {
      delete tabAdvanced;
   }
}

void TSched::init(const char *cmd_dir)
{
   bool started = (timer >= 0);
   if (started) {
      stop();
   }

   bfree_and_null(command_dir);
   command_dir = bstrdup(cmd_dir);

   if (started) {
      start();
   }
}

TSched::TSched() {
   timer = -1;
   command_dir = NULL;
}

TSched::~TSched() {
   if (timer >= 0) {
      stop();
   }
   bfree_and_null(command_dir);
}

#include <dirent.h>
int breaddir(DIR *dirp, POOLMEM *&dname);

bool TSched::read_command_file(const char *file, alist *lst, btime_t mtime)
{
   POOLMEM *line;
   bool ret=false;
   char *p;
   TSchedJob *s;
   Dmsg1(50, "open command file %s\n", file);
   FILE *fp = fopen(file, "r");
   if (!fp) {
      return false;
   }
   line = get_pool_memory(PM_FNAME);

   /* Get the first line, client/component:command */
   while (bfgets(line, fp) != NULL) {
      strip_trailing_junk(line);
      Dmsg1(50, "%s\n", line);
      if (line[0] == '#') {
         continue;
      }

      if ((p = strchr(line, ':')) != NULL) {
         *p=0;
         s = new TSchedJob(line, p+1, mtime);
         lst->append(s);
         ret = true;
      }
   }

   free_pool_memory(line);
   fclose(fp);
   return ret;
}

#include "lib/plugins.h"
#include "lib/cmd_parser.h"

void TSched::timerEvent(QTimerEvent *event)
{
   Q_UNUSED(event)
   POOL_MEM tmp, command;
   TSchedJob *j;
   alist lst(10, not_owned_by_alist);
   arg_parser parser;
   int i;
   task *t;
   RESMON *res;
   scan_for_commands(&lst);

   foreach_alist(j, (&lst)) {
      if (parser.parse_cmd(j->command) == bRC_OK) {
         if ((i = parser.find_arg_with_value("job")) > 0) {
            QMessageBox msgbox;
            foreach_res(res, R_CLIENT) {
               if (strcmp(res->hdr.name, j->component) == 0) {
                  break;
               }
            }
            if (!res) {
               foreach_res(res, R_DIRECTOR) {
                  if (strcmp(res->hdr.name, j->component) == 0) {
                     break;
                  }
               }
            }
            if (!res) {
               msgbox.setIcon(QMessageBox::Information);
               msgbox.setText(QString("Unable to find the component \"%1\" to run the job \"%2\".").arg(j->component, j->command));
               msgbox.setStandardButtons(QMessageBox::Ignore);
            } else {

               msgbox.setIcon(QMessageBox::Information);
               msgbox.setText(QString("The job \"%1\" will start automatically in few seconds...").arg(parser.argv[i]));
               msgbox.setStandardButtons(QMessageBox::Ok | QMessageBox::Ignore);
               msgbox.setDefaultButton(QMessageBox::Ok);
               msgbox.button(QMessageBox::Ok)->animateClick(6000);
            }
            switch(msgbox.exec()) {
               case QMessageBox::Ok:
                  Mmsg(command, "%s yes", j->command);

                  if (res->type == R_CLIENT) {
                     pm_strcat(command, " fdcalled=1");
                  }
   
                  // Build the command and run it!
                  t = new task();
                  t->init(res, TASK_RUN);
                  connect(t, SIGNAL(done(task *)), this, SLOT(jobStarted(task *)), Qt::QueuedConnection);
                  t->arg = command.c_str();
                  res->wrk->queue(t);

                  break;
               case QMessageBox::Cancel:
               case QMessageBox::Ignore:
                  break;
            }
         }
      }
      delete j;
   }
}

void TSched::jobStarted(task *t)
{
   Dmsg1(10, "-> jobid=%d\n", t->result.i);
   t->deleteLater();
}


bool TSched::scan_for_commands(alist *commands)
{
   int name_max, len;
   DIR* dp = NULL;
   POOL_MEM fname(PM_FNAME), fname2(PM_FNAME);
   POOL_MEM dir_entry;
   bool ret=false, found=false;
   struct stat statp;

   name_max = pathconf(".", _PC_NAME_MAX);
   if (name_max < 1024) {
      name_max = 1024;
   }

   if (!(dp = opendir(command_dir))) {
      berrno be;
      Dmsg2(0, "Failed to open directory %s: ERR=%s\n",
            command_dir, be.bstrerror());
      goto bail_out;
   }

   for ( ;; ) {
      if (breaddir(dp, dir_entry.addr()) != 0) {
         if (!found) {
            goto bail_out;
         }
         break;
      }
      if (strcmp(dir_entry.c_str(), ".") == 0 ||
          strcmp(dir_entry.c_str(), "..") == 0) {
         continue;
      }
      len = strlen(dir_entry.c_str());
      if (len <= 5) {
         continue;
      }
      if (strcmp(dir_entry.c_str() + len - 5, ".bcmd") != 0) {
         continue;
      }

      Mmsg(fname, "%s/%s", command_dir, dir_entry.c_str());

      if (lstat(fname.c_str(), &statp) != 0 || !S_ISREG(statp.st_mode)) {
         continue;                 /* ignore directories & special files */
      }

      if (read_command_file(fname.c_str(), commands, statp.st_mtime)) {
         Mmsg(fname2, "%s.ok", fname.c_str());
         unlink(fname2.c_str());
         rename(fname.c_str(), fname2.c_str()); // TODO: We should probably unlink the file
      }
   }
bail_out:
   if (dp) {
      closedir(dp);
   }
   return ret;
}
