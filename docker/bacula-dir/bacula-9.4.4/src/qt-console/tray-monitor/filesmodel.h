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
 * definition of models related to file selection wizard page.
 *
 * Written by Norbert Bizet, May MMXVII
 *
 */
#ifndef FILESMODEL_H
#define FILESMODEL_H
#include <QStandardItemModel>
#include <QFileIconProvider>
#include <QMimeData>
#include "task.h"
#include <QTableWidgetItem>
#include "../util/fmtwidgetitem.h"

enum {
    PathIdRole = Qt::UserRole+1,
    FilenameIdRole = Qt::UserRole+2,
    FileIdRole = Qt::UserRole+3,
    JobIdRole = Qt::UserRole+4,
    LStatRole = Qt::UserRole+5,
    PathRole = Qt::UserRole+6,
    TypeRole = Qt::UserRole+7,
    FullPathRole = Qt::UserRole+8
};

enum {
    TYPEROLE_DIRECTORY = 0,
    TYPEROLE_FILE
};

class DirectoryItem : public QStandardItem
{
public:
    DirectoryItem() : QStandardItem()
    {
        /* explicit set the data, so it can be serialized in Mime Data for D&D */
        setData(QVariant(TYPEROLE_DIRECTORY), TypeRole);
    }
    QVariant data(int role = Qt::UserRole + 1) const
    {
        if (role == Qt::DecorationRole) {
            QFileIconProvider provider;
            return provider.icon(QFileIconProvider::Folder);
        }

        return QStandardItem::data(role);
    }
};

class FileItem : public QStandardItem
{
public:
    FileItem() : QStandardItem()
    {
        /* explicit set the data, so it can be serialized in Mime Data for D&D */
        setData(QVariant(TYPEROLE_FILE), TypeRole);
    }
    enum {
        FILE_TYPE = UserType +2
    };

    QVariant data(int role = Qt::UserRole + 1) const
    {
        if (role == Qt::DecorationRole) {
            QFileIconProvider provider;
            return provider.icon(QFileIconProvider::File);
        }

        return QStandardItem::data(role);
    }
};

#define BATCH_SIZE 100
class FileSourceModel : public QStandardItemModel
{
public:
    FileSourceModel() : QStandardItemModel(),
        m_cursor(0),
        m_batchSize(BATCH_SIZE),
        m_canFetchMore(true)
    {}

    bool canFetchMore(const QModelIndex &parent) const
    {
        Q_UNUSED(parent)
        return false/*m_canFetchMore*/;
    }
    void fetchMore(const QModelIndex &parent)
    {
        Q_UNUSED(parent)
    }

public slots:
    void taskComplete(task *t)
    {
        t->deleteLater();
    }

private:
    u_int64_t       m_cursor;
    u_int64_t       m_batchSize;
    bool            m_canFetchMore;
};

extern int decode_stat(char *buf, struct stat *statp, int stat_size, int32_t *LinkFI);

class FileDestModel : public QStandardItemModel
{
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const
    {
        Q_UNUSED(action)
        Q_UNUSED(row)
        Q_UNUSED(column)
        Q_UNUSED(parent)

        if (data->hasFormat("application/x-qstandarditemmodeldatalist")) {
            QByteArray encoded = data->data("application/x-qabstractitemmodeldatalist");
            QDataStream stream(&encoded, QIODevice::ReadOnly);

            while (!stream.atEnd())
            {
                int row, col;
                QMap<int,  QVariant> roleDataMap;
                stream >> row >> col >> roleDataMap;

                /* do something with the data */
                int type = roleDataMap[TypeRole].toInt();

                switch(type) {
                case TYPEROLE_DIRECTORY:
                case TYPEROLE_FILE:
                    break;
                default:
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    bool dropMimeData(const QMimeData * data, Qt::DropAction action, int row, int column, const QModelIndex & parent)
    {
        Q_UNUSED(action)
        Q_UNUSED(row)
        Q_UNUSED(column)
        Q_UNUSED(parent)

        QByteArray encoded = data->data("application/x-qabstractitemmodeldatalist");
        QDataStream stream(&encoded, QIODevice::ReadOnly);

        while (!stream.atEnd())
        {
            int row, col;
            QMap<int,  QVariant> roleDataMap;
            stream >> row >> col >> roleDataMap;

            if (col == 0) {
                QStandardItem *item;
                /* do something with the data */
                int type = roleDataMap[TypeRole].toInt();

                switch(type) {
                case TYPEROLE_DIRECTORY:
                    item = new DirectoryItem();
                    break;
                case TYPEROLE_FILE:
                    item = new FileItem();
                    break;
                default:
                    return false;
                }

                item->setData(roleDataMap[PathIdRole], PathIdRole);
                item->setData(roleDataMap[FilenameIdRole], FilenameIdRole);
                item->setData(roleDataMap[FileIdRole], FileIdRole);
                item->setData(roleDataMap[JobIdRole], JobIdRole);
                item->setData(roleDataMap[LStatRole], LStatRole);
                item->setData(roleDataMap[PathRole], PathRole);
                item->setData(roleDataMap[Qt::DisplayRole], Qt::DisplayRole);
                item->setData(roleDataMap[Qt::ToolTipRole], Qt::ToolTipRole);
                if (type == TYPEROLE_FILE) {
                    QList<QStandardItem*> colums;
                    struct stat statp;
                    int32_t LinkFI;
                    decode_stat(roleDataMap[LStatRole].toString().toLocal8Bit().data(),
                                &statp, sizeof(statp), &LinkFI);
                    char buf[200];
                    bstrutime(buf, sizeof(buf), statp.st_mtime);
                    colums << item << new QStandardItem(convertBytesSI(statp.st_size)) << new QStandardItem(buf);
                    appendRow(colums);
                } else {
                    appendRow(item);
                }
            }
        }
        return true;
    }
};

#endif // FILESMODEL_H
