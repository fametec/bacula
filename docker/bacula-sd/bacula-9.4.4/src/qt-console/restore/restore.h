#ifndef _RESTORE_H_
#define _RESTORE_H_

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
 *
 *  Kern Sibbald, February 2007
 */

#include <stdint.h>
#include <sys/types.h>     /* Needed for some systems */
#if QT_VERSION >= 0x050000
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include "pages.h"
#include "ui_runrestore.h"

class bRestoreTable : public QTableWidget
{
   Q_OBJECT 
private:
   QPoint dragStartPosition;
public:
   bRestoreTable(QWidget *parent)
      : QTableWidget(parent)
   {
   }
   void mousePressEvent(QMouseEvent *event);
   void mouseMoveEvent(QMouseEvent *event);

   void dragEnterEvent(QDragEnterEvent *event);
   void dragMoveEvent(QDragMoveEvent *event);
   void dropEvent(QDropEvent *event);
};

#include "ui_brestore.h"
#include "ui_restore.h"
#include "ui_prerestore.h"

enum {
   R_NONE,
   R_JOBIDLIST,
   R_JOBDATETIME
};

/*
 * The pre-restore dialog selects the Job/Client to be restored
 * It really could use considerable enhancement.
 */
class prerestorePage : public Pages, public Ui::prerestoreForm
{
   Q_OBJECT 

public:
   prerestorePage();
   prerestorePage(QString &data, unsigned int);

private slots:
   void okButtonPushed();
   void cancelButtonPushed();
   void job_name_change(int index);
   void recentChanged(int);
   void jobRadioClicked(bool);
   void jobidsRadioClicked(bool);
   void jobIdEditFinished();

private:
   int m_conn;
   int jobdefsFromJob(QStringList &, QString &);
   void buildPage();
   bool checkJobIdList();
   QString m_dataIn;
   unsigned int m_dataInType;
};

/*  
 * The restore dialog is brought up once we are in the Bacula
 * restore tree routines.  It handles putting up a GUI tree
 * representation of the files to be restored.
 */
class restorePage : public Pages, public Ui::restoreForm
{
   Q_OBJECT 

public:
   restorePage(int conn);
   ~restorePage();
   void fillDirectory();
   char *get_cwd();
   bool cwd(const char *);

private slots:
   void okButtonPushed();
   void cancelButtonPushed();
   void fileDoubleClicked(QTreeWidgetItem *item, int column);
   void directoryItemChanged(QTreeWidgetItem *, QTreeWidgetItem *);
   void upButtonPushed();
   void unmarkButtonPushed();
   void markButtonPushed();
   void addDirectory(QString &);

private:
   int m_conn;
   void writeSettings();
   void readSettings();
   QString m_cwd;
   QHash<QString, QTreeWidgetItem *> m_dirPaths;
   QHash<QTreeWidgetItem *,QString> m_dirTreeItems;
   QRegExp m_rx;
   QString m_splitText;
};

class bRestore : public Pages, public Ui::bRestoreForm
{
   Q_OBJECT 

public:
   bRestore();
   ~bRestore();
   void PgSeltreeWidgetClicked();
   QString m_client;
   QString m_jobids;
   void get_info_from_selection(QStringList &fileids, QStringList &jobids,
                                QStringList &dirids, QStringList &fileindexes);

public slots:
   void setClient();
   void setJob();
   void showInfoForFile(QTableWidgetItem *);
   void applyLocation();
   void clearVersions(QTableWidgetItem *);
   void clearRestoreList();
   void runRestore();
   void refreshView();
private:
   QString m_path;
   int64_t m_pathid;
   QTableWidgetItem *m_current;
   void setupPage();
   bool m_populated;
   void displayFiles(int64_t pathid, QString path);
   void displayFileVersion(QString pathid, QString fnid, 
                           QString client, QString filename);
};

class bRunRestore : public QDialog, public Ui::bRunRestoreForm
{
   Q_OBJECT 
private:
   bRestore *brestore;
   QStringList m_fileids, m_jobids, m_dirids, m_findexes;

public:
   bRunRestore(bRestore *parent);
   ~bRunRestore() {}
   void computeVolumeList();
   int64_t runRestore(QString tablename);

public slots:
   void useRegexp();
   void UFRcb();
   void computeRestore();
};

#endif /* _RESTORE_H_ */
