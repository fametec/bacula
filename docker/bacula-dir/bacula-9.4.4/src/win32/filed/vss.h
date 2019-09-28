/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2019 Kern Sibbald

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
/*                               -*- Mode: C -*-
 * vss.h --
 */
//
// Copyright transferred from MATRIX-Computer GmbH to
//   Kern Sibbald by express permission.
/*
 *
 * Author          : Thorsten Engel
 * Created On      : Fri May 06 21:44:00 2006 
 */

#ifndef __VSS_H_
#define __VSS_H_

#ifndef b_errno_win32
#define b_errno_win32 (1<<29)
#endif
 
#ifdef WIN32_VSS

#define VSS_INIT_RESTORE_AFTER_INIT   1
#define VSS_INIT_RESTORE_AFTER_GATHER 2

// some forward declarations
struct IVssAsync;

#define bwcsdup(str) wcscpy((WCHAR *)bmalloc((wcslen(str)+1)*sizeof(WCHAR)),(str))

/* The MTabEntry class is representing a mounted volume,
 * it associates a volume name with mount paths and a device name
 */
class MTabEntry: public SMARTALLOC {
public:
   WCHAR *volumeName;         // Name of the current volume
   WCHAR *mountPaths;         // List of mount paths
   WCHAR *deviceName;
   WCHAR *shadowCopyName;
   bool   in_SnapshotSet;
   bool   can_Snapshot;
   UINT   driveType;
   rblink link;

   MTabEntry() {
      mountPaths = NULL;
      volumeName = NULL;
      deviceName = NULL;
      in_SnapshotSet = false;
      shadowCopyName = NULL;
      can_Snapshot = false;
      driveType = 0;
   };

   MTabEntry(WCHAR *DeviceName, WCHAR *VolumeName) {
      int last = wcslen(VolumeName);
      if (VolumeName[last - 1] == L'\\') {
         volumeName = bwcsdup(VolumeName);
      } else {                         /* \\ + \0 */
         volumeName = (WCHAR *)bmalloc(last+2*sizeof(WCHAR));
         wcscpy(volumeName, VolumeName);
         volumeName[last] = L'\\';
         volumeName[last+1] = L'\0';
      }
      mountPaths = NULL;
      in_SnapshotSet = false;
      deviceName = bwcsdup(DeviceName);
      shadowCopyName = NULL;
      driveType = 0;
      can_Snapshot = false;
      get_paths();
   };

   ~MTabEntry() {
      destroy();
   };

   void destroy() {
      if (mountPaths) {
         free(mountPaths);
         mountPaths = NULL;
      }
      if (volumeName) {
         free(volumeName);
         volumeName = NULL;
      }
      if (deviceName) {
         free(deviceName);
         deviceName = NULL;
      }
      if (shadowCopyName) {
         free(shadowCopyName);
         shadowCopyName = NULL;
      }
   };

   /* Return  the drive type (cdrom, fixed, network, ...) */
   UINT getDriveType();

   /* Return true if the current volume can be snapshoted (ie not CDROM or fat32) */
   bool isSuitableForSnapshot();

   void setInSnapshotSet() {
      Dmsg1(050, "Marking %ls for the SnapshotSet\n", mountPaths);
      in_SnapshotSet = true;
   }

   void debug_paths() {
      WCHAR *p;
      /*  Display the paths in the list. */
      if (mountPaths != NULL) {
         Dmsg2(DT_VOLUME|10, "Device: [%ls], Volume: [%ls]\n", deviceName, volumeName);
         for ( p = mountPaths;  p[0] != L'\0'; p += wcslen(p) + 1) {
            Dmsg1(DT_VOLUME|10, "  %ls\n", p);
         }
      }
   };

   /* Compute the path list assiciated with the current volume */
   bool get_paths() {
      DWORD  count = MAX_PATH + 1;
      bool   ret = false;

      for (;;) {
         //  Allocate a buffer to hold the paths.
         mountPaths = (WCHAR*) malloc(count * sizeof(WCHAR));

         //  Obtain all of the paths
         //  for this volume.
         ret = GetVolumePathNamesForVolumeNameW(volumeName, mountPaths, 
                                                count, &count);
         if (ret) {
            break;
         }

         if (GetLastError() != ERROR_MORE_DATA) {
            break;
         }

         //  Try again with the
         //  new suggested size.
         free(mountPaths);
         mountPaths = NULL;
      }
      debug_paths();
      return ret;
   };

   /* Return the first mount point */
   WCHAR *first() {
      return mountPaths;
   };

   /* Return the next mount point */
   WCHAR *next(WCHAR *prev) {
      if (prev == NULL || prev[0] == L'\0') {
         return NULL;
      }

      prev += wcslen(prev) + 1;

      return (prev[0] == L'\0') ? NULL : prev;
   };
};

/* Class to handle all volumes of the system, it contains
 * a list of all current volumes (MTabEntry)
 */
class MTab: public SMARTALLOC {
public:
   DWORD       lasterror;
   const char *lasterror_str;
   rblist     *entries;         /* MTabEntry */
   int         nb_in_SnapshotSet;

   MTab() {
      MTabEntry *elt = NULL;
      lasterror = ERROR_SUCCESS;
      lasterror_str = "";
      nb_in_SnapshotSet = 0;
      entries = New(rblist(elt, &elt->link));
   };

   ~MTab() {
      if (entries) {
         MTabEntry *elt;
         foreach_rblist(elt, entries) {
            elt->destroy();
         }
         delete entries;
      }
   };
   /* Get a Volume by name */
   MTabEntry *search(char *file);

   /* Try to add a volume to the current snapshotset */
   bool addInSnapshotSet(char *file);

   /* Fill the "entries" list will all detected volumes of the system*/
   bool get();
};

class VSSClient
{
public:
    VSSClient();
    virtual ~VSSClient();

    // Backup Process
    bool InitializeForBackup(JCR *jcr);
    bool InitializeForRestore(JCR *jcr);
    virtual bool CreateSnapshots(alist *mount_points) = 0;
    virtual bool CloseBackup() = 0;
    virtual bool CloseRestore() = 0;
    virtual WCHAR *GetMetadata() = 0;
    virtual const char* GetDriverName() = 0;
    bool GetShadowPath  (const char* szFilePath, char* szShadowPath, int nBuflen);
    bool GetShadowPathW (const wchar_t* szFilePath, wchar_t* szShadowPath, int nBuflen); /* nBuflen in characters */

    const size_t GetWriterCount();
    const char* GetWriterInfo(int nIndex);
    const int   GetWriterState(int nIndex);
    void DestroyWriterInfo();
    void AppendWriterInfo(int nState, const char* pszInfo);
    const bool  IsInitialized() { return m_bBackupIsInitialized; };
    IUnknown *GetVssObject() { return m_pVssObject; };

private:
    virtual bool Initialize(DWORD dwContext, bool bDuringRestore = FALSE) = 0;
    virtual bool WaitAndCheckForAsyncOperation(IVssAsync*  pAsync) = 0;
    virtual void QuerySnapshotSet(GUID snapshotSetID) = 0;

protected:
    JCR       *m_jcr;

    DWORD      m_dwContext;

    IUnknown*  m_pVssObject;
    GUID       m_uidCurrentSnapshotSet;

    MTab      *m_VolumeList;

    alist     *m_pAlistWriterState;
    alist     *m_pAlistWriterInfoText;

    bool       m_bCoInitializeCalled;
    bool       m_bCoInitializeSecurityCalled;
    bool       m_bDuringRestore;  /* true if we are doing a restore */
    bool       m_bBackupIsInitialized;
    bool       m_bWriterStatusCurrent;

    WCHAR     *m_metadata;

    void       CreateVSSVolumeList();
    void       DeleteVSSVolumeList();
};

class VSSClientXP:public VSSClient
{
public:
   VSSClientXP();
   virtual ~VSSClientXP();
   virtual bool CreateSnapshots(alist *mount_points);
   virtual bool CloseBackup();
   virtual bool CloseRestore();
   virtual WCHAR *GetMetadata();
#ifdef _WIN64
   virtual const char* GetDriverName() { return "Win64 VSS"; };
#else
   virtual const char* GetDriverName() { return "Win32 VSS"; };
#endif
private:
   virtual bool Initialize(DWORD dwContext, bool bDuringRestore);
   virtual bool WaitAndCheckForAsyncOperation(IVssAsync* pAsync);
   virtual void QuerySnapshotSet(GUID snapshotSetID);
   bool CheckWriterStatus();   
};

class VSSClient2003:public VSSClient
{
public:
   VSSClient2003();
   virtual ~VSSClient2003();
   virtual bool CreateSnapshots(alist *mount_points);
   virtual bool CloseBackup();   
   virtual bool CloseRestore();
   virtual WCHAR *GetMetadata();
#ifdef _WIN64
   virtual const char* GetDriverName() { return "Win64 VSS"; };
#else
   virtual const char* GetDriverName() { return "Win32 VSS"; };
#endif
private:
   virtual bool Initialize(DWORD dwContext, bool bDuringRestore);
   virtual bool WaitAndCheckForAsyncOperation(IVssAsync*  pAsync);
   virtual void QuerySnapshotSet(GUID snapshotSetID);
   bool CheckWriterStatus();
};

class VSSClientVista:public VSSClient
{
public:
   VSSClientVista();
   virtual ~VSSClientVista();
   virtual bool CreateSnapshots(alist *mount_points);
   virtual bool CloseBackup();   
   virtual bool CloseRestore();
   virtual WCHAR *GetMetadata();
#ifdef _WIN64
   virtual const char* GetDriverName() { return "Win64 VSS"; };
#else
   virtual const char* GetDriverName() { return "Win32 VSS"; };
#endif
private:
   virtual bool Initialize(DWORD dwContext, bool bDuringRestore);
   virtual bool WaitAndCheckForAsyncOperation(IVssAsync*  pAsync);
   virtual void QuerySnapshotSet(GUID snapshotSetID);
   bool CheckWriterStatus();
};


BOOL VSSPathConvert(const char *szFilePath, char *szShadowPath, int nBuflen);
BOOL VSSPathConvertW(const wchar_t *szFilePath, wchar_t *szShadowPath, int nBuflen);

#endif /* WIN32_VSS */

#endif /* __VSS_H_ */
