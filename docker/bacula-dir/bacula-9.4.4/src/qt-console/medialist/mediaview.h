#ifndef _MEDIAVIEW_H_
#define _MEDIAVIEW_H_
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

#if QT_VERSION >= 0x050000
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include "ui_mediaview.h"
#include "console.h"
#include <qstringlist.h>

class MediaView : public Pages, public Ui::MediaViewForm
{
   Q_OBJECT 

public:
   MediaView();
   ~MediaView();

private slots:
   void populateTable();
   void populateForm();
   void PgSeltreeWidgetClicked();
   void currentStackItem();
   void applyPushed();
   void editPushed();
   void purgePushed();
   void prunePushed();
   void deletePushed();
   bool getSelection(QStringList &ret);
   void showInfoForMedia(QTableWidgetItem * item);
   void filterExipired(QStringList &list);
//   void relabelVolume();
//   void allVolumesFromPool();
//   void allVolumes();
//   void volumeFromPool();

private:
   bool m_populated;
   bool m_checkcurwidget;
   QTreeWidgetItem *m_topItem;
};

#endif /* _MEDIAVIEW_H_ */
