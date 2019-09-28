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

#include "status.h"
#include "lib/worker.h"

void ResStatus::doUpdate()
{
   if (count == 0) {
      task *t = new task();
      connect(t, SIGNAL(done(task *)), this, SLOT(taskDone(task *)), Qt::QueuedConnection);
      t->init(res, TASK_STATUS);
      res->wrk->queue(t);
      Dmsg0(0, "doUpdate()\n");
      count++;
   }
}

void ResStatus::taskDone(task *t)
{
   if (!t->status) {
      Dmsg2(0, "  Task %p failed => %s\n", t, t->errmsg);
   }
   delete t;
   count--;
}
