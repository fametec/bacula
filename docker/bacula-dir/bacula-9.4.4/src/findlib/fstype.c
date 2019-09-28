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
 *  Implement routines to determine file system types.
 *
 *   Written by Preben 'Peppe' Guldberg, December MMIV
 *   Updated by Kern Sibbald, April MMXV
 */


#ifndef TEST_PROGRAM

#include "bacula.h"
#include "find.h"
#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_SUN_OS
  #include <sys/mnttab.h>
#endif
#else /* Set up for testing a stand alone program */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define bstrncpy           strncpy
#define Dmsg0(n,s)         fprintf(stderr, s)
#define Dmsg1(n,s,a1)      fprintf(stderr, s, a1)
#define Dmsg2(n,s,a1,a2)   fprintf(stderr, s, a1, a2)
#endif

#define is_rootfs(x) bstrcmp("rootfs", x)

#if defined(HAVE_GETMNTINFO) || defined(HAVE_GETMNTENT)
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

struct mtab_item {
   rblink link;
   uint64_t dev;
   char fstype[1];
};

/* Compare two device types */
static int compare_mtab_items(void *item1, void *item2)
{
   mtab_item *mtab1, *mtab2;
   mtab1 = (mtab_item *)item1;
   mtab2 = (mtab_item *)item2;
   if (mtab1->dev < mtab2->dev) return -1;
   if (mtab1->dev > mtab2->dev) return 1;
   return 0;
}

void add_mtab_item(void *user_ctx, struct stat *st, const char *fstype,
                      const char *mountpoint, const char *mntopts,
                      const char *fsname)
{
   rblist *mtab_list = (rblist *)user_ctx;
   mtab_item *item, *ritem;
   int len = strlen(fstype) + 1;
   
   item = (mtab_item *)malloc(sizeof(mtab_item) + len);
   item->dev = (uint64_t)st->st_dev;
   bstrncpy(item->fstype, fstype, len);
   ritem = (mtab_item *)mtab_list->insert((void *)item, compare_mtab_items);
   if (ritem != item) {
      /* Item already inserted, so we discard this one */
      free(item);
   }
}


/*
 * These functions should be implemented for each OS
 *
 *       bool fstype(FF_PKT *ff_pkt, char *fs, int fslen);
 */
#if defined(HAVE_DARWIN_OS) \
   || defined(HAVE_FREEBSD_OS ) \
   || defined(HAVE_KFREEBSD_OS ) \
   || defined(HAVE_OPENBSD_OS)

#include <sys/param.h>
#include <sys/mount.h>

bool fstype(FF_PKT *ff_pkt, char *fs, int fslen)
{
   char *fname = ff_pkt->fname;
   struct statfs st;
 
   if (statfs(fname, &st) == 0) {
      bstrncpy(fs, st.f_fstypename, fslen);
      return true;
   }
   Dmsg1(50, "statfs() failed for \"%s\"\n", fname);
   return false;
}
#elif defined(HAVE_NETBSD_OS)
#include <sys/param.h>
#include <sys/mount.h>
#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#else
#define statvfs statfs
#endif

bool fstype(FF_PKT *ff_pkt, char *fs, int fslen)
{
   char *fname = ff_pkt->fname;
   struct statvfs st;
   if (statvfs(fname, &st) == 0) {
      bstrncpy(fs, st.f_fstypename, fslen);
      return true;
   }
   Dmsg1(50, "statfs() failed for \"%s\"\n", fname);
   return false;
}
#elif defined(HAVE_HPUX_OS) \
   || defined(HAVE_IRIX_OS)

#include <sys/types.h>
#include <sys/statvfs.h>

bool fstype(FF_PKT *ff_pkt, char *fs, int fslen)
{
   char *fname = ff_pkt->fname;
   struct statvfs st;
   if (statvfs(fname, &st) == 0) {
      bstrncpy(fs, st.f_basetype, fslen);
      return true;
   }
   Dmsg1(50, "statfs() failed for \"%s\"\n", fname);
   return false;
}

#elif defined(HAVE_LINUX_OS)

#include <sys/vfs.h>
#include <mntent.h>

/*
 * Linux statfs() does not return the filesystem name type.  It
 *  only returns a binary fstype, so we must look up the type name
 *  in mtab.
 */
bool fstype(FF_PKT *ff_pkt, char *fs, int fslen)
{
   char *fname = ff_pkt->fname;
   struct statfs st;
   const char *fstype;

   if (statfs(fname, &st) == 0) {
      mtab_item *item, search_item;
      if (*ff_pkt->last_fstypename && ff_pkt->last_fstype == (uint64_t)st.f_type) {
         bstrncpy(fs, ff_pkt->last_fstypename, fslen);
         return true; 
      }
      if (!ff_pkt->mtab_list) {
         ff_pkt->mtab_list = New(rblist());
         read_mtab(add_mtab_item, ff_pkt->mtab_list);
      }
      search_item.dev = st.f_type;
      item = (mtab_item *)ff_pkt->mtab_list->search((void *)&search_item, compare_mtab_items);
      if (item) {
         ff_pkt->last_fstype = st.f_type;
         bstrncpy(ff_pkt->last_fstypename, item->fstype, sizeof(ff_pkt->last_fstypename));
         bstrncpy(fs, ff_pkt->last_fstypename, fslen);
         return true;
      }
      /*
       * Values obtained from statfs(2), testing and
       *
       *    $ grep -r SUPER_MAGIC /usr/include/linux
       */
      switch (st.f_type) {
      /* Known good values */
      /* ext2, ext3, and ext4 have the same code */
      case 0xef53:         fstype = "ext2"; break;          /* EXT2_SUPER_MAGIC */
      case 0x3153464a:     fstype = "jfs"; break;           /* JFS_SUPER_MAGIC */
      case 0x5346544e:     fstype = "ntfs"; break;          /* NTFS_SB_MAGIC */
      case 0x9fa0:         fstype = "proc"; break;          /* PROC_SUPER_MAGIC */
      case 0x52654973:     fstype = "reiserfs"; break;      /* REISERFS_SUPER_MAGIC */
      case 0x58465342:     fstype = "xfs"; break;           /* XFS_SB_MAGIC */
      case 0x9fa2:         fstype = "usbdevfs"; break;      /* USBDEVICE_SUPER_MAGIC */
      case 0x62656572:     fstype = "sysfs"; break;         /* SYSFS_MAGIC */
      case 0x517B:         fstype = "smbfs"; break;         /* SMB_SUPER_MAGIC */
      case 0x9660:         fstype = "iso9660"; break;       /* ISOFS_SUPER_MAGIC */
      case 0xadf5:         fstype = "adfs"; break;          /* ADFS_SUPER_MAGIC */
      case 0xadff:         fstype = "affs"; break;          /* AFFS_SUPER_MAGIC */
      case 0x42465331:     fstype = "befs"; break;          /* BEFS_SUPER_MAGIC */
      case 0xFF534D42:     fstype = "cifs"; break;          /* CIFS_MAGIC_NUMBER */
      case 0x73757245:     fstype = "coda"; break;          /* CODA_SUPER_MAGIC */
      case 0x012ff7b7:     fstype = "coherent"; break;      /* COH_SUPER_MAGIC */
      case 0x28cd3d45:     fstype = "cramfs"; break;        /* CRAMFS_MAGIC */
      case 0x1373:         fstype = "devfs"; break;         /* DEVFS_SUPER_MAGIC */
      case 0x414A53:       fstype = "efs"; break;           /* EFS_SUPER_MAGIC */
      case 0x137d:         fstype = "ext"; break;           /* EXT_SUPER_MAGIC */
      case 0xef51:         fstype = "oldext2"; break;          /* EXT2_OLD_SUPER_MAGIC */
      case 0x4244:         fstype = "hfs"; break;          /* EXT2_OLD_SUPER_MAGIC */
      case 0xf995e849:     fstype = "hpfs"; break;          /* HPFS_SUPER_MAGIC */
      case 0x958458f6:     fstype = "hugetlbfs"; break;     /* HUGETLBFS_MAGIC */
      case 0x72b6:         fstype = "jffs2"; break;         /* JFFS2_SUPER_MAGIC */
      case 0x2468:         fstype = "minix"; break;         /* MINIX2_SUPER_MAGIC */
      case 0x2478:         fstype = "minix"; break;         /* MINIX2_SUPER_MAGIC2 */
      case 0x137f:         fstype = "minix"; break;         /* MINIX_SUPER_MAGIC */
      case 0x138f:         fstype = "minix"; break;         /* MINIX_SUPER_MAGIC2 */
      case 0x4d44:         fstype = "msdos"; break;         /* MSDOS_SUPER_MAGIC */
      case 0x564c:         fstype = "ncpfs"; break;         /* NCP_SUPER_MAGIC */
      case 0x6969:         fstype = "nfs"; break;           /* NFS_SUPER_MAGIC */
      case 0x9fa1:         fstype = "openpromfs"; break;    /* OPENPROM_SUPER_MAGIC */
      case 0x002f:         fstype = "qnx4"; break;          /* QNX4_SUPER_MAGIC */
      case 0x7275:         fstype = "romfs"; break;          /* QNX4_SUPER_MAGIC */
      case 0x012ff7b6:     fstype = "sysv2"; break;
      case 0x012ff7b5:     fstype = "sysv4"; break;
      case 0x01021994:     fstype = "tmpfs"; break;
      case 0x15013346:     fstype = "udf"; break;
      case 0x00011954:     fstype = "ufs"; break;
      case 0xa501FCF5:     fstype = "vxfs"; break;
      case 0x012FF7B4:     fstype = "xenix"; break;
      case 0x012FD16D:     fstype = "xiafs"; break;
      case 0x9123683e:     fstype = "btrfs"; break;
      case 0x7461636f:     fstype = "ocfs2"; break;         /* OCFS2_SUPER_MAGIC */

#if 0       /* These need confirmation */
      case 0x6B414653:     fstype = "afs"; break;           /* AFS_FS_MAGIC */
      case 0x0187:         fstype = "autofs"; break;        /* AUTOFS_SUPER_MAGIC */
      case 0x62646576:     fstype = "bdev"; break;          /* ??? */
      case 0x1BADFACE:     fstype = "bfs"; break;           /* BFS_MAGIC */
      case 0x42494e4d:     fstype = "binfmt_misc"; break;   /* ??? */
      case (('C'<<8)|'N'): fstype = "capifs"; break;        /* CAPIFS_SUPER_MAGIC */
      case 0x1cd1:         fstype = "devpts"; break;        /* ??? */
      case 0x03111965:     fstype = "eventpollfs"; break;   /* EVENTPOLLFS_MAGIC */
      case 0xBAD1DEA:      fstype = "futexfs"; break;       /* ??? */
      case 0xaee71ee7:     fstype = "gadgetfs"; break;      /* GADGETFS_MAGIC */
      case 0x00c0ffee:     fstype = "hostfs"; break;        /* HOSTFS_SUPER_MAGIC */
      case 0xb00000ee:     fstype = "hppfs"; break;         /* HPPFS_SUPER_MAGIC */
      case 0x12061983:     fstype = "hwgfs"; break;         /* HWGFS_MAGIC */
      case 0x66726f67:     fstype = "ibmasmfs"; break;      /* IBMASMFS_MAGIC */
      case 0x19800202:     fstype = "mqueue"; break;        /* MQUEUE_MAGIC */
      case 0x6f70726f:     fstype = "oprofilefs"; break;    /* OPROFILEFS_MAGIC */
      case 0xa0b4d889:     fstype = "pfmfs"; break;         /* PFMFS_MAGIC */
      case 0x50495045:     fstype = "pipfs"; break;         /* PIPEFS_MAGIC */
      case 0x858458f6:     fstype = "ramfs"; break;         /* RAMFS_MAGIC */
      case 0x7275:         fstype = "romfs"; break;         /* ROMFS_MAGIC */
      case 0x858458f6:     fstype = "rootfs"; break;        /* RAMFS_MAGIC */
      case 0x67596969:     fstype = "rpc_pipefs"; break;    /* RPCAUTH_GSSMAGIC */
      case 0x534F434B:     fstype = "sockfs"; break;        /* SOCKFS_MAGIC */
      case 0x858458f6:     fstype = "tmpfs"; break;         /* RAMFS_MAGIC */
      case 0x01021994:     fstype = "tmpfs"; break;         /* TMPFS_MAGIC */
#endif

      default:
         Dmsg2(10, "Unknown file system type \"0x%x\" for \"%s\".\n", st.f_type,
               fname);
         return false;
      }
      ff_pkt->last_fstype = st.f_type;
      bstrncpy(ff_pkt->last_fstypename, fstype, sizeof(ff_pkt->last_fstypename));
      bstrncpy(fs, fstype, fslen);
      return true;
   }
   Dmsg1(50, "statfs() failed for \"%s\"\n", fname);
   return false;
}

#elif defined(HAVE_SUN_OS)

#include <sys/types.h>
#include <sys/stat.h>

bool fstype(FF_PKT *ff_pkt, char *fs, int fslen)
{
   /* Solaris has the filesystem type name in the lstat packet */
   bstrncpy(fs, ff_pkt->statp.st_fstype, fslen);
   return true;
}
 
#elif defined (__digital__) && defined (__unix__)  /* Tru64 */
/* Tru64 */
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/mnttab.h>

bool fstype(FF_PKT *ff_pkt, char *fs, int fslen)
{
   char *fname = ff_pkt->fname;
   struct statfs st;
   if (statfs((char *)fname, &st) == 0) {
      switch (st.f_type) {
      /* Known good values */
      case 0xa:         bstrncpy(fs, "advfs", fslen); return true;        /* Tru64 AdvFS */
      case 0xe:         bstrncpy(fs, "nfs", fslen); return true;          /* Tru64 NFS   */
      default:
         Dmsg2(10, "Unknown file system type \"0x%x\" for \"%s\".\n", st.f_type,
               fname);
         return false;
      }
   }
   Dmsg1(50, "statfs() failed for \"%s\"\n", fname);
   return false;
}
/* Tru64 */

#elif defined (HAVE_WIN32)
/* Windows */
bool fstype(FF_PKT *ff_pkt, char *fs, int fslen)
{
   char *fname = ff_pkt->fname;
   DWORD componentlength;
   DWORD fsflags;
   CHAR rootpath[4];
   UINT oldmode;
   BOOL result;

   /* Copy Drive Letter, colon, and backslash to rootpath */
   bstrncpy(rootpath, fname, sizeof(rootpath));

   /* We don't want any popups if there isn't any media in the drive */
   oldmode = SetErrorMode(SEM_FAILCRITICALERRORS);
   result = GetVolumeInformation(rootpath, NULL, 0, NULL, &componentlength, &fsflags, fs, fslen);
   SetErrorMode(oldmode);

   if (result) {
      /* Windows returns NTFS, FAT, etc.  Make it lowercase to be consistent with other OSes */
      lcase(fs);
   } else {
      Dmsg2(10, "GetVolumeInformation() failed for \"%s\", Error = %d.\n", rootpath, GetLastError());
   }
   return result != 0;
}
/* Windows */

#else    /* No recognised OS */

bool fstype(FF_PKT *ff_pkt, char *fs, int fslen)
{
   char *fname = ff_pkt->fname;
   Dmsg0(10, "!!! fstype() not implemented for this OS. !!!\n");
   return false;
}
#endif

/* Read mtab entries  */
bool read_mtab(mtab_handler_t *mtab_handler, void *user_ctx)
{
/* Debian stretch GNU/KFreeBSD has both getmntinfo and getmntent, but
   only the first seems to work, so we skip over getmntent in this case */
#ifndef HAVE_KFREEBSD_OS
#ifdef HAVE_GETMNTENT
   FILE *mntfp;
   struct stat st;
 
#ifdef HAVE_LINUX_OS
   struct mntent *mnt;
   P(mutex);
   if ((mntfp = setmntent("/proc/mounts", "r")) == NULL) {
      if ((mntfp = setmntent(_PATH_MOUNTED, "r")) == NULL) {
         V(mutex);
         return false;
      }
   } 
   while ((mnt = getmntent(mntfp)) != NULL) {
      if (is_rootfs(mnt->mnt_type)) {
         continue;
      }

      if (stat(mnt->mnt_dir, &st) < 0) {
         continue;
      }
      mtab_handler(user_ctx, &st, mnt->mnt_type, mnt->mnt_dir,
         mnt->mnt_opts, mnt->mnt_fsname);
   }
   endmntent(mntfp);
   V(mutex);
#endif

#ifdef HAVE_SUN_OS
   struct mnttab mnt;

   P(mutex);
   if ((mntfp = bfopen(MNTTAB, "r")) == NULL) {
      V(mutex);
      return false;
   }

   while (getmntent(mntfp, &mnt) == 0) {
      if (is_rootfs(mnt.mnt_fstype)) {
         continue;
      }
      if (stat(mnt.mnt_mountp, &st) < 0) {
         continue;
      }
      mtab_handler(user_ctx, &st, mnt.mnt_fstype, mnt.mnt_mountp,
         mnt.mnt_mntopts, mnt.mnt_special);
   }
   fclose(mntfp);
   V(mutex);
#endif

#endif /* HAVE_GETMNTENT */
#endif /* HAVE_KFREEBSD_OS */

#ifdef HAVE_GETMNTINFO
   struct stat st;
#if defined(ST_NOWAIT)
   int flags = ST_NOWAIT;
#elif defined(MNT_NOWAIT)
   int flags = MNT_NOWAIT;
#else
   int flags = 0;
#endif
#if defined(HAVE_NETBSD_OS)
   struct statvfs *mntinfo;
#else
   struct statfs *mntinfo;
#endif
   int nument;

   P(mutex);
   if ((nument = getmntinfo(&mntinfo, flags)) > 0) {
      while (nument-- > 0) {
         if (is_rootfs(mntinfo->f_fstypename)) {
            continue;
         }
         if (stat(mntinfo->f_mntonname, &st) < 0) {
            continue;
         }
         mtab_handler(user_ctx, &st, mntinfo->f_mntfromname,
            mntinfo->f_mntonname, mntinfo->f_fstypename, NULL);
         mntinfo++;
      }
   }
   V(mutex);
#endif /* HAVE_GETMNTINFO */
   return true;
} 

#ifdef TEST_PROGRAM
int main(int argc, char **argv)
{
   char *p;
   char fs[1000];
   int status = 0;

   if (argc < 2) {
      p = (argc < 1) ? "fstype" : argv[0];
      printf("usage:\t%s path ...\n"
            "\t%s prints the file system type and pathname of the paths.\n",
            p, p);
      return EXIT_FAILURE;
   }
   while (*++argv) {
      if (!fstype(*argv, fs, sizeof(fs))) {
         status = EXIT_FAILURE;
      } else {
         printf("%s\t%s\n", fs, *argv);
      }
   }
   return status;
}
#endif
