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
 *  Bacula File Daemon to Json
 *
 *    Kern Sibbald, Sept MMXII
 *
 */

#include "bacula.h"
#include "filed.h"

/* Imported Functions */
extern bool parse_fd_config(CONFIG *config, const char *configfile, int exit_code);
void store_msgs(LEX *lc, RES_ITEM *item, int index, int pass);

typedef struct
{
   /* default                   { { "Director": { "Name": aa, ...} }, { "Job": {..} */
   bool do_list;             /* [ {}, {}, ..] or { "aa": {}, "bb": {}, ...} */
   bool do_one;              /* { "Name": "aa", "Description": "test, ... } */
   bool do_only_data;        /* [ {}, {}, {}, ] */
   char *resource_type;
   char *resource_name;
   regex_t directive_reg;
} display_filter;

/* Forward referenced functions */
static bool check_resources();
static void sendit(void *sock, const char *fmt, ...);
static void dump_json(display_filter *filter);

/* Exported variables */
CLIENT *me;                           /* my resource */
#if defined(_MSC_VER)
extern "C" { // work around visual compiler mangling variables
   extern URES res_all;
}
#else
extern URES res_all;
#endif
extern s_kw msg_types[];
extern s_ct ciphertypes[];
extern s_ct digesttypes[];
extern RES_TABLE resources[];

#define CONFIG_FILE "bacula-fd.conf" /* default config file */

char *configfile = NULL;
static CONFIG *config;

static void usage()
{
   fprintf(stderr, _(
PROG_COPYRIGHT
"\n%sVersion: %s (%s)\n\n"
"Usage: bfdjson [options] [config_file]\n"
"        -r <res>    get resource type <res>\n"
"        -n <name>   get resource <name>\n"
"        -l <dirs>   get only directives matching dirs (use with -r)\n"
"        -D          get only data\n"
"        -c <file>   use <file> as configuration file\n"
"        -d <nn>     set debug level to <nn>\n"
"        -dt         print a timestamp in debug output\n"
"        -t          test configuration file and exit\n"
"        -v          verbose user messages\n"
"        -?          print this message.\n"
"\n"), 2012, "", VERSION, BDATE);

   exit(1);
}

static void display_cipher(HPKT &hpkt)
{
   int i;
   for (i=0; ciphertypes[i].type_name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == ciphertypes[i].type_value) {
         sendit(NULL, "\n    \"%s\": \"%s\"", hpkt.ritem->name,
                ciphertypes[i].type_name);
         return;
      }
   }
}

static void display_digest(HPKT &hpkt)
{
   int i;
   for (i=0; digesttypes[i].type_name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == digesttypes[i].type_value) {
         sendit(NULL, "\n    \"%s\": \"%s\"", hpkt.ritem->name,
                digesttypes[i].type_name);
         return;
      }
   }
}


/*********************************************************************
 *
 *  Bacula File daemon to Json
 *
 */

int main (int argc, char *argv[])
{
   int ch;
   bool test_config = false;
   display_filter filter;
   memset(&filter, 0, sizeof(filter));

   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");

   if (init_crypto() != 0) {
      Emsg0(M_ERROR_TERM, 0, _("Cryptography library initialization failed.\n"));
   }

   my_name_is(argc, argv, "bacula-fd");
   init_msg(NULL, NULL);

   while ((ch = getopt(argc, argv, "Dr:n:c:d:tv?l:")) != -1) {
      switch (ch) {
      case 'D':
         filter.do_only_data = true;
         break;
      case 'l':
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
   parse_fd_config(config, configfile, M_ERROR_TERM);

   if (!check_resources()) {
      Emsg1(M_ERROR, 0, _("Please correct configuration file: %s\n"), configfile);
      terminate_filed(1);
   }

   if (test_config) {
      terminate_filed(0);
   }

   dump_json(&filter);

   if (filter.do_list) {
      regfree(&filter.directive_reg);
   }

   terminate_filed(0);
   exit(0);                           /* should never get here */
}

void terminate_filed(int sig)
{
   static bool already_here = false;

   if (already_here) {
      bmicrosleep(2, 0);              /* yield */
      exit(1);                        /* prevent loops */
   }
   already_here = true;
   debug_level = 0;                   /* turn off debug */

   if (configfile != NULL) {
      free(configfile);
   }

   if (debug_level > 0) {
      print_memory_pool_stats();
   }
   if (config) {
      delete config;
      config = NULL;
   }
   term_msg();
   free(res_head);
   res_head = NULL;
   close_memory_pool();               /* release free memory in pool */
   //sm_dump(false);                    /* dump orphaned buffers */
   exit(sig);
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

   init_hpkt(hpkt);

   me = (CLIENT *)GetNextRes(R_CLIENT, NULL);

   if (filter->do_only_data) {
      sendit(NULL, "[");

   /* List resources and directives */
   /* { "aa": { "Name": "aa",.. }, "bb": { "Name": "bb", ... }
    * or print a single item
    */
   } else if (filter->do_one || filter->do_list) {
      sendit(NULL, "{");

   } else {
   /* [ { "Client": { "Name": "aa",.. } }, { "Director": { "Name": "bb", ... } } ]*/
      sendit(NULL, "[");
   }

   first_res = true;

   /* Loop over all resource types */
   for (resinx=0; resources[resinx].name; resinx++) {
      /* Skip Client alias */
      if (strcmp(resources[resinx].name, "Client") == 0) {
         continue;
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
            break;
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

         if (!first_res) {
            printf(",\n");
         }

         first_directive = 0;

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

         /* dirtry trick for a deprecated directive */
         bool dedup_index_directory_set = false;

         /* Loop over all items in the resource */
         for (item=0; items[item].name; item++) {
            /* Check user argument -l */
            if (filter->do_list &&
                regexec(&filter->directive_reg,
                        items[item].name, 32, pmatch, 0) != 0)
            {
               continue;
            }

            /* Special tweak for a deprecated variable */
            if (strcmp(items[item].name, "DedupIndexDirectory") == 0) {
               dedup_index_directory_set = bit_is_set(item, res_all.hdr.item_present);
               continue;
            }
            if (strcmp(items[item].name, "EnableClientRehydration") == 0) {
               if (dedup_index_directory_set && !bit_is_set(item, res_all.hdr.item_present)) {
                  set_bit(item, res_all.hdr.item_present);
                  *(bool *)(items[item].value) = true;
               }
            }

            hpkt.ritem = &items[item];
            if (bit_is_set(item, res_all.hdr.item_present)) {
               if (first_directive++ > 0) printf(",");
               if (display_global_item(hpkt)) {
                  /* Fall-through wanted */
               } else if (items[item].handler == store_cipher_type) {
                  display_cipher(hpkt);
               } else if (items[item].handler == store_digest_type) {
                  display_digest(hpkt);
               } else {
                  printf("\n    \"%s\": null", items[item].name);
               }
               directives++;
            } else { /* end if is present */
               /* For some directives, the bit_is_set() is not set (e.g. addresses) */
               if (me && strcmp(resources[resinx].name, "FileDaemon") == 0) {
                  if (strcmp(items[item].name, "FdPort") == 0) {
                     if (get_first_port_host_order(me->FDaddrs) != items[item].default_value) {
                        if (first_directive++ > 0) printf(",");
                        printf("\n    \"FdPort\": %d",
                           get_first_port_host_order(me->FDaddrs));
                     }
                  } else if (me && strcmp(items[item].name, "FdAddress") == 0) {
                     char buf[500];
                     get_first_address(me->FDaddrs, buf, sizeof(buf));
                     if (strcmp(buf, "0.0.0.0") != 0) {
                        if (first_directive++ > 0) printf(",");
                        printf("\n    \"FdAddress\": \"%s\"", buf);
                     }
                  } else if (me && strcmp(items[item].name, "FdSourceAddress") == 0
                             && me->FDsrc_addr) {
                     char buf[500];
                     get_first_address(me->FDsrc_addr, buf, sizeof(buf));
                     if (strcmp(buf, "0.0.0.0") != 0) {
                        if (first_directive++ > 0) printf(",");
                        printf("\n    \"FdSourceAddress\": \"%s\"", buf);
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
               sendit(NULL, "}\n}");
            }
         }
         first_res = false;
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


/*
* Make a quick check to see that we have all the
* resources needed.
*/
static bool check_resources()
{
   bool OK = true;
   DIRRES *director;
   CONSRES *cons;
   bool need_tls;

   LockRes();

   me = (CLIENT *)GetNextRes(R_CLIENT, NULL);
   if (!me) {
      Emsg1(M_FATAL, 0, _("No File daemon resource defined in %s\n"
            "Without that I don't know who I am :-(\n"), configfile);
      OK = false;
   } else {
      if (GetNextRes(R_CLIENT, (RES *) me) != NULL) {
         Emsg1(M_FATAL, 0, _("Only one Client resource permitted in %s\n"),
              configfile);
         OK = false;
      }
      my_name_is(0, NULL, me->hdr.name);
      if (!me->messages) {
         me->messages = (MSGS *)GetNextRes(R_MSGS, NULL);
         if (!me->messages) {
             Emsg1(M_FATAL, 0, _("No Messages resource defined in %s\n"), configfile);
             OK = false;
         }
      }
      /* tls_require implies tls_enable */
      if (me->tls_require) {
#ifndef HAVE_TLS
         Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
         OK = false;
#else
         me->tls_enable = true;
#endif
      }
      need_tls = me->tls_enable || me->tls_authenticate;

      if ((!me->tls_ca_certfile && !me->tls_ca_certdir) && need_tls) {
         Emsg1(M_FATAL, 0, _("Neither \"TLS CA Certificate\""
            " or \"TLS CA Certificate Dir\" are defined for File daemon in %s.\n"),
                            configfile);
        OK = false;
      }

      /* pki_encrypt implies pki_sign */
      if (me->pki_encrypt) {
         me->pki_sign = true;
      }

      if ((me->pki_encrypt || me->pki_sign) && !me->pki_keypair_file) {
         Emsg2(M_FATAL, 0, _("\"PKI Key Pair\" must be defined for File"
            " daemon \"%s\" in %s if either \"PKI Sign\" or"
            " \"PKI Encrypt\" are enabled.\n"), me->hdr.name, configfile);
         OK = false;
      }
   }


   /* Verify that a director record exists */
   LockRes();
   director = (DIRRES *)GetNextRes(R_DIRECTOR, NULL);
   UnlockRes();
   if (!director) {
      Emsg1(M_FATAL, 0, _("No Director resource defined in %s\n"),
            configfile);
      OK = false;
   }

   foreach_res(director, R_DIRECTOR) {
      /* tls_require implies tls_enable */
      if (director->tls_require) {
#ifndef HAVE_TLS
         Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
         OK = false;
         continue;
#else
         director->tls_enable = true;
#endif
      }
      need_tls = director->tls_enable || director->tls_authenticate;

      if (!director->tls_certfile && need_tls) {
         Emsg2(M_FATAL, 0, _("\"TLS Certificate\" file not defined for Director \"%s\" in %s.\n"),
               director->hdr.name, configfile);
         OK = false;
      }

      if (!director->tls_keyfile && need_tls) {
         Emsg2(M_FATAL, 0, _("\"TLS Key\" file not defined for Director \"%s\" in %s.\n"),
               director->hdr.name, configfile);
         OK = false;
      }

      if ((!director->tls_ca_certfile && !director->tls_ca_certdir) && need_tls && director->tls_verify_peer) {
         Emsg2(M_FATAL, 0, _("Neither \"TLS CA Certificate\""
                             " or \"TLS CA Certificate Dir\" are defined for Director \"%s\" in %s."
                             " At least one CA certificate store is required"
                             " when using \"TLS Verify Peer\".\n"),
                             director->hdr.name, configfile);
         OK = false;
      }
   }

   foreach_res(cons, R_CONSOLE) {
      /* tls_require implies tls_enable */
      if (cons->tls_require) {
#ifndef HAVE_TLS
         Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
         OK = false;
         continue;
#else
         cons->tls_enable = true;
#endif
      }
      need_tls = cons->tls_enable || cons->tls_authenticate;

      if (!cons->tls_certfile && need_tls) {
         Emsg2(M_FATAL, 0, _("\"TLS Certificate\" file not defined for Console \"%s\" in %s.\n"),
               cons->hdr.name, configfile);
         OK = false;
      }

      if (!cons->tls_keyfile && need_tls) {
         Emsg2(M_FATAL, 0, _("\"TLS Key\" file not defined for Console \"%s\" in %s.\n"),
               cons->hdr.name, configfile);
         OK = false;
      }

      if ((!cons->tls_ca_certfile && !cons->tls_ca_certdir) && need_tls && cons->tls_verify_peer) {
         Emsg2(M_FATAL, 0, _("Neither \"TLS CA Certificate\""
                             " or \"TLS CA Certificate Dir\" are defined for Console \"%s\" in %s."
                             " At least one CA certificate store is required"
                             " when using \"TLS Verify Peer\".\n"),
               cons->hdr.name, configfile);
         OK = false;
      }
   }

   UnlockRes();

   return OK;
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
