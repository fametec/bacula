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
 *   Main configuration file parser for Bacula User Agent
 *    some parts may be split into separate files such as
 *    the schedule configuration (sch_config.c).
 *
 *   Note, the configuration file parser consists of three parts
 *
 *   1. The generic lexical scanner in lib/lex.c and lib/lex.h
 *
 *   2. The generic config  scanner in lib/parse_config.c and
 *      lib/parse_config.h.
 *      These files contain the parser code, some utility
 *      routines, and the common store routines (name, int,
 *      string).
 *
 *   3. The daemon specific file, which contains the Resource
 *      definitions as well as any specific store routines
 *      for the resource records.
 *
 *     Kern Sibbald, January MM, September MM
 */

#include "bacula.h"
#include "console_conf.h"

/* Define the first and last resource ID record
 * types. Note, these should be unique for each
 * daemon though not a requirement.
 */
int32_t r_first = R_FIRST;
int32_t r_last  = R_LAST;
RES_HEAD **res_head;

/* Forward referenced subroutines */


/* We build the current resource here as we are
 * scanning the resource configuration definition,
 * then move it to allocated memory when the resource
 * scan is complete.
 */
#if defined(_MSC_VER)
extern "C" { // work around visual compiler mangling variables
    URES res_all;
}
#else
URES res_all;
#endif
int32_t res_all_size = sizeof(res_all);

/* Definition of records permitted within each
 * resource with the routine to process the record
 * information.
 */

/*  Console "globals" */
static RES_ITEM cons_items[] = {
   {"Name",           store_name,     ITEM(res_cons.hdr.name), 0, ITEM_REQUIRED, 0},
   {"Description",    store_str,      ITEM(res_cons.hdr.desc), 0, 0, 0},
   {"RCFile",         store_dir,      ITEM(res_cons.rc_file), 0, 0, 0},
   {"HistoryFile",    store_dir,      ITEM(res_cons.hist_file), 0, 0, 0},
   {"Password",       store_password, ITEM(res_cons.password), 0, ITEM_REQUIRED, 0},
   {"TlsAuthenticate",store_bool,    ITEM(res_cons.tls_authenticate), 0, 0, 0},
   {"TlsEnable",      store_bool,    ITEM(res_cons.tls_enable), 0, 0, 0},
   {"TlsRequire",     store_bool,    ITEM(res_cons.tls_require), 0, 0, 0},
   {"TlsCaCertificateFile", store_dir, ITEM(res_cons.tls_ca_certfile), 0, 0, 0},
   {"TlsCaCertificateDir", store_dir,  ITEM(res_cons.tls_ca_certdir), 0, 0, 0},
   {"TlsCertificate", store_dir,       ITEM(res_cons.tls_certfile), 0, 0, 0},
   {"TlsKey",         store_dir,       ITEM(res_cons.tls_keyfile), 0, 0, 0},
   {"Director",       store_str,       ITEM(res_cons.director), 0, 0, 0},
   {"HeartbeatInterval", store_time, ITEM(res_cons.heartbeat_interval), 0, ITEM_DEFAULT, 5 * 60},
   {"CommCompression",   store_bool, ITEM(res_cons.comm_compression), 0, ITEM_DEFAULT, true},
   {NULL, NULL, {0}, 0, 0, 0}
};


/*  Director's that we can contact */
static RES_ITEM dir_items[] = {
   {"Name",           store_name,      ITEM(res_dir.hdr.name), 0, ITEM_REQUIRED, 0},
   {"Description",    store_str,       ITEM(res_dir.hdr.desc), 0, 0, 0},
   {"DirPort",        store_pint32,    ITEM(res_dir.DIRport),  0, ITEM_DEFAULT, 9101},
   {"Address",        store_str,       ITEM(res_dir.address),  0, 0, 0},
   {"Password",       store_password,  ITEM(res_dir.password), 0, ITEM_REQUIRED, 0},
   {"TlsAuthenticate",store_bool,    ITEM(res_dir.tls_enable), 0, 0, 0},
   {"TlsEnable",      store_bool,    ITEM(res_dir.tls_enable), 0, 0, 0},
   {"TlsRequire",     store_bool,    ITEM(res_dir.tls_require), 0, 0, 0},
   {"TlsCaCertificateFile", store_dir, ITEM(res_dir.tls_ca_certfile), 0, 0, 0},
   {"TlsCaCertificateDir", store_dir,  ITEM(res_dir.tls_ca_certdir), 0, 0, 0},
   {"TlsCertificate", store_dir,       ITEM(res_dir.tls_certfile), 0, 0, 0},
   {"TlsKey",         store_dir,       ITEM(res_dir.tls_keyfile), 0, 0, 0},
   {"HeartbeatInterval", store_time, ITEM(res_dir.heartbeat_interval), 0, ITEM_DEFAULT, 5 * 60},
   {NULL, NULL, {0}, 0, 0, 0}
};

/*
 * This is the master resource definition.
 * It must have one item for each of the resources.
 *
 *   name            item         rcode
 */
RES_TABLE resources[] = {
   {"Console",       cons_items,  R_CONSOLE},
   {"Director",      dir_items,   R_DIRECTOR},
   {NULL,            NULL,        0}
};

/* Dump contents of resource */
void dump_resource(int type, RES *rres, void sendit(void *sock, const char *fmt, ...), void *sock)
{
   URES *res = (URES *)rres;
   bool recurse = true;

   if (res == NULL) {
      printf(_("No record for %d %s\n"), type, res_to_str(type));
      return;
   }
   if (type < 0) {                    /* no recursion */
      type = - type;
      recurse = false;
   }
   switch (type) {
   case R_CONSOLE:
      printf(_("Console: name=%s rcfile=%s histfile=%s\n"), rres->name,
             res->res_cons.rc_file, res->res_cons.hist_file);
      break;
   case R_DIRECTOR:
      printf(_("Director: name=%s address=%s DIRport=%d\n"), rres->name,
              res->res_dir.address, res->res_dir.DIRport);
      break;
   default:
      printf(_("Unknown resource type %d\n"), type);
   }

   rres = GetNextRes(type, rres);
   if (recurse && rres) {
      dump_resource(type, rres, sendit, sock);
   }
}

/*
 * Free memory of resource.
 * NB, we don't need to worry about freeing any references
 * to other resources as they will be freed when that
 * resource chain is traversed.  Mainly we worry about freeing
 * allocated strings (names).
 */
void free_resource(RES *rres, int type)
{
   URES *res = (URES *)rres;

   if (res == NULL) {
      return;
   }

   /* common stuff -- free the resource name */
   if (res->res_dir.hdr.name) {
      free(res->res_dir.hdr.name);
   }
   if (res->res_dir.hdr.desc) {
      free(res->res_dir.hdr.desc);
   }

   switch (type) {
   case R_CONSOLE:
      if (res->res_cons.rc_file) {
         free(res->res_cons.rc_file);
      }
      if (res->res_cons.hist_file) {
         free(res->res_cons.hist_file);
      }
      if (res->res_cons.tls_ctx) {
         free_tls_context(res->res_cons.tls_ctx);
      }
      if (res->res_cons.tls_ca_certfile) {
         free(res->res_cons.tls_ca_certfile);
      }
      if (res->res_cons.tls_ca_certdir) {
         free(res->res_cons.tls_ca_certdir);
      }
      if (res->res_cons.tls_certfile) {
         free(res->res_cons.tls_certfile);
      }
      if (res->res_cons.tls_keyfile) {
         free(res->res_cons.tls_keyfile);
      }
      if (res->res_cons.director) {
         free(res->res_cons.director);
      }
      if (res->res_cons.password) {
         free(res->res_cons.password);
      }
      break;
   case R_DIRECTOR:
      if (res->res_dir.address) {
         free(res->res_dir.address);
      }
      if (res->res_dir.tls_ctx) {
         free_tls_context(res->res_dir.tls_ctx);
      }
      if (res->res_dir.tls_ca_certfile) {
         free(res->res_dir.tls_ca_certfile);
      }
      if (res->res_dir.tls_ca_certdir) {
         free(res->res_dir.tls_ca_certdir);
      }
      if (res->res_dir.tls_certfile) {
         free(res->res_dir.tls_certfile);
      }
      if (res->res_dir.tls_keyfile) {
         free(res->res_dir.tls_keyfile);
      }
      if (res->res_dir.password) {
         free(res->res_dir.password);
      }
      break;
   default:
      printf(_("Unknown resource type %d\n"), type);
   }
   /* Common stuff again -- free the resource, recurse to next one */
   free(res);
}

/* Save the new resource by chaining it into the head list for
 * the resource. If this is pass 2, we update any resource
 * pointers (currently only in the Job resource).
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
         if (!bit_is_set(i, res_all.res_dir.hdr.item_present)) {
            Mmsg(config->m_errmsg, _("\"%s\" directive is required in \"%s\" resource, but not found.\n"),
                 items[i].name, resources[rindex].name);
            return false;
         }
      }
   }

   /* During pass 2, we looked up pointers to all the resources
    * referrenced in the current resource, , now we
    * must copy their address from the static record to the allocated
    * record.
    */
   if (pass == 2) {
      switch (type) {
         /* Resources not containing a resource */
         case R_CONSOLE:
         case R_DIRECTOR:
            break;

         default:
            Emsg1(M_ERROR, 0, _("Unknown resource type %d\n"), type);
            error = 1;
            break;
      }
      /* Note, the resoure name was already saved during pass 1,
       * so here, we can just release it.
       */
      if (res_all.res_dir.hdr.name) {
         free(res_all.res_dir.hdr.name);
         res_all.res_dir.hdr.name = NULL;
      }
      if (res_all.res_dir.hdr.desc) {
         free(res_all.res_dir.hdr.desc);
         res_all.res_dir.hdr.desc = NULL;
      }
      return true;
   }

   /* The following code is only executed during pass 1 */
   switch (type) {
   case R_CONSOLE:
      size = sizeof(CONRES);
      break;
   case R_DIRECTOR:
      size = sizeof(DIRRES);
      break;
   default:
      printf(_("Unknown resource type %d\n"), type);
      error = 1;
      size = 1;
      break;
   }
   /* Common */
   if (!error) {
      if (!config->insert_res(rindex, size)) {
         return false;
      }
   }
   return true;
}

bool parse_cons_config(CONFIG *config, const char *configfile, int exit_code)
{
   config->init(configfile, NULL, exit_code, (void *)&res_all, res_all_size,
      r_first, r_last, resources, &res_head);
   return config->parse_config();
}
