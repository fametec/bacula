#ifndef _HELP_H_
#define _HELP_H_

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
 *    It reads an html file and displays it in a "browser" window.
 *
 *   Kern Sibbald, May MMVII
 *
 *  $Id$
 */ 

#include "bat.h"
#include "ui_help.h"

class Help : public QWidget, public Ui::helpForm
{
   Q_OBJECT 

public:
   Help(const QString &path, const QString &file, QWidget *parent = NULL);
   virtual ~Help() { };
   static void displayFile(const QString &file);

public slots:
   void updateTitle();

private:
};

#endif /* _HELP_H_ */
