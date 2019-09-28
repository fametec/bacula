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
 *   os.c  -- Operating System dependent dev.c routines
 *
 *     written by, Kern Sibbald, MM
 *     separated from dev.c February 2014
 *
 *     Note, this is the device dependent code, and may have
 *           to be modified for each system.
 */


#include "bacula.h"
#include "stored.h"

/* Returns file position on tape or -1 */
int32_t DEVICE::get_os_tape_file()
{
   struct mtget mt_stat;

   if (has_cap(CAP_MTIOCGET) &&
       d_ioctl(m_fd, MTIOCGET, (char *)&mt_stat) == 0) {
      return mt_stat.mt_fileno;
   }
   return -1;
}


void set_os_device_parameters(DCR *dcr)
{
   DEVICE *dev = dcr->dev;

   if (strcmp(dev->dev_name, "/dev/null") == 0) {
      return;                            /* no use trying to set /dev/null */
   }

#if defined(HAVE_LINUX_OS) || defined(HAVE_WIN32)
   struct mtop mt_com;

   Dmsg0(100, "In set_os_device_parameters\n");
#if defined(MTSETBLK)
   if (dev->min_block_size == dev->max_block_size &&
       dev->min_block_size == 0) {    /* variable block mode */
      mt_com.mt_op = MTSETBLK;
      mt_com.mt_count = 0;
      Dmsg0(100, "Set block size to zero\n");
      if (dev->d_ioctl(dev->fd(), MTIOCTOP, (char *)&mt_com) < 0) {
         dev->clrerror(MTSETBLK);
      }
   }
#endif
#if defined(MTSETDRVBUFFER)
   if (getuid() == 0) {          /* Only root can do this */
      mt_com.mt_op = MTSETDRVBUFFER;
      mt_com.mt_count = MT_ST_CLEARBOOLEANS;
      if (!dev->has_cap(CAP_TWOEOF)) {
         mt_com.mt_count |= MT_ST_TWO_FM;
      }
      if (dev->has_cap(CAP_EOM)) {
         mt_com.mt_count |= MT_ST_FAST_MTEOM;
      }
      Dmsg0(100, "MTSETDRVBUFFER\n");
      if (dev->d_ioctl(dev->fd(), MTIOCTOP, (char *)&mt_com) < 0) {
         dev->clrerror(MTSETDRVBUFFER);
      }
   }
#endif
   return;
#endif

#ifdef HAVE_NETBSD_OS
   struct mtop mt_com;
   if (dev->min_block_size == dev->max_block_size &&
       dev->min_block_size == 0) {    /* variable block mode */
      mt_com.mt_op = MTSETBSIZ;
      mt_com.mt_count = 0;
      if (dev->d_ioctl(dev->fd(), MTIOCTOP, (char *)&mt_com) < 0) {
         dev->clrerror(MTSETBSIZ);
      }
      /* Get notified at logical end of tape */
      mt_com.mt_op = MTEWARN;
      mt_com.mt_count = 1;
      if (dev->d_ioctl(dev->fd(), MTIOCTOP, (char *)&mt_com) < 0) {
         dev->clrerror(MTEWARN);
      }
   }
   return;
#endif

#if HAVE_FREEBSD_OS || HAVE_OPENBSD_OS
   struct mtop mt_com;
   if (dev->min_block_size == dev->max_block_size &&
       dev->min_block_size == 0) {    /* variable block mode */
      mt_com.mt_op = MTSETBSIZ;
      mt_com.mt_count = 0;
      if (dev->d_ioctl(dev->fd(), MTIOCTOP, (char *)&mt_com) < 0) {
         dev->clrerror(MTSETBSIZ);
      }
   }
#if defined(MTIOCSETEOTMODEL)
   if (dev->is_fifo()) {
      return;   /* do not do tape stuff */
   }
   uint32_t neof;
   if (dev->has_cap(CAP_TWOEOF)) {
      neof = 2;
   } else {
      neof = 1;
   }
   if (dev->d_ioctl(dev->fd(), MTIOCSETEOTMODEL, (caddr_t)&neof) < 0) {
      berrno be;
      dev->dev_errno = errno;         /* save errno */
      Mmsg2(dev->errmsg, _("Unable to set eotmodel on device %s: ERR=%s\n"),
            dev->print_name(), be.bstrerror(dev->dev_errno));
      Jmsg(dcr->jcr, M_FATAL, 0, dev->errmsg);
   }
#endif
   return;
#endif

#ifdef HAVE_SUN_OS
   struct mtop mt_com;
   if (dev->min_block_size == dev->max_block_size &&
       dev->min_block_size == 0) {    /* variable block mode */
      mt_com.mt_op = MTSRSZ;
      mt_com.mt_count = 0;
      if (dev->d_ioctl(dev->fd(), MTIOCTOP, (char *)&mt_com) < 0) {
         dev->clrerror(MTSRSZ);
      }
   }
   return;
#endif
}

bool dev_get_os_pos(DEVICE *dev, struct mtget *mt_stat)
{
   Dmsg0(100, "dev_get_os_pos\n");
   return dev->has_cap(CAP_MTIOCGET) &&
          dev->d_ioctl(dev->fd(), MTIOCGET, (char *)mt_stat) == 0 &&
          mt_stat->mt_fileno >= 0;
}

/*
 * Return the status of the device.  This was meant
 * to be a generic routine. Unfortunately, it doesn't
 * seem possible (at least I do not know how to do it
 * currently), which means that for the moment, this
 * routine has very little value.
 *
 *   Returns: status
 */
uint32_t status_dev(DEVICE *dev)
{
   struct mtget mt_stat;
   uint32_t stat = 0;

   if (dev->state & (ST_EOT | ST_WEOT)) {
      stat |= BMT_EOD;
      Pmsg0(-20, " EOD");
   }
   if (dev->state & ST_EOF) {
      stat |= BMT_EOF;
      Pmsg0(-20, " EOF");
   }
   if (dev->is_tape()) {
      stat |= BMT_TAPE;
      Pmsg0(-20,_(" Bacula status:"));
      Pmsg2(-20,_(" file=%d block=%d\n"), dev->file, dev->block_num);
      if (dev->d_ioctl(dev->fd(), MTIOCGET, (char *)&mt_stat) < 0) {
         berrno be;
         dev->dev_errno = errno;
         Mmsg2(dev->errmsg, _("ioctl MTIOCGET error on %s. ERR=%s.\n"),
            dev->print_name(), be.bstrerror());
         return 0;
      }
      Pmsg0(-20, _(" Device status:"));

#if defined(HAVE_LINUX_OS)
      if (GMT_EOF(mt_stat.mt_gstat)) {
         stat |= BMT_EOF;
         Pmsg0(-20, " EOF");
      }
      if (GMT_BOT(mt_stat.mt_gstat)) {
         stat |= BMT_BOT;
         Pmsg0(-20, " BOT");
      }
      if (GMT_EOT(mt_stat.mt_gstat)) {
         stat |= BMT_EOT;
         Pmsg0(-20, " EOT");
      }
      if (GMT_SM(mt_stat.mt_gstat)) {
         stat |= BMT_SM;
         Pmsg0(-20, " SM");
      }
      if (GMT_EOD(mt_stat.mt_gstat)) {
         stat |= BMT_EOD;
         Pmsg0(-20, " EOD");
      }
      if (GMT_WR_PROT(mt_stat.mt_gstat)) {
         stat |= BMT_WR_PROT;
         Pmsg0(-20, " WR_PROT");
      }
      if (GMT_ONLINE(mt_stat.mt_gstat)) {
         stat |= BMT_ONLINE;
         Pmsg0(-20, " ONLINE");
      }
      if (GMT_DR_OPEN(mt_stat.mt_gstat)) {
         stat |= BMT_DR_OPEN;
         Pmsg0(-20, " DR_OPEN");
      }
      if (GMT_IM_REP_EN(mt_stat.mt_gstat)) {
         stat |= BMT_IM_REP_EN;
         Pmsg0(-20, " IM_REP_EN");
      }
#elif defined(HAVE_WIN32)
      if (GMT_EOF(mt_stat.mt_gstat)) {
         stat |= BMT_EOF;
         Pmsg0(-20, " EOF");
      }
      if (GMT_BOT(mt_stat.mt_gstat)) {
         stat |= BMT_BOT;
         Pmsg0(-20, " BOT");
      }
      if (GMT_EOT(mt_stat.mt_gstat)) {
         stat |= BMT_EOT;
         Pmsg0(-20, " EOT");
      }
      if (GMT_EOD(mt_stat.mt_gstat)) {
         stat |= BMT_EOD;
         Pmsg0(-20, " EOD");
      }
      if (GMT_WR_PROT(mt_stat.mt_gstat)) {
         stat |= BMT_WR_PROT;
         Pmsg0(-20, " WR_PROT");
      }
      if (GMT_ONLINE(mt_stat.mt_gstat)) {
         stat |= BMT_ONLINE;
         Pmsg0(-20, " ONLINE");
      }
      if (GMT_DR_OPEN(mt_stat.mt_gstat)) {
         stat |= BMT_DR_OPEN;
         Pmsg0(-20, " DR_OPEN");
      }
      if (GMT_IM_REP_EN(mt_stat.mt_gstat)) {
         stat |= BMT_IM_REP_EN;
         Pmsg0(-20, " IM_REP_EN");
      }

#endif /* !SunOS && !OSF */
      if (dev->has_cap(CAP_MTIOCGET)) {
         Pmsg2(-20, _(" file=%d block=%d\n"), mt_stat.mt_fileno, mt_stat.mt_blkno);
      } else {
         Pmsg2(-20, _(" file=%d block=%d\n"), -1, -1);
      }
   } else {
      stat |= BMT_ONLINE | BMT_BOT;
   }
   return stat;
}

/*
 * If implemented in system, clear the tape
 * error status.
 */
void DEVICE::clrerror(int func)
{
   const char *msg = NULL;
   char buf[100];

   dev_errno = errno;         /* save errno */
   if (errno == EIO) {
      VolCatInfo.VolCatErrors++;
   }

   if (!is_tape()) {
      return;
   }

   if (errno == ENOTTY || errno == ENOSYS) { /* Function not implemented */
      switch (func) {
      case -1:
         break;                  /* ignore message printed later */
      case MTWEOF:
         msg = "WTWEOF";
         clear_cap(CAP_EOF);     /* turn off feature */
         break;
#ifdef MTEOM
      case MTEOM:
         msg = "WTEOM";
         clear_cap(CAP_EOM);     /* turn off feature */
         break;
#endif
      case MTFSF:
         msg = "MTFSF";
         clear_cap(CAP_FSF);     /* turn off feature */
         break;
      case MTBSF:
         msg = "MTBSF";
         clear_cap(CAP_BSF);     /* turn off feature */
         break;
      case MTFSR:
         msg = "MTFSR";
         clear_cap(CAP_FSR);     /* turn off feature */
         break;
      case MTBSR:
         msg = "MTBSR";
         clear_cap(CAP_BSR);     /* turn off feature */
         break;
      case MTREW:
         msg = "MTREW";
         break;
#ifdef MTSETBLK
      case MTSETBLK:
         msg = "MTSETBLK";
         break;
#endif
#ifdef MTSETDRVBUFFER
      case MTSETDRVBUFFER:
         msg = "MTSETDRVBUFFER";
         break;
#endif
#ifdef MTRESET
      case MTRESET:
         msg = "MTRESET";
         break;
#endif

#ifdef MTSETBSIZ
      case MTSETBSIZ:
         msg = "MTSETBSIZ";
         break;
#endif
#ifdef MTSRSZ
      case MTSRSZ:
         msg = "MTSRSZ";
         break;
#endif
#ifdef MTLOAD
      case MTLOAD:
         msg = "MTLOAD";
         break;
#endif
#ifdef MTUNLOCK
      case MTUNLOCK:
         msg = "MTUNLOCK";
         break;
#endif
      case MTOFFL:
         msg = "MTOFFL";
         break;
      default:
         bsnprintf(buf, sizeof(buf), _("unknown func code %d"), func);
         msg = buf;
         break;
      }
      if (msg != NULL) {
         dev_errno = ENOSYS;
         Mmsg1(errmsg, _("I/O function \"%s\" not supported on this device.\n"), msg);
         Emsg0(M_ERROR, 0, errmsg);
      }
   }

   /*
    * Now we try different methods of clearing the error
    *  status on the drive so that it is not locked for
    *  further operations.
    */

   /* On some systems such as NetBSD, this clears all errors */
   get_os_tape_file();

/* Found on Solaris */
#ifdef MTIOCLRERR
{
   d_ioctl(m_fd, MTIOCLRERR);
   Dmsg0(200, "Did MTIOCLRERR\n");
}
#endif

/* Typically on FreeBSD */
#ifdef MTIOCERRSTAT
{
  berrno be;
   /* Read and clear SCSI error status */
   union mterrstat mt_errstat;
   Dmsg2(200, "Doing MTIOCERRSTAT errno=%d ERR=%s\n", dev_errno,
      be.bstrerror(dev_errno));
   d_ioctl(m_fd, MTIOCERRSTAT, (char *)&mt_errstat);
}
#endif

/* Clear Subsystem Exception TRU64 */
#ifdef MTCSE
{
   struct mtop mt_com;
   mt_com.mt_op = MTCSE;
   mt_com.mt_count = 1;
   /* Clear any error condition on the tape */
   d_ioctl(m_fd, MTIOCTOP, (char *)&mt_com);
   Dmsg0(200, "Did MTCSE\n");
}
#endif
}
