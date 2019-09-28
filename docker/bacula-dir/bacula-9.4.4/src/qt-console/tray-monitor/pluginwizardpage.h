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
#ifndef PLUGINWIZARDPAGE_H
#define PLUGINWIZARDPAGE_H

#include <QWizardPage>

namespace Ui {
class PluginWizardPage;
}

class RESMON;

class PluginWizardPage : public QWizardPage
{
    Q_OBJECT
    Q_PROPERTY(QString pluginKeysStr READ pluginKeysStr NOTIFY pluginKeysStrChanged)

private: QString    m_pluginKeysStr;
public:  QString    pluginKeysStr() const { return m_pluginKeysStr; }
signals: void       pluginKeysStrChanged();

public:
    explicit PluginWizardPage(QWidget *parent = 0);
    ~PluginWizardPage();
    /* QWizardPage interface */
    void initializePage();
    bool validatePage();
    /* local interface */
    inline void setRes(RESMON *r) {res=r;}
private:
    Ui::PluginWizardPage *ui;
    RESMON *res;
    QStringList         registeredFields;

};

#endif // PLUGINWIZARDPAGE_H
