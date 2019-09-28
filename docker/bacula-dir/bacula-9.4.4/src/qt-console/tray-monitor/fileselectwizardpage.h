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
 * Restore Wizard: File selection page
 *
 * Written by Norbert Bizet, May MMXVII
 *
 */
#ifndef FILESELECTWIZARDPAGE_H
#define FILESELECTWIZARDPAGE_H

#include <QWizardPage>

class QStandardItemModel;
class QModelIndex;
class RESMON;
class task;

namespace Ui {
class FileSelectWizardPage;
}

class FileSelectWizardPage : public QWizardPage
{
    Q_OBJECT

    Q_PROPERTY(qulonglong currentSourceId READ currentSourceId NOTIFY currentSourceIdChanged)
    Q_PROPERTY(QString currentPathStr READ currentPathStr NOTIFY currentPathStrChanged)
    Q_PROPERTY(QString jobIds READ jobIds NOTIFY jobIdsChanged)
    Q_PROPERTY(QString fileIds READ fileIds NOTIFY fileIdsChanged)
    Q_PROPERTY(QString dirIds READ dirIds NOTIFY dirIdsChanged)
    Q_PROPERTY(QString hardlinks READ hardlinks NOTIFY hardlinksChanged)
    Q_PROPERTY(QString pluginIds READ pluginIds NOTIFY pluginIdsChanged)
    Q_PROPERTY(QString pluginNames READ pluginNames NOTIFY pluginNamesChanged)

private:    qulonglong          m_currentSourceId;
public:     qulonglong          currentSourceId() const { return m_currentSourceId; }
signals:    void                currentSourceIdChanged();
private:    QString             m_currentPathStr;
public:     QString             currentPathStr() const { return m_currentPathStr; }
signals:    void                currentPathStrChanged();
private:    QString             m_jobIds;
public:     QString             jobIds() const { return m_jobIds; }
signals:    void                jobIdsChanged();
private:    QString             m_fileIds;
public:     QString             fileIds() const { return m_fileIds; }
signals:    void                fileIdsChanged();
private:    QString             m_dirIds;
public:     QString             dirIds() const { return m_dirIds; }
signals:    void                dirIdsChanged();
private:    QString             m_hardlinks;
public:     QString             hardlinks() const { return m_hardlinks; }
signals:    void                hardlinksChanged();
private:    QString             m_pluginIds;
public:     QString             pluginIds() const { return m_pluginIds; }
signals:    void                pluginIdsChanged();
private:    QString             m_pluginNames;
public:     QString             pluginNames() const { return m_pluginNames; }
signals:    void                pluginNamesChanged();

public:
    explicit FileSelectWizardPage(QWidget *parent = 0);
    ~FileSelectWizardPage();
    /* QWizardPage interface */
    void initializePage();
    bool isComplete() const;
    int nextId() const;
    bool validatePage();
    /* local interface */
    void setRes(RESMON *r) {res=r;}

protected slots:
    void updateSourceModel();

    void optimizeSize();

    void changeCurrentFolder(const QModelIndex& current);
    void changeCurrentText(const QString &current);

    void deleteDestSelection();

    void delayedFilter();

    void freezeSrcView();
    void unFreezeSrcView();

private:
    Ui::FileSelectWizardPage    *ui;
    QStandardItemModel          *src_files_model;
    QStandardItemModel          *dest_files_model;
    QTimer                      *m_filterTimer;
    RESMON                      *res;
    bool                        need_optimize;
};

#endif // FILESELECTWIZARDPAGE_H
