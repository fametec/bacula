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
 *
 *   dev.c  -- low level operations on device (storage device)
 *
 *     written by, Kern Sibbald, MM
 *
 *     NOTE!!!! None of these routines are reentrant. You must
 *        use dev->rLock() and dev->Unlock() at a higher level,
 *        or use the xxx_device() equivalents.  By moving the
 *        thread synchronization to a higher level, we permit
 *        the higher level routines to "seize" the device and
 *        to carry out operations without worrying about who
 *        set what lock (i.e. race conditions).
 *
 *     Note, this is the device dependent code, and may have
 *           to be modified for each system, but is meant to
 *           be as "generic" as possible.
 *
 *     The purpose of this code is to develop a SIMPLE Storage
 *     daemon. More complicated coding (double buffering, writer
 *     thread, ...) is left for a later version.
 */

/*
 * Handling I/O errors and end of tape conditions are a bit tricky.
 * This is how it is currently done when writing.
 * On either an I/O error or end of tape,
 * we will stop writing on the physical device (no I/O recovery is
 * attempted at least in this daemon). The state flag will be sent
 * to include ST_EOT, which is ephemeral, and ST_WEOT, which is
 * persistent. Lots of routines clear ST_EOT, but ST_WEOT is
 * cleared only when the problem goes away.  Now when ST_WEOT
 * is set all calls to write_block_to_device() call the fix_up
 * routine. In addition, all threads are blocked
 * from writing on the tape by calling lock_dev(), and thread other
 * than the first thread to hit the EOT will block on a condition
 * variable. The first thread to hit the EOT will continue to
 * be able to read and write the tape (he sort of tunnels through
 * the locking mechanism -- see lock_dev() for details).
 *
 * Now presumably somewhere higher in the chain of command
 * (device.c), someone will notice the EOT condition and
 * get a new tape up, get the tape label read, and mark
 * the label for rewriting. Then this higher level routine
 * will write the unwritten buffer to the new volume.
 * Finally, he will release
 * any blocked threads by doing a broadcast on the condition
 * variable.  At that point, we should be totally back in
 * business with no lost data.
 */

#include "bacula.h"
#include "stored.h"

#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

static const int dbglvl = 150;

/* Imported functions */
extern void set_os_device_parameters(DCR *dcr);
extern bool dev_get_os_pos(DEVICE *dev, struct mtget *mt_stat);
extern uint32_t status_dev(DEVICE *dev);

/* Forward referenced functions */
const char *mode_to_str(int mode);
DEVICE *m_init_dev(JCR *jcr, DEVRES *device, bool adata);

/*
 * Device specific initialization.
 */
void DEVICE::device_specific_init(JCR *jcr, DEVRES *device)
{
}

/*
 * Initialize the device with the operating system and
 * initialize buffer pointers.
 *
 * Returns:  true  device already open
 *           false device setup but not open
 *
 * Note, for a tape, the VolName is the name we give to the
 *    volume (not really used here), but for a file, the
 *    VolName represents the name of the file to be created/opened.
 *    In the case of a file, the full name is the device name
 *    (archive_name) with the VolName concatenated.
 *
 * This is generic common code. It should be called prior to any
 *     device specific code.  Note! This does not open anything.
 */
bool DEVICE::open_device(DCR *dcr, int omode)
{
   Enter(dbglvl);
   preserve = 0;
   ASSERT2(!adata, "Attempt to open adata dev");
   if (is_open()) {
      if (openmode == omode) {
         return true;
      } else {
         Dmsg1(200, "Close fd=%d for mode change in open().\n", m_fd);
         d_close(m_fd);
         clear_opened();
         preserve = state & (ST_LABEL|ST_APPEND|ST_READ);
      }
   }
   openmode = omode;
   if (dcr) {
      dcr->setVolCatName(dcr->VolumeName);
      VolCatInfo = dcr->VolCatInfo;    /* structure assign */
   }

   state &= ~(ST_NOSPACE|ST_LABEL|ST_APPEND|ST_READ|ST_EOT|ST_WEOT|ST_EOF);
   label_type = B_BACULA_LABEL;

   if (openmode == OPEN_READ_WRITE && has_cap(CAP_STREAM)) {
      openmode = OPEN_WRITE_ONLY;
   }
   return false;
}

void DEVICE::set_mode(int new_mode)
{
   switch (new_mode) {
   case CREATE_READ_WRITE:
      mode = O_CREAT | O_RDWR | O_BINARY;
      break;
   case OPEN_READ_WRITE:
      mode = O_RDWR | O_BINARY;
      break;
   case OPEN_READ_ONLY:
      mode = O_RDONLY | O_BINARY;
      break;
   case OPEN_WRITE_ONLY:
      mode = O_WRONLY | O_BINARY;
      break;
   default:
      Jmsg0(NULL, M_ABORT, 0, _("Illegal mode given to open dev.\n"));
   }
}

/*
 * Called to indicate that we have just read an
 *  EOF from the device.
 */
void DEVICE::set_ateof()
{
   set_eof();
   file_addr = 0;
   file_size = 0;
   block_num = 0;
}

/*
 * Called to indicate we are now at the end of the tape, and
 *   writing is not possible.
 */
void DEVICE::set_ateot()
{
   /* Make tape effectively read-only */
   Dmsg0(200, "==== Set AtEof\n");
   state |= (ST_EOF|ST_EOT|ST_WEOT);
   clear_append();
}


/*
 * Set the position of the device -- only for files
 *   For other devices, there is no generic way to do it.
 *  Returns: true  on succes
 *           false on error
 */
bool DEVICE::update_pos(DCR *dcr)
{
   boffset_t pos;
   bool ok = true;

   if (!is_open()) {
      dev_errno = EBADF;
      Mmsg0(errmsg, _("Bad device call. Device not open\n"));
      Emsg1(M_FATAL, 0, "%s", errmsg);
      return false;
   }

   if (is_file()) {
      file = 0;
      file_addr = 0;
      pos = lseek(dcr, (boffset_t)0, SEEK_CUR);
      if (pos < 0) {
         berrno be;
         dev_errno = errno;
         Pmsg1(000, _("Seek error: ERR=%s\n"), be.bstrerror());
         Mmsg2(errmsg, _("lseek error on %s. ERR=%s.\n"),
               print_name(), be.bstrerror());
         ok = false;
      } else {
         file_addr = pos;
         block_num = (uint32_t)pos;
         file = (uint32_t)(pos >> 32);
      }
   }
   return ok;
}

void DEVICE::set_slot(int32_t slot)
{
   m_slot = slot;
   if (vol) vol->clear_slot();
}

void DEVICE::clear_slot()
{
   m_slot = -1;
   if (vol) vol->set_slot(-1);
}

/*
 * Set to unload the current volume in the drive
 */
void DEVICE::set_unload()
{
   if (!m_unload && VolHdr.VolumeName[0] != 0) {
       m_unload = true;
       notify_newvol_in_attached_dcrs(NULL);
   }
}

/*
 * Clear volume header
 */
void DEVICE::clear_volhdr()
{
   Dmsg1(100, "Clear volhdr vol=%s\n", VolHdr.VolumeName);
   memset(&VolHdr, 0, sizeof(VolHdr));
   setVolCatInfo(false);
}

void DEVICE::set_volcatinfo_from_dcr(DCR *dcr)
{
   VolCatInfo = dcr->VolCatInfo;
}

/*
 * Close the device
 *   Can enter with dcr==NULL
 */
bool DEVICE::close(DCR *dcr)
{
   bool ok = true;

   Dmsg5(40, "close_dev vol=%s fd=%d dev=%p adata=%d dev=%s\n",
      VolHdr.VolumeName, m_fd, this, adata, print_name());
   offline_or_rewind(dcr);

   if (!is_open()) {
      Dmsg2(200, "device %s already closed vol=%s\n", print_name(),
         VolHdr.VolumeName);
      return true;                    /* already closed */
   }

   switch (dev_type) {
   case B_VTL_DEV:
   case B_VTAPE_DEV:
   case B_TAPE_DEV:
      unlock_door();
      /* Fall through wanted */
   default:
      if (d_close(m_fd) != 0) {
         berrno be;
         dev_errno = errno;
         Mmsg2(errmsg, _("Error closing device %s. ERR=%s.\n"),
               print_name(), be.bstrerror());
         ok = false;
      }
      break;
   }

   unmount(1);                       /* do unmount if required */

   /* Clean up device packet so it can be reused */
   clear_opened();

   state &= ~(ST_LABEL|ST_READ|ST_APPEND|ST_EOT|ST_WEOT|ST_EOF|
              ST_NOSPACE|ST_MOUNTED|ST_MEDIA|ST_SHORT);
   label_type = B_BACULA_LABEL;
   file = block_num = 0;
   file_size = 0;
   file_addr = 0;
   EndFile = EndBlock = 0;
   openmode = 0;
   clear_volhdr();
   memset(&VolCatInfo, 0, sizeof(VolCatInfo));
   if (tid) {
      stop_thread_timer(tid);
      tid = 0;
   }
   return ok;
}

/*
 * If timeout, wait until the mount command returns 0.
 * If !timeout, try to mount the device only once.
 */
bool DEVICE::mount(int timeout)
{
   Enter(dbglvl);
   if (!is_mounted() && device->mount_command) {
      return mount_file(1, timeout);
   }
   return true;
}

/*
 * Unmount the device
 * If timeout, wait until the unmount command returns 0.
 * If !timeout, try to unmount the device only once.
 */
bool DEVICE::unmount(int timeout)
{
   Enter(dbglvl);
   if (is_mounted() && requires_mount() && device->unmount_command) {
      return mount_file(0, timeout);
   }
   return true;
}


/*
 * Edit codes into (Un)MountCommand, Write(First)PartCommand
 *  %% = %
 *  %a = archive device name
 *  %e = erase (set if cannot mount and first part)
 *  %n = part number
 *  %m = mount point
 *  %v = last part name
 *
 *  omsg = edited output message
 *  imsg = input string containing edit codes (%x)
 *
 */
void DEVICE::edit_mount_codes(POOL_MEM &omsg, const char *imsg)
{
   const char *p;
   const char *str;
   char add[20];

   POOL_MEM archive_name(PM_FNAME);

   omsg.c_str()[0] = 0;
   Dmsg1(800, "edit_mount_codes: %s\n", imsg);
   for (p=imsg; *p; p++) {
      if (*p == '%') {
         switch (*++p) {
         case '%':
            str = "%";
            break;
         case 'a':
            str = dev_name;
            break;
         case 'e':
            str = "0";
            /* ***FIXME*** this may be useful for Cloud */
#ifdef xxx
            if (num_dvd_parts == 0) {
               if (truncating || blank_dvd) {
                  str = "2";
               } else {
                  str = "1";
               }
            } else {
               str = "0";
            }
#endif
            break;
         case 'n':
            bsnprintf(add, sizeof(add), "%d", part);
            str = add;
            break;
         case 'm':
            str = device->mount_point;
            break;
         default:
            add[0] = '%';
            add[1] = *p;
            add[2] = 0;
            str = add;
            break;
         }
      } else {
         add[0] = *p;
         add[1] = 0;
         str = add;
      }
      Dmsg1(1900, "add_str %s\n", str);
      pm_strcat(omsg, (char *)str);
      Dmsg1(1800, "omsg=%s\n", omsg.c_str());
   }
}

/* return the last timer interval (ms)
 * or 0 if something goes wrong
 */
btime_t DEVICE::get_timer_count()
{
   btime_t temp = last_timer;
   last_timer = get_current_btime();
   temp = last_timer - temp;   /* get elapsed time */
   return (temp>0)?temp:0;     /* take care of skewed clock */
}

/* read from fd */
ssize_t DEVICE::read(void *buf, size_t len)
{
   ssize_t read_len;

   get_timer_count();

   read_len = d_read(m_fd, buf, len);

   last_tick = get_timer_count();

   DevReadTime += last_tick;
   VolCatInfo.VolReadTime += last_tick;

   if (read_len > 0) {          /* skip error */
      DevReadBytes += read_len;
   }

   return read_len;
}

/* write to fd */
ssize_t DEVICE::write(const void *buf, size_t len)
{
   ssize_t write_len;

   get_timer_count();

   write_len = d_write(m_fd, buf, len);

   last_tick = get_timer_count();

   DevWriteTime += last_tick;
   VolCatInfo.VolWriteTime += last_tick;

   if (write_len > 0) {         /* skip error */
      DevWriteBytes += write_len;
   }

   return write_len;
}

/* Return the resource name for the device */
const char *DEVICE::name() const
{
   return device->hdr.name;
}

uint32_t DEVICE::get_file()
{
   if (is_tape()) {
      return file;
   } else {
      uint64_t bytes = VolCatInfo.VolCatAdataBytes + VolCatInfo.VolCatAmetaBytes;
      return (uint32_t)(bytes >> 32);
   }
}

uint32_t DEVICE::get_block_num()
{
   if (is_tape()) {
      return block_num;
   } else {
      return  VolCatInfo.VolCatAdataBlocks + VolCatInfo.VolCatAmetaBlocks;
   }
}

/*
 * Walk through all attached jcrs indicating the volume has changed
 *   Note: If you have the new VolumeName, it is passed here,
 *     otherwise pass a NULL.
 */
void
DEVICE::notify_newvol_in_attached_dcrs(const char *newVolumeName)
{
   Dmsg2(140, "Notify dcrs of vol change. oldVolume=%s NewVolume=%s\n",
      getVolCatName(), newVolumeName?newVolumeName:"*None*");
   Lock_dcrs();
   DCR *mdcr;
   foreach_dlist(mdcr, attached_dcrs) {
      if (mdcr->jcr->JobId == 0) {
         continue;                 /* ignore console */
      }
      mdcr->NewVol = true;
      mdcr->NewFile = true;
      if (newVolumeName && mdcr->VolumeName != newVolumeName) {
         bstrncpy(mdcr->VolumeName, newVolumeName, sizeof(mdcr->VolumeName));
         Dmsg2(140, "Set NewVol=%s in JobId=%d\n", mdcr->VolumeName, mdcr->jcr->JobId);
      }
   }
   Unlock_dcrs();
}

/*
 * Walk through all attached jcrs indicating the File has changed
 */
void
DEVICE::notify_newfile_in_attached_dcrs()
{
   Dmsg1(140, "Notify dcrs of file change. Volume=%s\n", getVolCatName());
   Lock_dcrs();
   DCR *mdcr;
   foreach_dlist(mdcr, attached_dcrs) {
      if (mdcr->jcr->JobId == 0) {
         continue;                 /* ignore console */
      }
      Dmsg1(140, "Notify JobI=%d\n", mdcr->jcr->JobId);
      mdcr->NewFile = true;
   }
   Unlock_dcrs();
}



/*
 * Free memory allocated for the device
 *  Can enter with dcr==NULL
 */
void DEVICE::term(DCR *dcr)
{
   Dmsg1(900, "term dev: %s\n", print_name());
   if (!dcr) {
      d_close(m_fd);
   } else {
      close(dcr);
   }
   if (dev_name) {
      free_memory(dev_name);
      dev_name = NULL;
   }
   if (adev_name) {
      free_memory(adev_name);
      adev_name = NULL;
   }
   if (prt_name) {
      free_memory(prt_name);
      prt_name = NULL;
   }
   if (errmsg) {
      free_pool_memory(errmsg);
      errmsg = NULL;
   }
   pthread_mutex_destroy(&m_mutex);
   pthread_cond_destroy(&wait);
   pthread_cond_destroy(&wait_next_vol);
   pthread_mutex_destroy(&spool_mutex);
   pthread_mutex_destroy(&freespace_mutex);
   if (attached_dcrs) {
      delete attached_dcrs;
      attached_dcrs = NULL;
   }
   /* We let the DEVRES pointer if not our device */
   if (device && device->dev == this) {
      device->dev = NULL;
   }
   delete this;
}

/* Get freespace values */
void DEVICE::get_freespace(uint64_t *freeval, uint64_t *totalval)
{
   get_os_device_freespace();
   P(freespace_mutex);
   if (is_freespace_ok()) {
      *freeval = free_space;
      *totalval = total_space;
   } else {
      *freeval = *totalval = 0;
   }
   V(freespace_mutex);
}

/* Set freespace values */
void DEVICE::set_freespace(uint64_t freeval, uint64_t totalval, int errnoval, bool valid)
{
   P(freespace_mutex);
   free_space = freeval;
   total_space = totalval;
   free_space_errno = errnoval;
   if (valid) {
      set_freespace_ok();
   } else {
      clear_freespace_ok();
   }
   V(freespace_mutex);
}

/* Convenient function that return true only  if the device back-end is a
 * filesystem that its nearly full. (the free space is below the given threshold)
 */
bool DEVICE::is_fs_nearly_full(uint64_t threshold)
{
   uint64_t freeval, totalval;
   if (is_file()) {
      get_freespace(&freeval, &totalval);
      if (totalval > 0) {
         if (freeval < threshold) {
            return true;
         }
      }
   }
   return false;
}

static const char *modes[] = {
   "CREATE_READ_WRITE",
   "OPEN_READ_WRITE",
   "OPEN_READ_ONLY",
   "OPEN_WRITE_ONLY"
};


const char *mode_to_str(int mode)
{
   static char buf[100];
   if (mode < 1 || mode > 4) {
      bsnprintf(buf, sizeof(buf), "BAD mode=%d", mode);
      return buf;
    }
   return modes[mode-1];
}

void DEVICE::setVolCatName(const char *name)
{
   bstrncpy(VolCatInfo.VolCatName, name, sizeof(VolCatInfo.VolCatName));
   setVolCatInfo(false);
}

void DEVICE::setVolCatStatus(const char *status)
{
   bstrncpy(VolCatInfo.VolCatStatus, status, sizeof(VolCatInfo.VolCatStatus));
   setVolCatInfo(false);
}

void DEVICE::updateVolCatBytes(uint64_t bytes)
{
   Lock_VolCatInfo();
   VolCatInfo.VolCatAmetaBytes += bytes;
   VolCatInfo.VolCatBytes += bytes;
   setVolCatInfo(false);
   Unlock_VolCatInfo();
}

void DEVICE::updateVolCatHoleBytes(uint64_t hole)
{
   return;
}

void DEVICE::updateVolCatPadding(uint64_t padding)
{
   Lock_VolCatInfo();
   VolCatInfo.VolCatAmetaPadding += padding;
   VolCatInfo.VolCatPadding += padding;
   setVolCatInfo(false);
   Unlock_VolCatInfo();
}


void DEVICE::updateVolCatBlocks(uint32_t blocks)
{
   Lock_VolCatInfo();
   VolCatInfo.VolCatAmetaBlocks += blocks;
   VolCatInfo.VolCatBlocks += blocks;
   setVolCatInfo(false);
   Unlock_VolCatInfo();
}

void DEVICE::updateVolCatWrites(uint32_t writes)
{
   Lock_VolCatInfo();
   VolCatInfo.VolCatAmetaWrites += writes;
   VolCatInfo.VolCatWrites += writes;
   setVolCatInfo(false);
   Unlock_VolCatInfo();
}

void DEVICE::updateVolCatReads(uint32_t reads)
{
   Lock_VolCatInfo();
   VolCatInfo.VolCatAmetaReads += reads;
   VolCatInfo.VolCatReads += reads;
   setVolCatInfo(false);
   Unlock_VolCatInfo();
}

void DEVICE::updateVolCatReadBytes(uint64_t bytes)
{
   Lock_VolCatInfo();
   VolCatInfo.VolCatAmetaRBytes += bytes;
   VolCatInfo.VolCatRBytes += bytes;
   setVolCatInfo(false);
   Unlock_VolCatInfo();
}

void DEVICE::set_nospace()
{
   state |= ST_NOSPACE;
}

void DEVICE::clear_nospace()
{
   state &= ~ST_NOSPACE;
}

/* Put device in append mode */
void DEVICE::set_append()
{
   state &= ~(ST_NOSPACE|ST_READ|ST_EOT|ST_EOF|ST_WEOT);  /* remove EOF/EOT flags */
   state |= ST_APPEND;
}

/* Clear append mode */
void DEVICE::clear_append()
{
   state &= ~ST_APPEND;
}

/* Put device in read mode */
void DEVICE::set_read()
{
   state &= ~(ST_APPEND|ST_EOT|ST_EOF|ST_WEOT);  /* remove EOF/EOT flags */
   state |= ST_READ;
}

/* Clear read mode */
void DEVICE::clear_read()
{
   state &= ~ST_READ;
}

/*
 * Get freespace using OS calls
 * TODO: See if it's working with mount commands
 */
bool DEVICE::get_os_device_freespace()
{
   int64_t freespace, totalspace;

   if (!is_file()) {
      return true;
   }
   if (fs_get_free_space(dev_name, &freespace, &totalspace) == 0) {
      set_freespace(freespace,  totalspace, 0, true);
      Mmsg(errmsg, "");
      return true;

   } else {
      set_freespace(0, 0, 0, false); /* No valid freespace */
   }
   return false;
}

/* Update the free space on the device */
bool DEVICE::update_freespace()
{
   POOL_MEM ocmd(PM_FNAME);
   POOLMEM* results;
   char* icmd;
   char* p;
   uint64_t free, total;
   char ed1[50];
   bool ok = false;
   int status;
   berrno be;

   if (!is_file()) {
      Mmsg(errmsg, "");
      return true;
   }

   /* The device must be mounted in order for freespace to work */
   if (requires_mount()) {
      mount(1);
   }

   if (get_os_device_freespace()) {
      Dmsg4(20, "get_os_device_freespace: free_space=%s freespace_ok=%d free_space_errno=%d have_media=%d\n",
         edit_uint64(free_space, ed1), !!is_freespace_ok(), free_space_errno, !!have_media());
      return true;
   }

   icmd = device->free_space_command;

   if (!icmd) {
      set_freespace(0, 0, 0, false);
      Dmsg2(20, "ERROR: update_free_space_dev: free_space=%s, free_space_errno=%d (!icmd)\n",
            edit_uint64(free_space, ed1), free_space_errno);
      Mmsg(errmsg, _("No FreeSpace command defined.\n"));
      return false;
   }

   edit_mount_codes(ocmd, icmd);

   Dmsg1(20, "update_freespace: cmd=%s\n", ocmd.c_str());

   results = get_pool_memory(PM_MESSAGE);

   Dmsg1(20, "Run freespace prog=%s\n", ocmd.c_str());
   status = run_program_full_output(ocmd.c_str(), max_open_wait/2, results);
   Dmsg2(20, "Freespace status=%d result=%s\n", status, results);
   /* Should report "1223232 12323232\n"  "free  total\n" */
   if (status == 0) {
      free = str_to_int64(results) * 1024;
      p = results;

      if (skip_nonspaces(&p)) {
         total = str_to_int64(p) * 1024;

      } else {
         total = 0;
      }

      Dmsg1(400, "Free space program run: Freespace=%s\n", results);
      if (free >= 0) {
         set_freespace(free, total, 0, true); /* have valid freespace */
         Mmsg(errmsg, "");
         ok = true;
      }
   } else {
      set_freespace(0, 0, EPIPE, false); /* no valid freespace */
      Mmsg2(errmsg, _("Cannot run free space command. Results=%s ERR=%s\n"),
            results, be.bstrerror(status));

      dev_errno = free_space_errno;
      Dmsg4(20, "Cannot get free space on device %s. free_space=%s, "
         "free_space_errno=%d ERR=%s\n",
            print_name(), edit_uint64(free_space, ed1),
            free_space_errno, errmsg);
   }
   free_pool_memory(results);
   Dmsg4(20, "leave update_freespace: free_space=%s freespace_ok=%d free_space_errno=%d have_media=%d\n",
      edit_uint64(free_space, ed1), !!is_freespace_ok(), free_space_errno, !!have_media());
   return ok;
}

bool DEVICE::weof(DCR */*dcr*/, int num)
{
   Dmsg1(129, "=== weof_dev=%s\n", print_name());

   if (!is_open()) {
      dev_errno = EBADF;
      Mmsg1(errmsg, _("Bad call to weof_dev. Device %s not open\n"), print_name());
      Emsg0(M_FATAL, 0, errmsg);
      return false;
   }

   if (!can_append()) {
      Mmsg1(errmsg, _("Attempt to WEOF on non-appendable Volume %s\n"), VolHdr.VolumeName);
      Emsg0(M_FATAL, 0, errmsg);
      return false;
   }

   file_size = 0;
   return true;
}

/*
 * Very basic functions -- no device specific code.
 *  Returns: true  on succes
 *           false on error
 */
bool DEVICE::eod(DCR *dcr)
{
   bool ok = true;

   Enter(dbglvl);
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
   Leave(100);
   return ok;
}

bool DEVICE::is_eod_valid(DCR *dcr)
{
   return true;
}

bool DEVICE::open_next_part(DCR */*dcr*/)
{
   return true;
}

bool DEVICE::close_part(DCR */*dcr*/)
{
   return true;
}

DEVICE *DEVICE::get_dev(DCR */*dcr*/)
{
   return this;
}

uint32_t DEVICE::get_hi_addr()
{
   return (uint32_t)(file_addr >> 32);
}

uint32_t DEVICE::get_hi_addr(boffset_t addr)
{
   return (uint32_t)(addr >> 32);
}

uint32_t DEVICE::get_low_addr()
{
   return (uint32_t)(file_addr);
}

uint32_t DEVICE::get_low_addr(boffset_t addr)
{
   return (uint32_t)(addr);
}


uint64_t DEVICE::get_full_addr()
{
   return file_addr;
}

uint64_t DEVICE::get_full_addr(boffset_t addr)
{
   return addr;
}


uint64_t DEVICE::get_full_addr(uint32_t hi, uint32_t low)
{
   return ((uint64_t)hi)<<32 | (uint64_t)low;
}

/* Note: this subroutine is not in the class */
uint64_t get_full_addr(uint32_t hi, uint32_t low)
{
   return ((uint64_t)hi)<<32 | (uint64_t)low;
}

/* Print the file address */
char *DEVICE::print_addr(char *buf, int32_t buf_len)
{
   buf[0] = 0;
   bsnprintf(buf, buf_len, "%llu", get_full_addr());
   return buf;
}

char *DEVICE::print_addr(char *buf, int32_t buf_len, boffset_t addr)
{
   buf[0] = 0;
   bsnprintf(buf, buf_len, "%llu", addr);
   return buf;
}


bool DEVICE::do_size_checks(DCR *dcr, DEV_BLOCK *block)
{
   JCR *jcr = dcr->jcr;

   if (is_user_volume_size_reached(dcr, true)) {
      Dmsg0(40, "Calling terminate_writing_volume\n");
      terminate_writing_volume(dcr);
      reread_last_block(dcr);   /* Only used on tapes */
      dev_errno = ENOSPC;
      return false;
   }

   /*
    * Limit maximum File size on volume to user specified value.
    *  In practical terms, this means to put an EOF mark on
    *  a tape after every X bytes. This effectively determines
    *  how many index records we have (JobMedia). If you set
    *  max_file_size too small, it will cause a lot of shoe-shine
    *  on very fast modern tape (LTO-3 and above).
    */
   if ((max_file_size > 0) &&
       (file_size+block->binbuf) >= max_file_size) {
      file_size = 0;             /* reset file size */

      if (!weof(dcr, 1)) {            /* write eof */
         Dmsg0(50, "WEOF error in max file size.\n");
         Jmsg(jcr, M_FATAL, 0, _("Unable to write EOF. ERR=%s\n"),
            bstrerror());
         Dmsg0(40, "Calling terminate_writing_volume\n");
         terminate_writing_volume(dcr);
         dev_errno = ENOSPC;
         return false;
      }

      if (!do_new_file_bookkeeping(dcr)) {
         /* Error message already sent */
         return false;
      }
   }
   return true;
}

bool DEVICE::get_tape_alerts(DCR *dcr)
{
   return true;
}

void DEVICE::show_tape_alerts(DCR *dcr, alert_list_type type,
       alert_list_which which, alert_cb alert_callback)
{
   return;
}

int DEVICE::delete_alerts()
{
   return 0;
}

bool DEVICE::get_tape_worm(DCR *dcr)
{
   return false;
}
