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
 * Restore Wizard: Job selection page
 *
 * Written by Norbert Bizet, May MMXVII
 *
 */
#ifndef JOBSELECTWIZARDPAGE_H
#define JOBSELECTWIZARDPAGE_H

#include "common.h"
#include <QWizardPage>

class QStandardItemModel;
class QItemSelection;
class RESMON;

namespace Ui {
class JobSelectWizardPage;
}

class JobSelectWizardPage : public QWizardPage
{
    Q_OBJECT
    Q_PROPERTY(qlonglong currentJob READ currentJob NOTIFY currentJobChanged)

public:
    explicit JobSelectWizardPage(QWidget *parent = 0);
    ~JobSelectWizardPage();
    /* QWizardPage interface */
    void initializePage();
    bool isComplete() const;
    /* local interface */
    inline void setRes(RESMON *r) {res=r;}
    /* currentJob READ */
    qlonglong currentJob() const;

signals:
    /* currentJob NOTIFY */
    void currentJobChanged();

protected slots:
    void populateModel();

private:
    Ui::JobSelectWizardPage     *ui;
    RESMON                      *res;
    QStandardItemModel          *model;
    qlonglong                   m_jobId;
};

#endif // JOBSELECTWIZARDPAGE_H
