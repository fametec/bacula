/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2015 Kern Sibbald

   The original author of Bacula is Kern Sibbald, with contributions
   from many others, a complete list can be found in the file AUTHORS.

   You may use this file and others of this release according to the
   license defined in the LICENSE file, which includes the Affero General
   Public License, v3.0 ("AGPLv3") and some additional permissions and
   terms pursuant to its AGPLv3 Section 7.

   This notice must be preserved when any source code is 
   conveyed and/or propagated.

   Bacula(R) is a registered trademark of Kern Sibbald.

   Written by Eric Bollengier 2015
*/

#ifndef FD_SNAPSHOT_H
#define FD_SNAPSHOT_H

/* default snapshot handler */
char *snapshot_get_command();

/* Internal objects */
class mtab;                     /* device list */
class fs_device;

class snapshot_manager: public SMARTALLOC
{
private:
   JCR  *jcr;
public:
   mtab *mount_list;

   snapshot_manager(JCR *ajcr);
   virtual ~snapshot_manager();

   /* Quiesce application and take snapshot */
   bool create_snapshots();
   
   /* Cleanup snapshots */
   bool cleanup_snapshots();

   /* List snapshots */
   bool list_snapshots(alist *ret);

   /* Scan the fileset for devices and application */
   bool scan_fileset();

   /* Scan the mtab */
   bool scan_mtab();

   /* Add a mount point to the mtab list */
   void add_mount_point(uint32_t dev, const char *device, 
                        const char *mountpoint, const char *fstype);
};

void close_snapshot_backup_session(JCR *jcr);
bool open_snapshot_backup_session(JCR *jcr);

bool snapshot_convert_path(JCR *jcr, FF_PKT *ff, dlist *filelist, dlistString *node);

#endif  /* FD_SNAPSHOT_H */
