/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

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
 * Bacula File Daemon specific configuration
 *
 *     Kern Sibbald, Sep MM
 */

/*
 * Resource codes -- they must be sequential for indexing
 */
#define R_FIRST                       1001

#define R_DIRECTOR                    1001
#define R_CLIENT                      1002
#define R_MSGS                        1003
#define R_CONSOLE                     1004

#define R_LAST                        R_CONSOLE

/*
 * Some resource attributes
 */
#define R_NAME                        1020
#define R_ADDRESS                     1021
#define R_PASSWORD                    1022
#define R_TYPE                        1023

/* Cipher/Digest keyword structure */
struct s_ct {
   const char *type_name;
   int32_t type_value;
};

/* Definition of the contents of each Resource */
struct CONSRES {
   RES   hdr;
   char *password;                    /* Director password */
   char *address;                     /* Director address or zero */
   int   heartbeat_interval;
   int   comm_compression;
   int32_t DIRport;
   bool tls_authenticate;             /* Authenticate with TSL */
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

/* Definition of the contents of each Resource */
struct DIRRES {
   RES   hdr;
   char *password;                    /* Director password */
   char *address;                     /* Director address or zero */
   bool monitor;                      /* Have only access to status and .status functions */
   bool remote;                       /* Remote console, can run and control jobs */
   bool tls_authenticate;             /* Authenticate with TSL */
   bool tls_enable;                   /* Enable TLS */
   bool tls_require;                  /* Require TLS */
   bool tls_verify_peer;              /* TLS Verify Client Certificate */
   char *tls_ca_certfile;             /* TLS CA Certificate File */
   char *tls_ca_certdir;              /* TLS CA Certificate Directory */
   char *tls_certfile;                /* TLS Server Certificate File */
   char *tls_keyfile;                 /* TLS Server Key File */
   char *tls_dhfile;                  /* TLS Diffie-Hellman Parameters */
   alist *tls_allowed_cns;            /* TLS Allowed Clients */
   uint64_t max_bandwidth_per_job;    /* Bandwidth limitation (per director) */
   TLS_CONTEXT *tls_ctx;              /* Shared TLS Context */
   alist *disable_cmds;               /* Commands to disable */
   bool *disabled_cmds_array;         /* Disabled commands array */
   CONSRES *console;
};

struct CLIENT {
   RES   hdr;
   dlist *FDaddrs;
   dlist *FDsrc_addr;                 /* address to source connections from */
   char *working_directory;
   char *pid_directory;
   char *subsys_directory;
   char *plugin_directory;            /* Plugin directory */
   char *scripts_directory;
   char *snapshot_command;
   MSGS *messages;                    /* daemon message handler */
   uint32_t MaxConcurrentJobs;
   utime_t SDConnectTimeout;          /* timeout in seconds */
   utime_t heartbeat_interval;        /* Interval to send heartbeats */
   uint32_t max_network_buffer_size;  /* max network buf size */
   bool comm_compression;             /* Enable comm line compression */
   bool pki_sign;                     /* Enable Data Integrity Verification via Digital Signatures */
   bool pki_encrypt;                  /* Enable Data Encryption */
   char *pki_keypair_file;            /* PKI Key Pair File */
   alist *pki_signing_key_files;      /* PKI Signing Key Files */
   alist *pki_master_key_files;       /* PKI Master Key Files */
   uint32_t pki_cipher;               /* PKI Cipher type */
   uint32_t pki_digest;               /* PKI Digest type */
   bool tls_authenticate;             /* Authenticate with TLS */
   bool tls_enable;                   /* Enable TLS */
   bool tls_require;                  /* Require TLS */
   char *tls_ca_certfile;             /* TLS CA Certificate File */
   char *tls_ca_certdir;              /* TLS CA Certificate Directory */
   char *tls_certfile;                /* TLS Client Certificate File */
   char *tls_keyfile;                 /* TLS Client Key File */

   X509_KEYPAIR *pki_keypair;         /* Shared PKI Public/Private Keypair */
   alist *pki_signers;                /* Shared PKI Trusted Signers */
   alist *pki_recipients;             /* Shared PKI Recipients */
   TLS_CONTEXT *tls_ctx;              /* Shared TLS Context */
   char *verid;                       /* Custom Id to print in version command */
   uint64_t max_bandwidth_per_job;    /* Bandwidth limitation (global) */
   alist *disable_cmds;               /* Commands to disable */
   bool *disabled_cmds_array;         /* Disabled commands array */
};



/* Define the Union of all the above
 * resource structure definitions.
 */
union URES {
   DIRRES res_dir;
   CLIENT res_client;
   MSGS   res_msgs;
   CONSRES res_cons;
   RES    hdr;
};
