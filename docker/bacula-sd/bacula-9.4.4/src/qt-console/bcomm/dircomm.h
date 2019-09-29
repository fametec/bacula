#ifndef _DIRCOMM_H_
#define _DIRCOMM_H_
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
#include <QObject>

#ifndef MAX_NAME_LENGTH
#define MAX_NAME_LENGTH 128
#endif

class DIRRES;
class BSOCK;
class JCR;
class CONRES;

//class DirComm : public QObject
class DirComm : public QObject
{
   Q_OBJECT
   friend class Console;
public:
   DirComm(Console *parent, int conn);
   ~DirComm();
   Console *m_console;
   int  sock_read();
   bool authenticate_director(JCR *jcr, DIRRES *director, CONRES *cons, 
          char *buf, int buflen);
   bool is_connected() { return m_sock != NULL; }
   bool is_ready() { return is_connected() && m_at_prompt && m_at_main_prompt; }
   char *msg();
   bool notify(bool enable); // enables/disables socket notification - returns the previous state
   bool is_notify_enabled() const;
   bool is_in_command() const { return m_in_command > 0; }
   void terminate();
   bool connect_dir();                     
   int read(void);
   int write(const char *msg);
   int write(QString msg);

public slots:
   void notify_read_dir(int fd);

private:
   BSOCK *m_sock;   
   bool m_at_prompt;
   bool m_at_main_prompt;
   bool m_sent_blank;
   bool m_notify;
   int  m_in_command;
   QSocketNotifier *m_notifier;
   bool m_api_set;
   int m_conn;
   bool m_in_select;
};

#endif /* _DIRCOMM_H_ */
