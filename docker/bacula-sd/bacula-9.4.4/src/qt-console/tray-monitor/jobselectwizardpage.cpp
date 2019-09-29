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
#include "common.h"
#include "jobselectwizardpage.h"
#include "conf.h"
#include "task.h"
#include "ui_jobselectwizardpage.h"
#include <QStandardItemModel>

JobSelectWizardPage::JobSelectWizardPage(QWidget *parent) :
    QWizardPage(parent),
    ui(new Ui::JobSelectWizardPage),
    res(NULL),
    model(new QStandardItemModel),
    m_jobId(-1)
{
    ui->setupUi(this);
    /* currentJob in mandatory */
    registerField("currentJob*", this, "currentJob", SIGNAL(currentJobChanged()));
    /* assign model to widget */
    ui->BackupTableView->setModel(model);
    /* when selection change, change the field value and the next button state */
    connect(ui->BackupTableView->selectionModel(), SIGNAL(selectionChanged(const QItemSelection&, const QItemSelection&)), this, SIGNAL(currentJobChanged()));
    connect(ui->BackupTableView->selectionModel(), SIGNAL(selectionChanged(const QItemSelection&, const QItemSelection&)), this, SIGNAL(completeChanged()));
}

JobSelectWizardPage::~JobSelectWizardPage()
{
    delete model;
    delete ui;
}

void JobSelectWizardPage::initializePage()
{
    /* populate model */
    if (res && (!res->terminated_jobs || res->terminated_jobs->empty())) {
        /* get terminated_jobs info if not present. Queue populateModel() at end of task */
        task *t = new task();
        connect(t, SIGNAL(done(task*)), this , SLOT(populateModel()), Qt::QueuedConnection);
        connect(t, SIGNAL(done(task*)), t, SLOT(deleteLater()));
        t->init(res, TASK_STATUS);
        res->wrk->queue(t);
    } else {
        /* populate Model directly */
        populateModel();
    }
}

bool JobSelectWizardPage::isComplete() const
{
    /* any selection will do since it's single selection */
    return (ui->BackupTableView->selectionModel()
            && !ui->BackupTableView->selectionModel()->selectedRows().isEmpty()
            );
}

qlonglong JobSelectWizardPage::currentJob() const
{
    /* single selection */
    QModelIndex idx = ui->BackupTableView->selectionModel()->currentIndex();
    /* return the JobId in column 0 */
    QModelIndex idIdx = idx.sibling(idx.row(), 0);
    return idIdx.data().toLongLong();
}

void JobSelectWizardPage::populateModel()
{
    if (res) {
        /* populate model with jobs listed in currentClient */
        task *t = new task();
        connect(t, SIGNAL(done(task*)), t, SLOT(deleteLater()));
        t->init(res, TASK_LIST_CLIENT_JOBS);
        int idx = field("currentClient").toInt();
        char *p = (char*) res->clients->get(idx);
        POOL_MEM info;
        pm_strcpy(info, p);
        t->arg = info.c_str();
        t->model = model;
        res->wrk->queue(t);
    }
}
