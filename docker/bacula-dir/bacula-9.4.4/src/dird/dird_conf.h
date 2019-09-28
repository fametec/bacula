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
 * Director specific configuration and defines
 *
 *     Kern Sibbald, Feb MM
 */

/* NOTE:  #includes at the end of this file */

/*
 * Resource codes -- they must be sequential for indexing
 */
enum {
   R_DIRECTOR = 1001,
   R_CLIENT,
   R_JOB,
   R_STORAGE,
   R_CATALOG,
   R_SCHEDULE,
   R_FILESET,
   R_POOL,
   R_MSGS,
   R_COUNTER,
   R_CONSOLE,
   R_JOBDEFS,
   R_DEVICE,       /* This is the real last device class */

   R_AUTOCHANGER,  /* Alias for R_STORAGE after R_LAST */
   R_FIRST = R_DIRECTOR,
   R_LAST  = R_DEVICE                 /* keep this updated */
};


/*
 * Some resource attributes
 */
enum {
   R_NAME = 1020,
   R_ADDRESS,
   R_PASSWORD,
   R_TYPE,
   R_BACKUP
};

/* Options for FileSet keywords */
struct s_fs_opt {
   const char *name;
   int keyword;
   const char *option;
};

/* Job Level keyword structure */
struct s_jl {
   const char *level_name;                 /* level keyword */
   int32_t  level;                         /* level */
   int32_t  job_type;                      /* JobType permitting this level */
};

/* Job Type keyword structure */
struct s_jt {
   const char *type_name;
   int32_t job_type;
};

/* Definition of the contents of each Resource */
/* Needed for forward references */
class SCHED;
class CLIENT;
class FILESET;
class POOL;
class RUN;
class DEVICE;
class RUNSCRIPT;

/*
 *   Director Resource
 *
 */
class DIRRES {
public:
   RES   hdr;
   dlist *DIRaddrs;
   dlist *DIRsrc_addr;                /* address to source connections from */
   char *password;                    /* Password for UA access */
   char *query_file;                  /* SQL query file */
   char *working_directory;           /* WorkingDirectory */
   const char *scripts_directory;     /* ScriptsDirectory */
   const char *plugin_directory;      /* Plugin Directory */
   char *pid_directory;               /* PidDirectory */
   char *subsys_directory;            /* SubsysDirectory */
   MSGS *messages;                    /* Daemon message handler */
   uint32_t MaxConcurrentJobs;        /* Max concurrent jobs for whole director */
   uint32_t MaxSpawnedJobs;           /* Max Jobs that can be started by Migration/Copy */
   uint32_t MaxConsoleConnect;        /* Max concurrent console session */
   uint32_t MaxReload;                /* Maximum reload requests */
   utime_t FDConnectTimeout;          /* timeout for connect in seconds */
   utime_t SDConnectTimeout;          /* timeout in seconds */
   utime_t heartbeat_interval;        /* Interval to send heartbeats */
   char *tls_ca_certfile;             /* TLS CA Certificate File */
   char *tls_ca_certdir;              /* TLS CA Certificate Directory */
   char *tls_certfile;                /* TLS Server Certificate File */
   char *tls_keyfile;                 /* TLS Server Key File */
   char *tls_dhfile;                  /* TLS Diffie-Hellman Parameters */
   alist *tls_allowed_cns;            /* TLS Allowed Clients */
   TLS_CONTEXT *tls_ctx;              /* Shared TLS Context */
   utime_t stats_retention;           /* Stats retention period in seconds */
   bool comm_compression;             /* Enable comm line compression */
   bool tls_authenticate;             /* Authenticated with TLS */
   bool tls_enable;                   /* Enable TLS */
   bool tls_require;                  /* Require TLS */
   bool tls_verify_peer;              /* TLS Verify Client Certificate */
   char *verid;                       /* Custom Id to print in version command */
   /* Methods */
   char *name() const;
};

inline char *DIRRES::name() const { return hdr.name; }

/*
 * Device Resource
 *  This resource is a bit different from the other resources
 *  because it is not defined in the Director
 *  by DEVICE { ... }, but rather by a "reference" such as
 *  DEVICE = xxx; Then when the Director connects to the
 *  SD, it requests the information about the device.
 */
class DEVICE {
public:
   RES hdr;

   bool found;                        /* found with SD */
   int32_t num_writers;               /* number of writers */
   int32_t max_writers;               /* = 1 for files */
   int32_t reserved;                  /* number of reserves */
   int32_t num_drives;                /* for autochanger */
   bool autochanger;                  /* set if device is autochanger */
   bool open;                         /* drive open */
   bool append;                       /* in append mode */
   bool read;                         /* in read mode */
   bool labeled;                      /* Volume name valid */
   bool offline;                      /* not available */
   bool autoselect;                   /* can be selected via autochanger */
   uint32_t PoolId;
   char ChangerName[MAX_NAME_LENGTH];
   char VolumeName[MAX_NAME_LENGTH];
   char MediaType[MAX_NAME_LENGTH];

   /* Methods */
   char *name() const;
};

inline char *DEVICE::name() const { return hdr.name; }

/*
 * Console ACL positions
 */
enum {
   Job_ACL = 0,
   Client_ACL,
   Storage_ACL,
   Schedule_ACL,
   Run_ACL,
   Pool_ACL,
   Command_ACL,
   FileSet_ACL,
   Catalog_ACL,
   Where_ACL,
   PluginOptions_ACL,
   RestoreClient_ACL,
   BackupClient_ACL,
   Directory_ACL,               /* List of directories that can be accessed in the restore tree */
   Num_ACL                      /* keep last */
};

/*
 *    Console Resource
 */
class CONRES {
public:
   RES   hdr;
   char *password;                    /* UA server password */
   alist *ACL_lists[Num_ACL];         /* pointers to ACLs */
   char *tls_ca_certfile;             /* TLS CA Certificate File */
   char *tls_ca_certdir;              /* TLS CA Certificate Directory */
   char *tls_certfile;                /* TLS Server Certificate File */
   char *tls_keyfile;                 /* TLS Server Key File */
   char *tls_dhfile;                  /* TLS Diffie-Hellman Parameters */
   alist *tls_allowed_cns;            /* TLS Allowed Clients */
   TLS_CONTEXT *tls_ctx;              /* Shared TLS Context */
   bool tls_authenticate;             /* Authenticated with TLS */
   bool tls_enable;                   /* Enable TLS */
   bool tls_require;                  /* Require TLS */
   bool tls_verify_peer;              /* TLS Verify Client Certificate */

   /* Methods */
   char *name() const;
};

inline char *CONRES::name() const { return hdr.name; }


/*
 *   Catalog Resource
 *
 */
class CAT {
public:
   RES   hdr;

   uint32_t db_port;                  /* Port */
   char *db_address;                  /* host name for remote access */
   char *db_socket;                   /* Socket for local access */
   char *db_password;
   char *db_user;
   char *db_name;
   char *db_driver;                   /* Select appropriate driver */
   char *db_ssl_mode;                 /* specifies the security state of the connection to the server */
   char *db_ssl_key;                  /* the path name to the key file */
   char *db_ssl_cert;                 /* the path name to the certificate file */
   char *db_ssl_ca;                   /* the path name to the certificate authority file */
   char *db_ssl_capath;               /* the path name to a directory that contains trusted SSL CA certificates in PEM format */
   char *db_ssl_cipher;               /* a list of permissible ciphers to use for SSL encryption */
   uint32_t mult_db_connections;      /* set for multiple db connections */
   bool disable_batch_insert;         /* set to disable batch inserts */

   /* Methods */
   char *name() const;
   char *display(POOLMEM *dst);       /* Get catalog information */
};

inline char *CAT::name() const { return hdr.name; }

class CLIENT_GLOBALS {
public:
   dlink link;                        /* double link */
   const char *name;                  /* resource name */
   int32_t NumConcurrentJobs;         /* number of concurrent jobs running */
   char *SetIPaddress;                /* address from SetIP command */
   int enabled;                       /* -1: not set, 0 disabled, 1 enabled */
};

/*
 *   Client Resource
 *
 */
class CLIENT {
public:
   RES hdr;
   CLIENT_GLOBALS *globals;           /* global variables */
   uint32_t FDport;                   /* Where File daemon listens */
   utime_t FileRetention;             /* file retention period in seconds */
   utime_t JobRetention;              /* job retention period in seconds */
   utime_t SnapRetention;             /* Snapshot retention period in seconds */
   utime_t heartbeat_interval;        /* Interval to send heartbeats */
   char *client_address;              /* Client address from .conf file */
   char *fd_storage_address;          /* Storage address to use from FD side  */
   char *password;
   CAT *catalog;                      /* Catalog resource */
   int32_t MaxConcurrentJobs;         /* Maximum concurrent jobs */
   char *tls_ca_certfile;             /* TLS CA Certificate File */
   char *tls_ca_certdir;              /* TLS CA Certificate Directory */
   char *tls_certfile;                /* TLS Client Certificate File */
   char *tls_keyfile;                 /* TLS Client Key File */
   alist *tls_allowed_cns;            /* TLS Allowed Clients */
   TLS_CONTEXT *tls_ctx;              /* Shared TLS Context */
   bool tls_authenticate;             /* Authenticated with TLS */
   bool tls_enable;                   /* Enable TLS */
   bool tls_require;                  /* Require TLS */
   bool Enabled;                      /* Set if client enabled */
   bool AutoPrune;                    /* Do automatic pruning? */
   bool sd_calls_client;              /* SD calls the client */
   int64_t max_bandwidth;             /* Limit speed on this client */

   /* Methods */
   char *name() const;
   void create_client_globals();
   int32_t getNumConcurrentJobs();
   void setNumConcurrentJobs(int32_t num);
   char *address(POOLMEM *&buf);
   void setAddress(char *addr);
   bool is_enabled();
   void setEnabled(bool val);
};

inline char *CLIENT::name() const { return hdr.name; }


class STORE_GLOBALS {
public:
   dlink link;                        /* double link */
   const char *name;                  /* resource name */
   int32_t NumConcurrentJobs;         /* number of concurrent jobs running */
   int32_t NumConcurrentReadJobs;     /* number of concurrent read jobs running */
   int enabled;                       /* -1: not set, 0: disabled, 1: enabled */
};


/*
 *   Store Resource
 *
 */
class STORE {
public:
   RES   hdr;
   STORE_GLOBALS *globals;            /* global variables */
   uint32_t SDport;                   /* port where Directors connect */
   uint32_t SDDport;                  /* data port for File daemon */
   char *address;
   char *fd_storage_address;          /* Storage address to use from FD side  */
   char *password;
   char *media_type;
   alist *device;                     /* Alternate devices for this Storage */
   int32_t MaxConcurrentJobs;         /* Maximum concurrent jobs */
   int32_t MaxConcurrentReadJobs;     /* Maximum concurrent jobs reading */
   char *tls_ca_certfile;             /* TLS CA Certificate File */
   char *tls_ca_certdir;              /* TLS CA Certificate Directory */
   char *tls_certfile;                /* TLS Client Certificate File */
   char *tls_keyfile;                 /* TLS Client Key File */
   TLS_CONTEXT *tls_ctx;              /* Shared TLS Context */
   bool tls_authenticate;             /* Authenticated with TLS */
   bool tls_enable;                   /* Enable TLS */
   bool tls_require;                  /* Require TLS */
   bool Enabled;                      /* Set if device is enabled */
   bool AllowCompress;                /* set if this Storage should allow jobs to enable compression */
   bool autochanger;                  /* set if we are part of an autochanger */
   POOLMEM *ac_group;                 /* Autochanger StorageId group */
   STORE *changer;                    /* points to autochanger */
   STORE *shared_storage;             /* points to shared storage */
   int64_t StorageId;                 /* Set from Storage DB record */
   utime_t heartbeat_interval;        /* Interval to send heartbeats */
   uint32_t drives;                   /* number of drives in autochanger */

   /* Methods */
   char *dev_name() const;
   char *name() const;
   void create_store_globals();
   int32_t getNumConcurrentJobs();
   int32_t getNumConcurrentReadJobs();
   void setNumConcurrentJobs(int32_t num);
   void setNumConcurrentReadJobs(int32_t num);
   bool is_enabled();
   void setEnabled(bool val);
};

inline char *STORE::dev_name() const
{
   DEVICE *dev = (DEVICE *)device->first();
   return dev->name();
}

inline char *STORE::name() const { return hdr.name; }

/*
 * This is a sort of "unified" store that has both the
 *  storage pointer and the text of where the pointer was
 *  found.
 */
class USTORE {
public:
   STORE *store;
   POOLMEM *store_source;

   /* Methods */
   USTORE() { store = NULL; store_source = get_pool_memory(PM_MESSAGE);
              *store_source = 0; };
   ~USTORE() { destroy(); }
   void set_source(const char *where);
   void destroy();
};

inline void USTORE::destroy()
{
   if (store_source) {
      free_pool_memory(store_source);
      store_source = NULL;
   }
}


inline void USTORE::set_source(const char *where)
{
   if (!store_source) {
      store_source = get_pool_memory(PM_MESSAGE);
   }
   pm_strcpy(store_source, where);
}

class JOB_GLOBALS {
public:
   dlink link;                        /* double link */
   const char *name;                  /* resource name */
   int32_t NumConcurrentJobs;         /* number of concurrent jobs running */
   int enabled;                       /* -1: disabled, 0: disabled, 1: Enabled */
};

/*
 *   Job Resource
 */
class JOB {
public:
   RES   hdr;
   JOB_GLOBALS *globals;              /* global variables */
   uint32_t JobType;                  /* job type (backup, verify, restore */
   uint32_t JobLevel;                 /* default backup/verify level */
   uint32_t RestoreJobId;             /* What -- JobId to restore */
   uint32_t replace;                  /* How (overwrite, ..) */
   uint32_t selection_type;

   int32_t  Priority;                 /* Job priority */
   int32_t  RescheduleTimes;          /* Number of times to reschedule job */

   char *RestoreWhere;                /* Where on disk to restore -- directory */
   char *RegexWhere;                  /* RegexWhere option */
   char *strip_prefix;                /* remove prefix from filename  */
   char *add_prefix;                  /* add prefix to filename  */
   char *add_suffix;                  /* add suffix to filename -- .old */
   char *RestoreBootstrap;            /* Bootstrap file */
   char *RestoreClient;               /* Who to restore */
   char *PluginOptions;               /* Options to pass to plugin */
   union {
      char *WriteBootstrap;           /* Where to write bootstrap Job updates */
      char *WriteVerifyList;          /* List of changed files */
   };
   utime_t MaxRunTime;                /* max run time in seconds */
   utime_t MaxWaitTime;               /* max blocking time in seconds */
   utime_t FullMaxRunTime;            /* Max Full job run time */
   utime_t DiffMaxRunTime;            /* Max Differential job run time */
   utime_t IncMaxRunTime;             /* Max Incremental job run time */
   utime_t MaxStartDelay;             /* max start delay in seconds */
   utime_t MaxRunSchedTime;           /* max run time in seconds from Scheduled time*/
   utime_t RescheduleInterval;        /* Reschedule interval */
   utime_t MaxFullInterval;           /* Maximum time interval between Fulls */
   utime_t MaxVirtualFullInterval;    /* Maximum time interval between Virtual Fulls */
   utime_t MaxDiffInterval;           /* Maximum time interval between Diffs */
   utime_t DuplicateJobProximity;     /* Permitted time between duplicicates */
   utime_t SnapRetention;             /* Snapshot retention period in seconds */
   int64_t spool_size;                /* Size of spool file for this job */
   int32_t MaxConcurrentJobs;         /* Maximum concurrent jobs */
   uint32_t MaxSpawnedJobs;           /* Max Jobs that can be started by Migration/Copy */
   uint32_t BackupsToKeep;            /* Number of backups to keep in Virtual Full */
   bool allow_mixed_priority;         /* Allow jobs with higher priority concurrently with this */

   MSGS      *messages;               /* How and where to send messages */
   SCHED     *schedule;               /* When -- Automatic schedule */
   CLIENT    *client;                 /* Who to backup */
   FILESET   *fileset;                /* What to backup -- Fileset */
   alist     *storage;                /* Where is device -- list of Storage to be used */
   POOL      *pool;                   /* Where is media -- Media Pool */
   POOL      *next_pool;              /* Next Pool for Copy/Migrate/VirtualFull */
   POOL      *full_pool;              /* Pool for Full backups */
   POOL      *vfull_pool;             /* Pool for Virtual Full backups */
   POOL      *inc_pool;               /* Pool for Incremental backups */
   POOL      *diff_pool;              /* Pool for Differental backups */
   char      *selection_pattern;
   union {
      JOB    *verify_job;             /* Job name to verify */
   };
   JOB       *jobdefs;                /* Job defaults */
   alist     *run_cmds;               /* Run commands */
   alist *RunScripts;                 /* Run {client} program {after|before} Job */

   bool where_use_regexp;             /* true if RestoreWhere is a BREGEXP */
   bool RescheduleOnError;            /* Set to reschedule on error */
   bool RescheduleIncompleteJobs;     /* Set to reschedule incomplete Jobs */
   bool PrefixLinks;                  /* prefix soft links with Where path */
   bool PruneJobs;                    /* Force pruning of Jobs */
   bool PruneFiles;                   /* Force pruning of Files */
   bool PruneVolumes;                 /* Force pruning of Volumes */
   bool SpoolAttributes;              /* Set to spool attributes in SD */
   bool spool_data;                   /* Set to spool data in SD */
   bool rerun_failed_levels;          /* Upgrade to rerun failed levels */
   bool PreferMountedVolumes;         /* Prefer vols mounted rather than new one */
   bool write_part_after_job;         /* Set to write part after job in SD */
   bool Enabled;                      /* Set if job enabled */
   bool accurate;                     /* Set if it is an accurate backup job */
   bool AllowDuplicateJobs;           /* Allow duplicate jobs */
   bool AllowHigherDuplicates;        /* Permit Higher Level */
   bool CancelLowerLevelDuplicates;   /* Cancel lower level backup jobs */
   bool CancelQueuedDuplicates;       /* Cancel queued jobs */
   bool CancelRunningDuplicates;      /* Cancel Running jobs */
   bool PurgeMigrateJob;              /* Purges source job on completion */
   bool DeleteConsolidatedJobs;       /* Delete or not consolidated Virtual Full jobs */

   alist *base;                       /* Base jobs */
   int64_t max_bandwidth;             /* Speed limit on this job */

   /* Methods */
   char *name() const;
   void create_job_globals();
   int32_t getNumConcurrentJobs();
   void setNumConcurrentJobs(int32_t num);
   bool is_enabled();
   void setEnabled(bool val);
};

inline char *JOB::name() const { return hdr.name; }

/* Define FileSet Options keyword values */
enum {
   INC_KW_NONE,
   INC_KW_COMPRESSION,
   INC_KW_DIGEST,
   INC_KW_ENCRYPTION,
   INC_KW_VERIFY,
   INC_KW_BASEJOB,
   INC_KW_ACCURATE,
   INC_KW_ONEFS,
   INC_KW_RECURSE,
   INC_KW_SPARSE,
   INC_KW_HARDLINK,
   INC_KW_REPLACE,               /* restore options */
   INC_KW_READFIFO,              /* Causes fifo data to be read */
   INC_KW_PORTABLE,
   INC_KW_MTIMEONLY,
   INC_KW_KEEPATIME,
   INC_KW_EXCLUDE,
   INC_KW_ACL,
   INC_KW_IGNORECASE,
   INC_KW_HFSPLUS,
   INC_KW_NOATIME,
   INC_KW_ENHANCEDWILD,
   INC_KW_CHKCHANGES,
   INC_KW_STRIPPATH,
   INC_KW_HONOR_NODUMP,
   INC_KW_XATTR,
   INC_KW_DEDUP,
   INC_KW_MAX                   /* Keep this last */
};


#undef  MAX_FOPTS
#define MAX_FOPTS 50

/* File options structure */
struct FOPTS {
   char opts[MAX_FOPTS];              /* options string */
   alist regex;                       /* regex string(s) */
   alist regexdir;                    /* regex string(s) for directories */
   alist regexfile;                   /* regex string(s) for files */
   alist wild;                        /* wild card strings */
   alist wilddir;                     /* wild card strings for directories */
   alist wildfile;                    /* wild card strings for files */
   alist wildbase;                    /* wild card strings for files without '/' */
   alist base;                        /* list of base names */
   alist fstype;                      /* file system type limitation */
   alist drivetype;                   /* drive type limitation */
   char *reader;                      /* reader program */
   char *writer;                      /* writer program */
   char *plugin;                      /* plugin program */
};


/* This is either an include item or an exclude item */
struct INCEXE {
   char  opt_present[INC_KW_MAX+1]; /* set if option is present in conf file */
   FOPTS *current_opts;               /* points to current options structure */
   FOPTS **opts_list;                 /* options list */
   int32_t num_opts;                  /* number of options items */
   alist name_list;                   /* filename list -- holds char * */
   alist plugin_list;                 /* filename list for plugins */
   char *ignoredir;                   /* ignoredir string */
};

/*
 *   FileSet Resource
 *
 */
class FILESET {
public:
   RES   hdr;

   bool new_include;                  /* Set if new include used */
   INCEXE **include_items;            /* array of incexe structures */
   int32_t num_includes;              /* number in array */
   INCEXE **exclude_items;
   int32_t num_excludes;
   bool have_MD5;                     /* set if MD5 initialized */
   struct MD5Context md5c;            /* MD5 of include/exclude */
   char MD5[30];                      /* base 64 representation of MD5 */
   bool ignore_fs_changes;            /* Don't force Full if FS changed */
   bool enable_vss;                   /* Enable Volume Shadow Copy */
   bool enable_snapshot;              /* Enable Snapshot */

   /* Methods */
   char *name() const;
};

inline char *FILESET::name() const { return hdr.name; }

class SCHED_GLOBALS {
public:
   dlink link;                        /* double link */
   const char *name;                  /* resource name */
   int enabled;                       /* -1: not set, 0: disabled, 1: Enabled */
};

/*
 *   Schedule Resource
 */
class SCHED {
public:
   RES   hdr;
   SCHED_GLOBALS *globals;
   RUN *run;
   bool Enabled;                      /* set if enabled */

   /* Methods */
   char *name() const;
   void create_sched_globals();
   bool is_enabled();
   void setEnabled(bool val);
};

inline char *SCHED::name() const { return hdr.name; }

/*
 *   Counter Resource
 */
class COUNTER {
public:
   RES   hdr;

   int32_t  MinValue;                 /* Minimum value */
   int32_t  MaxValue;                 /* Maximum value */
   int32_t  CurrentValue;             /* Current value */
   COUNTER *WrapCounter;              /* Wrap counter name */
   CAT     *Catalog;                  /* Where to store */
   bool     created;                  /* Created in DB */

   /* Methods */
   char *name() const;
};

inline char *COUNTER::name() const { return hdr.name; }

/*
 *   Pool Resource
 *
 */
class POOL {
public:
   RES   hdr;

   char *pool_type;                   /* Pool type */
   char *label_format;                /* Label format string */
   char *cleaning_prefix;             /* Cleaning label prefix */
   int32_t LabelType;                 /* Bacula/ANSI/IBM label type */
   uint32_t max_volumes;              /* max number of volumes */
   utime_t VolRetention;              /* volume retention period in seconds */
   utime_t CacheRetention;            /* cloud cache retention period in seconds */
   utime_t VolUseDuration;            /* duration volume can be used */
   uint32_t MaxVolJobs;               /* Maximum jobs on the Volume */
   uint32_t MaxVolFiles;              /* Maximum files on the Volume */
   uint64_t MaxVolBytes;              /* Maximum bytes on the Volume */
   uint64_t MaxPoolBytes;             /* Maximum bytes on the pool to create new vol */
   utime_t MigrationTime;             /* Time to migrate to next pool */
   uint64_t MigrationHighBytes;       /* When migration starts */
   uint64_t MigrationLowBytes;        /* When migration stops */
   POOL  *NextPool;                   /* Next pool for migration */
   alist *storage;                    /* Where is device -- list of Storage to be used */
   bool  use_catalog;                 /* maintain catalog for media */
   bool  catalog_files;               /* maintain file entries in catalog */
   bool  use_volume_once;             /* write on volume only once */
   bool  purge_oldest_volume;         /* purge oldest volume */
   bool  recycle_oldest_volume;       /* attempt to recycle oldest volume */
   bool  recycle_current_volume;      /* attempt recycle of current volume */
   bool  AutoPrune;                   /* default for pool auto prune */
   bool  Recycle;                     /* default for media recycle yes/no */
   uint32_t action_on_purge;          /* action on purge, e.g. truncate the disk volume */
   POOL  *RecyclePool;                /* RecyclePool destination when media is purged */
   POOL  *ScratchPool;                /* ScratchPool source when requesting media */
   alist *CopyPool;                   /* List of copy pools */
   CAT *catalog;                      /* Catalog to be used */
   utime_t FileRetention;             /* file retention period in seconds */
   utime_t JobRetention;              /* job retention period in seconds */

   /* Methods */
   char *name() const;
};

inline char *POOL::name() const { return hdr.name; }


/* Define the Union of all the above
 * resource structure definitions.
 */
union URES {
   DIRRES     res_dir;
   CONRES     res_con;
   CLIENT     res_client;
   STORE      res_store;
   CAT        res_cat;
   JOB        res_job;
   FILESET    res_fs;
   SCHED      res_sch;
   POOL       res_pool;
   MSGS       res_msgs;
   COUNTER    res_counter;
   DEVICE     res_dev;
   RES        hdr;
   RUNSCRIPT  res_runscript;
};



/* Run structure contained in Schedule Resource */
class RUN {
public:
   RUN *next;                         /* points to next run record */
   uint32_t level;                    /* level override */
   int32_t Priority;                  /* priority override */
   uint32_t job_type;
   utime_t MaxRunSchedTime;           /* max run time in sec from Sched time */
   bool MaxRunSchedTime_set;          /* MaxRunSchedTime given */
   bool spool_data;                   /* Data spooling override */
   bool spool_data_set;               /* Data spooling override given */
   bool accurate;                     /* accurate */
   bool accurate_set;                 /* accurate given */
   bool write_part_after_job;         /* Write part after job override */
   bool write_part_after_job_set;     /* Write part after job override given */
   bool priority_set;                 /* priority override given */
   bool level_set;                    /* level override given */

   POOL *pool;                        /* Pool override */
   POOL *next_pool;                   /* Next pool override */
   POOL *full_pool;                   /* Pool override */
   POOL *vfull_pool;                  /* Pool override */
   POOL *inc_pool;                    /* Pool override */
   POOL *diff_pool;                   /* Pool override */
   STORE *storage;                    /* Storage override */
   MSGS *msgs;                        /* Messages override */
   char *since;
   uint32_t level_no;
   uint32_t minute;                   /* minute to run job */
   time_t last_run;                   /* last time run */
   time_t next_run;                   /* next time to run */
   bool last_day_set;                 /* set if last_day is used */
   char hour[nbytes_for_bits(24)];    /* bit set for each hour */
   char mday[nbytes_for_bits(32)];    /* bit set for each day of month */
   char month[nbytes_for_bits(12)];   /* bit set for each month */
   char wday[nbytes_for_bits(7)];     /* bit set for each day of the week */
   char wom[nbytes_for_bits(6)];      /* week of month */
   char woy[nbytes_for_bits(54)];     /* week of year */
};

#define GetPoolResWithName(x) ((POOL *)GetResWithName(R_POOL, (x)))
#define GetStoreResWithName(x) ((STORE *)GetResWithName(R_STORAGE, (x)))
#define GetSchedResWithName(x) ((SCHED *)GetResWithName(R_SCHEDULE, (x)))
#define GetClientResWithName(x) ((CLIENT *)GetResWithName(R_CLIENT, (x)))
#define GetJobResWithName(x) ((JOB *)GetResWithName(R_JOB, (x)))
#define GetFileSetResWithName(x) ((FILESET *)GetResWithName(R_FILESET, (x)))
#define GetCatalogResWithName(x) ((CAT *)GetResWithName(R_CATALOG, (x)))

/* Imported subroutines */
void store_jobtype(LEX *lc, RES_ITEM *item, int index, int pass);
void store_level(LEX *lc, RES_ITEM *item, int index, int pass);
void store_replace(LEX *lc, RES_ITEM *item, int index, int pass);
void store_migtype(LEX *lc, RES_ITEM *item, int index, int pass);
void store_acl(LEX *lc, RES_ITEM *item, int index, int pass);
void store_ac_res(LEX *lc, RES_ITEM *item, int index, int pass);
void store_device(LEX *lc, RES_ITEM *item, int index, int pass);
void store_actiononpurge(LEX *lc, RES_ITEM *item, int index, int pass);
void store_inc(LEX *lc, RES_ITEM *item, int index, int pass);
void store_regex(LEX *lc, RES_ITEM *item, int index, int pass);
void store_wild(LEX *lc, RES_ITEM *item, int index, int pass);
void store_fstype(LEX *lc, RES_ITEM *item, int index, int pass);
void store_drivetype(LEX *lc, RES_ITEM *item, int index, int pass);
void store_opts(LEX *lc, RES_ITEM *item, int index, int pass);
void store_lopts(LEX *lc, RES_ITEM *item, int index, int pass);
void store_base(LEX *lc, RES_ITEM *item, int index, int pass);
void store_plugin(LEX *lc, RES_ITEM *item, int index, int pass);
void store_run(LEX *lc, RES_ITEM *item, int index, int pass);
void store_runscript(LEX *lc, RES_ITEM *item, int index, int pass);
