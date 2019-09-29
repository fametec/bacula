#ifndef _CLIENTSTAT_H_
#define _CLIENTSTAT_H_
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
#include "ui_clientstat.h"
#include "console.h"
#include "pages.h"

class ClientStat : public Pages, public Ui::ClientStatForm
{
   Q_OBJECT 

public:
   ClientStat(QString&, QTreeWidgetItem*);
   ~ClientStat();
   virtual void PgSeltreeWidgetClicked();
   virtual void currentStackItem();

public slots:
   void populateHeader();
   void populateTerminated();
   void populateRunning();
   void populateAll();
   void populateCurrentTab(int);

private slots:
   void timerTriggered();

private:
   void createConnections();
   void writeSettings();
   void readSettings();
   bool m_populated;
   QTextCursor *m_cursor;
   void getFont();
   QString m_groupText, m_splitText;
   QTimer *m_timer;
   QString m_client;
};

#endif /* _CLIENTSTAT_H_ */
