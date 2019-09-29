#ifndef _JOBS_H_
#define _JOBS_H_
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
#include "ui_jobs.h"
#include "console.h"
#include "pages.h"

class Jobs : public Pages, public Ui::jobsForm
{
   Q_OBJECT 

public:
   Jobs();
   ~Jobs();
   virtual void PgSeltreeWidgetClicked();
   virtual void currentStackItem();

public slots:
   void tableItemChanged(QTableWidgetItem *, QTableWidgetItem *);

private slots:
   void populateTable();
   void consoleListFiles();
   void consoleListVolume();
   void consoleListNextVolume();
   void consoleEnable();
   void consoleDisable();
   void consoleCancel();
   void listJobs();
   void runJob();

private:
   void createContextMenu();
   QString m_currentlyselected;
   bool m_populated;
   bool m_checkcurwidget;
   int m_typeIndex;
};

#endif /* _JOBS_H_ */
