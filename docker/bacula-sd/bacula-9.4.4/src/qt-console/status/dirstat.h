#ifndef _DIRSTAT_H_
#define _DIRSTAT_H_
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
#include "ui_dirstat.h"
#include "console.h"
#include "pages.h"

class DirStat : public Pages, public Ui::DirStatForm
{
   Q_OBJECT 

public:
   DirStat();
   ~DirStat();
   virtual void PgSeltreeWidgetClicked();
   virtual void currentStackItem();

public slots:
   void populateHeader();
   void populateTerminated();
   void populateScheduled();
   void populateRunning();
   void populateAll();

private slots:
   void timerTriggered();
   void consoleCancelJob();
   void consoleDisableJob();

private:
   void createConnections();
   void writeSettings();
   void readSettings();
   bool m_populated;
   QTextCursor *m_cursor;
   void getFont();
   QString m_groupText, m_splitText;
   QTimer *m_timer;
};

#endif /* _DIRSTAT_H_ */
