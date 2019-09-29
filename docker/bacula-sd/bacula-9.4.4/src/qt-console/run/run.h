/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

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

#ifndef _RUN_H_
#define _RUN_H_

#if QT_VERSION >= 0x050000
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include "ui_run.h"
#include "ui_runcmd.h"
#include "ui_estimate.h"
#include "ui_prune.h"
#include "console.h"

class runPage : public Pages, public Ui::runForm
{
   Q_OBJECT 

public:
   runPage();

   runPage(const QString &defJob);

   runPage(const QString &defJob, 
           const QString &level,
           const QString &pool,
           const QString &storage,
           const QString &client,
           const QString &fileset);

public slots:
   void okButtonPushed();
   void cancelButtonPushed();
   void job_name_change(int index);

private:
   void init();
   int m_conn;
};

class runCmdPage : public Pages, public Ui::runCmdForm
{
   Q_OBJECT 

public:
   runCmdPage(int conn);

public slots:
   void okButtonPushed();
   void cancelButtonPushed();

private:
   void fill();
   int m_conn;
};

class estimatePage : public Pages, public Ui::estimateForm
{
   Q_OBJECT 

public:
   estimatePage();

public slots:
   void okButtonPushed();
   void cancelButtonPushed();
   void job_name_change(int index);

private:
   int m_conn;
   bool m_aButtonPushed;
};

class prunePage : public Pages, public Ui::pruneForm
{
   Q_OBJECT 

public:
   prunePage(const QString &volume, const QString &client);

public slots:
   void okButtonPushed();
   void cancelButtonPushed();
   void volumeChanged();
   void clientChanged();

private:
   int m_conn;
};

#endif /* _RUN_H_ */
