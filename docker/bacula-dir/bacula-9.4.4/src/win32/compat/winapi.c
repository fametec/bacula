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
/*
 * Windows APIs that are different for each system.
 *   We use pointers to the entry points so that a
 *   single binary will run on all Windows systems.
 *
 *     Kern Sibbald MMIII
 */

#include "bacula.h"


#ifdef HAVE_VSS64
/* 64 bit entrypoint name */
#define VSSVBACK_ENTRY "?CreateVssBackupComponents@@YAJPEAPEAVIVssBackupComponents@@@Z"
#define VSSVMETA_ENTRY "?CreateVssExamineWriterMetadata@@YAJPEAGPEAPEAVIVssExamineWriterMetadata@@@Z"
#else
/* 32 bit entrypoint name */
#define VSSVMETA_ENTRY "?CreateVssExamineWriterMetadata@@YGJPAGPAPAVIVssExamineWriterMetadata@@@Z"
#define VSSVBACK_ENTRY "?CreateVssBackupComponents@@YGJPAPAVIVssBackupComponents@@@Z"
#endif


// init with win9x, but maybe set to NT in InitWinAPI
DWORD g_platform_id = VER_PLATFORM_WIN32_WINDOWS;
DWORD g_MinorVersion = 0;
DWORD g_MajorVersion = 0;

/* API Pointers */

t_OpenProcessToken      p_OpenProcessToken = NULL;
t_AdjustTokenPrivileges p_AdjustTokenPrivileges = NULL;
t_LookupPrivilegeValue  p_LookupPrivilegeValue = NULL;

t_SetProcessShutdownParameters p_SetProcessShutdownParameters = NULL;

t_CreateFileA           p_CreateFileA = NULL;
t_CreateFileW           p_CreateFileW = NULL;

t_OpenEncryptedFileRawA p_OpenEncryptedFileRawA = NULL;
t_OpenEncryptedFileRawW p_OpenEncryptedFileRawW = NULL;
t_ReadEncryptedFileRaw  p_ReadEncryptedFileRaw = NULL;
t_WriteEncryptedFileRaw p_WriteEncryptedFileRaw = NULL;
t_CloseEncryptedFileRaw p_CloseEncryptedFileRaw = NULL;


t_CreateDirectoryA      p_CreateDirectoryA;
t_CreateDirectoryW      p_CreateDirectoryW;

t_GetFileInformationByHandleEx p_GetFileInformationByHandleEx = NULL;

t_wunlink               p_wunlink = NULL;
t_wmkdir                p_wmkdir = NULL;

t_GetFileAttributesA    p_GetFileAttributesA = NULL;
t_GetFileAttributesW    p_GetFileAttributesW = NULL;

t_GetFileAttributesExA  p_GetFileAttributesExA = NULL;
t_GetFileAttributesExW  p_GetFileAttributesExW = NULL;

t_SetFileAttributesA    p_SetFileAttributesA = NULL;
t_SetFileAttributesW    p_SetFileAttributesW = NULL;
t_BackupRead            p_BackupRead = NULL;
t_BackupWrite           p_BackupWrite = NULL;
t_WideCharToMultiByte   p_WideCharToMultiByte = NULL;
t_MultiByteToWideChar   p_MultiByteToWideChar = NULL;

t_AttachConsole         p_AttachConsole = NULL;

t_FindFirstFileA        p_FindFirstFileA = NULL;
t_FindFirstFileW        p_FindFirstFileW = NULL;

t_FindNextFileA         p_FindNextFileA = NULL;
t_FindNextFileW         p_FindNextFileW = NULL;

t_SetCurrentDirectoryA  p_SetCurrentDirectoryA = NULL;
t_SetCurrentDirectoryW  p_SetCurrentDirectoryW = NULL;

t_GetCurrentDirectoryA  p_GetCurrentDirectoryA = NULL;
t_GetCurrentDirectoryW  p_GetCurrentDirectoryW = NULL;

t_GetVolumePathNameW    p_GetVolumePathNameW = NULL;
t_GetVolumeNameForVolumeMountPointW p_GetVolumeNameForVolumeMountPointW = NULL;

t_SHGetFolderPath       p_SHGetFolderPath = NULL;

t_CreateProcessA        p_CreateProcessA = NULL;
t_CreateProcessW        p_CreateProcessW = NULL;

t_CreateSymbolicLinkA   p_CreateSymbolicLinkA = NULL;
t_CreateSymbolicLinkW   p_CreateSymbolicLinkW = NULL;
t_InetPton              p_InetPton = NULL;
t_GetProcessMemoryInfo p_GetProcessMemoryInfo = NULL;
t_EmptyWorkingSet      p_EmptyWorkingSet = NULL;

HMODULE                vsslib = NULL;
t_CreateVssBackupComponents p_CreateVssBackupComponents = NULL;
t_VssFreeSnapshotProperties p_VssFreeSnapshotProperties = NULL;
t_CreateVssExamineWriterMetadata p_CreateVssExamineWriterMetadata;


static void atexit_handler()
{
   CoUninitialize();
}

/* http://thrysoee.dk/InsideCOM+/ch18d.htm
 * The COM+ security infrastructure is initialized on a per-process basis at
 * start-up. The CoInitializeSecurity function sets the default security values
 * for the process. If an application does not call CoInitializeSecurity, COM+
 * calls the function automatically the first time an interface pointer is
 * marshaled into or out of an apartment (or context) in the
 * process. Attempting to call CoInitializeSecurity after marshaling takes
 * place yields the infamous RPC_E_TOO_LATE error. Thus, programs that want to
 * call CoInitializeSecurity explicitly are advised to do so immediately after
 * calling CoInitializeEx. Note that CoInitializeSecurity is called only once per
 * process, not in each thread that calls CoInitializeEx.
*/
static void InitComInterface()
{
   /* Setup ComSecurity */
   HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
   if (FAILED(hr)) {
      Dmsg1(0, "CoInitializeEx returned 0x%08X\n", hr);

   } else {
      // Initialize COM security
      hr =
         CoInitializeSecurity(
            NULL,           //  Allow *all* VSS writers to communicate back!
            -1,             //  Default COM authentication service
            NULL,           //  Default COM authorization service
            NULL,           //  reserved parameter
            RPC_C_AUTHN_LEVEL_PKT_PRIVACY,  //  Strongest COM authentication level
            RPC_C_IMP_LEVEL_IDENTIFY,       //  Minimal impersonation abilities
            NULL,            //  Default COM authentication settings
            EOAC_NONE,       //  No special options
            NULL             //  Reserved parameter
            );
      if (FAILED(hr)  && (hr != RPC_E_TOO_LATE)) {
         Dmsg1(0, "CoInitializeSecurity returned 0x%08X\n", hr);
      }
      atexit(atexit_handler);
   }
}

void
InitWinAPIWrapper()
{
   OSVERSIONINFO osversioninfo = { sizeof(OSVERSIONINFO) };

   // Get the current OS version
   if (!GetVersionEx(&osversioninfo)) {
      g_platform_id = 0;
   } else {
      g_platform_id = osversioninfo.dwPlatformId;
      g_MinorVersion = osversioninfo.dwMinorVersion;
      g_MajorVersion = osversioninfo.dwMajorVersion;
   }

   HMODULE hLib = LoadLibraryA("KERNEL32.DLL");
   if (hLib) {
      /* Might be defined in Kernel32.dll or PSAPI.DLL */
      p_GetProcessMemoryInfo = (t_GetProcessMemoryInfo)
         GetProcAddress(hLib, "K32GetProcessMemoryInfo");

      /* Might be defined in Kernel32.dll or PSAPI.DLL */
      p_EmptyWorkingSet = (t_EmptyWorkingSet)
         GetProcAddress(hLib, "K32EmptyWorkingSet");

      /* Not defined before win2008 */
      p_CreateSymbolicLinkA = (t_CreateSymbolicLinkA)
         GetProcAddress(hLib, "CreateSymbolicLinkA");
      p_CreateSymbolicLinkW = (t_CreateSymbolicLinkW)
         GetProcAddress(hLib, "CreateSymbolicLinkW");

      /* create process calls */
      p_CreateProcessA = (t_CreateProcessA)
         GetProcAddress(hLib, "CreateProcessA");
      p_CreateProcessW = (t_CreateProcessW)
         GetProcAddress(hLib, "CreateProcessW");

      /* create file calls */
      p_CreateFileA = (t_CreateFileA)GetProcAddress(hLib, "CreateFileA");
      p_CreateDirectoryA = (t_CreateDirectoryA)GetProcAddress(hLib, "CreateDirectoryA");

      p_GetFileInformationByHandleEx = (t_GetFileInformationByHandleEx)
         GetProcAddress(hLib, "GetFileInformationByHandleEx");

      /* attribute calls */
      p_GetFileAttributesA = (t_GetFileAttributesA)GetProcAddress(hLib, "GetFileAttributesA");
      p_GetFileAttributesExA = (t_GetFileAttributesExA)GetProcAddress(hLib, "GetFileAttributesExA");
      p_SetFileAttributesA = (t_SetFileAttributesA)GetProcAddress(hLib, "SetFileAttributesA");

      /* process calls */
      p_SetProcessShutdownParameters = (t_SetProcessShutdownParameters)
          GetProcAddress(hLib, "SetProcessShutdownParameters");

      /* char conversion calls */
      p_WideCharToMultiByte = (t_WideCharToMultiByte)
          GetProcAddress(hLib, "WideCharToMultiByte");
      p_MultiByteToWideChar = (t_MultiByteToWideChar)
          GetProcAddress(hLib, "MultiByteToWideChar");

      /* find files */
      p_FindFirstFileA = (t_FindFirstFileA)GetProcAddress(hLib, "FindFirstFileA");
      p_FindNextFileA = (t_FindNextFileA)GetProcAddress(hLib, "FindNextFileA");

      /* get and set directory */
      p_GetCurrentDirectoryA = (t_GetCurrentDirectoryA)
          GetProcAddress(hLib, "GetCurrentDirectoryA");
      p_SetCurrentDirectoryA = (t_SetCurrentDirectoryA)
          GetProcAddress(hLib, "SetCurrentDirectoryA");

      if (g_platform_id != VER_PLATFORM_WIN32_WINDOWS) {
         p_CreateFileW = (t_CreateFileW)
             GetProcAddress(hLib, "CreateFileW");
         p_CreateDirectoryW = (t_CreateDirectoryW)
             GetProcAddress(hLib, "CreateDirectoryW");

         /* backup calls */
         p_BackupRead = (t_BackupRead)GetProcAddress(hLib, "BackupRead");
         p_BackupWrite = (t_BackupWrite)GetProcAddress(hLib, "BackupWrite");

         p_GetFileAttributesW = (t_GetFileAttributesW)
             GetProcAddress(hLib, "GetFileAttributesW");
         p_GetFileAttributesExW = (t_GetFileAttributesExW)
             GetProcAddress(hLib, "GetFileAttributesExW");
         p_SetFileAttributesW = (t_SetFileAttributesW)
             GetProcAddress(hLib, "SetFileAttributesW");
         p_FindFirstFileW = (t_FindFirstFileW)
             GetProcAddress(hLib, "FindFirstFileW");
         p_FindNextFileW = (t_FindNextFileW)
             GetProcAddress(hLib, "FindNextFileW");
         p_GetCurrentDirectoryW = (t_GetCurrentDirectoryW)
             GetProcAddress(hLib, "GetCurrentDirectoryW");
         p_SetCurrentDirectoryW = (t_SetCurrentDirectoryW)
             GetProcAddress(hLib, "SetCurrentDirectoryW");

         /* some special stuff we need for VSS
            but static linkage doesn't work on Win 9x */
         p_GetVolumePathNameW = (t_GetVolumePathNameW)
             GetProcAddress(hLib, "GetVolumePathNameW");
         p_GetVolumeNameForVolumeMountPointW = (t_GetVolumeNameForVolumeMountPointW)
             GetProcAddress(hLib, "GetVolumeNameForVolumeMountPointW");

         p_AttachConsole = (t_AttachConsole)
             GetProcAddress(hLib, "AttachConsole");
      }
   }

   if (g_platform_id != VER_PLATFORM_WIN32_WINDOWS) {
      hLib = LoadLibraryA("MSVCRT.DLL");
      if (hLib) {
         /* unlink */
         p_wunlink = (t_wunlink)
         GetProcAddress(hLib, "_wunlink");
         /* wmkdir */
         p_wmkdir = (t_wmkdir)
         GetProcAddress(hLib, "_wmkdir");
      }

      hLib = LoadLibraryA("ADVAPI32.DLL");
      if (hLib) {
         p_OpenProcessToken = (t_OpenProcessToken)
            GetProcAddress(hLib, "OpenProcessToken");
         p_AdjustTokenPrivileges = (t_AdjustTokenPrivileges)
            GetProcAddress(hLib, "AdjustTokenPrivileges");
         p_LookupPrivilegeValue = (t_LookupPrivilegeValue)
            GetProcAddress(hLib, "LookupPrivilegeValueA");

         p_OpenEncryptedFileRawA = (t_OpenEncryptedFileRawA)
            GetProcAddress(hLib, "OpenEncryptedFileRawA");
         p_OpenEncryptedFileRawW = (t_OpenEncryptedFileRawW)
            GetProcAddress(hLib, "OpenEncryptedFileRawW");
         p_ReadEncryptedFileRaw = (t_ReadEncryptedFileRaw)
            GetProcAddress(hLib, "ReadEncryptedFileRaw");
         p_WriteEncryptedFileRaw = (t_WriteEncryptedFileRaw)
            GetProcAddress(hLib, "WriteEncryptedFileRaw");
         p_CloseEncryptedFileRaw = (t_CloseEncryptedFileRaw)
            GetProcAddress(hLib, "CloseEncryptedFileRaw");
      }
   }

   hLib = LoadLibraryA("SHELL32.DLL");
   if (hLib) {
      p_SHGetFolderPath = (t_SHGetFolderPath)
         GetProcAddress(hLib, "SHGetFolderPathA");
   } else {
      /* If SHELL32 isn't found try SHFOLDER for older systems */
      hLib = LoadLibraryA("SHFOLDER.DLL");
      if (hLib) {
         p_SHGetFolderPath = (t_SHGetFolderPath)
            GetProcAddress(hLib, "SHGetFolderPathA");
      }
   }
   hLib = LoadLibraryA("WS2_32.DLL");
   if (hLib) {
      p_InetPton = (t_InetPton)GetProcAddress(hLib, "InetPtonA");
   }
   if (!p_GetProcessMemoryInfo) {
      hLib = LoadLibraryA("PSAPI.DLL");
      if (hLib) {
         p_GetProcessMemoryInfo = (t_GetProcessMemoryInfo)GetProcAddress(hLib, "GetProcessMemoryInfo");
         p_EmptyWorkingSet = (t_EmptyWorkingSet) GetProcAddress(hLib, "EmptyWorkingSet");
      }
   }

   vsslib = LoadLibraryA("VSSAPI.DLL");
   if (vsslib) {
      p_CreateVssBackupComponents = (t_CreateVssBackupComponents)
         GetProcAddress(vsslib, VSSVBACK_ENTRY);
      p_VssFreeSnapshotProperties = (t_VssFreeSnapshotProperties)
          GetProcAddress(vsslib, "VssFreeSnapshotProperties");
      p_CreateVssExamineWriterMetadata = (t_CreateVssExamineWriterMetadata)
          GetProcAddress(vsslib, VSSVMETA_ENTRY);
   }

   /* In recent version of windows, the function is in Kernel32 */
   if (!p_GetFileInformationByHandleEx) {
      hLib = LoadLibraryA("FileExtd.lib");
      if (hLib) {
         p_GetFileInformationByHandleEx = (t_GetFileInformationByHandleEx)
            GetProcAddress(hLib, "GetFileInformationByHandleEx");
      }
   }

   atexit(Win32ConvCleanupCache);

   /* Setup Com Object security interface (called once per process) */
   InitComInterface();
}
