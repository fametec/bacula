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
 * Restore Wizard : Options page
 *
 * Written by Norbert Bizet, May MMXVII
 *
 */
#include "common.h"
#include "restoreoptionswizardpage.h"
#include "ui_restoreoptionswizardpage.h"
#include "common.h"
#include "filesmodel.h"
#include "task.h"

#define TABLENAME "b21234"
RestoreOptionsWizardPage::RestoreOptionsWizardPage(QWidget *parent) :
    QWizardPage(parent),
    ui(new Ui::RestoreOptionsWizardPage),
    res(0)

{
    ui->setupUi(this);

    registerField("restoreClient", ui->restoreClientComboxBox);
    registerField("restoreWhere", ui->whereLineEdit);
    registerField("restoreReplace", ui->replaceComboBox);
    registerField("restoreComment", ui->commentLineEdit);
}

RestoreOptionsWizardPage::~RestoreOptionsWizardPage()
{
    delete ui;
}

void RestoreOptionsWizardPage::initializePage()
{
    /* first request synchronous */
    if (res) {
        QStringList list;
        char *str;
        foreach_alist(str, res->clients) {
           list << QString(str);
        }

        ui->restoreClientComboxBox->clear();
        if (!list.isEmpty()) {
            ui->restoreClientComboxBox->addItems(list);
            ui->restoreClientComboxBox->setEnabled(true);
            ui->restoreClientComboxBox->setCurrentIndex(0);
        } else {
            ui->restoreClientComboxBox->setEnabled(false);
        }

        /* find RestoreFiles default */
        const char *p = "RestoreFiles";
        POOL_MEM info;
        pm_strcpy(info, p);
        res->mutex->lock();
        bfree_and_null(res->defaults.job);
        res->defaults.job = bstrdup(info.c_str());
        res->mutex->unlock();

        task t;
        t.init(res, -1);
        t.get_job_defaults();

        ui->whereLineEdit->setText(res->defaults.where);
        ui->replaceComboBox->setCurrentIndex(res->defaults.replace);
    }
}

bool RestoreOptionsWizardPage::validatePage()
{
    task *t = new task();
    connect(t, SIGNAL(done(task*)), wizard(), SLOT(deleteLater()));
    connect(t, SIGNAL(done(task*)), t, SLOT(deleteLater()));
    t->init(res, TASK_RESTORE);

    t->restore_field.tableName = QString(TABLENAME);
    t->restore_field.jobIds = field("jobIds").toString();
    t->restore_field.fileIds = field("fileIds").toString();
    t->restore_field.dirIds = field("dirIds").toString();
    t->restore_field.hardlinks = field("hardlinks").toString();
    int idx = field("currentClient").toInt();
    t->restore_field.client = QString((char*)res->clients->get(idx));
    t->restore_field.where = field("restoreWhere").toString();
    t->restore_field.replace = ui->replaceComboBox->currentText();
    t->restore_field.comment = field("restoreComment").toString();
    t->restore_field.pluginnames = field("pluginNames").toString();
    t->restore_field.pluginkeys = field("pluginKeysStr").toString();

    res->wrk->queue(t);
    return true;
}
