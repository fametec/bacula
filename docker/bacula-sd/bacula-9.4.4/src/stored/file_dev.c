/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2017 Kern Sibbald

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
 *   file_dev.c  -- low level operations on file devices
 *
 *     written by, Kern Sibbald, MM
 *     separated from dev.c February 2014
 *
 */

#include "bacula.h"
#include "stored.h"

static const int dbglvl = 100;

/* Imported functions */
const char *mode_to_str(int mode);


/* default primitives are designed for file */
int DEVICE::d_open(const char *pathname, int flags)
{
   return ::open(pathname, flags | O_CLOEXEC);
}

int DEVICE::d_close(int fd)
{
   return ::close(fd);
}

int DEVICE::d_ioctl(int fd, ioctl_req_t request, char *mt_com)
{
#ifdef HAVE_WIN32
   return -1;
#else
   return ::ioctl(fd, request, mt_com);
#endif
}

ssize_t DEVICE::d_read(int fd, void *buffer, size_t count)
{
   return ::read(fd, buffer, count);
}

ssize_t DEVICE::d_write(int fd, const void *buffer, size_t count)
{
   return ::write(fd, buffer, count);
}

/* Rewind file device */
bool DEVICE::rewind(DCR *dcr)

{
   Enter(dbglvl);
   Dmsg3(400, "rewind res=%d fd=%d %s\n", num_reserved(), m_fd, print_name());
   state &= ~(ST_EOT|ST_EOF|ST_WEOT);  /* remove EOF/EOT flags */
   block_num = file = 0;
   file_size = 0;
   file_addr = 0;
   if (m_fd < 0) {
      Mmsg1(errmsg, _("Rewind failed: device %s is not open.\n"), print_name());
      return false;
   }
   if (is_file()) {
      if (lseek(dcr, (boffset_t)0, SEEK_SET) < 0) {
         berrno be;
         dev_errno = errno;
         Mmsg2(errmsg, _("lseek error on %s. ERR=%s.\n"),
            print_name(), be.bstrerror());
         return false;
      }
   }
   return true;
}

/*
 * Reposition the device to file, block
 * Returns: false on failure
 *          true  on success
 */
bool DEVICE::reposition(DCR *dcr, uint64_t raddr)
{
   if (!is_open()) {
      dev_errno = EBADF;
      Mmsg0(errmsg, _("Bad call to reposition. Device not open\n"));
      Emsg0(M_FATAL, 0, errmsg);
      return false;
   }

   Dmsg1(100, "===== lseek to %llu\n", raddr);
   if (lseek(dcr, (boffset_t)raddr, SEEK_SET) == (boffset_t)-1) {
      berrno be;
      dev_errno = errno;
      Mmsg2(errmsg, _("lseek error on %s. ERR=%s.\n"),
         print_name(), be.bstrerror());
      return false;
   }
   file_addr = raddr;
   return true;
}


/* Seek to specified place */
boffset_t DEVICE::lseek(DCR *dcr, boffset_t offset, int whence)
{
#if defined(HAVE_WIN32)
  return ::_lseeki64(m_fd, (__int64)offset, whence);
#else
  return ::lseek(m_fd, offset, whence);
#endif
}

/*
 * Open a file device. For Aligned type we open both Volumes
 */
bool file_dev::open_device(DCR *dcr, int omode)
{
   POOL_MEM archive_name(PM_FNAME);
   struct stat sp;

   Enter(dbglvl);
   if (DEVICE::open_device(dcr, omode)) {
      Leave(dbglvl);
      return true;
   }
   omode = openmode;

   get_autochanger_loaded_slot(dcr);

   /*
    * Handle opening of File Autochanger
    */

   pm_strcpy(archive_name, dev_name);
   /*
    * If this is a virtual autochanger (i.e. changer_res != NULL)
    *  we simply use the device name, assuming it has been
    *  appropriately setup by the "autochanger".
    */
   if (!device->changer_res || device->changer_command[0] == 0 ||
        strcmp(device->changer_command, "/dev/null") == 0) {
      if (VolCatInfo.VolCatName[0] == 0) {
         Mmsg(errmsg, _("Could not open file device %s. No Volume name given.\n"),
            print_name());
         if (dcr->jcr) {
            pm_strcpy(dcr->jcr->errmsg, errmsg);
         }
         clear_opened();
         Leave(dbglvl);
         return false;
      }

      /* If not /dev/null concatenate VolumeName */
      if (!is_null()) {
         if (!IsPathSeparator(archive_name.c_str()[strlen(archive_name.c_str())-1])) {
            pm_strcat(archive_name, "/");
         }
         pm_strcat(archive_name, getVolCatName());
      }
   }

   mount(1);                          /* do mount if required */

   set_mode(omode);
   /* If creating file, give 0640 permissions */
   Dmsg3(100, "open disk: mode=%s open(%s, 0x%x, 0640)\n", mode_to_str(omode),
         archive_name.c_str(), mode);
   /* Use system open() */
   if ((m_fd = ::open(archive_name.c_str(), mode|O_CLOEXEC, 0640)) < 0) {
      berrno be;
      dev_errno = errno;
      Mmsg3(errmsg, _("Could not open(%s,%s,0640): ERR=%s\n"),
            archive_name.c_str(), mode_to_str(omode), be.bstrerror());
      Dmsg1(40, "open failed: %s", errmsg);
   } else {
      /* Open is OK, now let device get control */
      Dmsg2(40, "Did open(%s,%s,0640)\n", archive_name.c_str(), mode_to_str(omode));
      device_specific_open(dcr);
   }
   if (m_fd >= 0) {
      dev_errno = 0;
      file = 0;
      file_addr = 0;

      /* Refresh the underline device id */
      if (fstat(m_fd, &sp) == 0) {
         devno = sp.st_dev;
      }
   } else {
      if (dcr->jcr) {
         pm_strcpy(dcr->jcr->errmsg, errmsg);
      }
   }
   Dmsg1(100, "open dev: disk fd=%d opened\n", m_fd);

   state |= preserve;                 /* reset any important state info */
   Leave(dbglvl);
   return m_fd >= 0;
}

/*
 * Truncate a volume.  If this is aligned disk, we
 *    truncate both volumes.
 */
bool DEVICE::truncate(DCR *dcr)
{
   struct stat st;
   DEVICE *dev = this;

   Dmsg1(100, "truncate %s\n", print_name());
   switch (dev_type) {
   case B_VTL_DEV:
   case B_VTAPE_DEV:
   case B_TAPE_DEV:
      /* maybe we should rewind and write and eof ???? */
      return true;                    /* we don't really truncate tapes */
   default:
      break;
   }

   Dmsg2(100, "Truncate adata=%d fd=%d\n", dev->adata, dev->m_fd);
   if (ftruncate(dev->m_fd, 0) != 0) {
      berrno be;
      Mmsg2(errmsg, _("Unable to truncate device %s. ERR=%s\n"),
            print_name(), be.bstrerror());
      return false;
   }

   /*
    * Check for a successful ftruncate() and issue a work-around for devices
    * (mostly cheap NAS) that don't support truncation.
    * Workaround supplied by Martin Schmid as a solution to bug #1011.
    * 1. close file
    * 2. delete file
    * 3. open new file with same mode
    * 4. change ownership to original
    */

   if (fstat(dev->m_fd, &st) != 0) {
      berrno be;
      Mmsg2(errmsg, _("Unable to stat device %s. ERR=%s\n"),
            print_name(), be.bstrerror());
      return false;
   }

   if (st.st_size != 0) {             /* ftruncate() didn't work */
      POOL_MEM archive_name(PM_FNAME);

      pm_strcpy(archive_name, dev_name);
      if (!IsPathSeparator(archive_name.c_str()[strlen(archive_name.c_str())-1])) {
         pm_strcat(archive_name, "/");
      }
      pm_strcat(archive_name, dcr->VolumeName);
      if (dev->is_adata()) {
         pm_strcat(archive_name, ADATA_EXTENSION);
      }

      Mmsg2(errmsg, _("Device %s doesn't support ftruncate(). Recreating file %s.\n"),
            print_name(), archive_name.c_str());

      /* Close file and blow it away */
      ::close(dev->m_fd);
      ::unlink(archive_name.c_str());

      /* Recreate the file -- of course, empty */
      dev->set_mode(CREATE_READ_WRITE);
      if ((dev->m_fd = ::open(archive_name.c_str(), mode|O_CLOEXEC, st.st_mode)) < 0) {
         berrno be;
         dev_errno = errno;
         Mmsg2(errmsg, _("Could not reopen: %s, ERR=%s\n"), archive_name.c_str(),
               be.bstrerror());
         Dmsg1(40, "reopen failed: %s", errmsg);
         Emsg0(M_FATAL, 0, errmsg);
         return false;
      }

      /* Reset proper owner */
      chown(archive_name.c_str(), st.st_uid, st.st_gid);
   }
   return true;
}


/*
 * (Un)mount the device
 */
bool DEVICE::mount_file(int mount, int dotimeout)
{
   POOL_MEM ocmd(PM_FNAME);
   POOLMEM *results;
   DIR* dp;
   char *icmd;
   POOL_MEM dname(PM_FNAME);
   int status, tries, name_max, count;
   berrno be;

   Dsm_check(200);
   if (mount) {
      icmd = device->mount_command;
   } else {
      icmd = device->unmount_command;
   }

   clear_freespace_ok();
   edit_mount_codes(ocmd, icmd);

   Dmsg2(100, "mount_file: cmd=%s mounted=%d\n", ocmd.c_str(), !!is_mounted());

   if (dotimeout) {
      /* Try at most 10 times to (un)mount the device. This should perhaps be configurable. */
      tries = 10;
   } else {
      tries = 1;
   }
   results = get_memory(4000);

   /* If busy retry each second */
   Dmsg1(100, "mount_file run_prog=%s\n", ocmd.c_str());
   while ((status = run_program_full_output(ocmd.c_str(), max_open_wait/2, results)) != 0) {
      /* Doesn't work with internationalization (This is not a problem) */
      if (mount && fnmatch("*is already mounted on*", results, 0) == 0) {
         break;
      }
      if (!mount && fnmatch("* not mounted*", results, 0) == 0) {
         break;
      }
      if (tries-- > 0) {
         /* Sometimes the device cannot be mounted because it is already mounted.
          * Try to unmount it, then remount it */
         if (mount) {
            Dmsg1(400, "Trying to unmount the device %s...\n", print_name());
            mount_file(0, 0);
         }
         bmicrosleep(1, 0);
         continue;
      }
      Dmsg5(100, "Device %s cannot be %smounted. stat=%d result=%s ERR=%s\n", print_name(),
           (mount ? "" : "un"), status, results, be.bstrerror(status));
      Mmsg(errmsg, _("Device %s cannot be %smounted. ERR=%s\n"),
           print_name(), (mount ? "" : "un"), be.bstrerror(status));

      /*
       * Now, just to be sure it is not mounted, try to read the filesystem.
       */
      name_max = pathconf(".", _PC_NAME_MAX);
      if (name_max < 1024) {
         name_max = 1024;
      }

      if (!(dp = opendir(device->mount_point))) {
         berrno be;
         dev_errno = errno;
         Dmsg3(100, "mount_file: failed to open dir %s (dev=%s), ERR=%s\n",
               device->mount_point, print_name(), be.bstrerror());
         goto get_out;
      }

      count = 0;
      while (1) {
         if (breaddir(dp, dname.addr()) != 0) {
            dev_errno = EIO;
            Dmsg2(129, "mount_file: failed to find suitable file in dir %s (dev=%s)\n",
                  device->mount_point, print_name());
            break;
         }
         if ((strcmp(dname.c_str(), ".")) && (strcmp(dname.c_str(), "..")) && (strcmp(dname.c_str(), ".keep"))) {
            count++; /* dname.c_str() != ., .. or .keep (Gentoo-specific) */
            break;
         } else {
            Dmsg2(129, "mount_file: ignoring %s in %s\n", dname.c_str(), device->mount_point);
         }
      }
      closedir(dp);

      Dmsg1(100, "mount_file: got %d files in the mount point (not counting ., .. and .keep)\n", count);

      if (count > 0) {
         /* If we got more than ., .. and .keep */
         /*   there must be something mounted */
         if (mount) {
            Dmsg1(100, "Did Mount by count=%d\n", count);
            break;
         } else {
            /* An unmount request. We failed to unmount - report an error */
            set_mounted(true);
            free_pool_memory(results);
            Dmsg0(200, "== error mount=1 wanted unmount\n");
            return false;
         }
      }
get_out:
      set_mounted(false);
      free_pool_memory(results);
      Dmsg0(200, "============ mount=0\n");
      Dsm_check(200);
      return false;
   }

   set_mounted(mount);              /* set/clear mounted flag */
   free_pool_memory(results);
   /* Do not check free space when unmounting */
   Dmsg1(200, "============ mount=%d\n", mount);
   return true;
}

/*
 * Check if the current position on the volume corresponds to
 *  what is in the catalog.
 *
 */
bool file_dev::is_eod_valid(DCR *dcr)
{
   JCR *jcr = dcr->jcr;

   if (has_cap(CAP_LSEEK)) {
      char ed1[50], ed2[50];
      boffset_t ameta_size, adata_size, size;

      ameta_size = lseek(dcr, (boffset_t)0, SEEK_END);
      adata_size = get_adata_size(dcr);
      size = ameta_size + adata_size;
      if (VolCatInfo.VolCatAmetaBytes == (uint64_t)ameta_size &&
          VolCatInfo.VolCatAdataBytes == (uint64_t)adata_size) {
         if (is_aligned()) {
            Jmsg(jcr, M_INFO, 0, _("Ready to append to end of Volumes \"%s\""
                 " ameta size=%s adata size=%s\n"), dcr->VolumeName,
                 edit_uint64_with_commas(VolCatInfo.VolCatAmetaBytes, ed1),
                 edit_uint64_with_commas(VolCatInfo.VolCatAdataBytes, ed2));
         } else {
            Jmsg(jcr, M_INFO, 0, _("Ready to append to end of Volume \"%s\""
                 " size=%s\n"), dcr->VolumeName,
                 edit_uint64_with_commas(VolCatInfo.VolCatAmetaBytes, ed1));
         }
      } else if ((uint64_t)ameta_size >= VolCatInfo.VolCatAmetaBytes &&
                 (uint64_t)adata_size >= VolCatInfo.VolCatAdataBytes) {
         if ((uint64_t)ameta_size != VolCatInfo.VolCatAmetaBytes) {
            Jmsg(jcr, M_WARNING, 0, _("For Volume \"%s\":\n"
               "   The sizes do not match! Metadata Volume=%s Catalog=%s\n"
               "   Correcting Catalog\n"),
               dcr->VolumeName, edit_uint64_with_commas(ameta_size, ed1),
               edit_uint64_with_commas(VolCatInfo.VolCatAmetaBytes, ed2));
         }
         if ((uint64_t)adata_size != VolCatInfo.VolCatAdataBytes) {
            Jmsg(jcr, M_WARNING, 0, _("For aligned Volume \"%s\":\n"
               "   Aligned sizes do not match! Aligned Volume=%s Catalog=%s\n"
               "   Correcting Catalog\n"),
               dcr->VolumeName, edit_uint64_with_commas(adata_size, ed1),
               edit_uint64_with_commas(VolCatInfo.VolCatAdataBytes, ed2));
         }
         VolCatInfo.VolCatAmetaBytes = ameta_size;
         VolCatInfo.VolCatAdataBytes = adata_size;
         VolCatInfo.VolCatBytes = size;
         VolCatInfo.VolCatFiles = (uint32_t)(size >> 32);
         if (!dir_update_volume_info(dcr, false, true)) {
            Jmsg(jcr, M_WARNING, 0, _("Error updating Catalog\n"));
            dcr->mark_volume_in_error();
            return false;
         }
      } else {
         Mmsg(jcr->errmsg, _("Bacula cannot write on disk Volume \"%s\" because: "
              "The sizes do not match! Volume=%s Catalog=%s\n"),
              dcr->VolumeName,
              edit_uint64_with_commas(size, ed1),
              edit_uint64_with_commas(VolCatInfo.VolCatBytes, ed2));
         Jmsg(jcr, M_ERROR, 0, jcr->errmsg);
         Dmsg0(100, jcr->errmsg);
         dcr->mark_volume_in_error();
         return false;
      }
   }
   return true;
}


/*
 * Position device to end of medium (end of data)
 *  Returns: true  on succes
 *           false on error
 */
bool file_dev::eod(DCR *dcr)
{
   boffset_t pos;

   Enter(100);
   if (m_fd < 0) {
      dev_errno = EBADF;
      Mmsg1(errmsg, _("Bad call to eod. Device %s not open\n"), print_name());
      Dmsg1(100, "%s", errmsg);
      return false;
   }

   if (at_eot()) {
      Leave(100);
      return true;
   }
   clear_eof();         /* remove EOF flag */
   block_num = file = 0;
   file_size = 0;
   file_addr = 0;
   if (is_fifo()) {
      Leave(100);
      return true;
   }
   pos = lseek(dcr, (boffset_t)0, SEEK_END);
   Dmsg1(200, "====== Seek to %lld\n", pos);
   if (pos >= 0) {
      update_pos(dcr);
      set_eot();
      Leave(100);
      return true;
   }
   dev_errno = errno;
   berrno be;
   Mmsg2(errmsg, _("lseek error on %s. ERR=%s.\n"),
          print_name(), be.bstrerror());
   Dmsg1(100, "%s", errmsg);
   Leave(100);
   return false;
}

const char *file_dev::print_type()
{
   return "File";
}
