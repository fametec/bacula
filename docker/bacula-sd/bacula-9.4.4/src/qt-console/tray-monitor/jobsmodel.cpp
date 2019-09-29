/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2017 Kern Sibbald

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
#include "jobsmodel.h"

JobsModel::JobsModel(const QList<row_struct>& t, QObject *parent)
    : QAbstractTableModel(parent)
    , table(t)
{
}

QVariant JobsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal) {
        switch (section) {
        case ID_COLUMN:
            return tr("Id");
        case TDATE_COLUMN:
            return tr("Date");
        case HASCACHE_COLUMN:
            return tr("Cache");
        case NAME_COLUMN:
            return tr("Name");
        default:
            return QVariant();
        }
    }
    return QVariant();


    return QVariant();
}

int JobsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return table.size();
}

int JobsModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return NUM_COLUMN;
}

QVariant JobsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (index.row() >= table.size() || index.row() < 0)
        return QVariant();

    if (index.column() >= NUM_COLUMN || index.column() < 0)
        return QVariant();

    if (role == Qt::DisplayRole) {
        row_struct row = table.at(index.row());
        switch(index.column())
        {
        case 0:
            return quint64(row.id);
            break;
        case 1:
            return row.tdate;
            break;
        case 2:
            return row.hasCache;
            break;
        case 3:
            return row.name;
            break;
        }
    }
    return QVariant();
}
