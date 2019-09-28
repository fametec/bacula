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

#ifndef TASK_H
#define TASK_H

#include "common.h"
#include <QtCore/QObject>
#include <QStandardItemModel>
#include "tray_conf.h"

enum {
   TASK_NONE,
   TASK_STATUS,
   TASK_RESOURCES,
   TASK_QUERY,
   TASK_RUN,
   TASK_LIST_CLIENT_JOBS,
   TASK_LIST_JOB_FILES,
   TASK_RESTORE,
   TASK_PLUGIN,
   TASK_DEFAULTS,
   TASK_CLOSE,
   TASK_INFO,
   TASK_BWLIMIT,
   TASK_DISCONNECT
};

/* The task should emit a signal when done */
class task: public QObject
{
   Q_OBJECT

public:
   RESMON       *res;
   POOLMEM      *errmsg;
   int          type;
   bool         status;
   char         *curline;
   char         *curend;
   const char   *arg;               /* Argument that can be used by some tasks */
   const char   *arg2;
   const char   *arg3;
   QStandardItemModel   *model;       /* model to fill, depending on context */
   uint64_t     pathId;

   union {
      bool  b;
      int   i;
      char  c[256];
   } result;                    /* The task might return something */
   
   struct r_field
   {
       QString tableName;
       QString jobIds;
       QString fileIds;
       QString dirIds;
       QString hardlinks;
       QString client;
       QString where;
       QString replace;
       QString comment;
       QString pluginnames;
       QString pluginkeys;
   } restore_field;

   task(): QObject(), res(NULL), type(TASK_NONE), status(false), curline(NULL),
      curend(NULL), arg(NULL), arg2(NULL), arg3(NULL), model(NULL), pathId(0)
   {
      errmsg = get_pool_memory(PM_FNAME);
      *errmsg = 0;
      memset(result.c, 0, sizeof(result.c));
   }

   ~task() {
      Enter();
      disconnect();             /* disconnect all signals */
      free_pool_memory(errmsg);
   }

   void init(int t) {
      res = NULL;
      type = t;
      status = false;
      arg = NULL;
      arg2 = NULL;
      arg3 = NULL;
      model = NULL;
      pathId = 0;
   }

   void init(RESMON *s, int t) {
      init(t);
      res = s;
   }

   RESMON *get_res();
   void lock_res();
   void unlock_res();
   bool connect_bacula();
   bool do_status();
   bool read_status_terminated(RESMON *res);
   bool read_status_header(RESMON *res);
   bool read_status_running(RESMON *res);
   bool set_bandwidth();
   bool disconnect_bacula();
   void mark_as_done() {
      status = true;
      emit done(this);
   }
   void mark_as_failed() {
      status = false;
      emit done(this);
   }
   bool get_resources();
   bool get_next_line(RESMON *res);
   bool get_job_defaults(); /* Look r->defaults.job */
   bool run_job();
   bool get_job_info(const char *level);     /* look r->info */
   bool get_client_jobs(const char* client);
   bool get_job_files(const char* job, uint64_t pathId);

   bool prepare_restore();
   bool run_restore();
   bool clean_restore();
   bool restore();

   QString plugins_ids(const QString &jobIds);
   QString plugins_names(const QString& jobIds);
   QString parse_plugins(const QString& jobIds, const QString& fieldName);
   QFile* plugin(const QString &name, const QString& jobIds, int id);

signals:

   void done(task *t);
};

worker *worker_start();
void worker_stop(worker *);

#endif
