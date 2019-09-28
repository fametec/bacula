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
 *  Run Dialog class
 *
 *   Kern Sibbald, February MMVII
 *
 *  $Id$
 */ 

#include "bat.h"
#include "run.h"

/*
 * Setup all the combo boxes and display the dialog
 */
estimatePage::estimatePage() : Pages()
{
   QDateTime dt;

   m_name = tr("Estimate");
   pgInitialize();
   setupUi(this);
   m_conn = m_console->notifyOff();

   m_console->beginNewCommand(m_conn);
   jobCombo->addItems(m_console->job_list);
   filesetCombo->addItems(m_console->fileset_list);
   levelCombo->addItems(m_console->level_list);
   clientCombo->addItems(m_console->client_list);
   job_name_change(0);
   connect(jobCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(job_name_change(int)));
   connect(okButton, SIGNAL(pressed()), this, SLOT(okButtonPushed()));
   connect(cancelButton, SIGNAL(pressed()), this, SLOT(cancelButtonPushed()));
   QTreeWidgetItem* thisitem = mainWin->getFromHash(this);
   thisitem->setIcon(0,QIcon(QString::fromUtf8(":images/estimate-job.png")));

   dockPage();
   setCurrent();
   this->show();
   m_aButtonPushed = false;
}

void estimatePage::okButtonPushed()
{
   if (m_aButtonPushed) return;
   m_aButtonPushed = true;
   this->hide();
   QString cmd;
   QTextStream(&cmd) << "estimate" << 
      " job=\"" << jobCombo->currentText() << "\"" <<
      " fileset=\"" << filesetCombo->currentText() << "\"" <<
      " level=\"" << levelCombo->currentText() << "\"" <<
      " client=\"" << clientCombo->currentText() << "\"";
   if (listingCheckBox->checkState() == Qt::Checked) {
      cmd += " listing";
   }

   if (mainWin->m_commandDebug) {
      Pmsg1(000, "command : %s\n", cmd.toUtf8().data());
   }

   consoleCommand(cmd, m_conn, true, true);
   m_console->notify(m_conn, true);
   closeStackPage();
   mainWin->resetFocus();
}


void estimatePage::cancelButtonPushed()
{
   if (m_aButtonPushed) return;
   m_aButtonPushed = true;
   mainWin->set_status(" Canceled");
   this->hide();
   m_console->notify(m_conn, true);
   closeStackPage();
   mainWin->resetFocus();
}

/*
 * Called here when the jobname combo box is changed.
 *  We load the default values for the new job in the
 *  other combo boxes.
 */
void estimatePage::job_name_change(int index)
{
   job_defaults job_defs;

   (void)index;
   job_defs.job_name = jobCombo->currentText();
   if (m_console->get_job_defaults(m_conn, job_defs)) {
      filesetCombo->setCurrentIndex(filesetCombo->findText(job_defs.fileset_name, Qt::MatchExactly));
      levelCombo->setCurrentIndex(levelCombo->findText(job_defs.level, Qt::MatchExactly));
      clientCombo->setCurrentIndex(clientCombo->findText(job_defs.client_name, Qt::MatchExactly));
   }
}
