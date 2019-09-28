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
 * Initialize a single device.
 *
 * This code was split from dev.c in January 2016
 *
 *     written by, Kern Sibbald, MM
 */

#include "bacula.h"
#include "stored.h"
#include <dlfcn.h>

/* Define possible extensions */
#if defined(HAVE_WIN32)
#define DRV_EXT ".dll"
#elif defined(HAVE_DARWIN_OS)
#define DRV_EXT ".dylib"
#else
#define DRV_EXT ".so"
#endif

#ifndef RTLD_NOW
#define RTLD_NOW 2
#endif

/* Forward referenced functions */
extern "C" {
typedef DEVICE *(*newDriver_t)(JCR *jcr, DEVRES *device);
}

static DEVICE *load_driver(JCR *jcr, DEVRES *device);

/*
 * Driver item for driver table
*/
struct driver_item {
   const char *name;
   void *handle;
   newDriver_t newDriver;
   bool builtin;
   bool loaded;
};

/*
 * Driver table. Must be in same order as the B_xxx_DEV type
 *   name   handle, builtin  loaded
 */
static driver_item driver_tab[] = {
/*   name   handle, newDriver builtin  loaded */
   {"file",    NULL, NULL,    true,  true},
   {"tape",    NULL, NULL,    true,  true},
   {"none",    NULL, NULL,    true,  true}, /* deprecated was DVD */
   {"fifo",    NULL, NULL,    true,  true},
   {"vtape",   NULL, NULL,    true,  true},
   {"ftp",     NULL, NULL,    true,  true},
   {"vtl",     NULL, NULL,    true,  true},
   {"none",    NULL, NULL,    true,  true}, /* B_ADATA_DEV */
   {"aligned", NULL, NULL,    false, false},
   {"none",    NULL, NULL,    true,  true}, /* deprecated was old dedup */
   {"null",    NULL, NULL,    true,  true},
   {"none",    NULL, NULL,    true,  true}, /* deprecated B_VALIGNED_DEV */
   {"none",    NULL, NULL,    true,  true}, /* deprecated B_VDEDUP_DEV */
   {"cloud",   NULL, NULL,    false, false},
   {"none",    NULL, NULL,    false, false},
   {NULL,      NULL, NULL,    false, false}
};

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/* The alist should be created with not_owned_by_alist argument */
void sd_list_loaded_drivers(alist *list)
{
   for(int i=0 ; driver_tab[i].name != NULL; i++) {
      if (driver_tab[i].loaded && !driver_tab[i].builtin) {
         list->append((void*)driver_tab[i].name);
      }
   }
}

/*
 * Allocate and initialize the DEVICE structure
 * Note, if dev is non-NULL, it is already allocated,
 * thus we neither allocate it nor free it. This allows
 * the caller to put the packet in shared memory.
 *
 *  Note, for a tape, the device->device_name is the device name
 *     (e.g. /dev/nst0), and for a file, the device name
 *     is the directory in which the file will be placed.
 *
 */
DEVICE *init_dev(JCR *jcr, DEVRES *device, bool adata)
{
   struct stat statp;
   DEVICE *dev = NULL;
   uint32_t n_drivers;

   generate_global_plugin_event(bsdGlobalEventDeviceInit, device);
   Dmsg1(150, "init_dev dev_type=%d\n", device->dev_type);
   /* If no device type specified, try to guess */
   if (!device->dev_type) {
      /* Check that device is available */
      if (stat(device->device_name, &statp) < 0) {
         berrno be;
         Jmsg3(jcr, M_ERROR, 0, _("[SE0001] Unable to stat device %s at %s: ERR=%s\n"),
            device->hdr.name, device->device_name, be.bstrerror());
         return NULL;
      }
      if (S_ISDIR(statp.st_mode)) {
         device->dev_type = B_FILE_DEV;
      } else if (S_ISCHR(statp.st_mode)) {
         device->dev_type = B_TAPE_DEV;
      } else if (S_ISFIFO(statp.st_mode)) {
         device->dev_type = B_FIFO_DEV;
#ifdef USE_VTAPE
      /* must set DeviceType = Vtape
       * in normal mode, autodetection is disabled
       */
      } else if (S_ISREG(statp.st_mode)) {
         device->dev_type = B_VTAPE_DEV;
#endif
      } else if (!(device->cap_bits & CAP_REQMOUNT)) {
         Jmsg2(jcr, M_ERROR, 0, _("[SE0002] %s is an unknown device type. Must be tape or directory."
               " st_mode=%x\n"),
            device->device_name, statp.st_mode);
         return NULL;
      }
      if (strcmp(device->device_name, "/dev/null") == 0) {
         device->dev_type = B_NULL_DEV;
      }
   }

   /* Count drivers */
   for (n_drivers=0; driver_tab[n_drivers].name; n_drivers++) { };
   Dmsg1(100, "Num drivers=%d\n", n_drivers);

   /* If invalid dev_type get out */
   if (device->dev_type < 0 || device->dev_type > n_drivers) {
      Jmsg2(jcr, M_FATAL, 0, _("[SF0001] Invalid device type=%d name=\"%s\"\n"),
         device->dev_type, device->hdr.name);
      return NULL;
   }
   Dmsg5(100, "loadable=%d type=%d loaded=%d name=%s handle=%p\n",
      !driver_tab[device->dev_type-1].builtin,
      device->dev_type,
      driver_tab[device->dev_type-1].loaded,
      driver_tab[device->dev_type-1].name,
      driver_tab[device->dev_type-1].handle);
   if (driver_tab[device->dev_type-1].builtin) {
      /* Built-in driver */
      switch (device->dev_type) {
#ifdef HAVE_WIN32
      case B_TAPE_DEV:
//       dev = New(win_tape_dev);
         break;
      case B_ADATA_DEV:
      case B_ALIGNED_DEV:
      case B_FILE_DEV:
         dev = New(win_file_dev);
         dev->capabilities |= CAP_LSEEK;
         break;
      case B_NULL_DEV:
         dev = New(win_file_dev);
         break;
#else
      case B_VTAPE_DEV:
         dev = New(vtape);
         break;
      case B_TAPE_DEV:
         dev = New(tape_dev);
         break;
      case B_FILE_DEV:
         dev = New(file_dev);
         dev->capabilities |= CAP_LSEEK;
         break;
      case B_NULL_DEV:
         dev = New(null_dev);
         break;
      case B_FIFO_DEV:
         dev = New(fifo_dev);
         break;
#endif
      default:
         Jmsg2(jcr, M_FATAL, 0, _("[SF0002] Unknown device type=%d device=\"%s\"\n"),
            device->dev_type, device->hdr.name);
         return NULL;
      }
   } else {
      /* Loadable driver */
      dev = load_driver(jcr, device);
   }
   if (!dev) {
      return NULL;
   }

   dev->adata = adata;

   /* Keep the device ID in the DEVICE struct to identify the hardware */
   if (dev->is_file() && stat(dev->archive_name(), &statp) == 0) {
      dev->devno = statp.st_dev;
   }

   dev->device_generic_init(jcr, device);

   /* Do device specific initialization */
   dev->device_specific_init(jcr, device);

   /* ***FIXME*** move to fifo driver */
   if (dev->is_fifo()) {
      dev->capabilities |= CAP_STREAM; /* set stream device */
   }

   return dev;
}

/*
 * Do all the generic initialization here. Same for all devices.
 */
void DEVICE::device_generic_init(JCR *jcr, DEVRES *device)
{
   struct stat statp;
   DEVICE *dev = this;
   DCR *dcr = NULL;
   int errstat;
   uint32_t max_bs;

   dev->clear_slot();         /* unknown */

   /* Copy user supplied device parameters from Resource */
   dev->dev_name = get_memory(strlen(device->device_name)+1);
   pm_strcpy(dev->dev_name, device->device_name);
   dev->prt_name = get_memory(strlen(device->device_name) + strlen(device->hdr.name) + 20);
   /* We edit "Resource-name" (physical-name) */
   Mmsg(dev->prt_name, "\"%s\" (%s)", device->hdr.name, device->device_name);
   Dmsg1(400, "Allocate dev=%s\n", dev->print_name());
   dev->capabilities = device->cap_bits;
   dev->min_free_space = device->min_free_space;
   dev->min_block_size = device->min_block_size;
   dev->max_block_size = device->max_block_size;
   dev->max_volume_size = device->max_volume_size;
   dev->max_file_size = device->max_file_size;
   dev->padding_size = device->padding_size;
   dev->file_alignment = device->file_alignment;
   dev->max_concurrent_jobs = device->max_concurrent_jobs;
   dev->volume_capacity = device->volume_capacity;
   dev->max_rewind_wait = device->max_rewind_wait;
   dev->max_open_wait = device->max_open_wait;
   dev->vol_poll_interval = device->vol_poll_interval;
   dev->max_spool_size = device->max_spool_size;
   dev->drive_index = device->drive_index;
   dev->enabled = device->enabled;
   dev->autoselect = device->autoselect;
   dev->read_only = device->read_only;
   dev->dev_type = device->dev_type;
   dev->device = device;
   if (dev->is_tape()) { /* No parts on tapes */
      dev->max_part_size = 0;
   } else {
      dev->max_part_size = device->max_part_size;
   }
   /* Sanity check */
   if (dev->vol_poll_interval && dev->vol_poll_interval < 60) {
      dev->vol_poll_interval = 60;
   }

   if (!device->dev) {
      device->dev = dev;
   }

   /* If the device requires mount :
    * - Check that the mount point is available
    * - Check that (un)mount commands are defined
    */
   if (dev->is_file() && dev->requires_mount()) {
      if (!device->mount_point || stat(device->mount_point, &statp) < 0) {
         berrno be;
         dev->dev_errno = errno;
         Jmsg2(jcr, M_ERROR_TERM, 0, _("[SA0003] Unable to stat mount point %s: ERR=%s\n"),
            device->mount_point, be.bstrerror());
      }

      if (!device->mount_command || !device->unmount_command) {
         Jmsg0(jcr, M_ERROR_TERM, 0, _("[SA0004] Mount and unmount commands must defined for a device which requires mount.\n"));
      }
   }


   /* Sanity check */
   if (dev->max_block_size == 0) {
      max_bs = DEFAULT_BLOCK_SIZE;
   } else {
      max_bs = dev->max_block_size;
   }
   if (dev->min_block_size > max_bs) {
      Jmsg(jcr, M_ERROR_TERM, 0, _("[SA0005] Min block size > max on device %s\n"),
           dev->print_name());
   }
   if (dev->max_block_size > MAX_BLOCK_SIZE) {
      Jmsg3(jcr, M_ERROR, 0, _("[SA0006] Block size %u on device %s is too large, using default %u\n"),
         dev->max_block_size, dev->print_name(), DEFAULT_BLOCK_SIZE);
      dev->max_block_size = DEFAULT_BLOCK_SIZE ;
   }
   if (dev->max_block_size % TAPE_BSIZE != 0) {
      Jmsg3(jcr, M_WARNING, 0, _("[SW0007] Max block size %u not multiple of device %s block size=%d.\n"),
         dev->max_block_size, dev->print_name(), TAPE_BSIZE);
   }
   if (dev->max_volume_size != 0 && dev->max_volume_size < (dev->max_block_size << 4)) {
      Jmsg(jcr, M_ERROR_TERM, 0, _("[SA0008] Max Vol Size < 8 * Max Block Size for device %s\n"),
           dev->print_name());
   }

   dev->errmsg = get_pool_memory(PM_EMSG);
   *dev->errmsg = 0;

   if ((errstat = dev->init_mutex()) != 0) {
      berrno be;
      dev->dev_errno = errstat;
      Mmsg1(dev->errmsg, _("[SA0009] Unable to init mutex: ERR=%s\n"), be.bstrerror(errstat));
      Jmsg0(jcr, M_ERROR_TERM, 0, dev->errmsg);
   }
   if ((errstat = pthread_cond_init(&dev->wait, NULL)) != 0) {
      berrno be;
      dev->dev_errno = errstat;
      Mmsg1(dev->errmsg, _("[SA0010] Unable to init cond variable: ERR=%s\n"), be.bstrerror(errstat));
      Jmsg0(jcr, M_ERROR_TERM, 0, dev->errmsg);
   }
   if ((errstat = pthread_cond_init(&dev->wait_next_vol, NULL)) != 0) {
      berrno be;
      dev->dev_errno = errstat;
      Mmsg1(dev->errmsg, _("[SA0011] Unable to init cond variable: ERR=%s\n"), be.bstrerror(errstat));
      Jmsg0(jcr, M_ERROR_TERM, 0, dev->errmsg);
   }
   if ((errstat = pthread_mutex_init(&dev->spool_mutex, NULL)) != 0) {
      berrno be;
      dev->dev_errno = errstat;
      Mmsg1(dev->errmsg, _("[SA0012] Unable to init spool mutex: ERR=%s\n"), be.bstrerror(errstat));
      Jmsg0(jcr, M_ERROR_TERM, 0, dev->errmsg);
   }
   if ((errstat = dev->init_acquire_mutex()) != 0) {
      berrno be;
      dev->dev_errno = errstat;
      Mmsg1(dev->errmsg, _("[SA0013] Unable to init acquire mutex: ERR=%s\n"), be.bstrerror(errstat));
      Jmsg0(jcr, M_ERROR_TERM, 0, dev->errmsg);
   }
   if ((errstat = dev->init_freespace_mutex()) != 0) {
      berrno be;
      dev->dev_errno = errstat;
      Mmsg1(dev->errmsg, _("[SA0014] Unable to init freespace mutex: ERR=%s\n"), be.bstrerror(errstat));
      Jmsg0(jcr, M_ERROR_TERM, 0, dev->errmsg);
   }
   if ((errstat = dev->init_read_acquire_mutex()) != 0) {
      berrno be;
      dev->dev_errno = errstat;
      Mmsg1(dev->errmsg, _("[SA0015] Unable to init read acquire mutex: ERR=%s\n"), be.bstrerror(errstat));
      Jmsg0(jcr, M_ERROR_TERM, 0, dev->errmsg);
   }
   if ((errstat = dev->init_volcat_mutex()) != 0) {
      berrno be;
      dev->dev_errno = errstat;
      Mmsg1(dev->errmsg, _("[SA0016] Unable to init volcat mutex: ERR=%s\n"), be.bstrerror(errstat));
      Jmsg0(jcr, M_ERROR_TERM, 0, dev->errmsg);
   }
   if ((errstat = dev->init_dcrs_mutex()) != 0) {
      berrno be;
      dev->dev_errno = errstat;
      Mmsg1(dev->errmsg, _("[SA0017] Unable to init dcrs mutex: ERR=%s\n"), be.bstrerror(errstat));
      Jmsg0(jcr, M_ERROR_TERM, 0, dev->errmsg);
   }

   dev->set_mutex_priorities();

   dev->clear_opened();
   dev->attached_dcrs = New(dlist(dcr, &dcr->dev_link));
   Dmsg2(100, "init_dev: tape=%d dev_name=%s\n", dev->is_tape(), dev->dev_name);
   dev->initiated = true;
}

static DEVICE *load_driver(JCR *jcr, DEVRES *device)
{
   POOL_MEM fname(PM_FNAME);
   DEVICE *dev;
   driver_item *drv;
   const char *slash;
   void *pHandle;
   int len;
   newDriver_t newDriver;

   P(mutex);
   if (!me->plugin_directory) {
      Jmsg2(jcr, M_FATAL, 0,  _("[SF0018] Plugin directory not defined. Cannot load SD %s driver for device %s.\n"),
         driver_tab[device->dev_type - 1], device->hdr.name);
      V(mutex);
      return NULL;
   }
   len = strlen(me->plugin_directory);
   if (len == 0) {
      Jmsg0(jcr, M_FATAL, 0,  _("[SF0019] Plugin directory not defined. Cannot load drivers.\n"));
      V(mutex);
      return NULL;
   }

   if (IsPathSeparator(me->plugin_directory[len - 1])) {
      slash = "";
   } else {
      slash = "/";
   }

   Dmsg5(100, "loadable=%d type=%d loaded=%d name=%s handle=%p\n",
      !driver_tab[device->dev_type-1].builtin,
      device->dev_type,
      driver_tab[device->dev_type-1].loaded,
      driver_tab[device->dev_type-1].name,
      driver_tab[device->dev_type-1].handle);
   drv = &driver_tab[device->dev_type - 1];
   Mmsg(fname, "%s%sbacula-sd-%s-driver%s%s", me->plugin_directory, slash,
        drv->name, "-" VERSION, DRV_EXT);
   if (!drv->loaded) {
      Dmsg1(10, "Open SD driver at %s\n", fname.c_str());
      pHandle = dlopen(fname.c_str(), RTLD_NOW);
      if (pHandle) {
         Dmsg2(100, "Driver=%s handle=%p\n", drv->name, pHandle);
         /* Get global entry point */
         Dmsg1(10, "Lookup \"BaculaSDdriver\" in driver=%s\n", drv->name);
         newDriver = (newDriver_t)dlsym(pHandle, "BaculaSDdriver");
         Dmsg2(10, "Driver=%s entry point=%p\n", drv->name, newDriver);
         if (!newDriver) {
            const char *error = dlerror();
            Jmsg(NULL, M_ERROR, 0, _("[SE0003] Lookup of symbol \"BaculaSDdriver\" in driver %s for device %s failed: ERR=%s\n"),
               device->hdr.name, fname.c_str(), NPRT(error));
            Dmsg2(10, "Lookup of symbol \"BaculaSDdriver\" driver=%s failed: ERR=%s\n",
               fname.c_str(), NPRT(error));
            dlclose(pHandle);
            V(mutex);
            return NULL;
         }
         drv->handle = pHandle;
         drv->loaded = true;
         drv->newDriver = newDriver;
      } else {
         /* dlopen failed */
         const char *error = dlerror();
         Jmsg3(jcr, M_FATAL, 0, _("[SF0020] dlopen of SD driver=%s at %s failed: ERR=%s\n"),
              drv->name, fname.c_str(), NPRT(error));
         Dmsg2(000, "dlopen plugin %s failed: ERR=%s\n", fname.c_str(),
               NPRT(error));
         V(mutex);
         return NULL;
      }
   } else {
      Dmsg1(10, "SD driver=%s is already loaded.\n", drv->name);
   }

   /* Call driver initialization */
   dev = drv->newDriver(jcr, device);
   V(mutex);
   return dev;
}
