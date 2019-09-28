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
 * Restore Wizard: Plugin selection page
 *
 * Written by Norbert Bizet, May MMXVII
 *
 */
#include "common.h"
#include "pluginwizardpage.h"
#include "ui_pluginwizardpage.h"
#include "pluginmodel.h"
#include "task.h"
#include <QStandardItemModel>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QDateTimeEdit>
#include "lib/ini.h"

PluginWizardPage::PluginWizardPage(QWidget *parent) :
    QWizardPage(parent),
    ui(new Ui::PluginWizardPage),
    res(NULL)
{
    ui->setupUi(this);
    ui->tabWidget->clear();
    registerField("pluginKeysStr", this, "pluginKeysStr", "pluginKeysStrChanged");
}

PluginWizardPage::~PluginWizardPage()
{
    delete ui;
}

void PluginWizardPage::initializePage()
{
    /* build plugin form UI dynamically*/
    task t;
    t.init(res, -1);
    QStringList idsList = field("pluginIds").toString().split(",");
    QStringList nameList = field("pluginNames").toString().split(",");
    /* process ids and name lists with the assumption that indexes match */
    ASSERT(idsList.count() == nameList.count());
    for( int c=0; c<idsList.count(); ++c ) {
        QString pluginId = idsList[c];
        QString pluginName = nameList[c];

        /* don't tab the same plugin twice */
        bool exists(false);
        for (int j=0; j<ui->tabWidget->count(); ++j) {
            if (ui->tabWidget->tabText(j) == pluginName) {
                exists=true;
                break;
            }
        }
        if (exists) continue;
        /* create a tab widget */
        QWidget *pluginWidget = new QWidget();
        /* insert a tab widget with an empty form layout */
        QFormLayout *layout = new QFormLayout();
        pluginWidget->setLayout(layout);
        ui->tabWidget->addTab(pluginWidget, pluginName);

        /* parse each plugin fields*/
        t.plugin(nameList[c], field("jobIds").toString(), pluginId.toInt());
        ConfigFile cf;
        cf.unserialize(pluginName.toLatin1().data());

        /* for each field */
        for (int i=0; i < MAX_INI_ITEMS && cf.items[i].name; i++) {
            ini_items f = cf.items[i];
            /* create the w Widget dynamically, based on the field value type */
            QWidget *w(NULL);
            if (f.handler == ini_store_str ||
                    f.handler == ini_store_name ||
                    f.handler == ini_store_alist_str) /*FIXME: treat alist separatly*/
            {
                QLineEdit *l = new QLineEdit();
                w=l;
                if (f.default_value)
                    l->setText(f.default_value);
            }
            else if (f.handler == ini_store_pint64 ||
                     f.handler == ini_store_int64 ||
                     f.handler == ini_store_pint32 ||
                     f.handler == ini_store_int32)
            {
                QLineEdit *l = new QLineEdit();
                w=l;
                l->setValidator(new QIntValidator());
                if (f.default_value)
                    l->setText(f.default_value);
            }
            else if (f.handler == ini_store_bool)
            {
                QCheckBox *c = new QCheckBox();
                w=c;
                if (f.default_value)
                    c->setChecked(f.default_value);
            }
            else if (f.handler == ini_store_date)
            {
                QDateTimeEdit *d = new QDateTimeEdit();
                w=d;
                if (f.default_value)
                    d->setDateTime(QDateTime::fromString(f.default_value, "yyyy-MM-dd hh:mm:ss"));
            }

            if (w) {
                w->setToolTip(f.comment);
                QString field_name = QString("%1_%2").arg(nameList[c]).arg(f.name);
                /* This doesn't work FIXME
                 * if (f.required) {
                    field_name.append("*");
                }*/

                registerField(field_name, w);
                /* there's no way to iterate thru page-register field */
                /* As a workaround we keep track of registered fields in a separate list */
                registeredFields.append(field_name);

                layout->addRow(f.name, w);
                emit completeChanged();
            }
        }
    }
}

bool PluginWizardPage::validatePage()
{
    QStringList pluginKeys;
    QStringList idsList = field("pluginIds").toString().split(",");
    QStringList nameList = field("pluginNames").toString().split(",");
    for (int idx=0; idx<ui->tabWidget->count(); ++idx) {
        QString name = ui->tabWidget->tabText(idx);
        ASSERT(name.compare(nameList[idx]) == 0);

        QFile file(name);
        if (file.open(QIODevice::WriteOnly)) {
            QTextStream outputStream(&file);
            foreach(QString fld, registeredFields) {
                QStringList sl = fld.split("_");
                if ( (name.compare(sl[0]) == 0) && field(fld).isValid()) {
                    QString s = QString("%1=%2\n").arg(sl[1]).arg(field(fld).toString());
                    outputStream << s;
                }
            }
        }

        /* create the key */
        QString key = QString("j%1i%2").arg(field("jobIds").toString().remove(',').simplified()).arg(idsList[idx].simplified());
        QString restoreKey = QString("%1:%2").arg(idsList[idx].simplified()).arg(key);
        pluginKeys.append(restoreKey);
    }

    m_pluginKeysStr = pluginKeys.join(",");
    emit pluginKeysStrChanged();

    return true;
}
