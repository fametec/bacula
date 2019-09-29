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
#include "common.h"
#include "fileselectwizardpage.h"
#include "filesmodel.h"
#include "ui_fileselectwizardpage.h"
#include "restorewizard.h"
#include "task.h"
#include <QStandardItemModel>
#include <QShortcut>

FileSelectWizardPage::FileSelectWizardPage(QWidget *parent) :
    QWizardPage(parent),
    m_currentSourceId(0),
    m_currentPathStr(""),
    ui(new Ui::FileSelectWizardPage),
    src_files_model(new FileSourceModel),
    dest_files_model(new FileDestModel),
    m_filterTimer(new QTimer),
    res(NULL),
    need_optimize(true)
{
    ui->setupUi(this);

    /* keep track of the current source view selection */
    registerField("currentSourceId", this, "currentSourceId", "currentSourceIdChanged");
    registerField("currentPathStr", this, "currentPathStr", "currentPathStrChanged");
    registerField("fileFilter", ui->FilterEdit);
    /* usefull to the following pages */
    registerField("jobIds", this, "jobIds", "jobIdsChanged");
    registerField("fileIds", this, "fileIds", "fileIdsChanged");
    registerField("dirIds", this, "dirIds", "dirIdsChanged");
    registerField("hardlinks", this, "hardlinks", "hardlinksChanged");
    registerField("pluginIds", this, "pluginIds", "pluginIdsChanged");
    registerField("pluginNames", this, "pluginNames", "pluginNamesChanged");

    QStringList headers;
    headers << tr("Name") << tr("Size") << tr("Date");

    ui->sourceTableView->setModel(src_files_model);
    //ui->sourceTableView->setLayoutMode(QListView::Batched);
    //ui->sourceTableView->setBatchSize(BATCH_SIZE);
    src_files_model->setHorizontalHeaderLabels(headers);
    connect(ui->sourceTableView, SIGNAL(activated(const QModelIndex &)), this, SLOT(changeCurrentFolder(const QModelIndex&)));
    connect(ui->sourceTableView, SIGNAL(activated(const QModelIndex &)), this, SLOT(updateSourceModel()));

    ui->destTableView->setModel(dest_files_model);
    dest_files_model->setHorizontalHeaderLabels(headers);
    connect(dest_files_model, SIGNAL(rowsInserted(const QModelIndex &, int, int)), this, SIGNAL(completeChanged()));
    connect(dest_files_model, SIGNAL(rowsRemoved(const QModelIndex &, int, int)), this, SIGNAL(completeChanged()));

    connect(ui->filePathComboPath, SIGNAL(activated(const QString &)), this, SLOT(changeCurrentText(const QString &)));
    connect(ui->filePathComboPath, SIGNAL(activated(const QString &)), this, SLOT(updateSourceModel()));

    connect(ui->FilterEdit, SIGNAL(textChanged(const QString &)), this, SLOT(delayedFilter()));

    m_filterTimer->setSingleShot(true);
    connect(m_filterTimer, SIGNAL(timeout()), this, SLOT(unFreezeSrcView()));
    connect(m_filterTimer, SIGNAL(timeout()), this, SLOT(updateSourceModel()));

    QShortcut *shortcut = new QShortcut(QKeySequence(Qt::Key_Delete), this);
    QObject::connect(shortcut, SIGNAL(activated()), this, SLOT(deleteDestSelection()));
}

FileSelectWizardPage::~FileSelectWizardPage()
{
    delete ui;
}

void FileSelectWizardPage::initializePage()
{
    /* first request synchronous */
    if (res) {
        task t;
        t.init(res, -1);

        const char *p = field("fileFilter").toString().toLatin1().data();
        POOL_MEM info2;
        pm_strcpy(info2, p);
        t.arg2 = info2.c_str();

        p = currentPathStr().toLatin1().data();
        POOL_MEM info3;
        pm_strcpy(info3, p);
        t.arg3 = info3.c_str();

        t.model = src_files_model;

        t.get_job_files(field("currentJob").toString().toLatin1().data(), currentSourceId());
    }

    need_optimize = true;
}

bool FileSelectWizardPage::isComplete() const
{
    return dest_files_model->rowCount() != 0;
}

int FileSelectWizardPage::nextId() const
{
    /* if some plugins are used in current selection, move to plugin page, otherwise options */
    if (field("pluginIds").toString().isEmpty()) {
        return RestoreWizard::RW_ADVANCEDOPTIONS_PAGE;
    }
    return RestoreWizard::RW_PLUGIN_PAGE;
}

bool FileSelectWizardPage::validatePage()
{
    /* compute result fields based on destination model content */
    QStringList fileids, jobids, dirids, findexes;
    struct stat statp;
    int32_t LinkFI;

    for (int row=0; row < dest_files_model->rowCount(); ++row) {
        QModelIndex idx = dest_files_model->index(row, 0);
        if (idx.data(TypeRole) == TYPEROLE_FILE) {
            fileids << idx.data(FileIdRole).toString();
            jobids << idx.data(JobIdRole).toString();
            decode_stat(idx.data(LStatRole).toString().toLocal8Bit().data(),
                        &statp, sizeof(statp), &LinkFI);
            if (LinkFI) {
                findexes << idx.data(JobIdRole).toString() + "," + QString().setNum(LinkFI);
            }
        } else /* TYPEROLE_DIRECTORY */
        {
            dirids << idx.data(PathIdRole).toString();
            jobids << idx.data(JobIdRole).toString().split(","); /* Can have multiple jobids */
        }
    }

    fileids.removeDuplicates();
    jobids.removeDuplicates();
    dirids.removeDuplicates();
    findexes.removeDuplicates();

    m_jobIds = jobids.join(",");
    m_fileIds = fileids.join(",");
    m_dirIds = dirids.join(",");
    m_hardlinks = findexes.join(",");

    /* plugin Ids and Names are retrieved now, so NextId() can decide to schedule the PluginPage before RestoreOptionsPage */
    /* Stored as properties so thru the next Wizard Page can use them */
    task t;
    t.init(res, -1); /* pass res, ID is set to -1 because task method is called synchronously */
    m_pluginIds = t.plugins_ids(m_jobIds);
    m_pluginNames = t.plugins_names(m_jobIds);

    return true;
}

void FileSelectWizardPage::updateSourceModel()
{
    /* subsequent request async */
    if (res) {
        task *t = new task();

        /* optimize size only once */
        if (need_optimize) {
            connect(t, SIGNAL(done(task*)), this, SLOT(optimizeSize()));
            need_optimize = false;
        }
        connect(t, SIGNAL(done(task*)), t, SLOT(deleteLater()));

        t->init(res, TASK_LIST_JOB_FILES);

        const char *p = field("currentJob").toString().toLatin1().data();
        POOL_MEM info;
        pm_strcpy(info, p);
        t->arg = info.c_str();

        p = field("fileFilter").toString().toLatin1().data();
        POOL_MEM info2;
        pm_strcpy(info2, p);
        t->arg2 = info2.c_str();

        p = currentPathStr().toLatin1().data();
        POOL_MEM info3;
        pm_strcpy(info3, p);
        t->arg3 = info3.c_str();

        t->pathId = currentSourceId();
        t->model = src_files_model;

        res->wrk->queue(t);
    }
}

void FileSelectWizardPage::optimizeSize()
{
    int w = ui->destTableView->width()/4;
    ui->destTableView->horizontalHeader()->resizeSection( 0, w*2);
    ui->destTableView->horizontalHeader()->resizeSection( 1, w);
    ui->destTableView->horizontalHeader()->resizeSection( 2, w );
    ui->destTableView->horizontalHeader()->setStretchLastSection(true);

    w = ui->sourceTableView->width()/4;
    ui->sourceTableView->horizontalHeader()->resizeSection( 0, w*2);
    ui->sourceTableView->horizontalHeader()->resizeSection( 1, w);
    ui->sourceTableView->horizontalHeader()->resizeSection( 2, w );
    ui->sourceTableView->horizontalHeader()->setStretchLastSection(true);
}

void FileSelectWizardPage::changeCurrentFolder(const QModelIndex& current)
{
    if (current.isValid()) {
        QStandardItem *item = src_files_model->itemFromIndex(current);
        if (item && item->data(TypeRole) == TYPEROLE_DIRECTORY) {
            QString path = item->text();
            m_currentSourceId = item->data(PathIdRole).toULongLong();
            if (m_currentSourceId  > 0) {
                // Choose .. update current path to parent dir
                if (path == "..") {
                    if (m_currentPathStr == "/") {
                        m_currentPathStr = "";
                    } else {
                        m_currentPathStr.remove(QRegExp("[^/]+/$"));
                    }

                } else if (path == "/" && m_currentPathStr == "") {
                    m_currentPathStr += path;

                } else if (path != "/") {
                    m_currentPathStr += path;
                }
            }

            emit currentSourceIdChanged();
            emit currentPathStrChanged();

            int idx = ui->filePathComboPath->findText(m_currentPathStr);
            if (idx >= 0) {
                ui->filePathComboPath->setCurrentIndex(idx);
            } else {
                ui->filePathComboPath->insertItem(-1, m_currentPathStr, QVariant(m_currentSourceId));
                ui->filePathComboPath->model()->sort(0);
                ui->filePathComboPath->setCurrentIndex(ui->filePathComboPath->findText(m_currentPathStr));
            }
        }
    }
}

void FileSelectWizardPage::changeCurrentText(const QString& current)
{
    int idx = ui->filePathComboPath->findText(current);
    m_currentSourceId = ui->filePathComboPath->itemData(idx).toULongLong();
    m_currentPathStr = ui->filePathComboPath->itemText(idx);
    emit currentSourceIdChanged();
}

void FileSelectWizardPage::deleteDestSelection()
{
    QMap<int, int> rows;
    foreach (QModelIndex index, ui->destTableView->selectionModel()->selectedIndexes())
        rows.insert(index.row(), 0);
    QMapIterator<int, int> r(rows);
    r.toBack();
    while (r.hasPrevious()) {
        r.previous();
        ui->destTableView->model()->removeRow(r.key());
    }
}

void FileSelectWizardPage::delayedFilter()
{
    freezeSrcView();
    m_filterTimer->start( 100 );
}

void FileSelectWizardPage::freezeSrcView()
{
    ui->sourceTableView->setUpdatesEnabled(false);
}

void FileSelectWizardPage::unFreezeSrcView()
{
    ui->sourceTableView->setUpdatesEnabled(true);
    ui->sourceTableView->update();
}
