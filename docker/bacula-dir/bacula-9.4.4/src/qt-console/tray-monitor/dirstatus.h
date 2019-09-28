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

#include "common.h"
#include "ui_dir-monitor.h"
#include "task.h"
#include "status.h"

class DIRStatus: public ResStatus
{
   Q_OBJECT

public:
   Ui::dirStatus status;

   DIRStatus(RESMON *d):  ResStatus(d)
   {
      status.setupUi(this);
      QObject::connect(status.pushButton, SIGNAL(clicked()), this, SLOT(doUpdate()),  Qt::QueuedConnection);
   };
   ~DIRStatus() {
   };
public slots:
   void doUpdate();
   void taskDone(task *);
};
