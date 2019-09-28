#ifndef _MEDIAEDIT_H_
#define _MEDIAEDIT_H_
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
#include "ui_mediaedit.h"
#include "console.h"
#include "pages.h"

class MediaEdit : public Pages, public Ui::mediaEditForm
{
   Q_OBJECT 

public:
   MediaEdit(QTreeWidgetItem *parentWidget, QString &mediaId);

private slots:
   void okButtonPushed();
   void cancelButtonPushed();
   void retentionChanged();
   void durationChanged();
   void useDurationChanged();
   void setSpins(int value);
   void retentionRadioPressed();
   void useDurationRadioPressed();

private:
   void connectSpins();
   void disconnectSpins();
   QString m_mediaName;
   QString m_pool;
   QString m_status;
   int m_slot;
   int m_retention;
   int m_useDuration;
   int m_maxVolJobs;
   int m_maxVolFiles;
   int m_maxVolBytes;
   bool m_recycle;
   bool m_enabled;
   QString m_recyclePool;
};

#endif /* _MEDIAEDIT_H_ */
