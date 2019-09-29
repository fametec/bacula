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

extern s_kw dev_types[];

/*
 * Cloud Truncate Cache options
 */
enum {
   TRUNC_NO           = 0,             /* default value */
   TRUNC_AFTER_UPLOAD = 1,
   TRUNC_AT_ENDOFJOB  = 2,
   TRUNC_CONF_DEFAULT = 3              /* only use as a parameter, not in the conf */
};

/*
 * Cloud Upload options
 */
enum {
   UPLOAD_EACHPART      = 0,             /* default value */
   UPLOAD_NO            = 1,
   UPLOAD_AT_ENDOFJOB   = 2
};


/*
 * Resource codes -- they must be sequential for indexing
 *
 */

enum {
   R_DIRECTOR    = 3001,
   R_STORAGE     = 3002,
   R_DEVICE      = 3003,
   R_MSGS        = 3004,
   R_AUTOCHANGER = 3005,
   R_CLOUD       = 3006,
   R_FIRST = R_DIRECTOR,
   R_LAST  = R_CLOUD                  /* keep this updated */
};

enum {
   R_NAME = 3020,
   R_ADDRESS,
   R_PASSWORD,
   R_TYPE,
   R_BACKUP
};


/* Definition of the contents of each Resource */

/*
 * Cloud drivers
 */
class CLOUD {
public:
   RES   hdr;
   char *host_name;
   char *bucket_name;
   char *access_key;
   char *secret_key;
   char *blob_endpoint;
   char *file_endpoint;
   char *queue_endpoint;
   char *table_endpoint;
   char *endpoint_suffix;
   char *region;
   int32_t protocol;
   int32_t uri_style;
   uint32_t driver_type;          /* Cloud driver type */
   uint32_t trunc_opt;
   uint32_t upload_opt;
   uint32_t max_concurrent_uploads;
   uint32_t max_concurrent_downloads;
   uint64_t upload_limit;
   uint64_t download_limit;
};

/*
 * Director resource
 */
class DIRRES {
public:
   RES   hdr;

   char *password;                    /* Director password */
   char *address;                     /* Director IP address or zero */
   bool monitor;                      /* Have only access to status and .status functions */
   bool tls_authenticate;             /* Authenticate with TLS */
   bool tls_enable;                   /* Enable TLS */
   bool tls_require;                  /* Require TLS */
   bool tls_verify_peer;              /* TLS Verify Client Certificate */
   char *tls_ca_certfile;             /* TLS CA Certificate File */
   char *tls_ca_certdir;              /* TLS CA Certificate Directory */
   char *tls_certfile;                /* TLS Server Certificate File */
   char *tls_keyfile;                 /* TLS Server Key File */
   char *tls_dhfile;                  /* TLS Diffie-Hellman Parameters */
   alist *tls_allowed_cns;            /* TLS Allowed Clients */

   TLS_CONTEXT *tls_ctx;              /* Shared TLS Context */
};


/* Storage daemon "global" definitions */
class s_res_store {
public:
   RES   hdr;

   dlist *sdaddrs;
   dlist *sddaddrs;
   char *working_directory;           /* working directory for checkpoints */
   char *pid_directory;
   char *subsys_directory;
   char *plugin_directory;            /* Plugin directory */
   char *scripts_directory;
   uint32_t max_concurrent_jobs;      /* maximum concurrent jobs to run */
   MSGS *messages;                    /* Daemon message handler */
   utime_t ClientConnectTimeout;      /* Max time to wait to connect client */
   utime_t heartbeat_interval;        /* Interval to send hb to FD */
   utime_t client_wait;               /* Time to wait for FD to connect */
   bool comm_compression;             /* Set to allow comm line compression */
   bool tls_authenticate;             /* Authenticate with TLS */
   bool tls_enable;                   /* Enable TLS */
   bool tls_require;                  /* Require TLS */
   bool tls_verify_peer;              /* TLS Verify Client Certificate */
   char *tls_ca_certfile;             /* TLS CA Certificate File */
   char *tls_ca_certdir;              /* TLS CA Certificate Directory */
   char *tls_certfile;                /* TLS Server Certificate File */
   char *tls_keyfile;                 /* TLS Server Key File */
   char *tls_dhfile;                  /* TLS Diffie-Hellman Parameters */
   alist *tls_allowed_cns;            /* TLS Allowed Clients */
   char *verid;                       /* Custom Id to print in version command */
   TLS_CONTEXT *tls_ctx;              /* Shared TLS Context */

};
typedef class s_res_store STORES;

class AUTOCHANGER {
public:
   RES hdr;
   alist *device;                     /* List of DEVRES device pointers */
   char *changer_name;                /* Changer device name */
   char *changer_command;             /* Changer command  -- external program */
   char *lock_command;                /* Share storage lock command -- external program */
   brwlock_t changer_lock;            /* One changer operation at a time */
};

/* Device specific definitions */
class DEVRES {
public:
   RES   hdr;

   char *media_type;                  /* User assigned media type */
   char *device_name;                 /* Archive device name */
   char *adevice_name;                /* Aligned device name */
   char *changer_name;                /* Changer device name */
   char *control_name;                /* SCSI control device name */
   char *changer_command;             /* Changer command  -- external program */
   char *alert_command;               /* Alert command -- external program */
   char *lock_command;                /* Share storage lock command -- external program */
   char *worm_command;                /* Worm detection command -- external program */
   char *spool_directory;             /* Spool file directory */
   uint32_t dev_type;                 /* device type */
   uint32_t label_type;               /* label type */
   bool enabled;                      /* Set when enabled (default) */
   bool autoselect;                   /* Automatically select from AutoChanger */
   bool read_only;                    /* Drive is read only */
   uint32_t drive_index;              /* Autochanger drive index */
   uint32_t cap_bits;                 /* Capabilities of this device */
   utime_t max_changer_wait;          /* Changer timeout */
   utime_t max_rewind_wait;           /* maximum secs to wait for rewind */
   utime_t max_open_wait;             /* maximum secs to wait for open */
   uint32_t padding_size;             /* adata block padding -- bytes */
   uint32_t file_alignment;           /* adata file alignment -- bytes */
   uint32_t min_aligned_size;         /* minimum adata size */
   uint32_t min_block_size;           /* min block size */
   uint32_t max_block_size;           /* max block size */
   uint32_t max_volume_jobs;          /* max jobs to put on one volume */
   uint32_t max_network_buffer_size;  /* max network buf size */
   uint32_t max_concurrent_jobs;      /* maximum concurrent jobs this drive */
   utime_t  vol_poll_interval;        /* interval between polling volume during mount */
   int64_t max_volume_files;          /* max files to put on one volume */
   int64_t max_volume_size;           /* max bytes to put on one volume */
   int64_t max_file_size;             /* max file size in bytes */
   int64_t volume_capacity;           /* advisory capacity */
   int64_t min_free_space;            /* Minimum disk free space */
   int64_t max_spool_size;            /* Max spool size for all jobs */
   int64_t max_job_spool_size;        /* Max spool size for any single job */

   int64_t max_part_size;             /* Max part size */
   char *mount_point;                 /* Mount point for require mount devices */
   char *mount_command;               /* Mount command */
   char *unmount_command;             /* Unmount command */
   char *write_part_command;          /* Write part command */
   char *free_space_command;          /* Free space command */
   CLOUD *cloud;                      /* pointer to cloud resource */

   /* The following are set at runtime */
   DEVICE *dev;                       /* Pointer to phyical dev -- set at runtime */
   AUTOCHANGER *changer_res;          /* pointer to changer res if any */
};

union URES {
   DIRRES      res_dir;
   STORES      res_store;
   DEVRES      res_dev;
   MSGS        res_msgs;
   AUTOCHANGER res_changer;
   CLOUD       res_cloud;
   RES         hdr;
};
