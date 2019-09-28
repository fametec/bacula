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
#include "select.h"

/*
 * Read the items for the selection
 */
selectDialog::selectDialog(Console *console, int conn) : QDialog()
{
   m_conn = conn;
   QDateTime dt;
   int stat;
   QListWidgetItem *item;
   int row = 0;

   m_console = console;
   m_console->notify(m_conn, false);
   setupUi(this);
   connect(listBox, SIGNAL(currentRowChanged(int)), this, SLOT(index_change(int)));
   setAttribute(Qt::WA_DeleteOnClose);
   m_console->read(m_conn);                 /* get title */
   labelWidget->setText(m_console->msg(m_conn));
   while ((stat=m_console->read(m_conn)) > 0) {
      item = new QListWidgetItem;
      item->setText(m_console->msg(m_conn));
      listBox->insertItem(row++, item);
   }
   m_console->displayToPrompt(m_conn);
   this->show();
}

void selectDialog::accept()
{
   char cmd[100];

   this->hide();
   bsnprintf(cmd, sizeof(cmd), "%d", m_index+1);
   m_console->write_dir(m_conn, cmd);
   m_console->displayToPrompt(m_conn);
   this->close();
   mainWin->resetFocus();
   m_console->displayToPrompt(m_conn);
   m_console->notify(m_conn, true);
}


void selectDialog::reject()
{
   this->hide();
   mainWin->set_status(tr(" Canceled"));
   this->close();
   mainWin->resetFocus();
   m_console->beginNewCommand(m_conn);
   m_console->notify(m_conn, true);
}

/*
 * Called here when the jobname combo box is changed.
 *  We load the default values for the new job in the
 *  other combo boxes.
 */
void selectDialog::index_change(int index)
{
   m_index = index;
}

/*
 * Handle yesno PopUp when Bacula asks a yes/no question.
 */
/*
 * Read the items for the selection
 */
yesnoPopUp::yesnoPopUp(Console *console, int conn)  : QDialog()
{
   QMessageBox msgBox;

   setAttribute(Qt::WA_DeleteOnClose);
   console->read(conn);                 /* get yesno question */
   msgBox.setWindowTitle(tr("Bat Question"));
   msgBox.setText(console->msg(conn));
   msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
   console->displayToPrompt(conn);
   switch (msgBox.exec()) {
   case QMessageBox::Yes:
      console->write_dir(conn, "yes");
      break;
   case QMessageBox::No:
      console->write_dir(conn, "no");
      break;
   }
   console->displayToPrompt(conn);
   mainWin->resetFocus();
}
