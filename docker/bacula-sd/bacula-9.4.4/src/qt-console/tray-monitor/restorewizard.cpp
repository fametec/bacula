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
 * Restore Wizard
 *
 * Written by Norbert Bizet, May MMXVII
 *
 */
#include "common.h"
#include "restorewizard.h"
#include "ui_restorewizard.h"
#include "filesmodel.h"
#include "task.h"
#include <QStandardItemModel>

RestoreWizard::RestoreWizard(RESMON *r, QWidget *parent) :
    QWizard(parent),
    res(r),
    ui(new Ui::RestoreWizard)
{
    ui->setupUi(this);
    ui->RestWizClientPage->setRes(res);
    ui->RestWizJobSelectPage->setRes(res);
    ui->RestWiFileSelectionPage->setRes(res);
    ui->RestWizPluginPage->setRes(res);
    ui->RestWizAdvancedOptionsPage->setRes(res);

    /* avoid connect warnings */
    qRegisterMetaType<Qt::Orientation>("Qt::Orientation");
    qRegisterMetaType< QList < QPersistentModelIndex > >("QList<QPersistentModelIndex>");
    qRegisterMetaType< QVector < int > >("QVector<int>");
#if QT_VERSION >= 0x050000
    qRegisterMetaType< QAbstractItemModel::LayoutChangeHint >("QAbstractItemModel::LayoutChangeHint");
#endif
}

RestoreWizard::~RestoreWizard()
{
    delete ui;
}
