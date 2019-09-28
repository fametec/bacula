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
 *
 *   Bacula Console .conf to Json program.
 *
 *     Kern Sibbald, September MMXII
 *
 */

#include "bacula.h"
#include "lib/breg.h"
#include "console_conf.h"
#include "jcr.h"

/* Imported variables */
#if defined(_MSC_VER)
extern "C" { // work around visual compiler mangling variables
   extern URES res_all;
}
#else
extern URES res_all;
#endif
extern s_kw msg_types[];
extern RES_TABLE resources[];

/* Exported functions */
void senditf(const char *fmt, ...);
void sendit(const char *buf);

/* Imported functions */
extern bool parse_cons_config(CONFIG *config, const char *configfile, int exit_code);

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
static void terminate_console(int sig);
static int check_resources();
//static void ressendit(void *ua, const char *fmt, ...);
//static void dump_resource_types();
//static void dump_directives();
static void dump_json(display_filter *filter);

/* Static variables */
static char *configfile = NULL;
static FILE *output = stdout;
static bool teeout = false;               /* output to output and stdout */
static int numdir;
static POOLMEM *args;
static CONFIG *config;


#define CONFIG_FILE "bconsole.conf"   /* default configuration file */

static void usage()
{
   fprintf(stderr, _(
PROG_COPYRIGHT
"\n%sVersion: " VERSION " (" BDATE ") %s %s %s\n\n"
"Usage: bconsjson [options] [config_file]\n"
"       -r <res>    get resource type <res>\n"
"       -n <name>   get resource <name>\n"
"       -l <dirs>   get only directives matching dirs (use with -r)\n"
"       -D          get only data\n"
"       -c <file>   set configuration file to file\n"
"       -d <nn>     set debug level to <nn>\n"
"       -dt         print timestamp in debug output\n"
"       -t          test - read configuration and exit\n"
"       -v          verbose\n"
"       -?          print this message.\n"
"\n"), 2012, BDEMO, HOST_OS, DISTNAME, DISTVER);

   exit(1);
}

/*********************************************************************
 *
 *        Bacula console conf to Json
 *
 */
int main(int argc, char *argv[])
{
   int ch;
   bool test_config = false;
   display_filter filter;
   memset(&filter, 0, sizeof(filter));
   int rtn = 0;

   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");

   if (init_crypto() != 0) {
      Emsg0(M_ERROR_TERM, 0, _("Cryptography library initialization failed.\n"));
   }

   init_stack_dump();
   lmgr_init_thread();
   my_name_is(argc, argv, "bconsole");
   init_msg(NULL, NULL);
   working_directory = "/tmp";
   args = get_pool_memory(PM_FNAME);

   while ((ch = getopt(argc, argv, "n:vDabc:d:jl:r:t?")) != -1) {
      switch (ch) {
      case 'D':
         filter.do_only_data = true;
         break;
      case 'a':
//         list_all = true;
         break;

      case 'c':                    /* configuration file */
         if (configfile != NULL) {
            free(configfile);
         }
         configfile = bstrdup(optarg);
         break;

         break;

      case 'd':
         if (*optarg == 't') {
            dbg_timestamp = true;
         } else {
            debug_level = atoi(optarg);
            if (debug_level <= 0) {
               debug_level = 1;
            }
         }

      case 'l':
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

      case 't':
         test_config = true;
         break;

      case 'v':                    /* verbose */
         verbose++;
         break;

      case '?':
      default:
         usage();
         exit(1);
      }
   }
   argc -= optind;
   argv += optind;

   OSDependentInit();

   if (argc) {
      usage();
      exit(1);
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
      printf("config_file=%s\n", buf);
   }

   config = New(CONFIG());
   config->encode_password(false);
   parse_cons_config(config, configfile, M_ERROR_TERM);

   if (!check_resources()) {
      Emsg1(M_ERROR_TERM, 0, _("Please correct configuration file: %s\n"), configfile);
   }

   if (test_config) {
      terminate_console(0);
      exit(0);
   }

   dump_json(&filter);

   terminate_console(0);
   return rtn;
}

/* Cleanup and then exit */
static void terminate_console(int sig)
{
   static bool already_here = false;

   if (already_here) {                /* avoid recursive temination problems */
      exit(1);
   }
   already_here = true;
   stop_watchdog();
   delete config;
   config = NULL;
   free(res_head);
   res_head = NULL;
   free_pool_memory(args);
   lmgr_cleanup_main();

   if (sig != 0) {
      exit(1);
   }
   return;
}


/*
 * Dump out all resources in json format.
 * Note!!!! This routine must be in this file rather
 *  than in src/lib/parser_conf.c otherwise the pointers
 *  will be all messed up.
 */
static void dump_json(display_filter *filter)
{
   int resinx, item;
   int directives;
   bool first_res;
   bool first_directive;
   RES_ITEM *items;
   RES *res;
   HPKT hpkt;
   regmatch_t pmatch[32];

   init_hpkt(hpkt);

   /* List resources and directives */
   if (filter->do_only_data) {
      printf("[");

   /* { "aa": { "Name": "aa",.. }, "bb": { "Name": "bb", ... }
    * or print a single item
    */
   } else if (filter->do_one ||  filter->do_list) {
      printf("{");

   } else {
   /* [ { "Client": { "Name": "aa",.. } }, { "Director": { "Name": "bb", ... } } ]*/
      printf("[");
   }

   first_res = true;
   /* Loop over all resource types */
   for (resinx=0; resources[resinx].name; resinx++) {
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

         if (first_res) {
            printf("\n");

         } else {
            printf(",\n");
         }

         if (filter->do_only_data) {
            printf(" {");

         } else if (filter->do_one) {
            /* Nothing to print */

         /* When sending the list, the form is:
          *  { aa: { Name: aa, Description: aadesc...}, bb: { Name: bb
          */
         } else if (filter->do_list) {
            /* Search and display Name, should be the first item */
            for (item=0; items[item].name; item++) {
               if (strcmp(items[item].name, "Name") == 0) {
                  printf("%s: {\n", quote_string(hpkt.edbuf, *items[item].value));
                  break;
               }
            }
         } else {
            /* Begin new resource */
            printf("{\n  \"%s\": {", resources[resinx].name);
         }

         first_res = false;
         first_directive = true;
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
               if (!first_directive) printf(",");
               if (display_global_item(hpkt)) {
                  /* Fall-through wanted */
               } else {
                  printf("\n      \"%s\": null", items[item].name);
               }
               directives++;
               first_directive = false;
            }
            if (items[item].flags & ITEM_LAST) {
               display_last(hpkt);    /* If last bit set always call to cleanup */
            }
         }
         /* { "aa": { "Name": "aa",.. }, "bb": { "Name": "bb", ... } */
         if (filter->do_only_data || filter->do_list) {
            printf("\n }"); /* Finish the Resource with a single } */

         } else {
            if (filter->do_one) {
               /* don't print anything */
            } else if (directives) {
               printf("\n   }\n}");  /* end of resource */
            } else {
               printf("}\n}");
            }
         }
      } /* End loop over all resources of this type */
   } /* End loop all resource types */

   if (filter->do_only_data) {
      printf("\n]\n");

   } else  if (filter->do_one || filter->do_list) {
      printf("\n}\n");

   } else {
      printf("\n]\n");
   }
   term_hpkt(hpkt);
}


/*
 * Make a quick check to see that we have all the
 * resources needed.
 */
static int check_resources()
{
   bool OK = true;
   DIRRES *director;
   bool tls_needed;

   LockRes();

   numdir = 0;
   foreach_res(director, R_DIRECTOR) {

      numdir++;
      /* tls_require implies tls_enable */
      if (director->tls_require) {
         if (have_tls) {
            director->tls_enable = true;
         } else {
            Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
            OK = false;
            continue;
         }
      }

      tls_needed = director->tls_enable || director->tls_authenticate;

      if ((!director->tls_ca_certfile && !director->tls_ca_certdir) && tls_needed) {
         Emsg2(M_FATAL, 0, _("Neither \"TLS CA Certificate\""
                             " or \"TLS CA Certificate Dir\" are defined for Director \"%s\" in %s."
                             " At least one CA certificate store is required.\n"),
                             director->hdr.name, configfile);
         OK = false;
      }
   }

   if (numdir == 0) {
      Emsg1(M_FATAL, 0, _("No Director resource defined in %s\n"
                          "Without that I don't how to speak to the Director :-(\n"), configfile);
      OK = false;
   }

   CONRES *cons;
   /* Loop over Consoles */
   foreach_res(cons, R_CONSOLE) {
      /* tls_require implies tls_enable */
      if (cons->tls_require) {
         if (have_tls) {
            cons->tls_enable = true;
         } else {
            Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
            OK = false;
            continue;
         }
      }
      tls_needed = cons->tls_enable || cons->tls_authenticate;
      if ((!cons->tls_ca_certfile && !cons->tls_ca_certdir) && tls_needed) {
         Emsg2(M_FATAL, 0, _("Neither \"TLS CA Certificate\""
                             " or \"TLS CA Certificate Dir\" are defined for Console \"%s\" in %s.\n"),
                             cons->hdr.name, configfile);
         OK = false;
      }
   }

   UnlockRes();

   return OK;
}

#ifdef needed
static void ressendit(void *sock, const char *fmt, ...)
{
   char buf[3000];
   va_list arg_ptr;

   va_start(arg_ptr, fmt);
   bvsnprintf(buf, sizeof(buf), (char *)fmt, arg_ptr);
   va_end(arg_ptr);
   sendit(buf);
}

static void dump_resource_types()
{
   int i;
   bool first;

   /* List resources and their code */
   printf("[\n");
   first = true;
   for (i=0; resources[i].name; i++) {
      if (!first) {
         printf(",\n");
      }
      printf("   \"%s\": %d", resources[i].name, resources[i].rcode);
      first = false;
   }
   printf("\n]\n");
}

static void dump_directives()
{
   int i, j;
   bool first_res;
   bool first_directive;
   RES_ITEM *items;

   /* List resources and directives */
   printf("[\n");
   first_res = true;
   for (i=0; resources[i].name; i++) {
      if (!first_res) {
         printf(",\n");
      }
      printf("{\n   \"%s\": {\n", resources[i].name);
      first_res = false;
      first_directive = true;
      items = resources[i].items;
      for (j=0; items[j].name; j++) {
         if (!first_directive) {
            printf(",\n");
         }
         printf("      \"%s\": null", items[j].name);
         first_directive = false;
      }
      printf("\n   }");  /* end of resource */
   }
   printf("\n]\n");
}
#endif

/*
 * Send a line to the output file and or the terminal
 */
void senditf(const char *fmt,...)
{
   char buf[3000];
   va_list arg_ptr;

   va_start(arg_ptr, fmt);
   bvsnprintf(buf, sizeof(buf), (char *)fmt, arg_ptr);
   va_end(arg_ptr);
   sendit(buf);
}

void sendit(const char *buf)
{
#ifdef CONIO_FIX
   char obuf[3000];
   if (output == stdout || teeout) {
      const char *p, *q;
      /*
       * Here, we convert every \n into \r\n because the
       *  terminal is in raw mode when we are using
       *  conio.
       */
      for (p=q=buf; (p=strchr(q, '\n')); ) {
         int len = p - q;
         if (len > 0) {
            memcpy(obuf, q, len);
         }
         memcpy(obuf+len, "\r\n", 3);
         q = ++p;                    /* point after \n */
         fputs(obuf, output);
      }
      if (*q) {
         fputs(q, output);
      }
      fflush(output);
   }
   if (output != stdout) {
      fputs(buf, output);
   }
#else

   fputs(buf, output);
   fflush(output);
   if (teeout) {
      fputs(buf, stdout);
      fflush(stdout);
   }
#endif
}
