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
 * Definitions for using the Device functions in Bacula
 *  Tape and File storage access
 *
 * Kern Sibbald, MM
 */

/*
 * Some details of how device reservations work
 *
 * class DEVICE:
 *   set_load()       set to load volume
 *   needs_load()     volume must be loaded (i.e. set_load done)
 *   clear_load()     load done.
 *   set_unload()     set to unload volume
 *   needs_unload()    volume must be unloaded
 *   clear_unload()   volume unloaded
 *
 *    reservations are temporary until the drive is acquired
 *   inc_reserved()   increments num of reservations
 *   dec_reserved()   decrements num of reservations
 *   num_reserved()   number of reservations
 *
 * class DCR:
 *   set_reserved()   sets local reserve flag and calls dev->inc_reserved()
 *   clear_reserved() clears local reserve flag and calls dev->dec_reserved()
 *   is_reserved()    returns local reserved flag
 *   unreserve_device()  much more complete unreservation
 *
 */

#ifndef __DEV_H
#define __DEV_H 1

#undef DCR                            /* used by Bacula */

/* Return values from wait_for_sysop() */
enum {
   W_ERROR   = 1,
   W_TIMEOUT = 2,
   W_POLL    = 3,
   W_MOUNT   = 4,
   W_WAKE    = 5
};

/* Arguments to open_dev() */
enum {
   CREATE_READ_WRITE = 1,
   OPEN_READ_WRITE   = 2,
   OPEN_READ_ONLY    = 3,
   OPEN_WRITE_ONLY   = 4
};

/* Argument to print tape alerts */
enum alert_list_type {
   list_codes = 1,
   list_short = 2,
   list_long  = 3
};

enum alert_list_which {
   list_last = 1,
   list_all  = 2
};

typedef void (alert_cb)(void *alert_ctx, const char *short_msg,
   const char *long_msg, char *Volume, int severity,
   int flags, int alert, utime_t alert_time);

/* Aligned Data Disk Volume extension */
#define ADATA_EXTENSION ".add"

/* Generic status bits returned from status_dev() */
#define BMT_TAPE           (1<<0)     /* is tape device */
#define BMT_EOF            (1<<1)     /* just read EOF */
#define BMT_BOT            (1<<2)     /* at beginning of tape */
#define BMT_EOT            (1<<3)     /* end of tape reached */
#define BMT_SM             (1<<4)     /* DDS setmark */
#define BMT_EOD            (1<<5)     /* DDS at end of data */
#define BMT_WR_PROT        (1<<6)     /* tape write protected */
#define BMT_ONLINE         (1<<7)     /* tape online */
#define BMT_DR_OPEN        (1<<8)     /* tape door open */
#define BMT_IM_REP_EN      (1<<9)     /* immediate report enabled */


/* Bits for device capabilities */
#define CAP_EOF            (1<<0)     /* has MTWEOF */
#define CAP_BSR            (1<<1)     /* has MTBSR */
#define CAP_BSF            (1<<2)     /* has MTBSF */
#define CAP_FSR            (1<<3)     /* has MTFSR */
#define CAP_FSF            (1<<4)     /* has MTFSF */
#define CAP_EOM            (1<<5)     /* has MTEOM */
#define CAP_REM            (1<<6)     /* is removable media */
#define CAP_RACCESS        (1<<7)     /* is random access device */
#define CAP_AUTOMOUNT      (1<<8)     /* Read device at start to see what is there */
#define CAP_LABEL          (1<<9)     /* Label blank tapes */
#define CAP_ANONVOLS       (1<<10)    /* Mount without knowing volume name */
#define CAP_ALWAYSOPEN     (1<<11)    /* always keep device open */
#define CAP_AUTOCHANGER    (1<<12)    /* AutoChanger */
#define CAP_OFFLINEUNMOUNT (1<<13)    /* Offline before unmount */
#define CAP_STREAM         (1<<14)    /* Stream device */
#define CAP_BSFATEOM       (1<<15)    /* Backspace file at EOM */
#define CAP_FASTFSF        (1<<16)    /* Fast forward space file */
#define CAP_TWOEOF         (1<<17)    /* Write two eofs for EOM */
#define CAP_CLOSEONPOLL    (1<<18)    /* Close device on polling */
#define CAP_POSITIONBLOCKS (1<<19)    /* Use block positioning */
#define CAP_MTIOCGET       (1<<20)    /* Basic support for fileno and blkno */
#define CAP_REQMOUNT       (1<<21)    /* Require mount and unmount */
#define CAP_CHECKLABELS    (1<<22)    /* Check for ANSI/IBM labels */
#define CAP_BLOCKCHECKSUM  (1<<23)    /* Create/test block checksum */
#define CAP_LSEEK          (1<<24)    /* Has lseek function defined i.e. basically File storage */

/* Test state */
#define dev_state(dev, st_state) ((dev)->state & (st_state))

/* Device state bits */
#define ST_XXXXXX          (1<<0)     /* was ST_OPENED */
#define ST_XXXXX           (1<<1)     /* was ST_TAPE */
#define ST_XXXX            (1<<2)     /* was ST_FILE */
#define ST_XXX             (1<<3)     /* was ST_FIFO */
#define ST_XX              (1<<4)     /* was ST_DVD */
#define ST_X               (1<<5)     /* was ST_PROG */

#define ST_LABEL           (1<<6)     /* label found */
#define ST_MALLOC          (1<<7)     /* dev packet malloc'ed in init_dev() */
#define ST_APPEND          (1<<8)     /* ready for Bacula append */
#define ST_READ            (1<<9)     /* ready for Bacula read */
#define ST_EOT             (1<<10)    /* at end of tape */
#define ST_WEOT            (1<<11)    /* Got EOT on write */
#define ST_EOF             (1<<12)    /* Read EOF i.e. zero bytes */
#define ST_NEXTVOL         (1<<13)    /* Start writing on next volume */
#define ST_SHORT           (1<<14)    /* Short block read */
#define ST_MOUNTED         (1<<15)    /* the device is mounted to the mount point */
#define ST_MEDIA           (1<<16)    /* Media found in mounted device */
#define ST_OFFLINE         (1<<17)    /* set offline by operator */
#define ST_PART_SPOOLED    (1<<18)    /* spooling part */
#define ST_FREESPACE_OK    (1<<19)    /* Have valid freespace */
#define ST_NOSPACE         (1<<20)    /* No space on device */


/* Volume Catalog Information structure definition */
struct VOLUME_CAT_INFO {
   /* Media info for the current Volume */
   uint64_t VolCatBytes;              /* Total bytes written */
   uint64_t VolCatAmetaBytes;         /* Ameta bytes written */
   uint64_t VolCatAdataBytes;         /* Adata bytes written */
   uint64_t VolCatPadding;            /* Total padding bytes written */
   uint64_t VolCatAmetaPadding;       /* Ameta zeros (padding) written */
   uint64_t VolCatAdataPadding;       /* Adata zeros (padding) written */
   uint32_t VolCatBlocks;             /* Total blocks */
   uint32_t VolCatAmetaBlocks;        /* Ameta blocks */
   uint32_t VolCatAdataBlocks;        /* Adata blocks */
   uint32_t VolCatWrites;             /* Total writes this volume */
   uint32_t VolCatAmetaWrites;        /* Ameta writes this volume */
   uint32_t VolCatAdataWrites;        /* Adata writes this volume */
   uint32_t VolCatReads;              /* Total reads this volume */
   uint32_t VolCatAmetaReads;         /* Ameta reads this volume */
   uint32_t VolCatAdataReads;         /* Adata reads this volume */
   uint64_t VolCatRBytes;             /* Total bytes read */
   uint64_t VolCatAmetaRBytes;        /* Ameta bytes read */
   uint64_t VolCatAdataRBytes;        /* Adata bytes read */
   uint64_t VolCatHoleBytes;          /* Total hole bytes */
   uint64_t VolEndAddr;               /* Last Volume address */
   uint64_t VolLastPartBytes;         /* Bytes in last part */

   uint32_t VolCatHoles;              /* Number of holes */
   uint32_t VolCatJobs;               /* Number of jobs on this Volume */
   uint32_t VolCatFiles;              /* Number of files */
   uint32_t VolCatType;               /* Volume drive type */
   uint32_t VolCatParts;              /* Max number of cache parts */
   uint32_t VolCatCloudParts;         /* Max number of cloud parts */
   uint32_t VolCatMounts;             /* Number of mounts this volume */
   uint32_t VolCatErrors;             /* Number of errors this volume */
   uint32_t VolCatRecycles;           /* Number of recycles this volume */
   uint32_t EndFile;                  /* Last file number */
   uint32_t EndBlock;                 /* Last block number */

   int32_t  LabelType;                /* Bacula/ANSI/IBM */
   int32_t  Slot;                     /* >0=Slot loaded, 0=nothing, -1=unknown */
   uint32_t VolCatMaxJobs;            /* Maximum Jobs to write to volume */
   uint32_t VolCatMaxFiles;           /* Maximum files to write to volume */
   uint64_t VolCatMaxBytes;           /* Max bytes to write to volume */
   uint64_t VolCatCapacityBytes;      /* capacity estimate */
   btime_t  VolReadTime;              /* time spent reading */
   btime_t  VolWriteTime;             /* time spent writing this Volume */
   int64_t  VolMediaId;               /* MediaId */
   int64_t  VolScratchPoolId;         /* ScratchPoolId */
   utime_t  VolFirstWritten;          /* Time of first write */
   utime_t  VolLastWritten;           /* Time of last write */
   bool     InChanger;                /* Set if vol in current magazine */
   bool     is_valid;                 /* set if this data is valid */
   bool     VolEnabled;               /* set if volume enabled */
   bool     VolRecycle;               /* set if volume can be recycled */
   char VolCatStatus[20];             /* Volume status */
   char VolCatName[MAX_NAME_LENGTH];  /* Desired volume to mount */
};

class DEVRES;                         /* Device resource defined in stored_conf.h */
class DCR;                            /* forward reference */
class VOLRES;                         /* forward reference */

/*
 * Device structure definition. There is one of these for
 *  each physical device. Everything here is "global" to
 *  that device and effects all jobs using the device.
 */
class DEVICE: public SMARTALLOC {
public:
   int m_blocked;                     /* set if we must wait (i.e. change tape) */
   int m_count;                       /* Mutex use count -- DEBUG only */
   int m_num_reserved;                /* counter of device reservations */
   pthread_t m_pid;                   /* Thread that locked -- DEBUG only */
   int32_t m_slot;                    /* slot loaded in drive or -1 if none */
   bool m_unload;                     /* set when Volume must be unloaded */
   bool m_load;                       /* set when Volume must be loaded */
   bool m_wait;                       /* must wait for device to free volume */
   bool m_append_reserve;             /* reserved for append or read in m_num_reserved set */
   bthread_mutex_t m_mutex;           /* access control */
   bthread_mutex_t acquire_mutex;     /* mutex for acquire code */
   pthread_mutex_t read_acquire_mutex; /* mutex for acquire read code */
   pthread_mutex_t adata_mutex;       /* Lock for writing adata */
   pthread_mutex_t volcat_mutex;      /* VolCatInfo mutex */
   pthread_mutex_t dcrs_mutex;        /* Attached dcr mutex */
   pthread_mutex_t freespace_mutex;   /* mutex to compute the freespace */

public:
   DEVICE() {};
   virtual ~DEVICE() {};
   DEVICE * volatile swap_dev;        /* Swap vol from this device */
   dlist *attached_dcrs;              /* attached DCR list */
   bthread_mutex_t spool_mutex;       /* mutex for updating spool_size */
   pthread_cond_t wait;               /* thread wait variable */
   pthread_cond_t wait_next_vol;      /* wait for tape to be mounted */
   pthread_t no_wait_id;              /* this thread must not wait */
   int m_fd;                          /* file descriptor */
   int dev_prev_blocked;              /* previous blocked state */
   int num_waiting;                   /* number of threads waiting */
   int num_writers;                   /* number of writing threads */
   int capabilities;                  /* capabilities mask */
   int state;                         /* state mask */
   int dev_errno;                     /* Our own errno */
   int mode;                          /* read/write modes */
   int openmode;                      /* parameter passed to open_dev (useful to reopen the device) */
   int preserve;                      /* preserve open state */
   int dev_type;                      /* device type */
   uint32_t blocked_by;               /* JobId that blocked */
   bool enabled;                      /* Set when enabled */
   bool autoselect;                   /* Autoselect in autochanger */
   bool read_only;                    /* Device is read only */
   bool initiated;                    /* set when init_dev() called */
   bool m_is_worm;                    /* set for worm tape */
   bool m_shstore;                    /* Shares storage can be used */
   bool m_shstore_lock;               /* set if shared lock set */
   bool m_shstore_user_lock;          /* set if user set shared lock */
   bool m_shstore_register;           /* set if register key set */
   bool m_shstore_blocked;            /* Set if I am blocked */
   bool adata;                        /* set if adata device */
   int label_type;                    /* Bacula/ANSI/IBM label types */
   uint32_t drive_index;              /* Autochanger drive index (base 0) */
   POOLMEM *dev_name;                 /* Physical device name */
   POOLMEM *adev_name;                /* Aligned device name */
   POOLMEM *prt_name;                 /* Name used for display purposes */
   char *errmsg;                      /* nicely edited error message */
   uint32_t block_num;                /* current block number base 0 */
   uint32_t LastBlock;                /* last DEV_BLOCK number written to Volume */
   uint32_t file;                     /* current file number base 0 */
   uint64_t file_addr;                /* Current file read/write address */
   uint64_t file_size;                /* Current file size */
   uint32_t EndBlock;                 /* last block written */
   uint32_t EndFile;                  /* last file written */
   uint64_t EndAddr;                  /* Ending write addr */
   uint32_t min_block_size;           /* min block size */
   uint32_t max_block_size;           /* max block size */
   uint32_t max_concurrent_jobs;      /* maximum simultaneous jobs this drive */
   uint64_t max_volume_size;          /* max bytes to put on one volume */
   uint64_t max_file_size;            /* max file size to put in one file on volume */
   uint64_t volume_capacity;          /* advisory capacity */
   uint64_t max_spool_size;           /* maximum spool file size */
   uint64_t spool_size;               /* current spool size for this device */
   uint32_t max_rewind_wait;          /* max secs to allow for rewind */
   uint32_t max_open_wait;            /* max secs to allow for open */
   uint32_t padding_size;             /* adata block padding -- bytes */
   uint32_t file_alignment;           /* adata file alignment -- bytes */
   uint32_t adata_size;               /* adata block size */
   uint64_t adata_addr;               /* Next adata write address */

   uint64_t max_part_size;            /* max part size */
   uint64_t part_size;                /* current part size */
   uint32_t part;                     /* current part number (starts at 0) */
   /* state ST_FREESPACE_OK is set if free_space is valid */
   uint64_t free_space;               /* current free space on device */
   uint64_t total_space;              /* current used space on device */
   uint64_t devno;                    /* device id */
   uint64_t min_free_space;           /* Minimum free disk space */
   int free_space_errno;              /* indicates errno getting freespace */
   bool truncating;                   /* if set, we are currently truncating */

   utime_t  vol_poll_interval;        /* interval between polling Vol mount */
   DEVRES *device;                    /* pointer to Device Resource */
   VOLRES *vol;                       /* Pointer to Volume reservation item */
   btimer_t *tid;                     /* timer id */

   VOLUME_CAT_INFO VolCatInfo;        /* Volume Catalog Information */
   VOLUME_LABEL VolHdr;               /* Actual volume label (only needs to be correct when writing a new header) */
   char pool_name[MAX_NAME_LENGTH];   /* pool name */
   char pool_type[MAX_NAME_LENGTH];   /* pool type */
   char reserved_pool_name[MAX_NAME_LENGTH]; /* pool name for reserves */

   char LoadedVolName[MAX_NAME_LENGTH];  /* Last loaded Volume */
   char lock_holder[12];              /* holder of SCSI lock */
   bool poll;                         /* set to poll Volume */
   /* Device wait times ***FIXME*** look at durations */
   int min_wait;
   int max_wait;
   int max_num_wait;
   int wait_sec;
   int rem_wait_sec;
   int num_wait;

   btime_t last_timer;         /* used by read/write/seek to get stats (usec) */
   btime_t last_tick;          /* contains last read/write time (usec) */

   btime_t  DevReadTime;
   btime_t  DevWriteTime;
   uint64_t DevWriteBytes;
   uint64_t DevReadBytes;
   uint64_t usage;             /* Drive usage read+write bytes */

   /* Methods */
   btime_t get_timer_count(); /* return the last timer interval (ms) */

   void device_generic_init(JCR *jcr, DEVRES *device);
   int has_cap(int cap) const { return capabilities & cap; }
   void clear_cap(int cap) { capabilities &= ~cap; }
   void set_cap(int cap) { capabilities |= cap; }
   void set_worm(bool is_worm) { m_is_worm = is_worm; }
   bool do_checksum() const { return (capabilities & CAP_BLOCKCHECKSUM) != 0; }
   int is_autochanger() const { return capabilities & CAP_AUTOCHANGER; }
   int requires_mount() const { return capabilities & CAP_REQMOUNT; }
   int is_removable() const { return capabilities & CAP_REM; }
   bool is_tape() const { return (dev_type == B_TAPE_DEV ||
                                 dev_type == B_VTAPE_DEV); }
   bool is_file() const { return (dev_type == B_FILE_DEV) || is_aligned() || is_cloud(); }
   bool is_cloud() const { return dev_type == B_CLOUD_DEV; }
   bool is_adata() const { return dev_type == B_ADATA_DEV; }
   bool is_aligned() const { return dev_type == B_ALIGNED_DEV; }
   bool is_null() const { return dev_type == B_NULL_DEV; }
   bool is_fifo() const { return dev_type == B_FIFO_DEV; }
   bool is_vtl() const  { return dev_type == B_VTL_DEV; }
   bool is_vtape() const  { return dev_type == B_VTAPE_DEV; }
   bool is_worm() const { return m_is_worm; }
   bool is_open() const { return m_fd >= 0; }
   int is_offline() const { return state & ST_OFFLINE; }
   int is_labeled() const { return state & ST_LABEL; }
   int is_mounted() const { return state & ST_MOUNTED; }
   int is_unmountable() const { return ((is_file() && is_removable())); }
   int num_reserved() const { return m_num_reserved; };
   int is_part_spooled() const { return state & ST_PART_SPOOLED; }
   int have_media() const { return state & ST_MEDIA; }
   int is_short_block() const { return state & ST_SHORT; }
   int is_busy() const { return (state & ST_READ) || num_writers || num_reserved(); }
   bool is_reserved_for_read() const { return num_reserved() && !m_append_reserve; }
   bool is_ateot() const { return (state & ST_EOF) && (state & ST_EOT) && (state & ST_WEOT); }
   int at_eof() const { return state & ST_EOF; }
   int at_eot() const { return state & ST_EOT; }
   int at_weot() const { return state & ST_WEOT; }
   int can_append() const { return state & ST_APPEND; }
   int is_freespace_ok() const { return state & ST_FREESPACE_OK; }
   int is_nospace() const { return (is_freespace_ok() && (state & ST_NOSPACE)); };
   /*
    * can_write() is meant for checking at the end of a job to see
    * if we still have a tape (perhaps not if at end of tape
    * and the job is canceled).
    */
   int can_write() const { return is_open() && can_append() &&
                            is_labeled() && !at_weot(); }
   bool can_read() const   { return (state & ST_READ) != 0; }
   bool can_obtain_block() const { return
                    (m_blocked == BST_NOT_BLOCKED ||
                     m_blocked == BST_UNMOUNTED ||
                     m_blocked == BST_WAITING_FOR_SYSOP ||
                     m_blocked == BST_UNMOUNTED_WAITING_FOR_SYSOP); };
   bool waiting_for_mount() const { return
                    (m_blocked == BST_UNMOUNTED ||
                     m_blocked == BST_WAITING_FOR_SYSOP ||
                     m_blocked == BST_UNMOUNTED_WAITING_FOR_SYSOP); };
   bool must_unload() const { return m_unload; };
   bool must_load() const { return m_load; };
   const char *strerror() const;
   const char *archive_name() const;
   const char *name() const;
   const char *print_name() const;    /* Name for display purposes */
   void set_ateot(); /* in dev.c */
   void set_eot() { state |= ST_EOT; };
   void set_eof() { state |= ST_EOF; };
   void set_labeled() { state |= ST_LABEL; };
   void set_offline() { state |= ST_OFFLINE; };
   void set_mounted() { state |= ST_MOUNTED; };
   void set_media() { state |= ST_MEDIA; };
   void set_short_block() { state |= ST_SHORT; };
   void set_freespace_ok() { state |= ST_FREESPACE_OK; }
   void set_part_spooled(int val) { if (val) state |= ST_PART_SPOOLED; \
          else state &= ~ST_PART_SPOOLED;
   };
   void get_freespace(uint64_t *freeval, uint64_t *totalval); /* in dev.c */
   void set_freespace(uint64_t freeval, uint64_t totalval, int errnoval, bool valid); /* in dev.c */
   bool is_fs_nearly_full(uint64_t threshold);
   bool is_volume_to_unload() const { \
      return m_unload && strcmp(VolHdr.VolumeName, LoadedVolName) == 0; };
   void set_load() { m_load = true; };
   void set_wait() { m_wait = true; };
   void clear_wait() { m_wait = false; };
   bool must_wait() const { return m_wait; };
   void inc_reserved() { m_num_reserved++; }
   void set_mounted(int val) { if (val) state |= ST_MOUNTED; \
          else state &= ~ST_MOUNTED; };
   void dec_reserved() { m_num_reserved--; ASSERT(m_num_reserved>=0); };
   void set_read_reserve() { m_append_reserve = false; };
   void set_append_reserve() { m_append_reserve = true; };
   void clear_labeled() { state &= ~ST_LABEL; };
   void clear_offline() { state &= ~ST_OFFLINE; };
   void clear_eot() { state &= ~ST_EOT; };
   void clear_eof() { state &= ~ST_EOF; };
   void clear_opened() { m_fd = -1; };
   void clear_mounted() { state &= ~ST_MOUNTED; };
   void clear_media() { state &= ~ST_MEDIA; };
   void clear_short_block() { state &= ~ST_SHORT; };
   void clear_freespace_ok() { state &= ~ST_FREESPACE_OK; };
   void clear_unload() { m_unload = false; };
   void clear_load() { m_load = false; };
   char *bstrerror(void) { return errmsg; };
   char *print_errmsg() { return errmsg; };
   int32_t get_slot() const { return m_slot; };
   void setVolCatInfo(bool valid) { VolCatInfo.is_valid = valid; };
   bool haveVolCatInfo() const { return VolCatInfo.is_valid; };

   void clearVolCatBytes() {
      VolCatInfo.VolCatBytes = 0;
      VolCatInfo.VolCatAmetaBytes = 0;
      VolCatInfo.VolCatAdataBytes = 0;
   };

   char *getVolCatName() { return VolCatInfo.VolCatName; };

   void set_unload();            /* in dev.c */
   void clear_volhdr();          /* in dev.c */
   ssize_t read(void *buf, size_t len); /* in dev.c */
   ssize_t write(const void *buf, size_t len);  /* in dev.c */
   void edit_mount_codes(POOL_MEM &omsg, const char *imsg); /* in dev.c */
   bool offline_or_rewind(DCR *dcr); /* in dev.c */
   bool fsr(int num);            /* in dev.c */
   bool bsr(int num);            /* in dev.c */
   int32_t get_os_tape_file();   /* in dev.c */
   bool scan_dir_for_volume(DCR *dcr); /* in scan.c */
   void clrerror(int func);      /* in dev.c */
   void set_slot(int32_t slot);  /* in dev.c */
   void clear_slot();            /* in dev.c */
   bool update_freespace();              /* in dev.c */
   bool get_os_device_freespace();       /* in dev.c */
   void notify_newvol_in_attached_dcrs(const char *VolumeName); /* in dev.c */
   void notify_newfile_in_attached_dcrs();/* in dev.c */
   void attach_dcr_to_dev(DCR *dcr);      /* in acquire.c */
   void detach_dcr_from_dev(DCR *dcr);    /* in acquire.c */


   uint32_t get_file();                   /* in dev.c */
   uint32_t get_block_num();              /* in dev.c */

   int fd() const { return m_fd; };
   bool mount_file(int mount, int dottimout);
   void dump_volume_label();
   DEV_BLOCK *new_block(DCR *dcr, int size=0);


   /* Virtual functions that can be overridden */
   virtual void setVolCatName(const char *name);
   virtual void setVolCatStatus(const char *status);
   virtual void free_dcr_blocks(DCR *dcr);        /* in block_util.c */
   virtual void new_dcr_blocks(DCR *dcr);         /* in block_util.c */
   virtual boffset_t get_adata_size(DCR *dcr) { return (boffset_t)0; };
   virtual void updateVolCatBytes(uint64_t);      /* in dev.c */
   virtual void updateVolCatBlocks(uint32_t);     /* in dev.c */
   virtual void updateVolCatWrites(uint32_t);     /* in dev.c */
   virtual void updateVolCatReads(uint32_t);      /* in dev.c */
   virtual void updateVolCatReadBytes(uint64_t);  /* in dev.c */
   virtual void updateVolCatPadding(uint64_t);    /* in dev.c */
   virtual void updateVolCatHoleBytes(uint64_t);  /* in dev.c */
   virtual bool setVolCatAdataBytes(uint64_t) { return false; };
   virtual void set_volcatinfo_from_dcr(DCR *dcr);
   virtual bool allow_maxbytes_concurrency(DCR *dcr) { return true; };
   virtual bool flush_before_eos(DCR *dcr) { return true; };
   virtual void set_nospace();           /* in dev.c */
   virtual void set_append();            /* in dev.c */
   virtual void set_read();              /* in dev.c */
   virtual void clear_nospace();         /* in dev.c */
   virtual void clear_append();          /* in dev.c */
   virtual void clear_read();            /* in dev.c */
   virtual void device_specific_init(JCR *jcr, DEVRES *device);
   virtual void device_specific_open(DCR *dcr) { return; };
   virtual int d_ioctl(int fd, ioctl_req_t request, char *mt_com=NULL);
   virtual int d_open(const char *pathname, int flags);
   virtual int d_close(int fd);
   virtual ssize_t d_read(int fd, void *buffer, size_t count);
   virtual ssize_t d_write(int fd, const void *buffer, size_t count);
   virtual boffset_t lseek(DCR *dcr, boffset_t offset, int whence);
   virtual bool update_pos(DCR *dcr);
   virtual bool rewind(DCR *dcr);
   virtual bool truncate(DCR *dcr);
   virtual int  truncate_cache(DCR *dcr, const char *VolName, int64_t *size) { return 0; };
   virtual bool get_cloud_volumes_list(DCR* dcr, alist *volumes, POOLMEM *&err) { pm_strcpy(err, "Not implemented"); return false;};
   virtual bool get_cloud_volume_parts_list(DCR *dcr, const char *VolumeName, ilist *parts, POOLMEM *&err) { pm_strcpy(err, "Not implemented"); return false; };
   virtual uint32_t get_cloud_upload_transfer_status(POOL_MEM &msg, bool verbose) { pm_strcpy(msg, "Not implemented"); return 0; };
   virtual uint32_t get_cloud_download_transfer_status(POOL_MEM &msg, bool verbose) { pm_strcpy(msg, "Not implemented"); return 0; };
   virtual bool upload_cache(DCR *dcr, const char *VolName, POOLMEM *&err) { return true; };
   virtual bool open_device(DCR *dcr, int omode) = 0;
   virtual bool open_next_part(DCR *dcr);
   virtual bool close(DCR *dcr);                /* in dev.c */
   virtual void term(DCR *dcr);                 /* in dev.c */
   virtual bool close_part(DCR *dcr);
   virtual bool reposition(DCR *dcr, uint64_t raddr);
   virtual bool mount(int timeout);
   virtual bool unmount(int timeout);
   virtual int read_dev_volume_label(DCR *dcr); /* in label.c */
   virtual bool write_volume_label_to_block(DCR *dcr);  /* in label.c */
   virtual bool write_volume_label(DCR *dcr,
                   const char *VolName, const char *PoolName,
                   bool relabel, bool no_prelabel);  /* in label.c */
   virtual bool write_volume_label_to_dev(DCR *dcr,
                   const char *VolName, const char *PoolName,
                   bool relabel, bool no_prelabel);  /* in label.c */
   virtual bool rewrite_volume_label(DCR *dcr, bool recycle);  /* in label.c */
   virtual bool is_eod_valid(DCR *dcr);         /* in dev.c */
   virtual bool eod(DCR *dcr);                  /* in dev.c */
   virtual bool weof(DCR *dcr, int num);        /* in dev.c */
   virtual bool end_of_volume(DCR *dcr) { return true; };
   virtual bool start_of_job(DCR *dcr) {return true; };
   virtual bool end_of_job(DCR *dcr) {return true; };
   virtual bool is_indexed() { return true; };
   virtual void set_ateof();                    /* in dev.c */
   virtual const char *print_type() = 0;        /* in dev.c */
   virtual DEVICE *get_dev(DCR *dcr);           /* in dev.c */
   virtual uint32_t get_hi_addr();              /* in dev.c */
   virtual uint32_t get_low_addr();             /* in dev.c */
   virtual uint32_t get_hi_addr(boffset_t addr);    /* in dev.c */
   virtual uint32_t get_low_addr(boffset_t addr);   /* in dev.c */
   virtual uint64_t get_full_addr();                /* in dev.c */
   virtual uint64_t get_full_addr(boffset_t addr);  /* in dev.c */
   virtual uint64_t get_full_addr(uint32_t hi, uint32_t low); /* in dev.c */
   virtual char *print_addr(char *buf, int32_t maxlen);
   virtual char *print_addr(char *buf, int32_t maxlen, boffset_t addr);
   virtual bool do_size_checks(DCR *dcr, DEV_BLOCK *block); /* in dev.c */

   virtual bool get_tape_worm(DCR *dcr);
   virtual bool get_tape_alerts(DCR *dcr);
   virtual void show_tape_alerts(DCR *dcr, alert_list_type type,
      alert_list_which which, alert_cb alert_callback);
   virtual int delete_alerts();

   /* These could probably be made tape_dev only */
   virtual bool bsf(int count) { return true; }
   virtual bool fsf(int num) { return true; }
   virtual bool offline(DCR *dcr) { return true; }
   virtual void lock_door() { return; }
   virtual void unlock_door() { return; }

   virtual bool write_adata_label(DCR *dcr, DEV_RECORD *rec) { return false; };
   virtual void write_adata(DCR *dcr, DEV_RECORD *rec) { return; };
   virtual void write_cont_adata(DCR *dcr, DEV_RECORD *rec) { return; };
   virtual int  write_adata_rechdr(DCR *dcr, DEV_RECORD *rec) { return -1; };
   virtual bool have_adata_header(DCR *dcr, DEV_RECORD *rec,
             int32_t  FileIndex, int32_t  Stream, uint32_t VolSessionId)
             { return false; };
   virtual bool read_adata_record_header(DCR *dcr, DEV_BLOCK *block,
            DEV_RECORD *rec) { return false; };
   virtual void read_adata_block_header(DCR *dcr) { return; };
   virtual int read_adata(DCR *dcr, DEV_RECORD *rec) { return -1; };
   virtual void select_data_stream(DCR *dcr, DEV_RECORD *rec) { return; };
   virtual bool flush_block(DCR *dcr);    /* in block_util.c */
   virtual bool do_pre_write_checks(DCR *dcr, DEV_RECORD *rec) { return true; };


   /*
    * Locking and blocking calls
    */
#ifdef DEV_DEBUG_LOCK
   virtual void dbg_Lock(const char *, int);                        /* in lock.c */
   virtual void dbg_Unlock(const char *, int);                      /* in lock.c */
   virtual void dbg_rLock(const char *, int, bool locked=false);    /* in lock.c */
   virtual void dbg_rUnlock(const char *, int);                     /* in lock.c */
#else
   virtual void Lock();                           /* in lock.c */
   virtual void Unlock();                         /* in lock.c */
   virtual void rLock(bool locked=false);         /* in lock.c */
   virtual void rUnlock();                        /* in lock.c */
#endif

#ifdef  SD_DEBUG_LOCK
   virtual void dbg_Lock_acquire(const char *, int);                /* in lock.c */
   virtual void dbg_Unlock_acquire(const char *, int);              /* in lock.c */
   virtual void dbg_Lock_read_acquire(const char *, int);           /* in lock.c */
   virtual void dbg_Unlock_read_acquire(const char *, int);         /* in lock.c */
   virtual void dbg_Lock_VolCatInfo(const char *, int);             /* in lock.c */
   virtual void dbg_Unlock_VolCatInfo(const char *, int);           /* in lock.c */
#else
   virtual void Lock_acquire();                   /* in lock.c */
   virtual void Unlock_acquire();                 /* in lock.c */
   virtual void Lock_read_acquire();              /* in lock.c */
   virtual void Unlock_read_acquire();            /* in lock.c */
   virtual void Lock_VolCatInfo();                /* in lock.c */
   virtual void Unlock_VolCatInfo();              /* in lock.c */
#endif
   virtual void dblock(int why);                  /* in lock.c */
   virtual void dunblock(bool locked=false);      /* in lock.c */

   /* use obtain_device_block() macro */
   bool _obtain_device_block(const char *file, int line,
                             bsteal_lock_t *hold, int retry, int state);


   int init_mutex();                      /* in lock.c */
   int init_acquire_mutex();              /* in lock.c */
   int init_read_acquire_mutex();         /* in lock.c */
   int init_volcat_mutex();               /* in lock.c */
   int init_dcrs_mutex();                 /* in lock.c */
   int init_freespace_mutex();            /* in lock.c */
   void set_mutex_priorities();           /* in lock.c */
   int next_vol_timedwait(const struct timespec *timeout);  /* in lock.c */
   void Lock_dcrs() { P(dcrs_mutex); };
   void Unlock_dcrs() { V(dcrs_mutex); };
   bool is_device_unmounted();            /* in lock.c */
   void set_blocked(int block) { m_blocked = block; };
   int blocked() const { return m_blocked; };
   bool is_blocked() const { return m_blocked != BST_NOT_BLOCKED; };
   const char *print_blocked() const;     /* in dev.c */
   void open_tape_device(DCR *dcr, int omode);   /* in dev.c */
   void open_file_device(DCR *dcr, int omode);   /* in dev.c */

private:
   bool mount_tape(int mount, int dotimeout); /* in dev.c */
protected:
   void set_mode(int omode);                     /* in dev.c */
};
inline const char *DEVICE::strerror() const { return errmsg; }
inline const char *DEVICE::archive_name() const { return dev_name; }
inline const char *DEVICE::print_name() const { return NPRT(prt_name); }


#define CHECK_BLOCK_NUMBERS    true
#define NO_BLOCK_NUMBER_CHECK  false

/*
 * Device Context (or Control) Record.
 *  There is one of these records for each Job that is using
 *  the device. Items in this record are "local" to the Job and
 *  do not affect other Jobs. Note, a job can have multiple
 *  DCRs open, each pointing to a different device.
 * Normally, there is only one JCR thread per DCR. However, the
 *  big and important exception to this is when a Job is being
 *  canceled. At that time, there may be two threads using the
 *  same DCR. Consequently, when creating/attaching/detaching
 *  and freeing the DCR we must lock it (m_mutex).
 */
class DCR {
private:
   bool m_dev_locked;                 /* set if dev already locked */
   int m_dev_lock;                    /* non-zero if rLock already called */
   bool m_reserved;                   /* set if reserved device */
   bool m_found_in_use;               /* set if a volume found in use */
   bool m_writing;                    /* set when DCR used for writing */

public:
   dlink dev_link;                    /* link to attach to dev */
   JCR *jcr;                          /* pointer to JCR */
   DEVICE * volatile dev;             /* pointer to device */
   DEVICE *adata_dev;                 /* pointer to adata dev */
   DEVICE *ameta_dev;                 /* pointer to ameta_dev */
   DEVRES *device;                    /* pointer to device resource */
   DEV_BLOCK *block;                  /* pointer to block */
   DEV_BLOCK *adata_block;            /* aligned data block */
   DEV_BLOCK *ameta_block;            /* aligned meta data block */
   DEV_RECORD *rec;                   /* pointer to record */

   VOL_LIST *CurrentVol;              /* From JCR::VolList, freed at the end, passed to records */

   alist *uploads;                    /* Current upload transfers to the cloud */
   alist *downloads;                  /* Current donwload transfers from the cloud */

   pthread_t tid;                     /* Thread running this dcr */
   int spool_fd;                      /* fd if spooling */
   int crc32_type;                    /* Handle bad CRC32 on Solaris for volumes written with 8.4 */
   bool adata_label;                  /* writing adata label block */
   bool adata;                        /* writing aligned data block */
   bool spool_data;                   /* set to spool data */
   bool spooling;                     /* set when actually spooling */
   bool despooling;                   /* set when despooling */
   bool despool_wait;                 /* waiting for despooling */
   bool NewVol;                       /* set if new Volume mounted */
   bool WroteVol;                     /* set if Volume written */
   bool NewFile;                      /* set when EOF written */
   bool reserved_volume;              /* set if we reserved a volume */
   bool any_volume;                   /* Any OK for dir_find_next... */
   bool attached_to_dev;              /* set when attached to dev */
   bool keep_dcr;                     /* do not free dcr in release_dcr */
   bool no_mount_request;             /* do not submit any mount request */
   bool reading_label;                /* Reading volume label */
   bool discard_invalid_records;      /* we should not try to assemble invalid records */
   bool force_update_volume_info;     /* update the volume information, no matter the job type */

   uint32_t VolFirstIndex;            /* First file index this Volume */
   uint32_t VolLastIndex;             /* Last file index this Volume */
   int32_t FileIndex;                 /* Current File Index */
   uint64_t StartAddr;                /* Starting write addr */
   uint64_t EndAddr;                  /* Ending write addr */
   int64_t  VolMediaId;               /* MediaId */
   int64_t job_spool_size;            /* Current job spool size */
   int64_t max_job_spool_size;        /* Max job spool size */
   char VolumeName[MAX_NAME_LENGTH];  /* Volume name */
   char pool_name[MAX_NAME_LENGTH];   /* pool name */
   char pool_type[MAX_NAME_LENGTH];   /* pool type */
   char media_type[MAX_NAME_LENGTH];  /* media type */
   char dev_name[MAX_NAME_LENGTH];    /* dev name */
   int Copy;                          /* identical copy number */
   int Stripe;                        /* RAIT stripe */
   VOLUME_CAT_INFO VolCatInfo;        /* Catalog info for desired volume */

   /* Methods */
   void set_no_mount_request() { no_mount_request = true; }; /* Just fail in case of mount request */
   void set_dev(DEVICE *ndev) { dev = ndev; ameta_dev = ndev; };
   void set_adata() { if (adata_dev) {dev = adata_dev; block = adata_block;} };
   void set_ameta() { dev = ameta_dev; block = ameta_block; };
   void set_dev_locked() { m_dev_locked = true; };
   void set_writing() { m_writing = true; };
   void clear_writing() { m_writing = false; };
   bool is_reading() const { return !m_writing; };
   bool is_writing() const { return m_writing; };
   void clear_dev_locked() { m_dev_locked = false; };
   void inc_dev_lock() { m_dev_lock++; };
   void dec_dev_lock() { m_dev_lock--; };
   bool found_in_use() const { return m_found_in_use; };
   void set_found_in_use() { m_found_in_use = true; };
   void clear_found_in_use() { m_found_in_use = false; };
   bool is_reserved() const { return m_reserved; };
   bool is_dev_locked() const { return m_dev_locked; }
   void setVolCatInfo(bool valid) { VolCatInfo.is_valid = valid; };
   bool haveVolCatInfo() const { return VolCatInfo.is_valid; };
   void setVolCatName(const char *name) {
     bstrncpy(VolCatInfo.VolCatName, name, sizeof(VolCatInfo.VolCatName));
     setVolCatInfo(false);
   };
   char *getVolCatName() { return VolCatInfo.VolCatName; };
   bool write_final_block_to_device() { return write_block_to_device(true); };

   /* Methods in autochanger.c */
   bool is_virtual_autochanger();

   /* Methods in lock.c */
   void dblock(int why) { dev->dblock(why); }

   /* Methods in record.c */
   bool write_record(DEV_RECORD *rec);

   /* Methods in read_record.c */
   bool read_records(
           bool record_cb(DCR *dcr, DEV_RECORD *rec),
           bool mount_cb(DCR *dcr));
   bool try_repositioning(DEV_RECORD *rec);
   BSR *position_to_first_file();

   /* Methods in reserve.c */
   void clear_reserved();
   void set_reserved_for_read();
   void set_reserved_for_append();
   void unreserve_device(bool locked);

   /* Methods in vol_mgr.c */
   bool can_i_use_volume();
   bool can_i_write_volume();

   /* Methods in mount.c */
   bool mount_next_write_volume();
   bool mount_next_read_volume();
   void mark_volume_in_error();
   void mark_volume_read_only();
   void mark_volume_not_inchanger();
   int try_autolabel(bool opened);
   bool find_a_volume();
   bool is_suitable_volume_mounted();
   int check_volume_label(bool &ask, bool &autochanger);
   void release_volume();
   void do_swapping(bool is_writing);
   bool do_unload();
   bool do_load(bool is_writing);
   bool is_tape_position_ok();

   /* Methods in block.c */
   bool write_block_to_device(bool final=false);
   bool write_block_to_dev();
   bool read_block_from_device(bool check_block_numbers);
   bool read_block_from_dev(bool check_block_numbers);


};

/* Get some definition of function to position
 *  to the end of the medium in MTEOM. System
 *  dependent. Arrgggg!
 */
#ifndef MTEOM
#ifdef  MTSEOD
#define MTEOM MTSEOD
#endif
#ifdef MTEOD
#undef MTEOM
#define MTEOM MTEOD
#endif
#endif

#endif /* __DEV_H */
