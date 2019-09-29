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
/*
 *  Bacula low level File I/O routines.  This routine simulates
 *    open(), read(), write(), and close(), but using native routines.
 *    I.e. on Windows, we use Windows APIs.
 *
 *    Kern Sibbald, April MMIII
 *
 */

#include "bacula.h"
#include "find.h"

const int dbglvl = 200;

int       (*plugin_bopen)(BFILE *bfd, const char *fname, uint64_t flags, mode_t mode) = NULL;
int       (*plugin_bclose)(BFILE *bfd) = NULL;
ssize_t   (*plugin_bread)(BFILE *bfd, void *buf, size_t count) = NULL;
ssize_t   (*plugin_bwrite)(BFILE *bfd, void *buf, size_t count) = NULL;
boffset_t (*plugin_blseek)(BFILE *bfd, boffset_t offset, int whence) = NULL;


#ifdef HAVE_DARWIN_OS
#include <sys/paths.h>
#endif

#if !defined(HAVE_FDATASYNC)
#define fdatasync(fd)
#endif

#ifdef HAVE_WIN32
void pause_msg(const char *file, const char *func, int line, const char *msg)
{
   char buf[1000];
   if (msg) {
      bsnprintf(buf, sizeof(buf), "%s:%s:%d %s", file, func, line, msg);
   } else {
      bsnprintf(buf, sizeof(buf), "%s:%s:%d", file, func, line);
   }
   MessageBox(NULL, buf, "Pause", MB_OK);
}
#endif

/* ===============================================================
 *
 *            U N I X   AND   W I N D O W S
 *
 * ===============================================================
 */

bool is_win32_stream(int stream)
{
   switch (stream) {
   case STREAM_WIN32_DATA:
   case STREAM_WIN32_GZIP_DATA:
   case STREAM_WIN32_COMPRESSED_DATA:
   case STREAM_ENCRYPTED_WIN32_DATA:
   case STREAM_ENCRYPTED_WIN32_GZIP_DATA:
   case STREAM_ENCRYPTED_WIN32_COMPRESSED_DATA:
      return true;
   }
   return false;
}

const char *stream_to_ascii(int stream)
{
   static char buf[20];

   switch (stream & STREAMMASK_TYPE) {
      case STREAM_UNIX_ATTRIBUTES:
         return _("Unix attributes");
      case STREAM_FILE_DATA:
         return _("File data");
      case STREAM_MD5_DIGEST:
         return _("MD5 digest");
      case STREAM_GZIP_DATA:
         return _("GZIP data");
      case STREAM_COMPRESSED_DATA:
         return _("Compressed data");
      case STREAM_UNIX_ATTRIBUTES_EX:
         return _("Extended attributes");
      case STREAM_SPARSE_DATA:
         return _("Sparse data");
      case STREAM_SPARSE_GZIP_DATA:
         return _("GZIP sparse data");
      case STREAM_SPARSE_COMPRESSED_DATA:
         return _("Compressed sparse data");
      case STREAM_PROGRAM_NAMES:
         return _("Program names");
      case STREAM_PROGRAM_DATA:
         return _("Program data");
      case STREAM_SHA1_DIGEST:
         return _("SHA1 digest");
      case STREAM_WIN32_DATA:
         return _("Win32 data");
      case STREAM_WIN32_GZIP_DATA:
         return _("Win32 GZIP data");
      case STREAM_WIN32_COMPRESSED_DATA:
         return _("Win32 compressed data");
      case STREAM_MACOS_FORK_DATA:
         return _("MacOS Fork data");
      case STREAM_HFSPLUS_ATTRIBUTES:
         return _("HFS+ attribs");
      case STREAM_UNIX_ACCESS_ACL:
         return _("Standard Unix ACL attribs");
      case STREAM_UNIX_DEFAULT_ACL:
         return _("Default Unix ACL attribs");
      case STREAM_SHA256_DIGEST:
         return _("SHA256 digest");
      case STREAM_SHA512_DIGEST:
         return _("SHA512 digest");
      case STREAM_SIGNED_DIGEST:
         return _("Signed digest");
      case STREAM_ENCRYPTED_FILE_DATA:
         return _("Encrypted File data");
      case STREAM_ENCRYPTED_WIN32_DATA:
         return _("Encrypted Win32 data");
      case STREAM_ENCRYPTED_SESSION_DATA:
         return _("Encrypted session data");
      case STREAM_ENCRYPTED_FILE_GZIP_DATA:
         return _("Encrypted GZIP data");
      case STREAM_ENCRYPTED_FILE_COMPRESSED_DATA:
         return _("Encrypted compressed data");
      case STREAM_ENCRYPTED_WIN32_GZIP_DATA:
         return _("Encrypted Win32 GZIP data");
      case STREAM_ENCRYPTED_WIN32_COMPRESSED_DATA:
         return _("Encrypted Win32 Compressed data");
      case STREAM_ENCRYPTED_MACOS_FORK_DATA:
         return _("Encrypted MacOS fork data");
      case STREAM_PLUGIN_NAME:
         return _("Plugin Name");
      case STREAM_PLUGIN_DATA:
         return _("Plugin Data");
      case STREAM_RESTORE_OBJECT:
         return _("Restore Object");
      case STREAM_XACL_AIX_TEXT:
         return _("AIX ACL attribs");
      case STREAM_XACL_DARWIN_ACCESS:
         return _("Darwin ACL attribs");
      case STREAM_XACL_FREEBSD_DEFAULT:
         return _("FreeBSD Default ACL attribs");
      case STREAM_XACL_FREEBSD_ACCESS:
         return _("FreeBSD Access ACL attribs");
      case STREAM_XACL_HPUX_ACL_ENTRY:
         return _("HPUX ACL attribs");
      case STREAM_XACL_IRIX_DEFAULT:
         return _("Irix Default ACL attribs");
      case STREAM_XACL_IRIX_ACCESS:
         return _("Irix Access ACL attribs");
      case STREAM_XACL_LINUX_DEFAULT:
         return _("Linux Default ACL attribs");
      case STREAM_XACL_LINUX_ACCESS:
         return _("Linux Access ACL attribs");
      case STREAM_XACL_TRU64_DEFAULT:
         return _("TRU64 Default ACL attribs");
      case STREAM_XACL_TRU64_ACCESS:
         return _("TRU64 Access ACL attribs");
      case STREAM_XACL_SOLARIS_POSIX:
         return _("Solaris POSIX ACL attribs");
      case STREAM_XACL_SOLARIS_NFS4:
         return _("Solaris NFSv4/ZFS ACL attribs");
      case STREAM_XACL_AFS_TEXT:
         return _("AFS ACL attribs");
      case STREAM_XACL_AIX_AIXC:
         return _("AIX POSIX ACL attribs");
      case STREAM_XACL_AIX_NFS4:
         return _("AIX NFSv4 ACL attribs");
      case STREAM_XACL_FREEBSD_NFS4:
         return _("FreeBSD NFSv4/ZFS ACL attribs");
      case STREAM_XACL_HURD_DEFAULT:
         return _("GNU Hurd Default ACL attribs");
      case STREAM_XACL_HURD_ACCESS:
         return _("GNU Hurd Access ACL attribs");
      case STREAM_XACL_HURD_XATTR:
         return _("GNU Hurd Extended attribs");
      case STREAM_XACL_IRIX_XATTR:
         return _("IRIX Extended attribs");
      case STREAM_XACL_TRU64_XATTR:
         return _("TRU64 Extended attribs");
      case STREAM_XACL_AIX_XATTR:
         return _("AIX Extended attribs");
      case STREAM_XACL_OPENBSD_XATTR:
         return _("OpenBSD Extended attribs");
      case STREAM_XACL_SOLARIS_SYS_XATTR:
         return _("Solaris Extensible attribs or System Extended attribs");
      case STREAM_XACL_SOLARIS_XATTR:
         return _("Solaris Extended attribs");
      case STREAM_XACL_DARWIN_XATTR:
         return _("Darwin Extended attribs");
      case STREAM_XACL_FREEBSD_XATTR:
         return _("FreeBSD Extended attribs");
      case STREAM_XACL_LINUX_XATTR:
         return _("Linux Extended attribs");
      case STREAM_XACL_NETBSD_XATTR:
         return _("NetBSD Extended attribs");
      default:
         sprintf(buf, "%d", stream);
         return (const char *)buf;
      }
}

/**
 *  Convert a 64 bit little endian to a big endian
 */
void int64_LE2BE(int64_t* pBE, const int64_t v)
{
   /* convert little endian to big endian */
   if (htonl(1) != 1L) { /* no work if on little endian machine */
      memcpy(pBE, &v, sizeof(int64_t));
   } else {
      int i;
      uint8_t rv[sizeof(int64_t)];
      uint8_t *pv = (uint8_t *) &v;

      for (i = 0; i < 8; i++) {
         rv[i] = pv[7 - i];
      }
      memcpy(pBE, &rv, sizeof(int64_t));
   }
}

/**
 *  Convert a 32 bit little endian to a big endian
 */
void int32_LE2BE(int32_t* pBE, const int32_t v)
{
   /* convert little endian to big endian */
   if (htonl(1) != 1L) { /* no work if on little endian machine */
      memcpy(pBE, &v, sizeof(int32_t));
   } else {
      int i;
      uint8_t rv[sizeof(int32_t)];
      uint8_t *pv = (uint8_t *) &v;

      for (i = 0; i < 4; i++) {
         rv[i] = pv[3 - i];
      }
      memcpy(pBE, &rv, sizeof(int32_t));
   }
}


/**
 *  Read a BackupRead block and pull out the file data
 */
bool processWin32BackupAPIBlock (BFILE *bfd, void *pBuffer, ssize_t dwSize)
{
   int64_t len = dwSize;
   char *dat=(char *)pBuffer;
   int64_t use_len;

   while (len>0 && bfd->win32filter.have_data(&dat, &len, &use_len)) {
      if (bwrite(bfd, dat, use_len) != (ssize_t)use_len) {
         return false;
      }
      dat+=use_len;
   }
   return true;
}

/* ===============================================================
 *
 *            W I N D O W S
 *
 * ===============================================================
 */

#if defined(HAVE_WIN32)

void unix_name_to_win32(POOLMEM **win32_name, const char *name);
extern "C" HANDLE get_osfhandle(int fd);


void binit(BFILE *bfd)
{
   memset(bfd, 0, sizeof(BFILE));
   bfd->fid = -1;
   bfd->mode = BF_CLOSED;
   bfd->use_backup_api = have_win32_api();
   bfd->cmd_plugin = false;
}

/*
 * Enables using the Backup API (win32_data).
 *   Returns 1 if function worked
 *   Returns 0 if failed (i.e. do not have Backup API on this machine)
 */
bool set_win32_backup(BFILE *bfd)
{
   /* We enable if possible here */
   bfd->use_backup_api = have_win32_api();
   return bfd->use_backup_api;
}

void set_fattrs(BFILE *bfd, struct stat *statp)
{
   bfd->fattrs = statp->st_fattrs;
   Dmsg1(200, "set_fattrs 0x%x\n", bfd->fattrs);
}

bool set_portable_backup(BFILE *bfd)
{
   bfd->use_backup_api = false;
   return true;
}

bool set_cmd_plugin(BFILE *bfd, JCR *jcr)
{
   bfd->cmd_plugin = true;
   bfd->jcr = jcr;
   return true;
}

/*
 * Return 1 if we are NOT using Win32 BackupWrite()
 * return 0 if are
 */
bool is_portable_backup(BFILE *bfd)
{
   return !bfd->use_backup_api;
}

bool is_plugin_data(BFILE *bfd)
{
   return bfd->cmd_plugin;
}

bool have_win32_api()
{
   return p_BackupRead && p_BackupWrite;
}


/*
 * Return true  if we support the stream
 *        false if we do not support the stream
 *
 *  This code is running under Win32, so we
 *    do not need #ifdef on MACOS ...
 */
bool is_restore_stream_supported(int stream)
{
   switch (stream) {

/* Streams known not to be supported */
#ifndef HAVE_LIBZ
   case STREAM_GZIP_DATA:
   case STREAM_SPARSE_GZIP_DATA:
   case STREAM_WIN32_GZIP_DATA:
#endif
#ifndef HAVE_LZO
   case STREAM_COMPRESSED_DATA:
   case STREAM_SPARSE_COMPRESSED_DATA:
   case STREAM_WIN32_COMPRESSED_DATA:
   case STREAM_ENCRYPTED_FILE_COMPRESSED_DATA:
   case STREAM_ENCRYPTED_WIN32_COMPRESSED_DATA:
#endif
   case STREAM_MACOS_FORK_DATA:
   case STREAM_HFSPLUS_ATTRIBUTES:
   case STREAM_ENCRYPTED_MACOS_FORK_DATA:
      return false;

   /* Known streams */
#ifdef HAVE_LIBZ
   case STREAM_GZIP_DATA:
   case STREAM_SPARSE_GZIP_DATA:
   case STREAM_WIN32_GZIP_DATA:
#endif
#ifdef HAVE_LZO
   case STREAM_COMPRESSED_DATA:
   case STREAM_SPARSE_COMPRESSED_DATA:
   case STREAM_WIN32_COMPRESSED_DATA:
#endif
   case STREAM_WIN32_DATA:
   case STREAM_UNIX_ATTRIBUTES:
   case STREAM_FILE_DATA:
   case STREAM_MD5_DIGEST:
   case STREAM_UNIX_ATTRIBUTES_EX:
   case STREAM_SPARSE_DATA:
   case STREAM_PROGRAM_NAMES:
   case STREAM_PROGRAM_DATA:
   case STREAM_SHA1_DIGEST:
#ifdef HAVE_SHA2
   case STREAM_SHA256_DIGEST:
   case STREAM_SHA512_DIGEST:
#endif
#ifdef HAVE_CRYPTO
   case STREAM_SIGNED_DIGEST:
   case STREAM_ENCRYPTED_FILE_DATA:
   case STREAM_ENCRYPTED_FILE_GZIP_DATA:
   case STREAM_ENCRYPTED_WIN32_DATA:
   case STREAM_ENCRYPTED_WIN32_GZIP_DATA:
#ifdef HAVE_LZO
   case STREAM_ENCRYPTED_FILE_COMPRESSED_DATA:
   case STREAM_ENCRYPTED_WIN32_COMPRESSED_DATA:
#endif
#endif     /* !HAVE_CRYPTO */
   case 0:                            /* compatibility with old tapes */
      return true;
   }
   return false;
}

HANDLE bget_handle(BFILE *bfd)
{
   return bfd->fh;
}

/*
 * The following code was contributed by Graham Keeling from his
 *  burp project.  August 2014
 */
static int encrypt_bopen(BFILE *bfd, const char *fname, uint64_t flags, mode_t mode)
{
   ULONG ulFlags = 0;
   POOLMEM *win32_fname;
   POOLMEM *win32_fname_wchar;

   bfd->mode = BF_CLOSED;
   bfd->fid = -1;

   if (!(p_OpenEncryptedFileRawA || p_OpenEncryptedFileRawW)) {
      Dmsg0(50, "No OpenEncryptedFileRawA and no OpenEncryptedFileRawW APIs!!!\n");
      return -1;
   }

   /* Convert to Windows path format */
   win32_fname = get_pool_memory(PM_FNAME);
   win32_fname_wchar = get_pool_memory(PM_FNAME);

   unix_name_to_win32(&win32_fname, (char *)fname);

   if (p_CreateFileW && p_MultiByteToWideChar) {
      make_win32_path_UTF8_2_wchar(&win32_fname_wchar, fname);
   }

   if ((flags & O_CREAT) || (flags & O_WRONLY)) {
      ulFlags = CREATE_FOR_IMPORT | OVERWRITE_HIDDEN;
      if (bfd->fattrs & FILE_ATTRIBUTE_DIRECTORY) {
         mkdir(fname, 0777);
         ulFlags |= CREATE_FOR_DIR;
      }
      bfd->mode = BF_WRITE;
      Dmsg0(200, "encrypt_bopen for write.\n");
   } else {
      /* Open existing for read */
      ulFlags = CREATE_FOR_EXPORT;
      bfd->mode = BF_READ;
      Dmsg0(200, "encrypt_bopen for read.\n");
   }

   if (p_OpenEncryptedFileRawW && p_MultiByteToWideChar) {
      /* unicode open */
      if (p_OpenEncryptedFileRawW((LPCWSTR)win32_fname_wchar,
                   ulFlags, &(bfd->pvContext))) {
         bfd->mode = BF_CLOSED;
         errno = b_errno_win32;
         bfd->berrno = b_errno_win32;
      }
   } else {
      /* ascii open */
      if (p_OpenEncryptedFileRawA(win32_fname, ulFlags, &(bfd->pvContext))) {
         bfd->mode = BF_CLOSED;
         errno = b_errno_win32;
         bfd->berrno = b_errno_win32;
      }
   }
   free_pool_memory(win32_fname_wchar);
   free_pool_memory(win32_fname);
   bfd->fid = (bfd->mode == BF_CLOSED) ? -1 : 0;
   return bfd->mode==BF_CLOSED ? -1: 1;
}

static int encrypt_bclose(BFILE *bfd)
{
   Dmsg0(200, "encrypt_bclose\n");
   if (p_CloseEncryptedFileRaw) {
      p_CloseEncryptedFileRaw(bfd->pvContext);
   }
   bfd->pvContext = NULL;
   bfd->mode = BF_CLOSED;
   bfd->fattrs = 0;
   bfd->fid = -1;
   return 0;
}

/* Windows */
int bopen(BFILE *bfd, const char *fname, uint64_t flags, mode_t mode)
{
   POOLMEM *win32_fname;
   POOLMEM *win32_fname_wchar;

   DWORD dwaccess, dwflags, dwshare;

   if (bfd->fattrs & FILE_ATTRIBUTE_ENCRYPTED) {
      return encrypt_bopen(bfd, fname, flags, mode);
   }

   /* Convert to Windows path format */
   win32_fname = get_pool_memory(PM_FNAME);
   win32_fname_wchar = get_pool_memory(PM_FNAME);

   unix_name_to_win32(&win32_fname, (char *)fname);

   if (bfd->cmd_plugin && plugin_bopen) {
      int rtnstat;
      Dmsg1(50, "call plugin_bopen fname=%s\n", fname);
      rtnstat = plugin_bopen(bfd, fname, flags, mode);
      Dmsg1(50, "return from plugin_bopen status=%d\n", rtnstat);
      if (rtnstat >= 0) {
         if (flags & O_CREAT || flags & O_WRONLY) {   /* Open existing for write */
            Dmsg1(50, "plugin_open for write OK file=%s.\n", fname);
            bfd->mode = BF_WRITE;
         } else {
            Dmsg1(50, "plugin_open for read OK file=%s.\n", fname);
            bfd->mode = BF_READ;
         }
         bfd->fid = -1;         /* The file descriptor is invalid */
      } else {
         bfd->mode = BF_CLOSED;
         bfd->fid = -1;
         Dmsg1(000, "==== plugin_bopen returned bad status=%d\n", rtnstat);
      }
      free_pool_memory(win32_fname_wchar);
      free_pool_memory(win32_fname);
      return bfd->mode == BF_CLOSED ? -1 : 1;
   }
   Dmsg0(100, "=== NO plugin\n");

   if (!(p_CreateFileA || p_CreateFileW)) {
      Dmsg0(50, "No CreateFileA and no CreateFileW!!!!!\n");
      return 0;
   }

   if (p_CreateFileW && p_MultiByteToWideChar) {
      make_win32_path_UTF8_2_wchar(&win32_fname_wchar, fname);
   }

   if (flags & O_CREAT) {             /* Create */
      if (bfd->use_backup_api) {
         dwaccess = GENERIC_WRITE|FILE_ALL_ACCESS|WRITE_OWNER|WRITE_DAC|ACCESS_SYSTEM_SECURITY;
         dwflags = FILE_FLAG_BACKUP_SEMANTICS;
      } else {
         dwaccess = GENERIC_WRITE;
         dwflags = 0;
      }

      if (p_CreateFileW && p_MultiByteToWideChar) {
         // unicode open for create write
         Dmsg1(100, "Create CreateFileW=%ls\n", win32_fname_wchar);
         bfd->fh = p_CreateFileW((LPCWSTR)win32_fname_wchar,
                dwaccess,                /* Requested access */
                0,                       /* Shared mode */
                NULL,                    /* SecurityAttributes */
                CREATE_ALWAYS,           /* CreationDisposition */
                dwflags,                 /* Flags and attributes */
                NULL);                   /* TemplateFile */
      } else {
         // ascii open
         Dmsg1(100, "Create CreateFileA=%s\n", win32_fname);
         bfd->fh = p_CreateFileA(win32_fname,
                dwaccess,                /* Requested access */
                0,                       /* Shared mode */
                NULL,                    /* SecurityAttributes */
                CREATE_ALWAYS,           /* CreationDisposition */
                dwflags,                 /* Flags and attributes */
                NULL);                   /* TemplateFile */
      }

      bfd->mode = BF_WRITE;

   } else if (flags & O_WRONLY) {     /* Open existing for write */
      if (bfd->use_backup_api) {
         dwaccess = GENERIC_READ|GENERIC_WRITE|WRITE_OWNER|WRITE_DAC;
         /* If deduped we do not want to open the reparse point */
         if (bfd->fattrs & FILE_ATTRIBUTE_DEDUP) {
            dwflags = FILE_FLAG_BACKUP_SEMANTICS;
         } else {
            dwflags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
         }
      } else {
         dwaccess = GENERIC_READ|GENERIC_WRITE;
         dwflags = 0;
      }

      if (p_CreateFileW && p_MultiByteToWideChar) {
         // unicode open for open existing write
         Dmsg1(100, "Write only CreateFileW=%s\n", win32_fname);
         bfd->fh = p_CreateFileW((LPCWSTR)win32_fname_wchar,
                dwaccess,                /* Requested access */
                0,                       /* Shared mode */
                NULL,                    /* SecurityAttributes */
                OPEN_EXISTING,           /* CreationDisposition */
                dwflags,                 /* Flags and attributes */
                NULL);                   /* TemplateFile */
      } else {
         // ascii open
         Dmsg1(100, "Write only CreateFileA=%s\n", win32_fname);
         bfd->fh = p_CreateFileA(win32_fname,
                dwaccess,                /* Requested access */
                0,                       /* Shared mode */
                NULL,                    /* SecurityAttributes */
                OPEN_EXISTING,           /* CreationDisposition */
                dwflags,                 /* Flags and attributes */
                NULL);                   /* TemplateFile */

      }

      bfd->mode = BF_WRITE;

   } else {                           /* Read */
      if (bfd->use_backup_api) {
         dwaccess = GENERIC_READ|READ_CONTROL|ACCESS_SYSTEM_SECURITY;
         dwflags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN |
                   FILE_FLAG_OPEN_REPARSE_POINT;
         dwshare = FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE;
      } else {
         dwaccess = GENERIC_READ;
         dwflags = 0;
         dwshare = FILE_SHARE_READ|FILE_SHARE_WRITE;
      }

      if (p_CreateFileW && p_MultiByteToWideChar) {
         // unicode open for open existing read
         Dmsg1(100, "Read CreateFileW=%ls\n", win32_fname_wchar);
         bfd->fh = p_CreateFileW((LPCWSTR)win32_fname_wchar,
                dwaccess,                /* Requested access */
                dwshare,                 /* Share modes */
                NULL,                    /* SecurityAttributes */
                OPEN_EXISTING,           /* CreationDisposition */
                dwflags,                 /* Flags and attributes */
                NULL);                   /* TemplateFile */
      } else {
         // ascii open
         Dmsg1(100, "Read CreateFileA=%s\n", win32_fname);
         bfd->fh = p_CreateFileA(win32_fname,
                dwaccess,                /* Requested access */
                dwshare,                 /* Share modes */
                NULL,                    /* SecurityAttributes */
                OPEN_EXISTING,           /* CreationDisposition */
                dwflags,                 /* Flags and attributes */
                NULL);                   /* TemplateFile */
      }

      bfd->mode = BF_READ;
   }

   if (bfd->fh == INVALID_HANDLE_VALUE) {
      berrno be;
      bfd->lerror = GetLastError();
      bfd->berrno = b_errno_win32;
      errno = b_errno_win32;
      bfd->mode = BF_CLOSED;
      Dmsg1(100, "Open failed: %s\n", be.bstrerror());
   }
   bfd->block = 0;
   bfd->total_bytes = 0;
   bfd->errmsg = NULL;
   bfd->lpContext = NULL;
   bfd->win32filter.init();
   free_pool_memory(win32_fname_wchar);
   free_pool_memory(win32_fname);
   bfd->fid = (bfd->mode == BF_CLOSED) ? -1 : 0;
   return bfd->mode == BF_CLOSED ? -1 : 1;
}

/*
 * Returns  0 on success
 *         -1 on error
 */
/* Windows */
int bclose(BFILE *bfd)
{
   int stat = 0;

   if (bfd->mode == BF_CLOSED) {
      Dmsg0(50, "=== BFD already closed.\n");
      return 0;
   }

   if (bfd->cmd_plugin && plugin_bclose) {
      stat = plugin_bclose(bfd);
      Dmsg0(50, "==== BFD closed!!!\n");
      goto all_done;
   }

   if (bfd->fattrs & FILE_ATTRIBUTE_ENCRYPTED) {
      return encrypt_bclose(bfd);
   }

   /*
    * We need to tell the API to release the buffer it
    *  allocated in lpContext.  We do so by calling the
    *  API one more time, but with the Abort bit set.
    */
   if (bfd->use_backup_api && bfd->mode == BF_READ) {
      BYTE buf[10];
      if (bfd->lpContext && !p_BackupRead(bfd->fh,
              buf,                    /* buffer */
              (DWORD)0,               /* bytes to read */
              &bfd->rw_bytes,         /* bytes read */
              1,                      /* Abort */
              1,                      /* ProcessSecurity */
              &bfd->lpContext)) {     /* Read context */
         errno = b_errno_win32;
         stat = -1;
      }
   } else if (bfd->use_backup_api && bfd->mode == BF_WRITE) {
      BYTE buf[10];
      if (bfd->lpContext && !p_BackupWrite(bfd->fh,
              buf,                    /* buffer */
              (DWORD)0,               /* bytes to read */
              &bfd->rw_bytes,         /* bytes written */
              1,                      /* Abort */
              1,                      /* ProcessSecurity */
              &bfd->lpContext)) {     /* Write context */
         errno = b_errno_win32;
         stat = -1;
      }
   }
   if (!CloseHandle(bfd->fh)) {
      stat = -1;
      errno = b_errno_win32;
   }

all_done:
   if (bfd->errmsg) {
      free_pool_memory(bfd->errmsg);
      bfd->errmsg = NULL;
   }
   bfd->mode = BF_CLOSED;
   bfd->fattrs = 0;
   bfd->fid = -1;
   bfd->lpContext = NULL;
   bfd->cmd_plugin = false;
   return stat;
}

/* Returns: bytes read on success
 *           0         on EOF
 *          -1         on error
 */
/* Windows */
ssize_t bread(BFILE *bfd, void *buf, size_t count)
{
   bfd->rw_bytes = 0;

   if (bfd->cmd_plugin && plugin_bread) {
      return plugin_bread(bfd, buf, count);
   }

   if (bfd->use_backup_api) {
      if (!p_BackupRead(bfd->fh,
           (BYTE *)buf,
           count,
           &bfd->rw_bytes,
           0,                           /* no Abort */
           1,                           /* Process Security */
           &bfd->lpContext)) {          /* Context */
         berrno be;
         bfd->lerror = GetLastError();
         bfd->berrno = b_errno_win32;
         errno = b_errno_win32;
         Dmsg1(100, "Read failed: %s\n", be.bstrerror());
         return -1;
      }
   } else {
      if (!ReadFile(bfd->fh,
           buf,
           count,
           &bfd->rw_bytes,
           NULL)) {
         bfd->lerror = GetLastError();
         bfd->berrno = b_errno_win32;
         errno = b_errno_win32;
         return -1;
      }
   }
   bfd->block++;
   if (bfd->rw_bytes > 0) {
      bfd->total_bytes += bfd->rw_bytes;
   }
   return (ssize_t)bfd->rw_bytes;
}

/* Windows */
ssize_t bwrite(BFILE *bfd, void *buf, size_t count)
{
   bfd->rw_bytes = 0;

   if (bfd->cmd_plugin && plugin_bwrite) {
      return plugin_bwrite(bfd, buf, count);
   }

   if (bfd->use_backup_api) {
      if (!p_BackupWrite(bfd->fh,
           (BYTE *)buf,
           count,
           &bfd->rw_bytes,
           0,                           /* No abort */
           1,                           /* Process Security */
           &bfd->lpContext)) {          /* Context */
         berrno be;
         bfd->lerror = GetLastError();
         bfd->berrno = b_errno_win32;
         errno = b_errno_win32;
         Dmsg1(100, "Write failed: %s\n", be.bstrerror());
         return -1;
      }
   } else {
      if (!WriteFile(bfd->fh,
           buf,
           count,
           &bfd->rw_bytes,
           NULL)) {
         bfd->lerror = GetLastError();
         bfd->berrno = b_errno_win32;
         errno = b_errno_win32;
         return -1;
      }
   }
   bfd->block++;
   if (bfd->rw_bytes > 0) {
      bfd->total_bytes += bfd->rw_bytes;
   }
   return (ssize_t)bfd->rw_bytes;
}

/* Windows */
bool is_bopen(BFILE *bfd)
{
   return bfd->mode != BF_CLOSED;
}

/* Windows */
boffset_t blseek(BFILE *bfd, boffset_t offset, int whence)
{
   LONG  offset_low = (LONG)offset;
   LONG  offset_high = (LONG)(offset >> 32);
   DWORD dwResult;

   if (bfd->cmd_plugin && plugin_blseek) {
      return plugin_blseek(bfd, offset, whence);
   }

   dwResult = SetFilePointer(bfd->fh, offset_low, &offset_high, whence);

   if (dwResult == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR) {
      return (boffset_t)-1;
   }

   return ((boffset_t)offset_high << 32) | dwResult;
}

#else  /* Unix systems */

/* ===============================================================
 *
 *            U N I X
 *
 * ===============================================================
 */
/* Unix */
void binit(BFILE *bfd)
{
   memset(bfd, 0, sizeof(BFILE));
   bfd->fid = -1;
}

/* Unix */
bool have_win32_api()
{
   return false;                       /* no can do */
}

/*
 * Enables using the Backup API (win32_data).
 *   Returns true  if function worked
 *   Returns false if failed (i.e. do not have Backup API on this machine)
 */
/* Unix */
bool set_win32_backup(BFILE *bfd)
{
   return false;                       /* no can do */
}


/* Unix */
bool set_portable_backup(BFILE *bfd)
{
   return true;                        /* no problem */
}

bool is_plugin_data(BFILE *bfd)
{
   return bfd->cmd_plugin;
}

/*
 * Return true  if we are writing in portable format
 * return false if not
 */
/* Unix */
bool is_portable_backup(BFILE *bfd)
{
   return true;                       /* portable by definition */
}

/* Unix */
bool set_prog(BFILE *bfd, char *prog, JCR *jcr)
{
   return false;
}

/* Unix */
bool set_cmd_plugin(BFILE *bfd, JCR *jcr)
{
   bfd->cmd_plugin = true;
   bfd->jcr = jcr;
   return true;
}

/*
 * This code is running on a non-Win32 machine
 */
/* Unix */
bool is_restore_stream_supported(int stream)
{
   /* No Win32 backup on this machine */
     switch (stream) {
#ifndef HAVE_LIBZ
   case STREAM_GZIP_DATA:
   case STREAM_SPARSE_GZIP_DATA:
   case STREAM_WIN32_GZIP_DATA:
#endif
#ifndef HAVE_LZO
   case STREAM_COMPRESSED_DATA:
   case STREAM_SPARSE_COMPRESSED_DATA:
   case STREAM_WIN32_COMPRESSED_DATA:
   case STREAM_ENCRYPTED_FILE_COMPRESSED_DATA:
   case STREAM_ENCRYPTED_WIN32_COMPRESSED_DATA:
#endif
#ifndef HAVE_DARWIN_OS
   case STREAM_MACOS_FORK_DATA:
   case STREAM_HFSPLUS_ATTRIBUTES:
#endif
      return false;

   /* Known streams */
#ifdef HAVE_LIBZ
   case STREAM_GZIP_DATA:
   case STREAM_SPARSE_GZIP_DATA:
   case STREAM_WIN32_GZIP_DATA:
#endif
#ifdef HAVE_LZO
   case STREAM_COMPRESSED_DATA:
   case STREAM_SPARSE_COMPRESSED_DATA:
   case STREAM_WIN32_COMPRESSED_DATA:
   case STREAM_ENCRYPTED_FILE_COMPRESSED_DATA:
   case STREAM_ENCRYPTED_WIN32_COMPRESSED_DATA:
#endif
   case STREAM_WIN32_DATA:
   case STREAM_UNIX_ATTRIBUTES:
   case STREAM_FILE_DATA:
   case STREAM_MD5_DIGEST:
   case STREAM_UNIX_ATTRIBUTES_EX:
   case STREAM_SPARSE_DATA:
   case STREAM_PROGRAM_NAMES:
   case STREAM_PROGRAM_DATA:
   case STREAM_SHA1_DIGEST:
#ifdef HAVE_SHA2
   case STREAM_SHA256_DIGEST:
   case STREAM_SHA512_DIGEST:
#endif
#ifdef HAVE_CRYPTO
   case STREAM_SIGNED_DIGEST:
   case STREAM_ENCRYPTED_FILE_DATA:
   case STREAM_ENCRYPTED_FILE_GZIP_DATA:
   case STREAM_ENCRYPTED_WIN32_DATA:
   case STREAM_ENCRYPTED_WIN32_GZIP_DATA:
#endif
#ifdef HAVE_DARWIN_OS
   case STREAM_MACOS_FORK_DATA:
   case STREAM_HFSPLUS_ATTRIBUTES:
#ifdef HAVE_CRYPTO
   case STREAM_ENCRYPTED_MACOS_FORK_DATA:
#endif /* HAVE_CRYPTO */
#endif /* HAVE_DARWIN_OS */
   case 0:   /* compatibility with old tapes */
      return true;

   }
   return false;
}

/* Unix */
int bopen(BFILE *bfd, const char *fname, uint64_t flags, mode_t mode)
{
   if (bfd->cmd_plugin && plugin_bopen) {
      Dmsg1(400, "call plugin_bopen fname=%s\n", fname);
      bfd->fid = plugin_bopen(bfd, fname, flags, mode);
      Dmsg2(400, "Plugin bopen fid=%d file=%s\n", bfd->fid, fname);
      return bfd->fid;
   }

   /* Normal file open */
   Dmsg1(dbglvl, "open file %s\n", fname);

   /* We use fnctl to set O_NOATIME if requested to avoid open error */
   bfd->fid = open(fname, (flags | O_CLOEXEC) & ~O_NOATIME, mode);

   /* Set O_NOATIME if possible */
   if (bfd->fid != -1 && flags & O_NOATIME) {
      int oldflags = fcntl(bfd->fid, F_GETFL, 0);
      if (oldflags == -1) {
         bfd->berrno = errno;
         close(bfd->fid);
         bfd->fid = -1;
      } else {
         int ret = fcntl(bfd->fid, F_SETFL, oldflags | O_NOATIME);
        /* EPERM means setting O_NOATIME was not allowed  */
         if (ret == -1 && errno != EPERM) {
            bfd->berrno = errno;
            close(bfd->fid);
            bfd->fid = -1;
         }
      }
   }
   bfd->berrno = errno;
   bfd->m_flags = flags;
   bfd->block = 0;
   bfd->total_bytes = 0;
   Dmsg1(400, "Open file %d\n", bfd->fid);
   errno = bfd->berrno;

   bfd->win32filter.init();

#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_WILLNEED)
   /* If not RDWR or WRONLY must be Read Only */
   if (bfd->fid != -1 && !(flags & (O_RDWR|O_WRONLY))) {
      int stat = posix_fadvise(bfd->fid, 0, 0, POSIX_FADV_WILLNEED);
      Dmsg3(400, "Did posix_fadvise WILLNEED on %s fid=%d stat=%d\n", fname, bfd->fid, stat);
   }
#endif

   return bfd->fid;
}

#ifdef HAVE_DARWIN_OS
/* Open the resource fork of a file. */
int bopen_rsrc(BFILE *bfd, const char *fname, uint64_t flags, mode_t mode)
{
   POOLMEM *rsrc_fname;

   rsrc_fname = get_pool_memory(PM_FNAME);
   pm_strcpy(rsrc_fname, fname);
   pm_strcat(rsrc_fname, _PATH_RSRCFORKSPEC);
   bopen(bfd, rsrc_fname, flags, mode);
   free_pool_memory(rsrc_fname);
   return bfd->fid;
}
#else /* Unix */

/* Unix */
int bopen_rsrc(BFILE *bfd, const char *fname, uint64_t flags, mode_t mode)
    { return -1; }

#endif


/* Unix */
int bclose(BFILE *bfd)
{
   int stat;

   Dmsg2(400, "Close bfd=%p file %d\n", bfd, bfd->fid);

   if (bfd->fid == -1) {
      return 0;
   }
   if (bfd->cmd_plugin && plugin_bclose) {
      stat = plugin_bclose(bfd);
      bfd->fid = -1;
      bfd->cmd_plugin = false;
   }

#if defined(HAVE_POSIX_FADVISE) && defined(POSIX_FADV_DONTNEED)
   /* If not RDWR or WRONLY must be Read Only */
   if (!(bfd->m_flags & (O_RDWR|O_WRONLY))) {
      fdatasync(bfd->fid);            /* sync the file */
      /* Tell OS we don't need it any more */
      posix_fadvise(bfd->fid, 0, 0, POSIX_FADV_DONTNEED);
      Dmsg1(400, "Did posix_fadvise DONTNEED on fid=%d\n", bfd->fid);
   }
#endif

   /* Close normal file */
   stat = close(bfd->fid);
   bfd->berrno = errno;
   bfd->fid = -1;
   bfd->cmd_plugin = false;
   return stat;
}

/* Unix */
ssize_t bread(BFILE *bfd, void *buf, size_t count)
{
   ssize_t stat;

   if (bfd->cmd_plugin && plugin_bread) {
      return plugin_bread(bfd, buf, count);
   }

   stat = read(bfd->fid, buf, count);
   bfd->berrno = errno;
   bfd->block++;
   if (stat > 0) {
      bfd->total_bytes += stat;
   }
   return stat;
}

/* Unix */
ssize_t bwrite(BFILE *bfd, void *buf, size_t count)
{
   ssize_t stat;

   if (bfd->cmd_plugin && plugin_bwrite) {
      return plugin_bwrite(bfd, buf, count);
   }
   stat = write(bfd->fid, buf, count);
   bfd->berrno = errno;
   bfd->block++;
   if (stat > 0) {
      bfd->total_bytes += stat;
   }
   return stat;
}

/* Unix */
bool is_bopen(BFILE *bfd)
{
   return bfd->fid >= 0;
}

/* Unix */
boffset_t blseek(BFILE *bfd, boffset_t offset, int whence)
{
   boffset_t pos;

   if (bfd->cmd_plugin && plugin_bwrite) {
      return plugin_blseek(bfd, offset, whence);
   }
   pos = (boffset_t)lseek(bfd->fid, offset, whence);
   bfd->berrno = errno;
   return pos;
}

#endif
