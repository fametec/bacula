#ifndef _PAGES_H_
#define _PAGES_H_
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
/*
 *   Dirk Bartley, March 2007
 */

#include <QtGlobal>
#if QT_VERSION >= 0x050000
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include <QList>

/*
 *  The Pages Class
 *
 *  This class is inherited by all widget windows which are on the stack
 *  It is for the purpose of having a consistent set of functions and properties
 *  in all of the subclasses to accomplish tasks such as pulling a window out
 *  of or into the stack.  It also provides virtual functions called
 *  from in mainwin so that subclasses can contain functions to allow them
 *  to populate the screens at the time of first viewing, (when selected) as
 *  opposed to  the first creation of the console connection.  The 
 *  console is not connected until after the page selector tree has been
 *  populated.
 */

class Console;

class Pages : public QWidget
{
   Q_OBJECT

public:
   /* methods */
   Pages();
   void dockPage();
   void undockPage();
   void hidePage();
   void togglePageDocking();
   bool isDocked();
   bool isOnceDocked();
   bool isCloseable();
   virtual void PgSeltreeWidgetClicked();
   virtual void currentStackItem();
   void closeStackPage();
   Console *console() { return m_console; };
   void setCurrent();
   void setContextMenuDockText();
   void setTreeWidgetItemDockColor();
   void consoleCommand(QString &);
   void consoleCommand(QString &, bool setCurrent);
   void consoleCommand(QString &, int conn, bool setCurrent=true, bool notify=true);
   QString &name() { return m_name; };
   void getVolumeList(QStringList &);
   void getStatusList(QStringList &);
   void firstUseDock();

   /* members */
   QTabWidget *m_parent;
   QList<QAction*> m_contextActions;


public slots:
   /* closeEvent is a virtual function inherited from QWidget */
   virtual void closeEvent(QCloseEvent* event);

protected:
   /* methods */
   void pgInitialize();
   void pgInitialize(const QString &);
   void pgInitialize(const QString &, QTreeWidgetItem *);
   virtual void treeWidgetName(QString &);
   virtual void changeEvent(QEvent *event);
   void setConsoleCurrent();
   void setTitle();

   /* members */
   bool m_closeable;
   bool m_docked;
   bool m_onceDocked;
   bool m_dockOnFirstUse;
   Console *m_console;
   QString m_name;
};

#endif /* _PAGES_H_ */
