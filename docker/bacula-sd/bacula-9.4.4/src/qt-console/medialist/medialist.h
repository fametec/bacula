#ifndef _MEDIALIST_H_
#define _MEDIALIST_H_
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
/*
 *   Dirk Bartley, March 2007
 */

#if QT_VERSION >= 0x050000
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include "ui_medialist.h"
#include "console.h"
#include <qstringlist.h>

class MediaList : public Pages, public Ui::MediaListForm
{
   Q_OBJECT 

public:
   MediaList();
   ~MediaList();
   virtual void PgSeltreeWidgetClicked();
   virtual void currentStackItem();

public slots:
   void treeItemChanged(QTreeWidgetItem *, QTreeWidgetItem *);

private slots:
   void populateTree();
   void showJobs();
   void viewVolume();
   void editVolume();
   void deleteVolume();
   void purgeVolume();
   void pruneVolume();
   void relabelVolume();
   void allVolumesFromPool();
   void allVolumes();
   void volumeFromPool();

private:
   void createContextMenu();
   void writeExpandedSettings();
   QString m_currentVolumeName;
   QString m_currentVolumeId;
   bool m_populated;
   bool m_checkcurwidget;
   QTreeWidgetItem *m_topItem;
};

#endif /* _MEDIALIST_H_ */
