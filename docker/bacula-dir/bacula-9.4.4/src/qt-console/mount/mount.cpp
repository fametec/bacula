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
 *  Label Dialog class
 *
 *   Kern Sibbald, February MMVII
 *
 */ 

#include "bat.h"
#include "mount/mount.h"
#include <QMessageBox>

/*
 * A constructor 
 */
mountDialog::mountDialog(Console *console, QString &storageName) : QDialog()
{
   m_console = console;
   m_storageName = storageName;
   m_conn = m_console->notifyOff();
   setupUi(this);
   this->show();

   QString labelText( tr("Storage : %1").arg(storageName) );
   storageLabel->setText(labelText);
}

void mountDialog::accept()
{
   QString scmd;
   if (m_storageName == "") {
      QMessageBox::warning(this, tr("No Storage name"), tr("No Storage name given"),
                           QMessageBox::Ok, QMessageBox::Ok);
      return;
   }
   this->hide();
   scmd = QString("mount storage=\"%1\" slot=%2")
                  .arg(m_storageName)
                  .arg(slotSpin->value());
   if (mainWin->m_commandDebug) {
      Pmsg1(000, "sending command : %s\n",scmd.toUtf8().data());
   }

   m_console->display_text( tr("Context sensitive command :\n\n"));
   m_console->display_text("****    ");
   m_console->display_text(scmd + "    ****\n");
   m_console->display_text(tr("Director Response :\n\n"));

   m_console->write_dir(scmd.toUtf8().data());
   m_console->displayToPrompt(m_conn);
   m_console->notify(m_conn, true);
   delete this;
   mainWin->resetFocus();
}

void mountDialog::reject()
{
   this->hide();
   m_console->notify(m_conn, true);
   delete this;
   mainWin->resetFocus();
}
