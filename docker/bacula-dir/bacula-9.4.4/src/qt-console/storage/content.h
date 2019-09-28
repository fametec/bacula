#ifndef _CONTENT_H_
#define _CONTENT_H_
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
#include "ui_content.h"
#include "console.h"
#include "pages.h"

class Content : public Pages, public Ui::ContentForm
{
   Q_OBJECT 

public:
   Content(QString storage, QTreeWidgetItem *parentWidget);
//   virtual void PgSeltreeWidgetClicked();
   virtual void currentStackItem();

public slots:
   void treeItemChanged(QTreeWidgetItem *, QTreeWidgetItem *);

   void consoleRelease();
   void consoleUpdateSlots();
   void consoleLabelStorage();
   void consoleMountStorage();
   void statusStorageWindow();
   void consoleUnMountStorage();
   void showMediaInfo(QTableWidgetItem * item);

private slots:
   void populateContent();

private:
   bool m_currentAutoChanger;
   bool m_populated;
   bool m_firstpopulation;
   bool m_checkcurwidget;
   QString m_currentStorage;
};

#endif /* _STORAGE_H_ */
