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
*   Main configuration file parser for Bacula Tray Monitor.
*
*   Adapted from dird_conf.c
*
*   Note, the configuration file parser consists of three parts
*
*   1. The generic lexical scanner in lib/lex.c and lib/lex.h
*
*   2. The generic config  scanner in lib/parse_config.c and
*       lib/parse_config.h.
*       These files contain the parser code, some utility
*       routines, and the common store routines (name, int,
*       string).
*
*   3. The daemon specific file, which contains the Resource
*       definitions as well as any specific store routines
*       for the resource records.
*
*     Nicolas Boichat, August MMIV
*
*/

#include "common.h"
#include "tray_conf.h"

worker *worker_start();
void worker_stop(worker *);

/* Define the first and last resource ID record
* types. Note, these should be unique for each
* daemon though not a requirement.
*/
int32_t r_first = R_FIRST;
int32_t r_last  = R_LAST;
RES_HEAD **res_head;

/* We build the current resource here as we are
* scanning the resource configuration definition,
* then move it to allocated memory when the resource
* scan is complete.
*/
URES res_all;
int32_t res_all_size = sizeof(res_all);


/* Definition of records permitted within each
* resource with the routine to process the record
* information.  NOTE! quoted names must be in lower case.
*/
/*
*    Monitor Resource
*
*   name           handler     value                 code flags    default_value
*/
static RES_ITEM mon_items[] = {
   {"Name",        store_name,     ITEM(res_monitor.hdr.name), 0, ITEM_REQUIRED, 0},
   {"Description", store_str,      ITEM(res_monitor.hdr.desc), 0, 0, 0},
   {"requiressl",  store_bool,     ITEM(res_monitor.require_ssl), 1, ITEM_DEFAULT, 0},
   {"RefreshInterval",  store_time,ITEM(res_monitor.RefreshInterval),    0, ITEM_DEFAULT, 60},
   {"CommCompression",  store_bool, ITEM(res_monitor.comm_compression), 0, ITEM_DEFAULT, true},
   {"CommandDirectory", store_dir, ITEM(res_monitor.command_dir), 0, 0, 0},
   {"DisplayAdvancedOptions", store_bool, ITEM(res_monitor.display_advanced_options), 0, 0, 0},
   {NULL, NULL, {0}, 0, 0, 0}
};

/*  Director's that we can contact */
static RES_ITEM dir_items[] = {
   {"Name",        store_name,     ITEM(res_main.hdr.name), 0, ITEM_REQUIRED, 0},
   {"Description", store_str,      ITEM(res_main.hdr.desc), 0, 0, 0},
   {"Port",        store_pint32,   ITEM(res_main.port),     0, ITEM_DEFAULT, 9101},
   {"Address",     store_str,      ITEM(res_main.address),  0, ITEM_REQUIRED, 0},
   {"Password",    store_password, ITEM(res_main.password), 0, ITEM_REQUIRED, 0},
   {"Monitor",  store_bool,       ITEM(res_main.use_monitor), 0, ITEM_DEFAULT, 0},
   {"ConnectTimeout", store_time,ITEM(res_main.connect_timeout),   0, ITEM_DEFAULT, 10},
   {"UseSetIp", store_bool,      ITEM(res_main.use_setip),  0, 0, 0},
   {"TlsEnable",      store_bool,    ITEM(res_main.tls_enable), 0, 0, 0},
   {"TlsCaCertificateFile", store_dir, ITEM(res_main.tls_ca_certfile), 0, 0, 0},
   {"TlsCaCertificateDir", store_dir,  ITEM(res_main.tls_ca_certdir), 0, 0, 0},
   {"TlsCertificate", store_dir,       ITEM(res_main.tls_certfile), 0, 0, 0},
   {"TlsKey",         store_dir,       ITEM(res_main.tls_keyfile), 0, 0, 0},

   {NULL, NULL, {0}, 0, 0, 0}
};

/*
*    Client or File daemon resource
*
*   name           handler     value                 code flags    default_value
*/

static RES_ITEM cli_items[] = {
   {"Name",     store_name,       ITEM(res_main.hdr.name), 0, ITEM_REQUIRED, 0},
   {"Description", store_str,     ITEM(res_main.hdr.desc), 0, 0, 0},
   {"Address",  store_str,        ITEM(res_main.address),  0, ITEM_REQUIRED, 0},
   {"Port",     store_pint32,     ITEM(res_main.port),   0, ITEM_DEFAULT, 9102},
   {"Password", store_password,   ITEM(res_main.password), 0, ITEM_REQUIRED, 0},
   {"ConnectTimeout", store_time,ITEM(res_main.connect_timeout),   0, ITEM_DEFAULT, 10},
   {"Remote",   store_bool,       ITEM(res_main.use_remote), 0, ITEM_DEFAULT, 0},
   {"Monitor",  store_bool,       ITEM(res_main.use_monitor), 0, ITEM_DEFAULT, 0},
   {"TlsEnable",      store_bool,    ITEM(res_main.tls_enable), 0, 0, 0},
   {"TlsCaCertificateFile", store_dir, ITEM(res_main.tls_ca_certfile), 0, 0, 0},
   {"TlsCaCertificateDir", store_dir,  ITEM(res_main.tls_ca_certdir), 0, 0, 0},
   {"TlsCertificate", store_dir,       ITEM(res_main.tls_certfile), 0, 0, 0},
   {"TlsKey",         store_dir,       ITEM(res_main.tls_keyfile), 0, 0, 0},
   {NULL, NULL, {0}, 0, 0, 0}
};

/* Storage daemon resource
*
*   name           handler     value                 code flags    default_value
*/
static RES_ITEM store_items[] = {
   {"Name",        store_name,     ITEM(res_main.hdr.name),   0, ITEM_REQUIRED, 0},
   {"Description", store_str,      ITEM(res_main.hdr.desc),   0, 0, 0},
   {"Port",      store_pint32,   ITEM(res_main.port),     0, ITEM_DEFAULT, 9103},
   {"Address",     store_str,      ITEM(res_main.address),    0, ITEM_REQUIRED, 0},
   {"Password",    store_password, ITEM(res_main.password),   0, ITEM_REQUIRED, 0},
   {"ConnectTimeout", store_time,ITEM(res_main.connect_timeout),   0, ITEM_DEFAULT, 10},
   {"Monitor",  store_bool,       ITEM(res_main.use_monitor), 0, ITEM_DEFAULT, 0},
   {"TlsEnable",      store_bool,    ITEM(res_main.tls_enable), 0, 0, 0},
   {"TlsCaCertificateFile", store_dir, ITEM(res_main.tls_ca_certfile), 0, 0, 0},
   {"TlsCaCertificateDir", store_dir,  ITEM(res_main.tls_ca_certdir), 0, 0, 0},
   {"TlsCertificate", store_dir,       ITEM(res_main.tls_certfile), 0, 0, 0},
   {"TlsKey",         store_dir,       ITEM(res_main.tls_keyfile), 0, 0, 0},
   {NULL, NULL, {0}, 0, 0, 0}
};

/*
* This is the master resource definition.
* It must have one item for each of the resources.
*
*  NOTE!!! keep it in the same order as the R_codes
*    or eliminate all resources[rindex].name
*
*  name      items        rcode        res_head
*/
RES_TABLE resources[] = {
   {"monitor",      mon_items,    R_MONITOR},
   {"director",     dir_items,    R_DIRECTOR},
   {"client",       cli_items,    R_CLIENT},
   {"storage",      store_items,  R_STORAGE},
   {NULL,           NULL,         0}
};

/* Dump contents of resource */
void dump_resource(int type, RES *ares, void sendit(void *sock, const char *fmt, ...), void *sock)
{
   RES *next;
   URES *res = (URES *)ares;
   bool recurse = true;

   if (res == NULL) {
      sendit(sock, _("No %s resource defined\n"), res_to_str(type));
      return;
   }
   if (type < 0) {                    /* no recursion */
      type = - type;
      recurse = false;
   }
   switch (type) {
   case R_MONITOR:
      sendit(sock, _("Monitor: name=%s\n"), ares->name);
      break;
   case R_DIRECTOR:
      sendit(sock, _("Director: name=%s address=%s port=%d\n"),
             res->res_main.hdr.name, res->res_main.address, res->res_main.port);
      break;
   case R_CLIENT:
      sendit(sock, _("Client: name=%s address=%s port=%d\n"),
             res->res_main.hdr.name, res->res_main.address, res->res_main.port);
      break;
   case R_STORAGE:
      sendit(sock, _("Storage: name=%s address=%s port=%d\n"),
             res->res_main.hdr.name, res->res_main.address, res->res_main.port);
      break;
   default:
      sendit(sock, _("Unknown resource type %d in dump_resource.\n"), type);
      break;
   }
   if (recurse) {
      next = GetNextRes(0, (RES *)res);
      if (next) {
         dump_resource(type, next, sendit, sock);
      }
   }
}

/*
* Free memory of resource -- called when daemon terminates.
* NB, we don't need to worry about freeing any references
* to other resources as they will be freed when that
* resource chain is traversed.  Mainly we worry about freeing
* allocated strings (names).
*/
void free_resource(RES *sres, int type)
{
   URES *res = (URES *)sres;

   if (res == NULL)
      return;
   /* common stuff -- free the resource name and description */
   if (res->res_monitor.hdr.name) {
      free(res->res_monitor.hdr.name);
   }
   if (res->res_monitor.hdr.desc) {
      free(res->res_monitor.hdr.desc);
   }

   switch (type) {
   case R_MONITOR:
      if (res->res_monitor.password) {
         free(res->res_monitor.password);
      }
      if (res->res_monitor.command_dir) {
         free(res->res_monitor.command_dir);
      }
      break;
   case R_DIRECTOR:
   case R_CLIENT:
   case R_STORAGE:
      delete res->res_main.mutex;
      free_bsock(res->res_main.bs);
      if (res->res_main.wrk) {
         worker_stop(res->res_main.wrk);
         res->res_main.wrk = NULL;
      }
      if (res->res_main.address) {
         free(res->res_main.address);
      }
      if (res->res_main.tls_ctx) {
         free_tls_context(res->res_main.tls_ctx);
      }
      if (res->res_main.tls_ca_certfile) {
         free(res->res_main.tls_ca_certfile);
      }
      if (res->res_main.tls_ca_certdir) {
         free(res->res_main.tls_ca_certdir);
      }
      if (res->res_main.tls_certfile) {
         free(res->res_main.tls_certfile);
      }
      if (res->res_main.tls_keyfile) {
         free(res->res_main.tls_keyfile);
      }
      if (res->res_main.jobs) {
         delete res->res_main.jobs;
      }
      if (res->res_main.clients) {
         delete res->res_main.clients;
      }
      if (res->res_main.filesets) {
         delete res->res_main.filesets;
      }
      if (res->res_main.pools) {
         delete res->res_main.pools;
      }
      if (res->res_main.storages) {
         delete res->res_main.storages;
      }
      if (res->res_main.running_jobs) {
         delete res->res_main.terminated_jobs;
      }
      break;
   default:
      printf(_("Unknown resource type %d in free_resource.\n"), type);
   }

   /* Common stuff again -- free the resource, recurse to next one */
   if (res) {
      free(res);
   }
}

/*
* Save the new resource by chaining it into the head list for
* the resource. If this is pass 2, we update any resource
* pointers because they may not have been defined until
* later in pass 1.
*/
bool save_resource(CONFIG *config, int type, RES_ITEM *items, int pass)
{
   int rindex = type - r_first;
   int i, size;
   int error = 0;

   /*
   * Ensure that all required items are present
   */
   for (i=0; items[i].name; i++) {
      if (items[i].flags & ITEM_REQUIRED) {
         if (!bit_is_set(i, res_all.res_monitor.hdr.item_present)) {
            Mmsg(config->m_errmsg, _("\"%s\" directive is required in \"%s\" resource, but not found.\n"),
                 items[i].name, resources[rindex].name);
            return false;
         }
      }
      /* If this triggers, take a look at lib/parse_conf.h */
      if (i >= MAX_RES_ITEMS) {
         Mmsg(config->m_errmsg, _("Too many directives in \"%s\" resource\n"), resources[rindex].name);
         return false;
      }
   }

   /*
   * During pass 2 in each "store" routine, we looked up pointers
   * to all the resources referrenced in the current resource, now we
   * must copy their addresses from the static record to the allocated
   * record.
   */
   if (pass == 2) {
      switch (type) {
      /* Resources not containing a resource */
      case R_STORAGE:
      case R_DIRECTOR:
      case R_CLIENT:
      case R_MONITOR:
         break;
      default:
         Emsg1(M_ERROR, 0, _("Unknown resource type %d in save_resource.\n"), type);
         error = 1;
         break;
      }
      /* Note, the resource name was already saved during pass 1,
      * so here, we can just release it.
      */
      if (res_all.res_monitor.hdr.name) {
         free(res_all.res_monitor.hdr.name);
         res_all.res_monitor.hdr.name = NULL;
      }
      if (res_all.res_monitor.hdr.desc) {
         free(res_all.res_monitor.hdr.desc);
         res_all.res_monitor.hdr.desc = NULL;
      }
      return true;
   }

   /*
   * The following code is only executed during pass 1
   */
   switch (type) {
   case R_MONITOR:
      size = sizeof(MONITOR);
      break;
   case R_CLIENT:
   case R_STORAGE:
   case R_DIRECTOR:
      // We need to initialize the mutex
      res_all.res_main.mutex = new QMutex();
      res_all.res_main.wrk = worker_start();
      size = sizeof(RESMON);
      break;
   default:
      printf(_("Unknown resource type %d in save_resource.\n"), type);
      error = 1;
      size = 1;
      break;
   }
   /* Common */
   if (!error) {
      res_all.res_main.type = type;
      if (!config->insert_res(rindex, size)) {
         return false;
      }
   }
   return true;
}

bool parse_tmon_config(CONFIG *config, const char *configfile, int exit_code)
{
   config->init(configfile, error_handler, exit_code,
                (void *)&res_all, res_all_size,
      r_first, r_last, resources, &res_head);
   return config->parse_config();
}
