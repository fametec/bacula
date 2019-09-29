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

#ifndef STATUS_H
#define STATUS_H

#include "common.h"
#include <QWidget>
#include "tray_conf.h"
#include "task.h"

class ResStatus: public QWidget
{
   Q_OBJECT

public:
   int count;
   RESMON *res;
   ResStatus(RESMON *c): count(0), res(c) {
   };
   virtual ~ResStatus() {
   };
public slots:
   virtual void doUpdate();
   virtual void taskDone(task *t);
};

#endif
