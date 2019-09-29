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
 * Bacula conf to json
 *
 *  Kern Sibbald, MMXII
 */

#include "bacula.h"
#include "stored.h"

/* Imported functions */
extern bool parse_sd_config(CONFIG *config, const char *configfile, int exit_code);

/* Imported variables */
#if defined(_MSC_VER)
extern "C" { // work around visual compiler mangling variables
   extern URES res_all;
}
#else
extern URES res_all;
#endif
extern s_kw msg_types[];
extern s_kw dev_types[];
extern s_kw tapelabels[];
extern s_kw cloud_drivers[];
extern s_kw trunc_opts[];
extern s_kw upload_opts[];
extern s_kw proto_opts[];
extern s_kw uri_opts[];

extern RES_TABLE resources[];

typedef struct
{
   bool do_list;
   bool do_one;
   bool do_only_data;
   char *resource_type;
   char *resource_name;
   regex_t directive_reg;
} display_filter;

/* Forward referenced functions */
void terminate_stored(int sig);
static int check_resources();
static void sendit(void *sock, const char *fmt, ...);
static void dump_json(display_filter *filter);

#define CONFIG_FILE "bacula-sd.conf"  /* Default config file */

/* Global variables exported */
STORES *me = NULL;                    /* our Global resource */

char *configfile = NULL;

/* Global static variables */
static CONFIG *config;


static void usage()
{
   fprintf(stderr, _(
PROG_COPYRIGHT
"\n%sVersion: %s (%s)\n\n"
"Usage: bsdjson [options] [config_file]\n"
"        -r <res>    get resource type <res>\n"
"        -n <name>   get resource <name>\n"
"        -l <dirs>   get only directives matching dirs (use with -r)\n"
"        -D          get only data\n"
"        -c <file>   use <file> as configuration file\n"
"        -d <nn>     set debug level to <nn>\n"
"        -dt         print timestamp in debug output\n"
"        -t          test - read config and exit\n"
"        -v          verbose user messages\n"
"        -?          print this message.\n"
"\n"), 2012, "", VERSION, BDATE);

   exit(1);
}

/*********************************************************************
 *
 *  Main Bacula Unix Storage Daemon
 *
 */
#if defined(HAVE_WIN32)
#define main BaculaMain
#endif

int main (int argc, char *argv[])
{
   int ch;
   bool test_config = false;
   display_filter filter;
   memset(&filter, 0, sizeof(filter));

   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");

   my_name_is(argc, argv, "bacula-sd");
   init_msg(NULL, NULL);

   while ((ch = getopt(argc, argv, "Dc:d:tv?r:n:l:")) != -1) {
      switch (ch) {
      case 'D':
         filter.do_only_data = true;
         break;

      case 'l':
         filter.do_list = true;
         /* Might use something like -l '^(Name|Description)$' */
         filter.do_list = true;
         if (regcomp(&filter.directive_reg, optarg, REG_EXTENDED) != 0) {
            Jmsg((JCR *)NULL, M_ERROR_TERM, 0,
                 _("Please use valid -l argument: %s\n"), optarg);
         }
         break;

      case 'r':
         filter.resource_type = optarg;
         break;

      case 'n':
         filter.resource_name = optarg;
         break;

      case 'c':                    /* configuration file */
         if (configfile != NULL) {
            free(configfile);
         }
         configfile = bstrdup(optarg);
         break;

      case 'd':                    /* debug level */
         if (*optarg == 't') {
            dbg_timestamp = true;
         } else {
            debug_level = atoi(optarg);
            if (debug_level <= 0) {
               debug_level = 1;
            }
         }
         break;

      case 't':
         test_config = true;
         break;

      case 'v':                    /* verbose */
         verbose++;
         break;

      case '?':
      default:
         usage();
         break;
      }
   }
   argc -= optind;
   argv += optind;

   if (argc) {
      if (configfile != NULL) {
         free(configfile);
      }
      configfile = bstrdup(*argv);
      argc--;
      argv++;
   }

   if (argc) {
      usage();
   }

   if (filter.do_list && !filter.resource_type) {
      usage();
   }

   if (filter.resource_type && filter.resource_name) {
      filter.do_one = true;
   }

   if (configfile == NULL || configfile[0] == 0) {
      configfile = bstrdup(CONFIG_FILE);
   }

   if (test_config && verbose > 0) {
      char buf[1024];
      find_config_file(configfile, buf, sizeof(buf));
      sendit(NULL, "config_file=%s\n", buf);
   }

   config = New(CONFIG());
   config->encode_password(false);
   parse_sd_config(config, configfile, M_ERROR_TERM);

   if (!check_resources()) {
      Jmsg((JCR *)NULL, M_ERROR_TERM, 0, _("Please correct configuration file: %s\n"), configfile);
   }

   if (test_config) {
      terminate_stored(0);
   }

   my_name_is(0, (char **)NULL, me->hdr.name);     /* Set our real name */

   dump_json(&filter);

   if (filter.do_list) {
      regfree(&filter.directive_reg);
   }

   terminate_stored(0);
}

static void display_devtype(HPKT &hpkt)
{
   int i;
   for (i=0; dev_types[i].name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == dev_types[i].token) {
         sendit(NULL, "\n    \"%s\": \"%s\"", hpkt.ritem->name,
                dev_types[i].name);
         return;
      }
   }
}

static void display_label(HPKT &hpkt)
{
   int i;
   for (i=0; tapelabels[i].name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == tapelabels[i].token) {
         sendit(NULL, "\n      \"%s\": \"%s\"", hpkt.ritem->name,
                tapelabels[i].name);
         return;
      }
   }
}

static void display_cloud_driver(HPKT &hpkt)
{
   int i;
   for (i=0; cloud_drivers[i].name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == cloud_drivers[i].token) {
         sendit(NULL, "\n      \"%s\": \"%s\"", hpkt.ritem->name,
                cloud_drivers[i].name);
         return;
      }
   }
}

static void display_protocol(HPKT &hpkt)
{
   int i;
   for (i=0; proto_opts[i].name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == proto_opts[i].token) {
         sendit(NULL, "\n    \"%s\": \"%s\"", hpkt.ritem->name,
                proto_opts[i].name);
         return;
      }
   }
}

static void display_truncate_cache(HPKT &hpkt)
{
   int i;
   for (i=0; trunc_opts[i].name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == trunc_opts[i].token) {
         sendit(NULL, "\n    \"%s\": \"%s\"", hpkt.ritem->name,
                trunc_opts[i].name);
         return;
      }
   }
}

static void display_uri_style(HPKT &hpkt)
{
   int i;
   for (i=0; uri_opts[i].name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == uri_opts[i].token) {
         sendit(NULL, "\n    \"%s\": \"%s\"", hpkt.ritem->name,
                uri_opts[i].name);
         return;
      }
   }
}

static void display_upload(HPKT &hpkt)
{
   int i;
   for (i=0; upload_opts[i].name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == upload_opts[i].token) {
         sendit(NULL, "\n    \"%s\": \"%s\"", hpkt.ritem->name,
                upload_opts[i].name);
         return;
      }
   }
}

/*
 * Dump out all resources in json format.
 * Note!!!! This routine must be in this file rather
 *  than in src/lib/parser_conf.c otherwise the pointers
 *  will be all messed up.
 */
static void dump_json(display_filter *filter)
{
   int resinx, item, directives, first_directive;
   bool first_res;
   RES_ITEM *items;
   RES *res;
   HPKT hpkt;
   regmatch_t pmatch[32];
   STORES *me = (STORES *)GetNextRes(R_STORAGE, NULL);

   if (init_crypto() != 0) {
      Emsg0(M_ERROR_TERM, 0, _("Cryptography library initialization failed.\n"));
   }

   init_hpkt(hpkt);

   if (filter->do_only_data) {
      sendit(NULL, "[");

   /* List resources and directives */
   /* { "aa": { "Name": "aa",.. }, "bb": { "Name": "bb", ... }
    * or print a single item
    */
   } else if (filter->do_one || filter->do_list) {
      sendit(NULL, "{");

   } else {
   /* [ { "Device": { "Name": "aa",.. } }, { "Director": { "Name": "bb", ... } } ]*/
      sendit(NULL, "[");
   }

   first_res = true;
   /* Loop over all resource types */
   for (resinx=0; resources[resinx].name; resinx++) {
      if (!resources[resinx].items) {
          continue;           /* skip dummy entries */
      }

      /* Skip this resource type */
      if (filter->resource_type &&
          strcasecmp(filter->resource_type, resources[resinx].name) != 0) {
         continue;
      }

      directives = 0;
      /* Loop over all resources of this type */
      foreach_rblist(res, res_head[resinx]->res_list) {
         hpkt.res = res;
         items = resources[resinx].items;
         if (!items) {
            continue;
         }

         /* Copy the resource into res_all */
         memcpy(&res_all, res, sizeof(res_all));

         if (filter->resource_name) {
            bool skip=true;
            /* The Name should be at the first place, so this is not a real loop */
            for (item=0; items[item].name; item++) {
               if (strcasecmp(items[item].name, "Name") == 0) {
                  if (strcasecmp(*(items[item].value), filter->resource_name) == 0) {
                     skip = false;
                  }
                  break;
               }
            }
            if (skip) {         /* The name doesn't match, so skip it */
               continue;
            }
         }

         if (first_res) {
            sendit(NULL, "\n");
         } else {
            sendit(NULL, ",\n");
         }

         if (filter->do_only_data) {
            sendit(NULL, " {");

         } else if (filter->do_one) {
            /* Nothing to print */

         /* When sending the list, the form is:
          *  { aa: { Name: aa, Description: aadesc...}, bb: { Name: bb
          */
         } else if (filter->do_list) {
            /* Search and display Name, should be the first item */
            for (item=0; items[item].name; item++) {
               if (strcmp(items[item].name, "Name") == 0) {
                  sendit(NULL, "%s: {\n", quote_string(hpkt.edbuf2, *items[item].value));
                  break;
               }
            }
         } else {
            /* Begin new resource */
            sendit(NULL, "{\n  \"%s\": {", resources[resinx].name);
         }

         first_res = false;
         first_directive = 0;
         directives = 0;
         for (item=0; items[item].name; item++) {
            /* Check user argument -l */
            if (filter->do_list &&
                regexec(&filter->directive_reg,
                        items[item].name, 32, pmatch, 0) != 0)
            {
               continue;
            }

            hpkt.ritem = &items[item];
            if (bit_is_set(item, res_all.hdr.item_present)) {
               if (first_directive++ > 0) printf(",");
               if (display_global_item(hpkt)) {
                  /* Fall-through wanted */
               } else if (items[item].handler == store_maxblocksize) {
                  display_int32_pair(hpkt);
               } else if (items[item].handler == store_devtype) {
                  display_devtype(hpkt);
               } else if (items[item].handler == store_label) {
                  display_label(hpkt);
               } else if (items[item].handler == store_cloud_driver) {
                  display_cloud_driver(hpkt);
               } else if (items[item].handler == store_protocol) {
                  display_protocol(hpkt);
               } else if (items[item].handler == store_uri_style) {
                  display_uri_style(hpkt);
               } else if (items[item].handler == store_truncate) {
                  display_truncate_cache(hpkt);
               } else if (items[item].handler == store_upload) {
                  display_upload(hpkt);
               } else {
                  printf("\n      \"%s\": \"null\"", items[item].name);
               }
               directives++;
            } else { /* end if is present */
               /* For some directive, the bitmap is not set (like addresses) */
               if (me && strcmp(resources[resinx].name, "Storage") == 0) {
                  if (strcmp(items[item].name, "SdPort") == 0) {
                     if (get_first_port_host_order(me->sdaddrs) != items[item].default_value) {
                        if (first_directive++ > 0) sendit(NULL, ",");
                        sendit(NULL, "\n    \"SdPort\": %d",
                           get_first_port_host_order(me->sdaddrs));
                     }
                  } else if (me && strcmp(items[item].name, "SdAddress") == 0) {
                     char buf[500];
                     get_first_address(me->sdaddrs, buf, sizeof(buf));
                     if (strcmp(buf, "0.0.0.0") != 0) {
                        if (first_directive++ > 0) sendit(NULL, ",");
                        sendit(NULL, "\n    \"SdAddress\": \"%s\"", buf);
                     }
                  }
               }
            }
            if (items[item].flags & ITEM_LAST) {
               display_last(hpkt);    /* If last bit set always call to cleanup */
            }
         }

         /* { "aa": { "Name": "aa",.. }, "bb": { "Name": "bb", ... } */
         if (filter->do_only_data || filter->do_list) {
            sendit(NULL, "\n }"); /* Finish the Resource with a single } */

         } else {
            if (filter->do_one) {
               /* don't print anything */

            } else if (first_directive > 0) {
               sendit(NULL, "\n  }\n}");  /* end of resource */

            } else {
               sendit(NULL, "}\n }");
            }
         }

      } /* End loop over all resources of this type */
   } /* End loop all resource types */

   if (filter->do_only_data) {
      sendit(NULL, "\n]\n");

   /* In list context, we are dealing with a hash */
   } else if (filter->do_one || filter->do_list) {
      sendit(NULL, "\n}\n");

   } else {
      sendit(NULL, "\n]\n");
   }
   term_hpkt(hpkt);
}


/* Check Configuration file for necessary info */
static int check_resources()
{
   bool OK = true;
   bool tls_needed;
   AUTOCHANGER *changer;
   DEVRES *device;

   me = (STORES *)GetNextRes(R_STORAGE, NULL);
   if (!me) {
      Jmsg1(NULL, M_ERROR, 0, _("No Storage resource defined in %s. Cannot continue.\n"),
         configfile);
      OK = false;
   }

   if (GetNextRes(R_STORAGE, (RES *)me) != NULL) {
      Jmsg1(NULL, M_ERROR, 0, _("Only one Storage resource permitted in %s\n"),
         configfile);
      OK = false;
   }
   if (GetNextRes(R_DIRECTOR, NULL) == NULL) {
      Jmsg1(NULL, M_ERROR, 0, _("No Director resource defined in %s. Cannot continue.\n"),
         configfile);
      OK = false;
   }
   if (GetNextRes(R_DEVICE, NULL) == NULL){
      Jmsg1(NULL, M_ERROR, 0, _("No Device resource defined in %s. Cannot continue.\n"),
           configfile);
      OK = false;
   }

   if (!me->messages) {
      me->messages = (MSGS *)GetNextRes(R_MSGS, NULL);
      if (!me->messages) {
         Jmsg1(NULL, M_ERROR, 0, _("No Messages resource defined in %s. Cannot continue.\n"),
            configfile);
         OK = false;
      }
   }

   if (!me->working_directory) {
      Jmsg1(NULL, M_ERROR, 0, _("No Working Directory defined in %s. Cannot continue.\n"),
         configfile);
      OK = false;
   }

   DIRRES *director;
   STORES *store;
   foreach_res(store, R_STORAGE) {
      /* tls_require implies tls_enable */
      if (store->tls_require) {
         if (have_tls) {
            store->tls_enable = true;
         } else {
            Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
            OK = false;
            continue;
         }
      }

      tls_needed = store->tls_enable || store->tls_authenticate;

      if (!store->tls_certfile && tls_needed) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Certificate\" file not defined for Storage \"%s\" in %s.\n"),
              store->hdr.name, configfile);
         OK = false;
      }

      if (!store->tls_keyfile && tls_needed) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Key\" file not defined for Storage \"%s\" in %s.\n"),
              store->hdr.name, configfile);
         OK = false;
      }

      if ((!store->tls_ca_certfile && !store->tls_ca_certdir) && tls_needed && store->tls_verify_peer) {
         Jmsg(NULL, M_FATAL, 0, _("Neither \"TLS CA Certificate\""
              " or \"TLS CA Certificate Dir\" are defined for Storage \"%s\" in %s."
              " At least one CA certificate store is required"
              " when using \"TLS Verify Peer\".\n"),
              store->hdr.name, configfile);
         OK = false;
      }
   }

   foreach_res(director, R_DIRECTOR) {
      /* tls_require implies tls_enable */
      if (director->tls_require) {
         director->tls_enable = true;
      }

      tls_needed = director->tls_enable || director->tls_authenticate;

      if (!director->tls_certfile && tls_needed) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Certificate\" file not defined for Director \"%s\" in %s.\n"),
              director->hdr.name, configfile);
         OK = false;
      }

      if (!director->tls_keyfile && tls_needed) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Key\" file not defined for Director \"%s\" in %s.\n"),
              director->hdr.name, configfile);
         OK = false;
      }

      if ((!director->tls_ca_certfile && !director->tls_ca_certdir) && tls_needed && director->tls_verify_peer) {
         Jmsg(NULL, M_FATAL, 0, _("Neither \"TLS CA Certificate\""
              " or \"TLS CA Certificate Dir\" are defined for Director \"%s\" in %s."
              " At least one CA certificate store is required"
              " when using \"TLS Verify Peer\".\n"),
              director->hdr.name, configfile);
         OK = false;
      }
   }

   foreach_res(changer, R_AUTOCHANGER) {
      foreach_alist(device, changer->device) {
         device->cap_bits |= CAP_AUTOCHANGER;
      }
   }

   return OK;
}

/* Clean up and then exit */
void terminate_stored(int sig)
{
   static bool in_here = false;

   if (in_here) {                     /* prevent loops */
      bmicrosleep(2, 0);              /* yield */
      exit(1);
   }
   in_here = true;
   debug_level = 0;                   /* turn off any debug */

   if (configfile) {
      free(configfile);
      configfile = NULL;
   }
   if (config) {
      delete config;
      config = NULL;
   }

   if (debug_level > 10) {
      print_memory_pool_stats();
   }
   term_msg();
   free(res_head);
   res_head = NULL;
   close_memory_pool();

   //sm_dump(false);                    /* dump orphaned buffers */
   exit(sig);
}

static void sendit(void *sock, const char *fmt, ...)
{
   char buf[3000];
   va_list arg_ptr;

   va_start(arg_ptr, fmt);
   bvsnprintf(buf, sizeof(buf), (char *)fmt, arg_ptr);
   va_end(arg_ptr);
   fputs(buf, stdout);
   fflush(stdout);
}
