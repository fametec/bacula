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
 *  Help Window class
 *
 *   Kern Sibbald, May MMVII
 *
 */ 

#include "bat.h"
#include "help.h"

/*
 * Note: HELPDIR is defined in src/host.h
 */

Help::Help(const QString &path, const QString &file, QWidget *parent) :
        QWidget(parent)
{
   setAttribute(Qt::WA_DeleteOnClose);     /* Make sure we go away */
   setAttribute(Qt::WA_GroupLeader);       /* allow calling from modal dialog */

   setupUi(this);                          /* create window */

   textBrowser->setSearchPaths(QStringList() << HELPDIR << path << ":/images");
   textBrowser->setSource(file);
   //textBrowser->setCurrentFont(mainWin->m_consoleHash.values()[0]->get_font());

   connect(textBrowser, SIGNAL(sourceChanged(const QUrl &)), this, SLOT(updateTitle()));
   connect(closeButton, SIGNAL(clicked()), this, SLOT(close()));
   connect(homeButton, SIGNAL(clicked()), textBrowser, SLOT(home()));
   connect(backButton, SIGNAL(clicked()), textBrowser, SLOT(backward()));
   this->show();
}

void Help::updateTitle()
{
   setWindowTitle(tr("Help: %1").arg(textBrowser->documentTitle()));
}

void Help::displayFile(const QString &file)
{
   QRegExp rx;
   rx.setPattern("/\\.libs");
   QString path = QApplication::applicationDirPath();
   int pos = rx.indexIn(path);
   if (pos)
      path = path.remove(pos, 6);
   path += "/help";
   new Help(path, file);
}
