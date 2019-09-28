#ifndef _JOB_H_
#define _JOB_H_
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

#if QT_VERSION >= 0x050000
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include "ui_job.h"
#include "console.h"

class Job : public Pages, public Ui::JobForm
{
   Q_OBJECT 

public:
   Job(QString &jobId, QTreeWidgetItem *parentTreeWidgetItem);

public slots:
   void populateAll();
   void deleteJob();
   void cancelJob();
   void showInfoVolume(QListWidgetItem *);
   void rerun();
   void storeBwLimit(int val);

private slots:

private:
   void updateRunInfo();
   void populateText();
   void populateForm();
   void populateVolumes();
   void getFont();
   QTextCursor *m_cursor;
   QString m_jobId;
   QString m_client;
   QTimer *m_timer;
   int m_bwlimit;
};

#endif /* _JOB_H_ */
