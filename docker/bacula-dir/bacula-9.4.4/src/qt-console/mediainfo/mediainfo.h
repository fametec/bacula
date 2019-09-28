#ifndef _MEDIAINFO_H_
#define _MEDIAINFO_H_
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
 *
 */

#if QT_VERSION >= 0x050000
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include "ui_mediainfo.h"
#include "console.h"
#include "pages.h"

class MediaInfo : public Pages, public Ui::mediaInfoForm
{
   Q_OBJECT 

public:
   MediaInfo(QTreeWidgetItem *parentWidget, QString &mediaId);

private slots:
   void pruneVol();
   void purgeVol();
   void deleteVol();
   void editVol();
   void showInfoForJob(QTableWidgetItem * item);

private:
   void populateForm();
   QString m_mediaName;
   QString m_mediaId;
};

#endif /* _MEDIAINFO_H_ */
