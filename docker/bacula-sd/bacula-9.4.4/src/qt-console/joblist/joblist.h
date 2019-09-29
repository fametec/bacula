#ifndef _JOBLIST_H_
#define _JOBLIST_H_
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
 *   Dirk Bartley, March 2007
 */

#if QT_VERSION >= 0x050000
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include "ui_joblist.h"
#include "console.h"
#include "pages.h"

class JobList : public Pages, public Ui::JobListForm
{
   Q_OBJECT 

public:
   JobList(const QString &medianame, const QString &clientname, 
           const QString &jobname, const QString &filesetname, QTreeWidgetItem *);
   ~JobList();
   virtual void PgSeltreeWidgetClicked();
   virtual void currentStackItem();
   int m_resultCount;

public slots:
   void populateTable();
   virtual void treeWidgetName(QString &);
   void selectionChanged();

private slots:
   void consoleListFilesOnJob();
   void consoleListJobMedia();
   void consoleListJobTotals();
   void consoleDeleteJob();
   void consoleRestartJob();
   void consolePurgeFiles();
   void preRestoreFromJob();
   void preRestoreFromTime();
   void showLogForJob();
   void showInfoForJob(QTableWidgetItem * item=NULL);
   void consoleCancelJob();
   void graphTable();
   void splitterMoved(int pos, int index);

private:
   void createConnections();
   void writeSettings();
   void readSettings();
   void prepareFilterWidgets();
   void fillQueryString(QString &query);
   QSplitter *m_splitter;

   QString m_groupText;
   QString m_splitText;
   QString m_mediaName;
   QString m_clientName;
   QString m_jobName;
   QString m_filesetName;
   QString m_currentJob;
   QString m_levelName;

   bool m_populated;
   bool m_checkCurrentWidget;
   int m_jobIdIndex;
   int m_purgedIndex;
   int m_typeIndex;
   int m_levelIndex;
   int m_clientIndex;
   int m_nameIndex;
   int m_filesetIndex;
   int m_statusIndex;
   int m_startIndex;
   int m_bytesIndex;
   int m_filesIndex;
   int m_selectedJobsCount;
   QString m_selectedJobs;
   QStringList m_selectedJobsList;
};

#endif /* _JOBLIST_H_ */
