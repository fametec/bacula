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

#include "sdstatus.h"
#include "../util/fmtwidgetitem.h"
#include "jcr.h"

void SDStatus::doUpdate()
{
   if (count == 0) {
      count++;
      task *t = new task();
      status.pushButton->setEnabled(false);
      connect(t, SIGNAL(done(task *)), this, SLOT(taskDone(task *)), Qt::QueuedConnection);
      t->init(res, TASK_STATUS);
      res->wrk->queue(t);
      status.statusBar->setText(QString("Trying to connect to Storage..."));
      Dmsg1(50, "doUpdate(%p)\n", res);
   }
}

void SDStatus::taskDone(task *t)
{
   count--;
   if (!t->status) {
      status.statusBar->setText(QString(t->errmsg));

   } else {
      status.statusBar->clear();
      if (t->type == TASK_STATUS) {
         char ed1[50];
         struct s_last_job *ljob;
         struct s_running_job *rjob;
         res->mutex->lock();
         status.labelName->setText(QString(res->name));
         status.labelVersion->setText(QString(res->version));
         status.labelStarted->setText(QString(res->started));
         status.labelPlugins->setText(QString(res->plugins));
         /* Clear the table first */
         Freeze(*status.tableRunning);
         Freeze(*status.tableTerminated);
         QStringList headerlistR = (QStringList() << tr("JobId")
                                    << tr("Job")  << tr("Level") << tr("Client")
                                    << tr("Storage")
                                    << tr("Files") << tr("Bytes") << tr("Errors"));
         status.tableRunning->clear();
         status.tableRunning->setRowCount(0);
         status.tableRunning->setColumnCount(headerlistR.count());
         status.tableRunning->setHorizontalHeaderLabels(headerlistR);
         status.tableRunning->setEditTriggers(QAbstractItemView::NoEditTriggers);
         status.tableRunning->verticalHeader()->hide();
         status.tableRunning->setSortingEnabled(true);

         if (res->running_jobs) {
            status.tableRunning->setRowCount(res->running_jobs->size());
            int row=0;
            foreach_alist(rjob, res->running_jobs) {
               int col=0;
               TableItemFormatter item(*status.tableRunning, row++);
               item.setNumericFld(col++, QString(edit_uint64(rjob->JobId, ed1)));
               item.setTextFld(col++, QString(rjob->Job));
               item.setJobLevelFld(col++, QString(rjob->JobLevel));
               item.setTextFld(col++, QString(rjob->Client));
               item.setTextFld(col++, QString(rjob->Storage));
               item.setNumericFld(col++, QString(edit_uint64(rjob->JobFiles, ed1)));
               item.setBytesFld(col++, QString(edit_uint64(rjob->JobBytes, ed1)));
               item.setNumericFld(col++, QString(edit_uint64(rjob->Errors, ed1)));
            }
         } else {
            Dmsg0(0, "Strange, the list is NULL\n");
         }

         QStringList headerlistT = (QStringList() << tr("JobId")
                                    << tr("Job")  << tr("Level")
                                    << tr("Status") << tr("Files") << tr("Bytes")
                                    << tr("Errors"));

         status.tableTerminated->clear();
         status.tableTerminated->setRowCount(0);
         status.tableTerminated->setColumnCount(headerlistT.count());
         status.tableTerminated->setHorizontalHeaderLabels(headerlistT);
         status.tableTerminated->setEditTriggers(QAbstractItemView::NoEditTriggers);
         status.tableTerminated->verticalHeader()->hide();
         status.tableTerminated->setSortingEnabled(true);

         if (res->terminated_jobs) {
            status.tableTerminated->setRowCount(res->terminated_jobs->size());
            int row=0;
            foreach_dlist(ljob, res->terminated_jobs) {
               int col=0;
               TableItemFormatter item(*status.tableTerminated, row++);
               item.setNumericFld(col++, QString(edit_uint64(ljob->JobId, ed1)));
               item.setTextFld(col++, QString(ljob->Job));
               item.setJobLevelFld(col++, QString(ljob->JobLevel));
               item.setJobStatusFld(col++, QString(ljob->JobStatus));
               item.setNumericFld(col++, QString(edit_uint64(ljob->JobFiles, ed1)));
               item.setBytesFld(col++, QString(edit_uint64(ljob->JobBytes, ed1)));
               item.setNumericFld(col++, QString(edit_uint64(ljob->Errors, ed1)));
            }
         } else {
            Dmsg0(0, "Strange, the list is NULL\n");
         }
         res->mutex->unlock();
      }
      Dmsg1(50, "  Task %p OK\n", t);
   }
   t->deleteLater();
   status.pushButton->setEnabled(true);   
}
