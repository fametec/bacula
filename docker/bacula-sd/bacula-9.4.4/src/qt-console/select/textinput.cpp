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
 *  Select dialog class
 *
 *   Kern Sibbald, March MMVII
 *
 */ 

#include "bat.h"
#include "textinput.h"

/*
 * Read input text box
 */
textInputDialog::textInputDialog(Console *console, int conn) 
{
   m_conn = conn;
   QDateTime dt;

   m_console = console;
   m_console->notify(m_conn, false);
   setupUi(this);
   setAttribute(Qt::WA_DeleteOnClose);
   m_console->read(m_conn);                 /* get title */
   labelWidget->setText(m_console->msg(m_conn));
   this->show();
}

void textInputDialog::accept()
{
   this->hide();
   m_console->write_dir(m_conn, lineEdit->text().toUtf8().data());
   /* Do not displayToPrompt because there may be another Text Input required */
   this->close();
   mainWin->resetFocus();
   m_console->notify(m_conn, true);
}


void textInputDialog::reject()
{
   this->hide();
   mainWin->set_status(tr(" Canceled"));
   m_console->write_dir(m_conn, ".");
   this->close();
   mainWin->resetFocus();
   m_console->beginNewCommand(m_conn);
   m_console->notify(m_conn, true);
}
