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
//                              -*- Mode: C++ -*-
// vss.cpp -- Interface to Volume Shadow Copies (VSS)
//
// Copyright transferred from MATRIX-Computer GmbH to
//   Kern Sibbald by express permission.
//
// Author          : Thorsten Engel
// Created On      : Fri May 06 21:44:00 2005


#ifdef WIN32_VSS

#include "bacula.h"
#include "filed/filed.h"

#undef setlocale

// STL includes
#include <vector>
#include <algorithm>
#include <string>
#include <sstream>
#include <fstream>
using namespace std;

#include "ms_atl.h"
#include <objbase.h>

/* 
 * Kludges to get Vista code to compile.             
 *  by Kern Sibbald - June 2007
 */
#define __in  IN
#define __out OUT
#define __RPC_unique_pointer
#define __RPC_string
#ifndef __RPC__out_ecount_part
#define __RPC__out_ecount_part(x, y)
#endif
#define __RPC__deref_inout_opt
#define __RPC__out

#if !defined(ENABLE_NLS)
#define setlocale(p, d)
#endif

#ifdef HAVE_STRSAFE_H
// Used for safe string manipulation
#include <strsafe.h>
#endif

#ifdef HAVE_MINGW
class IXMLDOMDocument;
#endif

/* Reduce compiler warnings from Windows vss code */
#undef uuid
#define uuid(x)

#ifdef B_VSS_XP
   #define VSSClientGeneric VSSClientXP
   #include "inc/WinXP/vss.h"
   #include "inc/WinXP/vswriter.h"
   #include "inc/WinXP/vsbackup.h"

#endif

#ifdef B_VSS_W2K3
   #define VSSClientGeneric VSSClient2003
   #include "inc/Win2003/vss.h"
   #include "inc/Win2003/vswriter.h"
   #include "inc/Win2003/vsbackup.h"
#endif

#ifdef B_VSS_VISTA
   #define VSSClientGeneric VSSClientVista
   #include "inc/Win2003/vss.h"
   #include "inc/Win2003/vswriter.h"
   #include "inc/Win2003/vsbackup.h"
#endif

#include "vss.h"

static void JmsgVssApiStatus(JCR *jcr, int msg_status, HRESULT hr, const char *apiName)
{
   const char *errmsg;
   if (hr == S_OK || hr == VSS_S_ASYNC_FINISHED) {
      return;
   }
   switch (hr) {
   case E_INVALIDARG:
      errmsg = "One of the parameter values is not valid.";
      break;
   case E_OUTOFMEMORY:
      errmsg = "The caller is out of memory or other system resources.";
      break;
   case E_ACCESSDENIED:
      errmsg = "The caller does not have sufficient backup privileges or is not an administrator.";
      break;
   case VSS_E_INVALID_XML_DOCUMENT:
      errmsg = "The XML document is not valid.";
      break;
   case VSS_E_OBJECT_NOT_FOUND:
      errmsg = "The specified file does not exist.";
      break;
   case VSS_E_BAD_STATE:
      errmsg = "Object is not initialized; called during restore or not called in correct sequence.";
      break;
   case VSS_E_WRITER_INFRASTRUCTURE:
      errmsg = "The writer infrastructure is not operating properly. Check that the Event Service and VSS have been started, and check for errors associated with those services in the error log.";
      break;
   case VSS_S_ASYNC_CANCELLED:
      errmsg = "The asynchronous operation was canceled by a previous call to IVssAsync::Cancel.";
      break;
   case VSS_S_ASYNC_PENDING:
      errmsg = "The asynchronous operation is still running.";
      break;
   case RPC_E_CHANGED_MODE:
      errmsg = "Previous call to CoInitializeEx specified the multithread apartment (MTA). This call indicates single-threaded apartment has occurred.";
      break;
   case S_FALSE:
      errmsg = "No writer found for the current component.";
      break;
   default:
      errmsg = "Unexpected error. The error code is logged in the error log file.";
      break;
   }
   Jmsg(jcr, msg_status, 0, "VSS API failure calling \"%s\". ERR=%s\n", apiName, errmsg);
}

#ifndef VSS_WS_FAILED_AT_BACKUPSHUTDOWN
#define VSS_WS_FAILED_AT_BACKUPSHUTDOWN (VSS_WRITER_STATE)15
#endif


static void JmsgVssWriterStatus(JCR *jcr, int msg_status, VSS_WRITER_STATE eWriterStatus, char *writer_name)
{
   const char *errmsg;
   
   /* The following are normal states */
   if (eWriterStatus == VSS_WS_STABLE ||
       eWriterStatus == VSS_WS_WAITING_FOR_BACKUP_COMPLETE) {
      return;
   }

   /* Potential errors */
   switch (eWriterStatus) {
   default:
   case VSS_WS_UNKNOWN:
      errmsg = "The writer's state is not known. This is a writer error.";
      break;
   case VSS_WS_WAITING_FOR_FREEZE:
      errmsg = "The writer is waiting for the freeze state.";
      break;
   case VSS_WS_WAITING_FOR_THAW:
      errmsg = "The writer is waiting for the thaw state.";
      break;
   case VSS_WS_WAITING_FOR_POST_SNAPSHOT:
      errmsg = "The writer is waiting for the PostSnapshot state.";
      break;
   case VSS_WS_WAITING_FOR_BACKUP_COMPLETE:
      errmsg = "The writer is waiting for the requester to finish its backup operation.";
      break;
   case VSS_WS_FAILED_AT_IDENTIFY:
      errmsg = "The writer vetoed the shadow copy creation process at the writer identification state.";
      break;
   case VSS_WS_FAILED_AT_PREPARE_BACKUP:
      errmsg = "The writer vetoed the shadow copy creation process during the backup preparation state.";
      break;
   case VSS_WS_FAILED_AT_PREPARE_SNAPSHOT:
      errmsg = "The writer vetoed the shadow copy creation process during the PrepareForSnapshot state.";
      break;
   case VSS_WS_FAILED_AT_FREEZE:
      errmsg = "The writer vetoed the shadow copy creation process during the freeze state.";
      break;
   case VSS_WS_FAILED_AT_THAW:
      errmsg = "The writer vetoed the shadow copy creation process during the thaw state.";
      break;
   case VSS_WS_FAILED_AT_POST_SNAPSHOT:
      errmsg = "The writer vetoed the shadow copy creation process during the PostSnapshot state.";
      break;
   case VSS_WS_FAILED_AT_BACKUP_COMPLETE:
      errmsg = "The shadow copy has been created and the writer failed during the BackupComplete state.";
      break;
   case VSS_WS_FAILED_AT_PRE_RESTORE:
      errmsg = "The writer failed during the PreRestore state.";
      break;
   case VSS_WS_FAILED_AT_POST_RESTORE:
      errmsg = "The writer failed during the PostRestore state.";
      break;
   case VSS_WS_FAILED_AT_BACKUPSHUTDOWN:
      errmsg = "The writer failed during the shutdown of the backup application.";
      
   }
   Jmsg(jcr, msg_status, 0, "VSS Writer \"%s\" has invalid state. ERR=%s\n", writer_name, errmsg);
}

/*  
 *
 * some helper functions 
 *
 *
 */


// Defined in vss.cpp
// Append a backslash to the current string 
wstring AppendBackslash(wstring str);
// Get the unique volume name for the given path
wstring GetUniqueVolumeNameForPath(wstring path, wstring &rootPath);

// Helper macro for quick treatment of case statements for error codes
#define GEN_MERGE(A, B) A##B
#define GEN_MAKE_W(A) GEN_MERGE(L, A)

#define CHECK_CASE_FOR_CONSTANT(value)                      \
    case value: return (GEN_MAKE_W(#value));


// Convert a writer status into a string
inline const wchar_t* GetStringFromWriterStatus(VSS_WRITER_STATE eWriterStatus)
{
    switch (eWriterStatus) {
    CHECK_CASE_FOR_CONSTANT(VSS_WS_STABLE);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_WAITING_FOR_FREEZE);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_WAITING_FOR_THAW);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_WAITING_FOR_POST_SNAPSHOT);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_WAITING_FOR_BACKUP_COMPLETE);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_IDENTIFY);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_PREPARE_BACKUP);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_PREPARE_SNAPSHOT);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_FREEZE);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_THAW);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_POST_SNAPSHOT);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_BACKUP_COMPLETE);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_PRE_RESTORE);
    CHECK_CASE_FOR_CONSTANT(VSS_WS_FAILED_AT_POST_RESTORE);
    default:
        return L"Error or Undefined";
    }
}

// Constructor

VSSClientGeneric::VSSClientGeneric()
{
}

// Destructor
VSSClientGeneric::~VSSClientGeneric()
{
}

// Initialize the COM infrastructure and the internal pointers
bool VSSClientGeneric::Initialize(DWORD dwContext, bool bDuringRestore)
{
   CComPtr<IVssAsync>  pAsync1;
   VSS_BACKUP_TYPE backup_type;
   IVssBackupComponents* pVssObj = (IVssBackupComponents*)m_pVssObject;

   if (!(p_CreateVssBackupComponents && p_VssFreeSnapshotProperties)) {
      Dmsg2(0, "VSSClientGeneric::Initialize: p_CreateVssBackupComponents=0x%08X, p_VssFreeSnapshotProperties=0x%08X\n", p_CreateVssBackupComponents, p_VssFreeSnapshotProperties);
      Jmsg(m_jcr, M_FATAL, 0, "Entry point CreateVssBackupComponents or VssFreeSnapshotProperties missing.\n");
      return false;
   }

   if (m_VolumeList) {
      delete m_VolumeList;
   }

   m_VolumeList = New(MTab());  // TODO: See if we do this part only in backup
   if (!m_VolumeList->get()) {
      Jmsg(m_jcr, M_ERROR, 0, "Unable to list devices and volumes.\n");
      return false;
   }

   HRESULT hr;
   // Initialize COM
   if (!m_bCoInitializeCalled) {
      hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
      if (FAILED(hr)) {
         Dmsg1(0, "VSSClientGeneric::Initialize: CoInitializeEx returned 0x%08X\n", hr);
         JmsgVssApiStatus(m_jcr, M_FATAL, hr, "CoInitializeEx");
         errno = b_errno_win32;
         return false;
      }
      m_bCoInitializeCalled = true;
   }

   // Release the any old IVssBackupComponents interface 
   if (pVssObj) {
      pVssObj->Release();
      m_pVssObject = NULL;
   }

   // Create new internal backup components object
   hr = p_CreateVssBackupComponents((IVssBackupComponents**)&m_pVssObject);
   if (FAILED(hr)) {
      berrno be;
      Dmsg2(0, "VSSClientGeneric::Initialize: CreateVssBackupComponents returned 0x%08X. ERR=%s\n",
            hr, be.bstrerror(b_errno_win32));
      JmsgVssApiStatus(m_jcr, M_FATAL, hr, "CreateVssBackupComponents");
      errno = b_errno_win32;
      return false;
   }

   /* Define shorthand VssObject with time */
   pVssObj = (IVssBackupComponents*)m_pVssObject;


   if (!bDuringRestore) {
#if   defined(B_VSS_W2K3) || defined(B_VSS_VISTA)
      if (dwContext != VSS_CTX_BACKUP) {
         hr = pVssObj->SetContext(dwContext);
         if (FAILED(hr)) {
            Dmsg1(0, "VSSClientGeneric::Initialize: IVssBackupComponents->SetContext returned 0x%08X\n", hr);
            JmsgVssApiStatus(m_jcr, M_FATAL, hr, "SetContext");
            errno = b_errno_win32;
            return false;
         }
      }
#endif

      // 1. InitializeForBackup
      hr = pVssObj->InitializeForBackup();
      if (FAILED(hr)) {
         Dmsg1(0, "VSSClientGeneric::Initialize: IVssBackupComponents->InitializeForBackup returned 0x%08X\n", hr);
         JmsgVssApiStatus(m_jcr, M_FATAL, hr, "InitializeForBackup");
         errno = b_errno_win32; 
         return false;
      }
 
      // 2. SetBackupState
      switch (m_jcr->getJobLevel()) {
      case L_FULL:
         backup_type = VSS_BT_FULL;
         break;
      case L_DIFFERENTIAL:
         backup_type = VSS_BT_DIFFERENTIAL;
         break;
      case L_INCREMENTAL:
         backup_type = VSS_BT_INCREMENTAL;
         break;
      default:
         Dmsg1(0, "VSSClientGeneric::Initialize: unknown backup level %d\n", m_jcr->getJobLevel());
         backup_type = VSS_BT_FULL;
         break;
      }
      hr = pVssObj->SetBackupState(true, true, backup_type, false); /* FIXME: need to support partial files - make last parameter true when done */
      if (FAILED(hr)) {
         Dmsg1(0, "VSSClientGeneric::Initialize: IVssBackupComponents->SetBackupState returned 0x%08X\n", hr);
         JmsgVssApiStatus(m_jcr, M_FATAL, hr, "SetBackupState");
         errno = b_errno_win32;
         return false;
      }

      // 3. GatherWriterMetaData
      hr = pVssObj->GatherWriterMetadata(&pAsync1.p);
      if (FAILED(hr)) {
         Dmsg1(0, "VSSClientGeneric::Initialize: IVssBackupComponents->GatherWriterMetadata returned 0x%08X\n", hr);
         JmsgVssApiStatus(m_jcr, M_FATAL, hr, "GatherWriterMetadata");
         errno = b_errno_win32;
         return false;
      }
      // Waits for the async operation to finish and checks the result
      if (!WaitAndCheckForAsyncOperation(pAsync1.p)) {
         /* Error message already printed */
         errno = b_errno_win32;
         return false;
      }
   }

   // We are during restore now?
   m_bDuringRestore = bDuringRestore;

   // Keep the context
   m_dwContext = dwContext;

   return true;
}

bool VSSClientGeneric::WaitAndCheckForAsyncOperation(IVssAsync* pAsync)
{
   // Wait until the async operation finishes
   // unfortunately we can't use a timeout here yet.
   // the interface would allow it on W2k3,
   // but it is not implemented yet....

   HRESULT hr;

   // Check the result of the asynchronous operation
   HRESULT hrReturned = S_OK;

   int timeout = 1800; // 30 minutes ...

   int queryErrors = 0;
   do {
      if (hrReturned != S_OK) {
         Sleep(1000);
      }
      hrReturned = S_OK;
      hr = pAsync->QueryStatus(&hrReturned, NULL);
      if (FAILED(hr)) { 
         queryErrors++;
      }
   } while ((timeout-- > 0) && (hrReturned == VSS_S_ASYNC_PENDING));

   if (hrReturned == VSS_S_ASYNC_FINISHED) {
      return true;
   }
   
   JmsgVssApiStatus(m_jcr, M_FATAL, hr, "Query Async Status after 30 minute wait");
   return false;
}

static int volume_cmp(void *e1, void *e2)
{
   WCHAR *v1 = (WCHAR *) e1;
   MTabEntry *v2 = (MTabEntry *) e2;
   return wcscmp(v1, v2->volumeName);
}

static pthread_mutex_t create_mutex = PTHREAD_MUTEX_INITIALIZER;

bool VSSClientGeneric::CreateSnapshots(alist *mount_points)
{
   IVssBackupComponents *pVssObj;
   bool ret = false;
   HRESULT hr;

   /* AddToSnapshotSet */
   CComPtr<IVssAsync>  pAsync1;
   CComPtr<IVssAsync>  pAsync2;   
   VSS_ID pid;

   /* While testing the concurrent snapshot creation, I found out that the entire snapshot
    * creation process should be protected by a mutex. (InitializeForBackups and CreateSnapshots).
    */

   /* Create only one snapshot set at a time */
   P(create_mutex);

   /* szDriveLetters contains all drive letters in uppercase */
   /* if a drive can not being added, it's converted to lowercase in szDriveLetters */
   /* http://msdn.microsoft.com/library/default.asp?url=/library/en-us/vss/base/ivssbackupcomponents_startsnapshotset.asp */
   if (!m_pVssObject || m_bBackupIsInitialized) {
      Jmsg(m_jcr, M_FATAL, 0, "No pointer to VssObject or Backup is not Initialized\n");
      errno = ENOSYS;
      goto bail_out;
   }

   m_uidCurrentSnapshotSet = GUID_NULL;

   pVssObj = (IVssBackupComponents*)m_pVssObject;

   /* startSnapshotSet */
   hr = pVssObj->StartSnapshotSet(&m_uidCurrentSnapshotSet);
   if (FAILED(hr)) {
      JmsgVssApiStatus(m_jcr, M_FATAL, hr, "StartSnapshotSet");
      errno = ENOSYS;
      goto bail_out;
   }

   /*
    * Now try all paths in case they are mount points
    */
   for (int i=0; i < mount_points->size(); i++) {
      wchar_t *p = (wchar_t *)mount_points->get(i);
      // store uniquevolumname
      if (SUCCEEDED(pVssObj->AddToSnapshotSet(p, GUID_NULL, &pid))) {
         MTabEntry *elt = (MTabEntry*)m_VolumeList->entries->search(p, volume_cmp);
         ASSERT2(elt, "Should find the volume in the list");
         Jmsg(m_jcr, M_INFO, 0, "    Snapshot mount point: %ls\n", elt->first());
         Dmsg1(50, "AddToSnapshot OK for Vol: %ls\n", p);
      } else {
         //Dmsg1(50, "AddToSnapshot() failed for Vol: %ls\n", (LPWSTR)volume.c_str());
         //Dmsg1(50, "AddToSnapshot() failed for path: %s\n", p);
      }
   }

   /* PrepareForBackup */
   hr = pVssObj->PrepareForBackup(&pAsync1.p);
   if (FAILED(hr)) {
      JmsgVssApiStatus(m_jcr, M_FATAL, hr, "PrepareForBackup");
      errno = b_errno_win32;
      goto bail_out;
   }
   
   // Waits for the async operation to finish and checks the result
   if (!WaitAndCheckForAsyncOperation(pAsync1.p)) {
      /* Error message already printed */
      errno = b_errno_win32;
      goto bail_out;
   }

   /* get latest info about writer status */
   if (!CheckWriterStatus()) {
      /* Error message already printed */
      errno = b_errno_win32;       /* Error already printed */
      goto bail_out;
   }

   /* DoSnapShotSet */   
   hr = pVssObj->DoSnapshotSet(&pAsync2.p);
   if (FAILED(hr)) {
      JmsgVssApiStatus(m_jcr, M_FATAL, hr, "DoSnapshotSet");
      errno = b_errno_win32;
      goto bail_out;
   }

   // Waits for the async operation to finish and checks the result
   if (!WaitAndCheckForAsyncOperation(pAsync2.p)) {
      /* Error message already printed */
      errno = b_errno_win32;
      goto bail_out;
   }
   
   /* get latest info about writer status */
   if (!CheckWriterStatus()) {
      /* Error message already printed */
      errno = b_errno_win32;      /* Error already printed */
      goto bail_out;
   }

   /* query snapshot info */   
   QuerySnapshotSet(m_uidCurrentSnapshotSet);

   m_bBackupIsInitialized = true;

   ret = true;
bail_out:
   V(create_mutex);
   return ret;
}

bool VSSClientGeneric::CloseBackup()
{
   bool bRet = false;
   HRESULT hr;
   BSTR xml;
   IVssBackupComponents* pVssObj = (IVssBackupComponents*)m_pVssObject;

   if (!m_pVssObject) {
      Jmsg(m_jcr, M_FATAL, 0, "VssOject is NULL.\n");
      errno = ENOSYS;
      return bRet;
   }
   /* Create or Delete Snapshot one at a time */
   P(create_mutex);

   CComPtr<IVssAsync>  pAsync;
   m_bBackupIsInitialized = false;

   hr = pVssObj->BackupComplete(&pAsync.p);
   if (SUCCEEDED(hr)) {
      // Waits for the async operation to finish and checks the result
      if (!WaitAndCheckForAsyncOperation(pAsync.p)) {
         /* Error message already printed */
         errno = b_errno_win32;
      } else {
         bRet = true;
      }
   } else {
      JmsgVssApiStatus(m_jcr, M_ERROR, hr, "BackupComplete");
      errno = b_errno_win32;
      pVssObj->AbortBackup();
   }

   /* get latest info about writer status */
   CheckWriterStatus();

   hr = pVssObj->SaveAsXML(&xml);
   if (SUCCEEDED(hr)) {
      m_metadata = xml;
   } else {
      m_metadata = NULL;
   }

   /* FIXME?: The docs http://msdn.microsoft.com/en-us/library/aa384582%28v=VS.85%29.aspx say this isn't required... */
   if (m_uidCurrentSnapshotSet != GUID_NULL) {
      VSS_ID idNonDeletedSnapshotID = GUID_NULL;
      LONG lSnapshots;

      pVssObj->DeleteSnapshots(
         m_uidCurrentSnapshotSet, 
         VSS_OBJECT_SNAPSHOT_SET,
         false,
         &lSnapshots,
         &idNonDeletedSnapshotID);

      m_uidCurrentSnapshotSet = GUID_NULL;
   }

   if (m_bWriterStatusCurrent) {
      m_bWriterStatusCurrent = false;
      pVssObj->FreeWriterStatus();
   }

   pVssObj->Release();
   m_pVssObject = NULL;

   // Call CoUninitialize if the CoInitialize was performed sucesfully
   if (m_bCoInitializeCalled) {
      CoUninitialize();
      m_bCoInitializeCalled = false;
   }

   V(create_mutex);
   return bRet;
}

WCHAR *VSSClientGeneric::GetMetadata()
{
   return m_metadata;
}

bool VSSClientGeneric::CloseRestore()
{
   //HRESULT hr;
   IVssBackupComponents* pVssObj = (IVssBackupComponents*)m_pVssObject;
   CComPtr<IVssAsync> pAsync;

   if (!pVssObj) {
      Jmsg(m_jcr, M_FATAL, 0, "No pointer to VssObject or Backup is not Initialized\n");
      errno = ENOSYS;
      return false;
   }
#if 0
/* done by plugin now */
   if (SUCCEEDED(hr = pVssObj->PostRestore(&pAsync.p))) {
      // Waits for the async operation to finish and checks the result
      if (!WaitAndCheckForAsyncOperation(pAsync1.p)) {
         /* Error message already printed */
         errno = b_errno_win32;
         return false;
      }
      /* get latest info about writer status */
      if (!CheckWriterStatus()) {
         /* Error message already printed */
         errno = b_errno_win32;
         return false;
      }
   } else {
      errno = b_errno_win32;
      return false;
   }
#endif
   return true;
}

// Query all the shadow copies in the given set
void VSSClientGeneric::QuerySnapshotSet(GUID snapshotSetID)
{   
   if (!(p_CreateVssBackupComponents && p_VssFreeSnapshotProperties)) {
      Jmsg(m_jcr, M_FATAL, 0, "CreateVssBackupComponents or VssFreeSnapshotProperties API is NULL.\n");
      errno = ENOSYS;
      return;
   }

   if (snapshotSetID == GUID_NULL || m_pVssObject == NULL) {
      Jmsg(m_jcr, M_FATAL, 0, "snapshotSetID == NULL or VssObject is NULL.\n");
      errno = ENOSYS;
      return;
   }

   IVssBackupComponents* pVssObj = (IVssBackupComponents*) m_pVssObject;
               
   // Get list all shadow copies. 
   CComPtr<IVssEnumObject> pIEnumSnapshots;
   HRESULT hr = pVssObj->Query( GUID_NULL, 
         VSS_OBJECT_NONE, 
         VSS_OBJECT_SNAPSHOT, 
         (IVssEnumObject**)(&pIEnumSnapshots) );    

   // If there are no shadow copies, just return
   if (FAILED(hr)) {
      Jmsg(m_jcr, M_FATAL, 0, "No Volume Shadow copies made.\n");
      errno = b_errno_win32;
      return;   
   }

   // Enumerate all shadow copies. 
   VSS_OBJECT_PROP Prop;
   VSS_SNAPSHOT_PROP& Snap = Prop.Obj.Snap;
   
   while (true) {
      // Get the next element
      ULONG ulFetched;
      hr = (pIEnumSnapshots.p)->Next(1, &Prop, &ulFetched);

      // We reached the end of list
      if (ulFetched == 0) {
         break;
      }

      Dmsg2(DT_VOLUME|50, "Adding %ls => %ls to m_VolumeList\n",
            Snap.m_pwszOriginalVolumeName, Snap.m_pwszSnapshotDeviceObject);

      // Print the shadow copy (if not filtered out)
      if (Snap.m_SnapshotSetId == snapshotSetID)  {
         MTabEntry *elt = (MTabEntry*)m_VolumeList->entries->search(Snap.m_pwszOriginalVolumeName, volume_cmp);
         if (!elt) {
            Dmsg1(DT_VOLUME|50, "Unable to find [%ls] in the device list\n", Snap.m_pwszOriginalVolumeName);
            foreach_rblist(elt, m_VolumeList->entries) {
               elt->debug_paths();
            }
            Jmsg(m_jcr, M_WARNING, 0, _("Unable to find volume %ls in the device list\n"), Snap.m_pwszOriginalVolumeName);
         } else {
            elt->shadowCopyName = bwcsdup(Snap.m_pwszSnapshotDeviceObject);
            elt->setInSnapshotSet();
         }
      }
      p_VssFreeSnapshotProperties(&Snap);
   }
   errno = 0;
}

// Check the status for all selected writers
bool VSSClientGeneric::CheckWriterStatus()
{
    /* 
    http://msdn.microsoft.com/library/default.asp?url=/library/en-us/vss/base/ivssbackupcomponents_startsnapshotset.asp
    */
    IVssBackupComponents* pVssObj = (IVssBackupComponents*)m_pVssObject;
    if (!pVssObj) {
       Jmsg(m_jcr, M_FATAL, 0, "Cannot get IVssBackupComponents pointer.\n");
       errno = ENOSYS;
       return false;
    }
    DestroyWriterInfo();

    if (m_bWriterStatusCurrent) {
       m_bWriterStatusCurrent = false;
       pVssObj->FreeWriterStatus();
    }
    // Gather writer status to detect potential errors
    CComPtr<IVssAsync>  pAsync;
    
    HRESULT hr = pVssObj->GatherWriterStatus(&pAsync.p);
    if (FAILED(hr)) {
       JmsgVssApiStatus(m_jcr, M_FATAL, hr, "GatherWriterStatus");
       errno = b_errno_win32;
       return false;
    }

    // Waits for the async operation to finish and checks the result
    if (!WaitAndCheckForAsyncOperation(pAsync.p)) {
       /* Error message already printed */
       errno = b_errno_win32;
       return false;
    }
      
    m_bWriterStatusCurrent = true;

    unsigned cWriters = 0;

    hr = pVssObj->GetWriterStatusCount(&cWriters);
    if (FAILED(hr)) {
       JmsgVssApiStatus(m_jcr, M_FATAL, hr, "GetWriterStatusCount");
       errno = b_errno_win32;
       return false;
    }
    
    int nState;
    POOLMEM *szBuf = get_pool_memory(PM_FNAME);        
    // Enumerate each writer
    for (unsigned iWriter = 0; iWriter < cWriters; iWriter++) {
        VSS_ID idInstance = GUID_NULL;
        VSS_ID idWriter= GUID_NULL;
        VSS_WRITER_STATE eWriterStatus = VSS_WS_UNKNOWN;
        CComBSTR bstrWriterName;
        HRESULT hrWriterFailure = S_OK;

        // Get writer status
        hr = pVssObj->GetWriterStatus(iWriter,
                             &idInstance,
                             &idWriter,
                             &bstrWriterName,
                             &eWriterStatus,
                             &hrWriterFailure);
        if (FAILED(hr)) {
            /* Api failed */
            JmsgVssApiStatus(m_jcr, M_WARNING, hr, "GetWriterStatus");
            nState = 0;         /* Unknown writer state -- API failed */
        } else {            
            switch(eWriterStatus) {
            case VSS_WS_FAILED_AT_IDENTIFY:
            case VSS_WS_FAILED_AT_PREPARE_BACKUP:
            case VSS_WS_FAILED_AT_PREPARE_SNAPSHOT:
            case VSS_WS_FAILED_AT_FREEZE:
            case VSS_WS_FAILED_AT_THAW:
            case VSS_WS_FAILED_AT_POST_SNAPSHOT:
            case VSS_WS_FAILED_AT_BACKUP_COMPLETE:
            case VSS_WS_FAILED_AT_PRE_RESTORE:
            case VSS_WS_FAILED_AT_POST_RESTORE:
    #if  defined(B_VSS_W2K3) || defined(B_VSS_VISTA)
            case VSS_WS_FAILED_AT_BACKUPSHUTDOWN:
    #endif
                /* Writer status problem */    
                wchar_2_UTF8(&szBuf, bstrWriterName.p);
                JmsgVssWriterStatus(m_jcr, M_WARNING, eWriterStatus, szBuf);
                nState = -1;       /* bad writer state */
                break;

            default:
                /* ok */
                nState = 1;        /* Writer state OK */
            }
        }
        /* store text info */
        char str[1000];
        bstrncpy(str, "\"", sizeof(str));
        wchar_2_UTF8(&szBuf, bstrWriterName.p);
        bstrncat(str, szBuf, sizeof(str));
        bstrncat(str, "\", State: 0x", sizeof(str));
        itoa(eWriterStatus, szBuf, sizeof_pool_memory(szBuf));
        bstrncat(str, szBuf, sizeof(str));
        bstrncat(str, " (", sizeof(str));
        wchar_2_UTF8(&szBuf, GetStringFromWriterStatus(eWriterStatus));
        bstrncat(str, szBuf, sizeof(str));
        bstrncat(str, ")", sizeof(str));
        AppendWriterInfo(nState, (const char *)str);
    }
    free_pool_memory(szBuf);
    errno = 0;
    return true;
}

#endif /* WIN32_VSS */
