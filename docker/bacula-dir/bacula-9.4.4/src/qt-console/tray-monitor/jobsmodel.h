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
#ifndef JOBSMODEL_H
#define JOBSMODEL_H

#include <QAbstractTableModel>
#include "common.h"

class JobsModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum {
        ID_COLUMN =0,
        TDATE_COLUMN,
        HASCACHE_COLUMN,
        NAME_COLUMN,
        NUM_COLUMN
    };

    struct row_struct {
        uint64_t    id;
        QDateTime   tdate;
        QString     hasCache;
        QString     name;
    };

    explicit JobsModel(const QList<row_struct>& t, QObject *parent = NULL);
    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    int columnCount(const QModelIndex &parent = QModelIndex()) const;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;

private:
    QList<row_struct> table;
};

#endif // JOBSMODEL_H
