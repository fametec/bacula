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

#ifndef CONF_H
#define CONF_H

#include "common.h"
#include "ui_main-conf.h"
#include "ui_res-conf.h"
#include "tray_conf.h"

class Conf: public QDialog
{
   Q_OBJECT

private:
   CONFIG *config;
   RES_HEAD **rhead;
public:
   int items;
   QLineEdit::EchoMode passtype;
   Ui::Conf UIConf;
   Conf();
   ~Conf();
   bool parse_config();
   void addResource(RESMON *res, const char *title);
   void addRes(int type, const char *title); /* create the resource */
public slots:
   void accept();
   void selectCommandDir();
   void addDir();
   void addStore();
   void addClient();
   void togglePassword();
};

class ConfTab: public QWidget
{
   Q_OBJECT

public:
   Ui::ResConf ui;
   RESMON *res;
   int  type;
   bool new_resource;
   ConfTab(RESMON *r): QWidget() {
      res = r;
      type = r->type;
      new_resource = r->new_resource;
      ui.setupUi(this);
      connect(ui.bpDelete, SIGNAL(clicked()), this, SLOT(disable()));
   };
   ~ConfTab() {
      if (new_resource && res) {
         free_resource((RES*) res, res->type);
         res = NULL;
      }
   };
public slots:
   void disable() {
      setEnabled(false);
   };
   void selectCaCertificateFile();
   void selectCaCertificateDir();
   void selectCertificate();
   void selectKey();
};

#endif
