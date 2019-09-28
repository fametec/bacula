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

#ifndef TRAYUI_H
#define TRAYUI_H


#include "common.h"
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QSpinBox>
#include <QMenu>
#include <QIcon>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QDebug>
#include <QMessageBox>
#include <QFont>
#include <QInputDialog>

#include "fdstatus.h"
#include "sdstatus.h"
#include "dirstatus.h"
#include "conf.h"
#include "runjob.h"
#include "restorewizard.h"

void display_error(const char *fmt, ...);

int tls_pem_callback(char *buf, int size, const void *userdata);

class TrayUI: public QMainWindow
{
   Q_OBJECT

public:
    QWidget *centralwidget;
    QTabWidget *tabWidget;
    QStatusBar *statusbar;

    QSystemTrayIcon *tray;
    QSpinBox *spinRefresh;
    QTimer *timer;
    bool    have_systray;
    RestoreWizard *restorewiz;

    TrayUI():
    QMainWindow(),
       tabWidget(NULL),
       statusbar(NULL),
       tray(NULL),
       spinRefresh(NULL),
       timer(NULL),
       have_systray(QSystemTrayIcon::isSystemTrayAvailable()),
       restorewiz(NULL)
       {
       }

    ~TrayUI() {
    }
    void addTab(RESMON *r)
    {
       QWidget *tab;
       QString t = QString(r->hdr.name);
       if (r->tls_enable) {
          char buf[512];
          /* Generate passphrase prompt */
          bsnprintf(buf, sizeof(buf), "Passphrase for \"%s\" TLS private key: ", r->hdr.name);

          /* Initialize TLS context:
           * Args: CA certfile, CA certdir, Certfile, Keyfile,
           * Keyfile PEM Callback, Keyfile CB Userdata, DHfile, Verify Peer
           */
          r->tls_ctx = new_tls_context(r->tls_ca_certfile,
                                       r->tls_ca_certdir, r->tls_certfile,
                                       r->tls_keyfile, tls_pem_callback, &buf, NULL, true);

          if (!r->tls_ctx) {
             display_error(_("Failed to initialize TLS context for \"%s\".\n"), r->hdr.name);
          }
       }
       switch(r->type) {
       case R_CLIENT:
          tab = new FDStatus(r);
          break;
       case R_STORAGE:
          tab = new SDStatus(r);
          break;
       case R_DIRECTOR:
          tab = new DIRStatus(r);
          break;
       default:
          return;
       }
       tabWidget->setUpdatesEnabled(false);
       tabWidget->addTab(tab, t);
       tabWidget->setUpdatesEnabled(true);       
    }
    void clearTabs()
    {
       tabWidget->setUpdatesEnabled(false);
       for(int i = tabWidget->count() - 1; i >= 0; i--) {
          QWidget *w = tabWidget->widget(i);
          tabWidget->removeTab(i);
          delete w;
       }
       tabWidget->setUpdatesEnabled(true);
       tabWidget->update();
    }
    void startTimer()
    {
       if (!timer) {
          timer = new QTimer(this);
          connect(timer, SIGNAL(timeout()), this, SLOT(refresh_screen()));
       }
       timer->start(spinRefresh->value()*1000);
    }
    void setupUi(QMainWindow *TrayMonitor, MONITOR *mon)
    {
        QPushButton *menubp = NULL;
        timer = NULL;
        if (TrayMonitor->objectName().isEmpty())
            TrayMonitor->setObjectName(QString::fromUtf8("TrayMonitor"));
        TrayMonitor->setWindowIcon(QIcon(":/images/cartridge1.png")); 
        TrayMonitor->resize(789, 595);
        centralwidget = new QWidget(TrayMonitor);
        centralwidget->setObjectName(QString::fromUtf8("centralwidget"));
        QVBoxLayout *verticalLayout = new QVBoxLayout(centralwidget);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        tabWidget = new QTabWidget(centralwidget);
        tabWidget->setObjectName(QString::fromUtf8("tabWidget"));
        tabWidget->setTabPosition(QTabWidget::North);
        tabWidget->setTabShape(QTabWidget::Rounded);
        tabWidget->setTabsClosable(false);
        verticalLayout->addWidget(tabWidget);

        QDialogButtonBox *buttonBox = new QDialogButtonBox(centralwidget);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        if (have_systray) {
           buttonBox->setStandardButtons(QDialogButtonBox::Close);
           connect(buttonBox, SIGNAL(rejected()), this, SLOT(cb_show()));
        } else {
           /* Here we can display something else, now it's just a simple menu */
           menubp = new QPushButton(tr("&Options"));
           buttonBox->addButton(menubp, QDialogButtonBox::ActionRole);
        }
        TrayMonitor->setCentralWidget(centralwidget);
        statusbar = new QStatusBar(TrayMonitor);
        statusbar->setObjectName(QString::fromUtf8("statusbar"));
        TrayMonitor->setStatusBar(statusbar);

        QHBoxLayout *hLayout = new QHBoxLayout();
        QLabel *refreshlabel = new QLabel(centralwidget);
        refreshlabel->setText("Refresh:");
        hLayout->addWidget(refreshlabel);
        spinRefresh = new QSpinBox(centralwidget);
        QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(spinRefresh->sizePolicy().hasHeightForWidth());
        spinRefresh->setSizePolicy(sizePolicy);
        spinRefresh->setMinimum(1);
        spinRefresh->setMaximum(600);
        spinRefresh->setSingleStep(10);
        spinRefresh->setValue(mon?mon->RefreshInterval:60);
        hLayout->addWidget(spinRefresh);
        hLayout->addWidget(buttonBox);

        verticalLayout->addLayout(hLayout);
        //QSystemTrayIcon::isSystemTrayAvailable

        tray = new QSystemTrayIcon(TrayMonitor);
        QMenu* stmenu = new QMenu(TrayMonitor);

#if QT_VERSION >= 0x050000
        QAction *actShow = new QAction(QApplication::translate("TrayMonitor",
                               "Display", 0),TrayMonitor);
        QAction* actQuit = new QAction(QApplication::translate("TrayMonitor",
                               "Quit", 0),TrayMonitor);
        QAction* actAbout = new QAction(QApplication::translate("TrayMonitor",
                               "About", 0),TrayMonitor);
        QAction* actRun = new QAction(QApplication::translate("TrayMonitor",
                               "Run...", 0),TrayMonitor);
        QAction* actRes = new QAction(QApplication::translate("TrayMonitor",
                              "Restore...", 0),TrayMonitor);

        QAction* actConf = new QAction(QApplication::translate("TrayMonitor",
                               "Configure...", 0),TrayMonitor);
#else
        QAction *actShow = new QAction(QApplication::translate("TrayMonitor",
                               "Display",
                                0, QApplication::UnicodeUTF8),TrayMonitor);
        QAction* actQuit = new QAction(QApplication::translate("TrayMonitor",
                               "Quit",
                                0, QApplication::UnicodeUTF8),TrayMonitor);
        QAction* actAbout = new QAction(QApplication::translate("TrayMonitor",
                               "About",
                                0, QApplication::UnicodeUTF8),TrayMonitor);
        QAction* actRun = new QAction(QApplication::translate("TrayMonitor",
                               "Run...",
                                0, QApplication::UnicodeUTF8),TrayMonitor);
        QAction* actRes = new QAction(QApplication::translate("TrayMonitor",
                              "Restore...",
                               0, QApplication::UnicodeUTF8),TrayMonitor);

        QAction* actConf = new QAction(QApplication::translate("TrayMonitor",
                               "Configure...",
                                0, QApplication::UnicodeUTF8),TrayMonitor);
#endif
        stmenu->addAction(actShow);
        stmenu->addAction(actRun);
        stmenu->addAction(actRes);
        stmenu->addSeparator();
        stmenu->addAction(actConf);
        stmenu->addSeparator();
        stmenu->addAction(actAbout);
        stmenu->addSeparator();
        stmenu->addAction(actQuit);
        
        connect(actRun, SIGNAL(triggered()), this, SLOT(cb_run()));
        connect(actShow, SIGNAL(triggered()), this, SLOT(cb_show()));
        connect(actConf, SIGNAL(triggered()), this, SLOT(cb_conf()));
        connect(actRes, SIGNAL(triggered()), this, SLOT(cb_restore()));
        connect(actQuit, SIGNAL(triggered()), this, SLOT(cb_quit()));
        connect(actAbout, SIGNAL(triggered()), this, SLOT(cb_about()));
        connect(spinRefresh, SIGNAL(valueChanged(int)), this, SLOT(cb_refresh(int)));
        connect(tray, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
                this, SLOT(cb_trayIconActivated(QSystemTrayIcon::ActivationReason)));
        tray->setContextMenu(stmenu);

        QIcon icon(":/images/cartridge1.png");
        tray->setIcon(icon);
        tray->setToolTip(QString("Bacula Tray Monitor"));
        tray->show();
        retranslateUi(TrayMonitor);
        QMetaObject::connectSlotsByName(TrayMonitor);
        startTimer();

        /* When we don't have the systemtray, we keep the menu, but disabled */
        if (!have_systray) {
           actShow->setEnabled(false);
           menubp->setMenu(stmenu);
           TrayMonitor->show();
        }
    } // setupUi

    void retranslateUi(QMainWindow *TrayMonitor)
    {
#if QT_VERSION >= 0x050000
        TrayMonitor->setWindowTitle(QApplication::translate("TrayMonitor", "Bacula Tray Monitor", 0));
#else
        TrayMonitor->setWindowTitle(QApplication::translate("TrayMonitor", "Bacula Tray Monitor", 0, QApplication::UnicodeUTF8));
#endif

    } // retranslateUi

private slots:
    void cb_quit() {
       QApplication::quit();
    }

    void cb_refresh(int val) {
       if (timer) {
          timer->setInterval(val*1000);
       }
    }

    void cb_about() {
       QMessageBox::about(this, "Bacula Tray Monitor", "Bacula Tray Monitor\n"
                          "For more information, see: www.bacula.org\n"
                          "Copyright (C) 2000-2018, Kern Sibbald\n"
                          "License: AGPLv3");
    }
    RESMON *get_director() {
       QStringList dirs;
       RESMON *d, *director=NULL;
       bool ok;
       foreach_res(d, R_DIRECTOR) {
          if (!director) {
             director = d;
          }
          dirs << QString(d->hdr.name);
       }
       foreach_res(d, R_CLIENT) {
          if (d->use_remote) {
             if (!director) {
                director = d;
             }
             dirs << QString(d->hdr.name);
          }
       }
       if (dirs.count() > 1) {
          /* TODO: Set Modal attribute */
          QString dir = QInputDialog::getItem(this, _("Select a Director"), "Director:", dirs, 0, false, &ok, 0);
          if (!ok) {
             return NULL;
          }
          if (ok && !dir.isEmpty()) {
             char *p = dir.toUtf8().data();
             foreach_res(d, R_DIRECTOR) {
                if (strcmp(p, d->hdr.name) == 0) {
                   director = d;
                   break;
                }
             }
             foreach_res(d, R_CLIENT) {
                if (strcmp(p, d->hdr.name) == 0) {
                   director = d;
                   break;
                }
             }
          }
       }
       if (dirs.count() == 0 || director == NULL) {
          /* We need the proxy feature */
          display_error("No Director defined");
          return NULL;
       }
       return director;
    }
    void cb_run() {
       RESMON *dir = get_director();
       if (!dir) {
          return;
       }
       task *t = new task();
       connect(t, SIGNAL(done(task *)), this, SLOT(run_job(task *)), Qt::QueuedConnection);
       t->init(dir, TASK_RESOURCES);
       dir->wrk->queue(t);
    }
    void refresh_item() {
       /* Probably do only the first one */
       int oldnbjobs = 0;

       for (int i=tabWidget->count() - 1; i >= 0; i--) {
          ResStatus *s = (ResStatus *) tabWidget->widget(i);
          if (s->res->use_monitor) {
             s->res->mutex->lock();
             if (s->res->running_jobs) {
                oldnbjobs += s->res->running_jobs->size();
             }
             s->res->mutex->unlock();
          }
          if (isVisible() || s->res->use_monitor) {
             s->doUpdate();
          }
       }
       /* We need to find an other way to compute running jobs */
       if (oldnbjobs) {
          QString q;
          tray->setIcon(QIcon(":/images/R.png"));
          tray->setToolTip(q.sprintf("Bacula Tray Monitor - %d job%s running", oldnbjobs, oldnbjobs>1?"s":""));
          //tray->showMessage();   Can use this function to display a popup

       } else {
          tray->setIcon(QIcon(":/images/cartridge1.png"));
          tray->setToolTip("Bacula Tray Monitor");
       }
    }
    void cb_conf() {
       new Conf();
    }
    void cb_restore() {
       RESMON *dir = get_director();
       if (!dir) {
          return;
       }
       task *t = new task();
       connect(t, SIGNAL(done(task *)), this, SLOT(start_restore_wizard(task *)), Qt::QueuedConnection);       
       connect(t, SIGNAL(done(task*)), t, SLOT(deleteLater()));

       t->init(dir, TASK_RESOURCES);
       dir->wrk->queue(t);
    }

    void cb_trayIconActivated(QSystemTrayIcon::ActivationReason r) {
       if (r == QSystemTrayIcon::Trigger) {
          cb_show();
       }
    }

    void refresh_screen() {
       refresh_item();
    }

    void cb_show() {
       if (isVisible()) {
          hide();
       } else {
          refresh_item();
          show();
       }
    }
public slots:
    void task_done(task *t) {
       Dmsg0(0, "Task done!\n");
       t->deleteLater();
    }
    void run_job(task *t) {
       Dmsg0(0, "Task done!\n");
       RESMON *dir = t->res;
       t->deleteLater();
       new RunJob(dir);
    }

    void start_restore_wizard(task *t) {
        restorewiz = new RestoreWizard(t->res);
        restorewiz->show();
    }
    
};


#endif  /* TRAYUI_H */
