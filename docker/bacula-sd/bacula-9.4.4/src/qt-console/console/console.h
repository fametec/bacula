#ifndef _CONSOLE_H_
#define _CONSOLE_H_
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
 *   Kern Sibbald, January 2007
 */

#if QT_VERSION >= 0x050000
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include "pages.h"
#include "ui_console.h"
#include "bcomm/dircomm.h"

#ifndef MAX_NAME_LENGTH
#define MAX_NAME_LENGTH 128
#endif

/*
 * Structure for obtaining the defaults for a job
 */
struct job_defaults {
   QString job_name;
   QString pool_name;
   QString messages_name;
   QString client_name;
   QString store_name;
   QString where;
   QString level;
   QString type;
   QString fileset_name;
   QString catalog_name;
   bool enabled;
};

//class DIRRES;
//class BSOCK;
//class JCR;
//class CONRES;

class Console : public Pages, public Ui::ConsoleForm
{
   Q_OBJECT 
   friend class DirComm;

public:
   Console(QTabWidget *parent);
   ~Console();
   int read(int conn);
   char *msg(int conn);
   void discardToPrompt(int conn);
   int write(int conn, const char *msg);
   int write(int conn, QString msg);
   int notifyOff(); // enables/disables socket notification - returns the previous state
   bool notify(int conn, bool enable); // enables/disables socket notification - returns the previous state
   bool is_notify_enabled(int conn) const;
   bool getDirComm(int &conn);  
   bool findDirComm(int &conn);
   void displayToPrompt(int conn);
   QString returnFromPrompt(int conn);

   bool dir_cmd(int conn, const char *cmd, QStringList &results);
   bool dir_cmd(const char *cmd, QStringList &results);
   bool dir_cmd(QString &cmd, QStringList &results);
   bool sql_cmd(const char *cmd, QStringList &results);
   bool sql_cmd(QString &cmd, QStringList &results);
   bool sql_cmd(int &conn, QString &cmd, QStringList &results);
   bool sql_cmd(int &conn, const char *cmd, QStringList &results, bool donotify);
   int write_dir(const char *buf);
   int write_dir(const char *buf, bool dowait);
   void write_dir(int conn, const char *buf);
   void write_dir(int conn, const char *buf, bool dowait);
   void getDirResName(QString &);
   void setDirRes(DIRRES *dir);
   void writeSettings();
   void readSettings();
   void setDirectorTreeItem(QTreeWidgetItem *);
   void terminate();
   bool is_messagesPending() { return m_messages_pending; };
   bool is_connected();
   bool is_connected(int conn);
   QTreeWidgetItem *directorTreeItem() { return m_directorTreeItem; };
   void startTimer();
   void display_text(const char *buf);
   void display_text(const QString buf);
   void display_textf(const char *fmt, ...);
   void display_html(const QString buf);
   bool get_job_defaults(struct job_defaults &);
   bool get_job_defaults(int &conn, struct job_defaults &);
   const QFont get_font();
   void beginNewCommand(int conn);
   void populateLists(bool forcenew);

private:
   bool get_job_defaults(int &conn, struct job_defaults &, bool donotify);
   void update_cursor(void);
   void stopTimer();
   bool is_connectedGui();
   bool newDirComm(int &conn);
   void populateLists(int conn);

public:
   QStringList job_list;
   QStringList restore_list;
   QStringList client_list;
   QStringList fileset_list;
   QStringList messages_list;
   QStringList pool_list;
   QStringList storage_list;
   QStringList type_list;
   QStringList level_list;
   QStringList volstatus_list;
   QStringList mediatype_list;
   QStringList location_list;

public slots:
   void connect_dir();                     
   void status_dir(void);
   void messages(void);
   void set_font(void);
   void poll_messages(void);
   void consoleHelp();
   void consoleReload();

public:
   DIRRES *m_dir;                  /* so various pages can reference it */
   bool m_warningPrevent;

private:
   QTextEdit *m_textEdit;
   QTextCursor *m_cursor;
   QTreeWidgetItem *m_directorTreeItem;
   bool m_messages_pending;
   QTimer *m_timer;
   bool messagesPending(bool pend);
   bool hasFocus();
   QHash<int, DirComm*> m_dircommHash;
   int m_dircommCounter;
};

#endif /* _CONSOLE_H_ */
