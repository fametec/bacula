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

#include "conf.h"
#include "tray-monitor.h"
#include <QFileDialog>

extern char *configfile;        // defined in tray-monitor.cpp
extern RES_TABLE resources[];
extern int32_t r_first;
extern int32_t r_last;
extern int32_t res_all_size;
extern URES res_all;

bool Conf::parse_config()
{
   bool ret;
   config = New(CONFIG());
   config->encode_password(false);
   config->init(configfile, NULL, M_ERROR, (void *)&res_all, res_all_size,
                r_first, r_last, resources, &rhead);
   ret = config->parse_config();
   return ret;
}

/* Check for \ at the end */
static char *is_str_valid(POOLMEM **buf, const char *p)
{
   char *p1;
   if (!p || !*p) {
      return NULL;
   }
   p1 = *buf = check_pool_memory_size(*buf, (strlen(p) + 1));
   for (; *p ; p++) {
      if (*p == '\\') {
         *p1++ = '/';

      } else if (*p == '"') {
         return NULL;

      } else {
         *p1++ = *p;
      }
   }
   *p1 = 0;
   return *buf;
}

/* The .toUtf8().data() function can be called only one time at a time */
void Conf::accept()
{
   POOLMEM *buf = get_pool_memory(PM_FNAME);
   POOL_MEM tmp, tmp2, name;
   QString str;
   const char *restype=NULL, *p=NULL, *pname;
   struct stat sp;
   FILE *fp=NULL;
   int   n;
   bool  commit=false;
   bool  doclose=true;
   bool  ok;

   Mmsg(tmp, "%s.temp", configfile);
   fp = fopen(tmp.c_str(), "w");
   if (!fp) {
      berrno be;
      display_error("Unable to open %s to write the new configuration file. ERR=%s\n", tmp.c_str(), be.bstrerror());
      goto bail_out;
   }
   str = UIConf.editName->text();
   p = is_str_valid(&buf, str.toUtf8().data());
   if (!p) {
      display_error(_("The Name of the Monitor should be set"));
      doclose = false;
      goto bail_out;
   }

   fprintf(fp, "Monitor {\n Name=\"%s\"\n", p);

   n = UIConf.spinRefresh->value();
   fprintf(fp, " Refresh Interval = %d\n", n);

   if (UIConf.cbDspAdvanced->isChecked()) {
      fprintf(fp, " Display Advanced Options = yes\n");
   }

   str = UIConf.editCommandDir->text();
   p = is_str_valid(&buf, str.toUtf8().data());
   if (p) {   // TODO: Check for \ at the end of the string
      fprintf(fp, " Command Directory = \"%s\"\n", p);
   }

   fprintf(fp, "}\n");

   for (int i = 1; i < UIConf.tabWidget->count() ; i++) {
      ConfTab *t = (ConfTab *) UIConf.tabWidget->widget(i);
      if (t->isEnabled() == false) {
         continue;              // This one was deleted
      }
      for(int i = 0; resources[i].name ; i++) {
         if (resources[i].rcode == t->res->type) {
            restype = resources[i].name;
            break;
         }
      }
      if (!restype) {
         goto bail_out;
      }

      str = t->ui.editName->text();
      pname = is_str_valid(&buf, str.toUtf8().data());
      if (!pname) {
         display_error(_("The name of the Resource should be set"));
         doclose = false;
         goto bail_out;
      }
      pm_strcpy(name, pname);

      str = t->ui.editAddress->text();
      p = is_str_valid(&buf, str.toUtf8().data());
      if (!p) {
         display_error(_("The address of the Resource should be set for resource %s"), name.c_str());
         doclose = false;
         goto bail_out;
      }
      fprintf(fp, "%s {\n Name = \"%s\"\n Address = \"%s\"\n", restype, name.c_str(), p);

      str = t->ui.editPassword->text();
      p = is_str_valid(&buf, str.toUtf8().data());
      if (!p) {
         display_error(_("The Password of should be set for resource %s"), name.c_str());
         doclose = false;
         goto bail_out;
      }
      fprintf(fp, " Password = \"%s\"\n", p);

      str = t->ui.editDescription->text();
      p = is_str_valid(&buf, str.toUtf8().data());
      if (p) {
         fprintf(fp, " Description = \"%s\"\n", p);
      }
      n = t->ui.editPort->text().toInt(&ok, 10);
      if (ok && n > 0 && n < 65636) {
         fprintf(fp, " Port = %d\n", n);
      }
      n = t->ui.editTimeout->text().toInt(&ok, 10);
      if (ok && n > 0) {
         fprintf(fp, " Connect Timeout = %d\n", n);
      }

      str = t->ui.editCaCertificateFile->text();
      p = is_str_valid(&buf, str.toUtf8().data());
      if (p) {
         if (stat(p, &sp) != 0 || !S_ISREG(sp.st_mode)) {
            display_error(_("The TLS CA Certificate File should be a PEM file for resource %s"), name.c_str());
            doclose = false;
            goto bail_out;
         }
         fprintf(fp, " TLSCaCertificateFile = \"%s\"\n", p);
      }

      str = t->ui.editCaCertificateDir->text();
      p = is_str_valid(&buf, str.toUtf8().data());
      if (p) {
         if (stat(p, &sp) != 0 || !S_ISDIR(sp.st_mode)) {
            display_error(_("The TLS CA Certificate Directory should be a directory for resource %s"), name.c_str());
            doclose = false;
            goto bail_out;
         }
         fprintf(fp, " TLSCaCertificateDir = \"%s\"\n", p);
      }

      str = t->ui.editCertificate->text();
      p = is_str_valid(&buf, str.toUtf8().data());
      if (p) {
         if (stat(p, &sp) != 0 || !S_ISREG(sp.st_mode)) {
            display_error(_("The TLS Certificate File should be a file for resource %s"), name.c_str());
            doclose = false;
            goto bail_out;
         }
         fprintf(fp, " TLSCertificate = \"%s\"\n", p);
      }

      str = t->ui.editKey->text();
      p = is_str_valid(&buf, str.toUtf8().data());
      if (p) {
         if (stat(p, &sp) != 0 || !S_ISREG(sp.st_mode)) {
            display_error(_("The TLS Key File should be a file for resource %s"), name.c_str());
            doclose = false;
            goto bail_out;
         }
         fprintf(fp, " TLSKey = \"%s\"\n", p);
      }
      if (t->ui.cbTLSEnabled->isChecked()) {
         fprintf(fp, " TLS Enable = yes\n");
      }
      if (strcmp(restype, "client") == 0 && t->ui.cbRemote->isChecked()) {
         fprintf(fp, " Remote = yes\n");
      }
      if (strcmp(restype, "director") == 0 && t->ui.cbUseSetIp->isChecked()) {
         fprintf(fp, " UseSetIp = yes\n");
      }
      if (t->ui.cbMonitor->isChecked()) {
         fprintf(fp, " Monitor = yes\n");
      }
      fprintf(fp, "}\n");
   }
   commit = true;
   // Save the configuration file
bail_out:
   if (fp) {
      fclose(fp);
   }
   if (commit) {
      // TODO: We probably need to load the configuration file to see if it works
      unlink(configfile);
      if (rename(tmp.c_str(), configfile) == 0) {
         reload();

      } else {
         berrno be;
         display_error("Unable to write to the configuration file %s ERR=%s\n", configfile, be.bstrerror());
      }
   }
   if (doclose) {
      close();
      deleteLater();
   }
}

Conf::~Conf()
{
   if (config) {
      delete config;
   }
}

Conf::Conf(): QDialog()
{
   RESMON  *res;
   MONITOR *mon;

   rhead = NULL;
   items = 0;
   UIConf.setupUi(this);
   if (parse_config()) {
      for(res=NULL; (res=(RESMON *)GetNextRes(rhead, R_CLIENT, (RES*)res));) {
         addResource(res, res->hdr.name);
      }
      for(res=NULL; (res=(RESMON *)GetNextRes(rhead, R_DIRECTOR, (RES*)res));) {
         addResource(res, res->hdr.name);
      }
      for(res=NULL; (res=(RESMON *)GetNextRes(rhead, R_STORAGE, (RES*)res));) {
         addResource(res, res->hdr.name);
      }
      mon = (MONITOR *)GetNextRes(rhead, R_MONITOR, NULL);
      UIConf.editName->setText(QString(mon->hdr.name));
      UIConf.spinRefresh->setValue(mon->RefreshInterval);
      UIConf.editCommandDir->setText(QString(NPRTB(mon->command_dir)));

      if (mon->display_advanced_options) {
         UIConf.cbDspAdvanced->setChecked(true);
      }
   }
   setAttribute(Qt::WA_DeleteOnClose, true);
   show();
}

void Conf::addResource(RESMON *res, const char *title)
{
   char ed1[50];
   ConfTab *w = new ConfTab(res);
   w->ui.editName->setText(QString(res->hdr.name));
   if (res->password) {
      w->ui.editPassword->setText(QString(res->password));
   }
   if (res->type != R_CLIENT) {
      w->ui.cbRemote->hide();
      w->ui.labelRemote->hide();

   } else if (res->use_remote) {
      w->ui.cbRemote->setChecked(true);
   }

   if (res->type != R_DIRECTOR) {
      w->ui.cbUseSetIp->hide();
      w->ui.labelSetIp->hide();

   } else if (res->use_setip) {
      w->ui.cbUseSetIp->setChecked(true);
   }

   if (res->use_monitor) {
      w->ui.cbMonitor->setChecked(true);
   }
   w->ui.editAddress->setText(QString(res->address));
   w->ui.editPort->setText(QString(edit_uint64(res->port, ed1)));
   w->ui.editTimeout->setText(QString(edit_uint64(res->connect_timeout, ed1)));
   if (!res->tls_enable) {
      if (w->ui.cbTLSEnabled->isChecked()) {
         emit w->ui.cbTLSEnabled->click();
      }
   }
   if (res->tls_ca_certfile) {
      w->ui.editCaCertificateFile->setText(QString(res->tls_ca_certfile));
   }
   if (res->tls_ca_certdir) {
      w->ui.editCaCertificateDir->setText(QString(res->tls_ca_certdir));
   }
   if (res->tls_certfile) {
      w->ui.editCertificate->setText(QString(res->tls_certfile));
   }
   if (res->tls_keyfile) {
      w->ui.editKey->setText(QString(res->tls_keyfile));
   }
   
   UIConf.tabWidget->addTab(w, QString(title));
   items++;
}

void Conf::addRes(int type, const char *title)
{
   RESMON *res = (RESMON *) malloc(sizeof(RESMON));
   init_resource(config, type, res);
   res->type = type;            // Not sure it's set by init_resource
   res->new_resource = true;    // We want to free this resource with the ConfTab
   addResource(res, title);
}

void Conf::addDir()
{
   addRes(R_DIRECTOR, "New Director");
}

void Conf::addStore()
{
   addRes(R_STORAGE, "New Storage");
}

void Conf::addClient()
{
   addRes(R_CLIENT, "New Client");
}

void Conf::togglePassword()
{
   if (passtype == QLineEdit::Normal) {
      passtype = QLineEdit::PasswordEchoOnEdit;
   } else {
      passtype = QLineEdit::Normal;
   }
   for (int i = 1; i < UIConf.tabWidget->count() ; i++) {
      ConfTab *tab = (ConfTab *) UIConf.tabWidget->widget(i);
      tab->ui.editPassword->setEchoMode(passtype);
   }
}

void Conf::selectCommandDir()
{
   QString directory = QFileDialog::getExistingDirectory(this,
                                                         tr("Select Command Directory"),
                                                         QDir::currentPath());
   UIConf.editCommandDir->setText(directory);
}

void ConfTab::selectCaCertificateFile()
{
   QString directory = QFileDialog::getOpenFileName(this,
                                                    tr("Select CA Certificate File PEM file"),
                                                    QDir::currentPath());
   ui.editCaCertificateFile->setText(directory);
}

void ConfTab::selectCaCertificateDir()
{
   QString directory = QFileDialog::getExistingDirectory(this,
                                                         tr("Select CA Certificate Directory"),
                                                         QDir::currentPath());
   ui.editCaCertificateDir->setText(directory);
}

void ConfTab::selectCertificate()
{
   QString file = QFileDialog::getOpenFileName(this,
                                               tr("Select TLS Certificate File"),
                                               QDir::currentPath());
   ui.editCertificate->setText(file);
}

void ConfTab::selectKey()
{
   QString file = QFileDialog::getOpenFileName(this,
                                               tr("Select TLS Certificate File"),
                                               QDir::currentPath());
   ui.editKey->setText(file);
}
