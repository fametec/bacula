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
 * Restore Wizard: Client selection page
 *
 * Written by Norbert Bizet, May MMXVII
 *
 */
#ifndef CLIENTSELECTWIZARDPAGE_H
#define CLIENTSELECTWIZARDPAGE_H

#include <QWizardPage>

class RESMON;

namespace Ui {
class ClientSelectWizardPage;
}

class ClientSelectWizardPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit ClientSelectWizardPage(QWidget *parent = 0);
    ~ClientSelectWizardPage();
    /* QWizardPage interface */
    void initializePage();    
    /* local interface */
    inline void setRes(RESMON *r) {res=r;}

private:
    Ui::ClientSelectWizardPage *ui;
    RESMON *res;
};

#endif // CLIENTSELECTWIZARDPAGE_H
