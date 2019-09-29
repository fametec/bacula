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
 *
 *   Bacula Director conf to Json
 *
 *     Kern Sibbald, September MMXII
 *
 */

#include "bacula.h"
#include "dird.h"

/* Exported subroutines */
extern bool parse_dir_config(CONFIG *config, const char *configfile, int exit_code);

static CONFIG *config;

/* Globals Exported */
DIRRES *director;                     /* Director resource */
int FDConnectTimeout;
int SDConnectTimeout;
char *configfile = NULL;
void *start_heap;

/* Globals Imported */
extern RES_ITEM job_items[];
extern s_jt jobtypes[];
extern s_jl joblevels[];
extern s_jt migtypes[];
extern s_kw ReplaceOptions[];
extern RES_ITEM2 newinc_items[];
extern RES_ITEM options_items[];
extern s_fs_opt FS_options[];
extern s_kw RunFields[];
extern s_kw tapelabels[];
extern s_kw msg_types[];
extern RES_TABLE resources[];

#if defined(_MSC_VER)
extern "C" { // work around visual compiler mangling variables
   extern URES res_all;
}
#else
extern URES res_all;
#endif


#define CONFIG_FILE "bacula-dir.conf" /* default configuration file */

static void usage()
{
   fprintf(stderr, _(
PROG_COPYRIGHT
"\n%sVersion: %s (%s)\n\n"
"Usage: bdirjson [<options>] [config_file]\n"
"       -r <res>    get resource type <res>\n"
"       -n <name>   get resource <name>\n"
"       -l <dirs>   get only directives matching dirs (use with -r)\n"
"       -D          get only data\n"
"       -R          do not apply JobDefs to Job\n"
"       -c <file>   set configuration file to file\n"
"       -d <nn>     set debug level to <nn>\n"
"       -dt         print timestamp in debug output\n"
"       -t          test - read configuration and exit\n"
"       -s          output in show text format\n"
"       -v          verbose user messages\n"
"       -?          print this message.\n"
"\n"), 2012, "", VERSION, BDATE);

   exit(1);
}

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

/* Forward referenced subroutines */
void terminate_dird(int sig);
static bool check_resources(bool config_test);
static void sendit(void *ua, const char *fmt, ...);
static void dump_json(display_filter *filter);

/*********************************************************************
 *
 *         Bacula Director conf to Json
 *
 */
int main (int argc, char *argv[])
{
   int ch;
   bool test_config = false;
   bool apply_jobdefs = true;
   bool do_show_format = false;
   display_filter filter;
   memset(&filter, 0, sizeof(filter));

   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");

   if (init_crypto() != 0) {
      Emsg0(M_ERROR_TERM, 0, _("Cryptography library initialization failed.\n"));
   }

   my_name_is(argc, argv, "bacula-dir");
   init_msg(NULL, NULL);

   while ((ch = getopt(argc, argv, "RCDc:d:stv?l:r:n:")) != -1) {
      switch (ch) {
      case 'R':
         apply_jobdefs = false;
         break;

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

      case 'c':                    /* specify config file */
         if (configfile != NULL) {
            free(configfile);
         }
         configfile = bstrdup(optarg);
         break;

      case 'd':                    /* set debug level */
         if (*optarg == 't') {
            dbg_timestamp = true;
         } else {
            debug_level = atoi(optarg);
            if (debug_level <= 0) {
               debug_level = 1;
            }
         }
         Dmsg1(10, "Debug level = %d\n", debug_level);
         break;

      case 's':                    /* Show text format */
         do_show_format = true;
         break;

      case 't':                    /* test config */
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
   parse_dir_config(config, configfile, M_ERROR_TERM);

   /* TODO: If we run check_resources, jobdefs will be copied to Job, and the job resource
    *  will no longer be the real job...
    */
   if (!check_resources(apply_jobdefs)) {
      Jmsg((JCR *)NULL, M_ERROR_TERM, 0, _("Please correct configuration file: %s\n"), configfile);
   }

   if (test_config) {
      terminate_dird(0);
   }

   my_name_is(0, NULL, director->name());    /* set user defined name */

   if (do_show_format) {
      /* Do show output in text */
      for (int i=r_first; i<=r_last; i++) {
         dump_each_resource(i, sendit, NULL);
      }
   } else {
      dump_json(&filter);
   }

   if (filter.do_list) {
      regfree(&filter.directive_reg);
   }

   terminate_dird(0);

   return 0;
}

/* Cleanup and then exit */
void terminate_dird(int sig)
{
   static bool already_here = false;

   if (already_here) {                /* avoid recursive temination problems */
      bmicrosleep(2, 0);              /* yield */
      exit(1);
   }
   already_here = true;
   debug_level = 0;                   /* turn off debug */
   if (configfile != NULL) {
      free(configfile);
   }
   if (debug_level > 5) {
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
   //sm_dump(false);
   exit(sig);
}


static void display_jobtype(HPKT &hpkt)
{
   int i;
   for (i=0; jobtypes[i].type_name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == jobtypes[i].job_type) {
         sendit(NULL, "\n    \"%s\": %s", hpkt.ritem->name,
            quote_string(hpkt.edbuf, jobtypes[i].type_name));
         return;
      }
   }
}

static void display_label(HPKT &hpkt)
{
   int i;
   for (i=0; tapelabels[i].name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == tapelabels[i].token) {
         sendit(NULL, "\n    \"%s\": %s", hpkt.ritem->name,
            quote_string(hpkt.edbuf, tapelabels[i].name));
         return;
      }
   }
}

static void display_joblevel(HPKT &hpkt)
{
   int i;
   for (i=0; joblevels[i].level_name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == joblevels[i].level) {
         sendit(NULL, "\n    \"%s\": %s", hpkt.ritem->name,
            quote_string(hpkt.edbuf, joblevels[i].level_name));
         return;
      }
   }
}

static void display_replace(HPKT &hpkt)
{
   int i;
   for (i=0; ReplaceOptions[i].name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == ReplaceOptions[i].token) {
         sendit(NULL, "\n    \"%s\": %s", hpkt.ritem->name,
            quote_string(hpkt.edbuf, ReplaceOptions[i].name));
         return;
      }
   }
}

static void display_migtype(HPKT &hpkt)
{
   int i;
   for (i=0; migtypes[i].type_name; i++) {
      if (*(int32_t *)(hpkt.ritem->value) == migtypes[i].job_type) {
         sendit(NULL, "\n    \"%s\": %s", hpkt.ritem->name,
            quote_string(hpkt.edbuf, migtypes[i].type_name));
         return;
      }
   }
}

static void display_actiononpurge(HPKT &hpkt)
{
   sendit(NULL, "\n    \"%s\":", hpkt.ritem->name);
   if (*(uint32_t *)(hpkt.ritem->value) | ON_PURGE_TRUNCATE) {
      sendit(NULL, "\"Truncate\"");
   } else {
      sendit(NULL, "null");
   }
}

static void display_acl(HPKT &hpkt)
{
   sendit(NULL, "\n    \"%s\":", hpkt.ritem->name);
   hpkt.list = ((alist **)hpkt.ritem->value)[hpkt.ritem->code];
   display_alist(hpkt);
}


static void display_options(HPKT &hpkt, INCEXE *ie)
{
   char *elt;
   bool first_opt = true;
   bool first_dir;
   int i, j, k;
   alist *list;

   sendit(NULL, "      \"Options\": [ \n       {\n");
   for (i=0; i<ie->num_opts; i++) {
      FOPTS *fo = ie->opts_list[i];
      if (!first_opt) {
         sendit(NULL, ",\n       {\n");
      }
      first_dir = true;
      for (j=0; options_items[j].name; j++) {
         if (options_items[j].handler == store_regex) {
            switch (options_items[j].code) {
            case 1:  /* RegexDir */
               list = &fo->regexdir;
               break;
            case 2:    /* RegexFile */
               list = &fo->regexfile;
               break;
            default:
               list = &fo->regex;
               break;
            }
            if (list->size() > 0) {
               if (!first_dir) {
                  sendit(NULL, ",\n");
               }
               sendit(NULL, "         \"%s\":", options_items[j].name);
               hpkt.list = list;
               display_alist(hpkt);
               first_dir = false;
               first_opt = false;
            }
         } else if (options_items[j].handler == store_wild) {
            switch (options_items[j].code) {
            case 1:  /* WildDir */
               list = &fo->wilddir;
               break;
            case 2:    /* WildFile */
               /*
                * Note: There used to be an enhanced wild card feature,
                *  which was not documented so it is removed, and
                *  apparently the wildfile patterns are stored in the
                *  wildbase list, so we dump it here.
                * The old enhanced wild implementation appears to be poorly
                *  done, because either there should be two clearly named
                *  lists, or one with everything.
                */
               /* We copy one list to the other, else we may print two
                * times the WildFile list. I don't know how, but sometime
                * the two lists contain elements.
                */
               list = &fo->wildfile;
               foreach_alist(elt, list) {
                  fo->wildbase.append(bstrdup(elt));
               }
               list = &fo->wildbase;
               break;
            default:
               list = &fo->wild;
               break;
            }
            if (list->size() > 0) {
               if (!first_dir) {
                  sendit(NULL, ",\n");
               }
               sendit(NULL, "         \"%s\":", options_items[j].name);
               hpkt.list = list;
               display_alist(hpkt);
               first_dir = false;
               first_opt = false;
            }
         } else if (options_items[j].handler == store_base) {
            list = &fo->base;
            if (list->size() > 0) {
               if (!first_dir) {
                  sendit(NULL, ",\n");
               }
               sendit(NULL, "         \"%s\":", options_items[j].name);
               hpkt.list = list;
               display_alist(hpkt);
               first_dir = false;
               first_opt = false;
            }
         } else if (options_items[j].handler == store_opts) {
            bool found = false;
            if (bit_is_set(options_items[j].flags, ie->opt_present)) {
               for (k=0; FS_options[k].name; k++) {
                  if (FS_options[k].keyword == (int)options_items[j].flags) {
                     char lopts[100];
                     strip_long_opts(lopts, fo->opts);
                     if (strstr(lopts, FS_options[k].option)) {
                        if (!first_dir) {
                           sendit(NULL, ",\n");
                        }
                        sendit(NULL, "         \"%s\": %s", options_items[j].name,
                           quote_string(hpkt.edbuf, FS_options[k].name));
                        found = true;
                        break;
                     }
                  }
               }
               if (found) {
                  first_dir = false;
                  first_opt = false;
               }
            }
         } else if (options_items[j].handler == store_lopts) {
            bool found = false;
            if (bit_is_set(options_items[j].flags, ie->opt_present)) {
               char *pos;
               /* Search long_options for code (V, J, C, P) */
               if ((pos=strchr(fo->opts, options_items[j].code))) {
                  char lopts[100];
                  char *end, bkp;
                  pos++;                   /* point to beginning of options */
                  bstrncpy(lopts, pos, sizeof(lopts));
                  /* Now terminate at first : */
                  end = strchr(pos, ':');
                  if (end) {
                     bkp = *end;     /* save the original char */
                     *end = 0;       /* terminate this string */
                  }
                  if (!first_dir) {
                     sendit(NULL, ",\n");
                  }
                  sendit(NULL, "         \"%s\": %s", options_items[j].name,
                     quote_string(hpkt.edbuf, pos));
                  found = true;
                  if (end) {    /* Still have other options to parse */
                     *end = bkp;
                  }
               }
               if (found) {
                  first_dir = false;
                  first_opt = false;
               }
            }
         } else if (options_items[j].handler == store_plugin) {
            if (fo->plugin) {
               if (!first_dir) {
                  sendit(NULL, ",\n");
               }
               sendit(NULL, "         \"%s\": %s", options_items[j].name,
                  quote_string(hpkt.edbuf, fo->plugin));
               first_dir = false;
               first_opt = false;
            }
         } else if (options_items[j].handler == store_fstype) {
            list = &fo->fstype;
            if (list->size() > 0) {
               if (!first_dir) {
                  sendit(NULL, ",\n");
               }
               sendit(NULL, "         \"%s\":", options_items[j].name);
               hpkt.list = list;
               display_alist(hpkt);
               first_dir = false;
               first_opt = false;
            }
         } else if (options_items[j].handler == store_drivetype) {
            list = &fo->drivetype;
            if (list->size() > 0) {
               if (!first_dir) {
                  sendit(NULL, ",\n");
               }
               sendit(NULL, "         \"%s\":", options_items[j].name);
               hpkt.list = list;
               display_alist(hpkt);
               first_dir = false;
               first_opt = false;
            }
         }
      }
      sendit(NULL, "\n       }");
   }
   sendit(NULL, "\n      ]");
}

/*
 * Include or Exclude in a FileSet
 * TODO: Not working with multiple Include{}
 *    O M
 *    N
 *    I /tmp/regress/build
 *    N
 *    O Z1
 *    N
 *    I /tmp
 *    N
 */
static void display_include_exclude(HPKT &hpkt)
{
   bool first_dir;
   int i, j;
   FILESET *fs = (FILESET *)hpkt.res;

   if (hpkt.ritem->code == 0) { /* Include */
      INCEXE *ie;
      sendit(NULL, "\n    \"%s\": [{\n", hpkt.ritem->name);
      for (j=0; j<fs->num_includes; j++) {
         if (j > 0) {
            sendit(NULL, ",\n    {\n");
         }
         first_dir = true;
         ie = fs->include_items[j];
         for (i=0; newinc_items[i].name; i++) {
            if (strcasecmp(newinc_items[i].name, "File") == 0) {
               if (!first_dir) {
                  sendit(NULL, ",\n");
               }
               sendit(NULL, "      \"%s\":", newinc_items[i].name);
               first_dir = false;
               hpkt.list = &ie->name_list;
               display_alist(hpkt);
            } if (strcasecmp(newinc_items[i].name, "Plugin") == 0 &&
                  ie->plugin_list.size() > 0) {
               if (!first_dir) {
                  sendit(NULL, ",\n");
               }
               sendit(NULL, "      \"%s\":", newinc_items[i].name);
               first_dir = false;
               hpkt.list = &ie->plugin_list;
               display_alist(hpkt);
            } if (strcasecmp(newinc_items[i].name, "Options") == 0 &&
                  ie->num_opts > 0) {
               if (!first_dir) {
                  sendit(NULL, ",\n");
               }
               display_options(hpkt, ie);
            } if (strcasecmp(newinc_items[i].name, "ExcludeDirContaining") == 0 &&
                  ie->ignoredir) {
               if (!first_dir) {
                  sendit(NULL, ",\n");
               }
               sendit(NULL, "      \"%s\": %s ", newinc_items[i].name,
                  quote_string(hpkt.edbuf, ie->ignoredir));
               first_dir = false;
            }
         }
         sendit(NULL, "\n    }");
      }
      sendit(NULL, "]");
   } else {
      /* Exclude */
      sendit(NULL, "\n    \"%s\": {\n", hpkt.ritem->name);
      first_dir = true;
      for (int i=0; newinc_items[i].name; i++) {
         INCEXE *ie;
         if (strcasecmp(newinc_items[i].name, "File") == 0) {
            if (!first_dir) {
               sendit(NULL, ",\n");
            }
            sendit(NULL, "      \"%s\": ", newinc_items[i].name);
            first_dir = false;
            ie = fs->exclude_items[0];
            hpkt.list = &ie->name_list;
            display_alist(hpkt);
         }
      }
      sendit(NULL, "\n    }");
   }
}

static bool display_runscript(HPKT &hpkt)
{
   RUNSCRIPT *script;
   RUNSCRIPT *def = new_runscript();
   alist **runscripts = (alist **)(hpkt.ritem->value) ;
   bool first=true;

   if (!*runscripts || (*runscripts)->size() == 0) {
      return false;
   }

   sendit(NULL, "\n    \"Runscript\": [\n");

   foreach_alist(script, *runscripts) {
      if (first) {
         sendit(NULL, "      {\n");
      } else {
         sendit(NULL, ",\n      {\n");
      }
      if (script->when == SCRIPT_Any) {
         sendit(NULL, "        \"RunsWhen\": \"Any\",\n");

      } else if (script->when == SCRIPT_After) {
         sendit(NULL, "        \"RunsWhen\": \"After\",\n");

      } else if (script->when == SCRIPT_Before) {
         sendit(NULL, "        \"RunsWhen\": \"Before\",\n");

      } else if (script->when == SCRIPT_AfterVSS) {
         sendit(NULL, "        \"RunsWhen\": \"AfterVSS\",\n");
      }

      if (script->fail_on_error != def->fail_on_error) {
         sendit(NULL, "        \"FailJobOnError\": %s,\n", script->fail_on_error?"true":"false");
      }

      if (script->on_success != def->on_success) {
         sendit(NULL, "        \"RunsOnSuccess\": %s,\n", script->on_success?"true":"false");
      }

      if (script->on_failure != def->on_failure) {
         sendit(NULL, "        \"RunsOnFailure\": %s,\n", script->on_failure?"true":"false");
      }

      if (script->is_local()) {
         sendit(NULL, "        \"RunsOnClient\": false,\n");
      }

      if (script->command) {
         sendit(NULL, "        \"%s\": %s\n",
                (script->cmd_type == SHELL_CMD)?"Command":"Console",
                quote_string(hpkt.edbuf, script->command));
      }
      sendit(NULL, "      }");
      first = false;
   }

   sendit(NULL, "\n    ]\n");
   free_runscript(def);
   return true;
}

static void display_run(HPKT &hpkt)
{
   int i, j;
   RUN **prun = (RUN **)hpkt.ritem->value;
   RUN *run = *prun;
   bool first = true;
   bool first_run = true;
   RES *res;

   sendit(NULL, "\n    \"%s\": [\n", hpkt.ritem->name);
   for ( ; run; run=run->next) {
      if (!first_run) sendit(NULL, ",\n");
      first_run = false;
      first = true;
      sendit(NULL, "     {\n");
      /* First do override fields */
      for (i=0; RunFields[i].name; i++) {
         switch (RunFields[i].token) {
         case 'f':  /* FullPool */
            if (run->full_pool) {
               res = (RES *)run->full_pool;
               if (!first) sendit(NULL, ",\n");
               sendit(NULL, "      \"%s\": %s", RunFields[i].name,
                     quote_string(hpkt.edbuf, res->name));
               first = false;
            }
            break;
         case 'i':  /* IncrementalPool */
            if (run->inc_pool) {
               res = (RES *)run->inc_pool;
               if (!first) sendit(NULL, ",\n");
               sendit(NULL, "      \"%s\": %s", RunFields[i].name,
                     quote_string(hpkt.edbuf, res->name));
               first = false;
            }
            break;
         case 'd':  /* Differential Pool */
            if (run->diff_pool) {
               res = (RES *)run->diff_pool;
               if (!first) sendit(NULL, ",\n");
               sendit(NULL, "      \"%s\": %s", RunFields[i].name,
                     quote_string(hpkt.edbuf, res->name));
               first = false;
            }
            break;
         case 'N':  /* Next Pool */
            if (run->next_pool) {
               res = (RES *)run->next_pool;
               if (!first) sendit(NULL, ",\n");
               sendit(NULL, "      \"%s\": %s", RunFields[i].name,
                     quote_string(hpkt.edbuf, res->name));
               first = false;
            }
            break;
         case 'L':  /* Level */
            /* TODO: It's not always set, only when having Level= in the line */
            //if (run->level_set) {
               for (j=0; joblevels[j].level_name; j++) {
                  if ((int)run->level == joblevels[j].level) {
                     if (!first) sendit(NULL, ",\n");
                     sendit(NULL, "      \"%s\": \"%s\"", RunFields[i].name,
                        joblevels[j].level_name);
                     first = false;
                     break;
                  }
               }
            //}
            break;
         case 'P':  /* Pool */
            if (run->pool) {
               res = (RES *)run->pool;
               if (!first) sendit(NULL, ",\n");
               sendit(NULL, "      \"%s\": %s", RunFields[i].name,
                     quote_string(hpkt.edbuf, res->name));
               first = false;
            }
            break;
         case 'S':  /* Storage */
            if (run->storage) {
               res = (RES *)run->storage;
               if (!first) sendit(NULL, ",\n");
               sendit(NULL, "      \"%s\": %s", RunFields[i].name,
                     quote_string(hpkt.edbuf, res->name));
               first = false;
            }
            break;
         case 'M':  /* Messages */
            if (run->msgs) {
               res = (RES *)run->msgs;
               if (!first) sendit(NULL, ",\n");
               sendit(NULL, "      \"%s\": %s", RunFields[i].name,
                     quote_string(hpkt.edbuf, res->name));
               first = false;
            }
            break;
         case 'p':  /* priority */
            if (run->priority_set) {
               if (!first) sendit(NULL, ",\n");
               sendit(NULL, "      \"%s\": %d", RunFields[i].name,
                     run->Priority);
               first = false;
            }
            break;
         case 's':  /* Spool Data */
            if (run->spool_data_set) {
               if (!first) sendit(NULL, ",\n");
               sendit(NULL, "      \"%s\": %s", RunFields[i].name,
                     run->spool_data?"true":"false");
               first = false;
            }
            break;
         case 'W':  /* Write Part After Job */
            if (run->write_part_after_job_set) {
               if (!first) sendit(NULL, ",\n");
               sendit(NULL, "      \"%s\": %s", RunFields[i].name,
                     run->write_part_after_job?"true":"false");
               first = false;
            }
            break;
         case 'm':  /* MaxRunScheduledTime */
            if (run->MaxRunSchedTime_set) {
               if (!first) sendit(NULL, ",\n");
               sendit(NULL, "      \"%s\": %lld", RunFields[i].name,
                     run->MaxRunSchedTime);
               first = false;
            }
            break;
         case 'a':  /* Accurate */
            if (run->accurate_set) {
               if (!first) sendit(NULL, ",\n");
               sendit(NULL, "      \"%s\": %s", RunFields[i].name,
                     run->accurate?"true":"false");
               first = false;
            }
            break;
         default:
            break;
         }
      } /* End all RunFields (overrides) */
      /* Now handle timing */
      if (byte_is_set(run->hour, sizeof(run->hour))) {
         if (!first) sendit(NULL, ",\n");
         sendit(NULL, "      \"Hour\":");
         display_bit_array(run->hour, 24);
         sendit(NULL, ",\n      \"Minute\": %d", run->minute);
         first = false;
      }
      /* bit 32 is used to store the keyword LastDay, so we look up to 0-31 */
      if (byte_is_set(run->mday, sizeof(run->mday))) {
         if (!first) sendit(NULL, ",\n");
         sendit(NULL, "      \"Day\":");
         display_bit_array(run->mday, 31);
         first = false;
      }
      if (run->last_day_set) {
         if (!first) sendit(NULL, ",\n");
         sendit(NULL, "      \"LastDay\": 1");
         first = false;
      }
      if (byte_is_set(run->month, sizeof(run->month))) {
         if (!first) sendit(NULL, ",\n");
         sendit(NULL, "      \"Month\":");
         display_bit_array(run->month, 12);
         first = false;
      }
      if (byte_is_set(run->wday, sizeof(run->wday))) {
         if (!first) sendit(NULL, ",\n");
         sendit(NULL, "      \"DayOfWeek\":");
         display_bit_array(run->wday, 7);
         first = false;
      }
      if (byte_is_set(run->wom, sizeof(run->wom))) {
         if (!first) sendit(NULL, ",\n");
         sendit(NULL, "      \"WeekOfMonth\":");
         display_bit_array(run->wom, 6);
         first = false;
      }
      if (byte_is_set(run->woy, sizeof(run->woy))) {
         if (!first) sendit(NULL, ",\n");
         sendit(NULL, "      \"WeekOfYear\":");
         display_bit_array(run->woy, 54);
         first = false;
      }
      sendit(NULL, "\n     }");

   } /* End this Run directive */
   sendit(NULL, "\n    ]");
}

/*
 * Dump out all resources in json format.
 * Note!!!! This routine must be in this file rather
 *  than in src/lib/parser_conf.c otherwise the pointers
 *  will be all messed up.
 */
static void dump_json(display_filter *filter)
{
   int resinx, item, first_directive, name_pos=0;
   bool first_res;
   RES_ITEM *items;
   RES *res;
   HPKT hpkt;
   regmatch_t pmatch[32];

   init_hpkt(hpkt);

   /* List resources and directives */
   if (filter->do_only_data) {
      /* Skip the Name */
      sendit(NULL, "[");

   /*
    * { "aa": { "Name": "aa",.. }, "bb": { "Name": "bb", ... }
    * or print a single item
    */
   } else if (filter->do_one || filter->do_list) {
      sendit(NULL, "{");

   } else {
   /* [ { "Client": { "Name": "aa",.. } }, { "Director": { "Name": "bb", ... } } ]*/
      sendit(NULL, "[");
   }

   first_res = true;
   /* Main loop over all resources */
   for (resinx=0; resources[resinx].items; resinx++) {

      /* Skip this resource type? */
      if (filter->resource_type &&
          strcasecmp(filter->resource_type, resources[resinx].name) != 0) {
         continue;
      }

      /* Loop over each resource of this type */
      foreach_rblist(res, res_head[resinx]->res_list) {
         hpkt.res = res;
         items = resources[resinx].items;
         if (!items) {
            continue;
         }

         /* Copy the resource into res_all */
         memcpy(&res_all, res, sizeof(res_all));

         /* If needed, skip this resource type */
         if (filter->resource_name) {
            bool skip=true;
            /* The Name should be at the first place, so this is not a real loop */
            for (item=0; items[item].name; item++) {
               if (strcasecmp(items[item].name, "Name") == 0) {
                  if (strcmp(*(items[item].value), filter->resource_name) == 0) {
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

         /* Find where the Name is defined, should always be 0 */
         for (item=0; items[item].name; item++) {
            if (strcmp(items[item].name, "Name") == 0) {
               name_pos = item;
               break;
            }
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

         /*
          * Here we walk through a resource displaying all the
          *   directives and sub-resources in the resource.
          */
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

               /* Skip Directive in lowercase, but check if the next
                * one is pointing to the same location (for example User and dbuser)
                */
               if (!B_ISUPPER(*(items[item].name))) {
                  int i=item+1;
                  while(!B_ISUPPER(*(items[i].name)) && items[i].value == items[item].value) {
                     i++;
                  }
                  if (items[i].value == items[item].value) {
                     set_bit(i, res_all.hdr.item_present);
                  }
                  continue;
               }

               if (first_directive++ > 0) sendit(NULL, ",");
               if (display_global_item(hpkt)) {
                  /* Fall-through wanted */
               } else if (items[item].handler == store_jobtype) {
                  display_jobtype(hpkt);
               } else if (items[item].handler == store_label) {
                  display_label(hpkt);
               } else if (items[item].handler == store_level) {
                  display_joblevel(hpkt);
               } else if (items[item].handler == store_replace) {
                  display_replace(hpkt);
               } else if (items[item].handler == store_migtype) {
                  display_migtype(hpkt);
               } else if (items[item].handler == store_actiononpurge) {
                  display_actiononpurge(hpkt);
               /* FileSet Include/Exclude directive */
               } else if (items[item].handler == store_inc) {
                  display_include_exclude(hpkt);
               } else if (items[item].handler == store_ac_res) {
                  display_res(hpkt);
               /* A different alist for each item.code */
               } else if (items[item].handler == store_acl) {
                  display_acl(hpkt);
               } else if (items[item].handler == store_device) {
                  display_alist_res(hpkt);
               } else if (items[item].handler == store_run) {
                  display_run(hpkt);
               } else if (items[item].handler == store_runscript) {
                  if (!display_runscript(hpkt)) {
                     first_directive = 0;  /* Do not print a comma after this empty runscript */
                  }
               } else {
                  sendit(NULL, "\n    \"%s\": null", items[item].name);
               }
            } else { /* end if is present */
               /* For some directive, the bitmap is not set (like addresses) */
               /* Special trick for the Autochanger directive, it can be yes/no/storage */
               if (strcmp(resources[resinx].name, "Storage") == 0) {
                  if (strcasecmp(items[item].name, "Autochanger") == 0
                      && items[item].handler == store_bool /* yes or no */
                      && *(bool *)(items[item].value) == true)
                  {
                     if (first_directive++ > 0) sendit(NULL, ",");
                     if (*(items[item-1].value) == NULL) {
                        sendit(NULL, "\n    \"Autochanger\": %s", quote_string(hpkt.edbuf2, *items[name_pos].value));
                     } else {
                        STORE *r = (STORE *)*(items[item-1].value);
                        sendit(NULL, "\n    \"Autochanger\": %s", quote_string(hpkt.edbuf2, r->name()));
                     }
                  }
               }

               if (strcmp(resources[resinx].name, "Director") == 0) {
                  if (strcmp(items[item].name, "DirPort") == 0) {
                     if (get_first_port_host_order(director->DIRaddrs) != items[item].default_value) {
                        if (first_directive++ > 0) sendit(NULL, ",");
                        sendit(NULL, "\n    \"DirPort\": %d",
                           get_first_port_host_order(director->DIRaddrs));
                     }

                  } else if (strcmp(items[item].name, "DirAddress") == 0) {
                     char buf[500];
                     get_first_address(director->DIRaddrs, buf, sizeof(buf));
                     if (strcmp(buf, "0.0.0.0") != 0) {
                        if (first_directive++ > 0) sendit(NULL, ",");
                        sendit(NULL, "\n    \"DirAddress\": \"%s\"", buf);
                     }

                  } else if (strcmp(items[item].name, "DirSourceAddress") == 0 && director->DIRsrc_addr) {
                     char buf[500];
                     get_first_address(director->DIRsrc_addr, buf, sizeof(buf));
                     if (strcmp(buf, "0.0.0.0") != 0) {
                        if (first_directive++ > 0) sendit(NULL, ",");
                        sendit(NULL, "\n    \"DirSourceAddress\": \"%s\"", buf);
                     }
                  }
               }
            }
            if (items[item].flags & ITEM_LAST) {
               display_last(hpkt);    /* If last bit set always call to cleanup */
            }
         } /* loop over directive names */

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
 *
 *  **** FIXME **** this routine could be a lot more
 *   intelligent and comprehensive.
 */
static bool check_resources(bool apply_jobdefs)
{
   bool OK = true;
   JOB *job;
   bool need_tls;

   LockRes();

   job = (JOB *)GetNextRes(R_JOB, NULL);
   director = (DIRRES *)GetNextRes(R_DIRECTOR, NULL);
   if (!director) {
      Jmsg(NULL, M_FATAL, 0, _("No Director resource defined in %s\n"
"Without that I don't know who I am :-(\n"), configfile);
      OK = false;
   } else {
      set_working_directory(director->working_directory);
      if (!director->messages) {       /* If message resource not specified */
         director->messages = (MSGS *)GetNextRes(R_MSGS, NULL);
         if (!director->messages) {
            Jmsg(NULL, M_FATAL, 0, _("No Messages resource defined in %s\n"), configfile);
            OK = false;
         }
      }
      if (GetNextRes(R_DIRECTOR, (RES *)director) != NULL) {
         Jmsg(NULL, M_FATAL, 0, _("Only one Director resource permitted in %s\n"),
            configfile);
         OK = false;
      }
      /* tls_require implies tls_enable */
      if (director->tls_require) {
         if (have_tls) {
            director->tls_enable = true;
         } else {
            Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
            OK = false;
         }
      }

      need_tls = director->tls_enable || director->tls_authenticate;

      if (!director->tls_certfile && need_tls) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Certificate\" file not defined for Director \"%s\" in %s.\n"),
            director->name(), configfile);
         OK = false;
      }

      if (!director->tls_keyfile && need_tls) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Key\" file not defined for Director \"%s\" in %s.\n"),
            director->name(), configfile);
         OK = false;
      }

      if ((!director->tls_ca_certfile && !director->tls_ca_certdir) &&
           need_tls && director->tls_verify_peer) {
         Jmsg(NULL, M_FATAL, 0, _("Neither \"TLS CA Certificate\" or \"TLS CA"
              " Certificate Dir\" are defined for Director \"%s\" in %s."
              " At least one CA certificate store is required"
              " when using \"TLS Verify Peer\".\n"),
              director->name(), configfile);
         OK = false;
      }
   }

   /* Loop over Consoles */
   CONRES *cons;
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

      need_tls = cons->tls_enable || cons->tls_authenticate;

      if (!cons->tls_certfile && need_tls) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Certificate\" file not defined for Console \"%s\" in %s.\n"),
            cons->name(), configfile);
         OK = false;
      }

      if (!cons->tls_keyfile && need_tls) {
         Jmsg(NULL, M_FATAL, 0, _("\"TLS Key\" file not defined for Console \"%s\" in %s.\n"),
            cons->name(), configfile);
         OK = false;
      }

      if ((!cons->tls_ca_certfile && !cons->tls_ca_certdir)
            && need_tls && cons->tls_verify_peer) {
         Jmsg(NULL, M_FATAL, 0, _("Neither \"TLS CA Certificate\" or \"TLS CA"
            " Certificate Dir\" are defined for Console \"%s\" in %s."
            " At least one CA certificate store is required"
            " when using \"TLS Verify Peer\".\n"),
            cons->name(), configfile);
         OK = false;
      }
      /* If everything is well, attempt to initialize our per-resource TLS context */
      if (OK && (need_tls || cons->tls_require)) {
         /* Initialize TLS context:
          * Args: CA certfile, CA certdir, Certfile, Keyfile,
          * Keyfile PEM Callback, Keyfile CB Userdata, DHfile, Verify Peer */
         cons->tls_ctx = new_tls_context(cons->tls_ca_certfile,
            cons->tls_ca_certdir, cons->tls_certfile,
            cons->tls_keyfile, NULL, NULL, cons->tls_dhfile, cons->tls_verify_peer);

         if (!cons->tls_ctx) {
            Jmsg(NULL, M_FATAL, 0, _("Failed to initialize TLS context for Console \"%s\" in %s.\n"),
               cons->name(), configfile);
            OK = false;
         }
      }

   }

   /* Loop over Clients */
   CLIENT *client;
   foreach_res(client, R_CLIENT) {
      /* tls_require implies tls_enable */
      if (client->tls_require) {
         if (have_tls) {
            client->tls_enable = true;
         } else {
            Jmsg(NULL, M_FATAL, 0, _("TLS required but not configured in Bacula.\n"));
            OK = false;
            continue;
         }
      }
      need_tls = client->tls_enable || client->tls_authenticate;
      if ((!client->tls_ca_certfile && !client->tls_ca_certdir) && need_tls) {
         Jmsg(NULL, M_FATAL, 0, _("Neither \"TLS CA Certificate\""
            " or \"TLS CA Certificate Dir\" are defined for File daemon \"%s\" in %s.\n"),
            client->name(), configfile);
         OK = false;
      }

   }

   if (!job) {
      Jmsg(NULL, M_FATAL, 0, _("No Job records defined in %s\n"), configfile);
      OK = false;
   }

   /* TODO: We can't really update all job, we need to show only the real configuration
    * and not Job+JobDefs
    */
   if (!apply_jobdefs) {
      UnlockRes();
      return OK;
   }

   foreach_res(job, R_JOB) {
      int i;

      if (job->jobdefs) {
         JOB *jobdefs = job->jobdefs;
         /* Handle RunScripts alists specifically */
         if (jobdefs->RunScripts) {
            RUNSCRIPT *rs, *elt;

            if (!job->RunScripts) {
               job->RunScripts = New(alist(10, not_owned_by_alist));
            }

            foreach_alist(rs, jobdefs->RunScripts) {
               elt = copy_runscript(rs);
               job->RunScripts->append(elt); /* we have to free it */
            }
         }

         /* Transfer default items from JobDefs Resource */
         for (i=0; job_items[i].name; i++) {
            char **def_svalue, **svalue;  /* string value */
            uint32_t *def_ivalue, *ivalue;     /* integer value */
            bool *def_bvalue, *bvalue;    /* bool value */
            int64_t *def_lvalue, *lvalue; /* 64 bit values */
            uint32_t offset;
            alist **def_avalue, **avalue; /* alist value */

            Dmsg4(1400, "Job \"%s\", field \"%s\" bit=%d def=%d\n",
                job->name(), job_items[i].name,
                bit_is_set(i, job->hdr.item_present),
                bit_is_set(i, job->jobdefs->hdr.item_present));

            if (!bit_is_set(i, job->hdr.item_present) &&
                 bit_is_set(i, job->jobdefs->hdr.item_present)) {
               Dmsg2(400, "Job \"%s\", field \"%s\": getting default.\n",
                 job->name(), job_items[i].name);
               offset = (char *)(job_items[i].value) - (char *)&res_all;
               /*
                * Handle strings and directory strings
                */
               if (job_items[i].handler == store_str ||
                   job_items[i].handler == store_dir) {
                  def_svalue = (char **)((char *)(job->jobdefs) + offset);
                  Dmsg5(400, "Job \"%s\", field \"%s\" def_svalue=%s item %d offset=%u\n",
                       job->name(), job_items[i].name, *def_svalue, i, offset);
                  svalue = (char **)((char *)job + offset);
                  if (*svalue) {
                     Pmsg1(000, _("Hey something is wrong. p=0x%lu\n"), *svalue);
                  }
                  *svalue = bstrdup(*def_svalue);
                  set_bit(i, job->hdr.item_present);
               /*
                * Handle resources
                */
               } else if (job_items[i].handler == store_res) {
                  def_svalue = (char **)((char *)(job->jobdefs) + offset);
                  Dmsg4(400, "Job \"%s\", field \"%s\" item %d offset=%u\n",
                       job->name(), job_items[i].name, i, offset);
                  svalue = (char **)((char *)job + offset);
                  if (*svalue) {
                     Pmsg1(000, _("Hey something is wrong. p=0x%lu\n"), *svalue);
                  }
                  *svalue = *def_svalue;
                  set_bit(i, job->hdr.item_present);
               /*
                * Handle alist resources
                */
               } else if (job_items[i].handler == store_alist_str) {
                  char *elt;

                  def_avalue = (alist **)((char *)(job->jobdefs) + offset);
                  avalue = (alist **)((char *)job + offset);
                  
                  *avalue = New(alist(10, owned_by_alist));

                  foreach_alist(elt, (*def_avalue)) {
                     (*avalue)->append(bstrdup(elt));
                  }
                  set_bit(i, job->hdr.item_present);
                  
               } else if (job_items[i].handler == store_alist_res) {
                  void *elt;

                  def_avalue = (alist **)((char *)(job->jobdefs) + offset);
                  avalue = (alist **)((char *)job + offset);
                  
                  *avalue = New(alist(10, not_owned_by_alist));

                  foreach_alist(elt, (*def_avalue)) {
                     (*avalue)->append(elt);
                  }
                  set_bit(i, job->hdr.item_present);
               /*
                * Handle integer fields
                *    Note, our store_bit does not handle bitmaped fields
                */
               } else if (job_items[i].handler == store_bit     ||
                          job_items[i].handler == store_pint32  ||
                          job_items[i].handler == store_jobtype ||
                          job_items[i].handler == store_level   ||
                          job_items[i].handler == store_int32   ||
                          job_items[i].handler == store_size32  ||
                          job_items[i].handler == store_migtype ||
                          job_items[i].handler == store_replace) {
                  def_ivalue = (uint32_t *)((char *)(job->jobdefs) + offset);
                  Dmsg5(400, "Job \"%s\", field \"%s\" def_ivalue=%d item %d offset=%u\n",
                       job->name(), job_items[i].name, *def_ivalue, i, offset);
                  ivalue = (uint32_t *)((char *)job + offset);
                  *ivalue = *def_ivalue;
                  set_bit(i, job->hdr.item_present);
               /*
                * Handle 64 bit integer fields
                */
               } else if (job_items[i].handler == store_time   ||
                          job_items[i].handler == store_size64 ||
                          job_items[i].handler == store_int64) {
                  def_lvalue = (int64_t *)((char *)(job->jobdefs) + offset);
                  Dmsg5(400, "Job \"%s\", field \"%s\" def_lvalue=%" lld " item %d offset=%u\n",
                       job->name(), job_items[i].name, *def_lvalue, i, offset);
                  lvalue = (int64_t *)((char *)job + offset);
                  *lvalue = *def_lvalue;
                  set_bit(i, job->hdr.item_present);
               /*
                * Handle bool fields
                */
               } else if (job_items[i].handler == store_bool) {
                  def_bvalue = (bool *)((char *)(job->jobdefs) + offset);
                  Dmsg5(400, "Job \"%s\", field \"%s\" def_bvalue=%d item %d offset=%u\n",
                       job->name(), job_items[i].name, *def_bvalue, i, offset);
                  bvalue = (bool *)((char *)job + offset);
                  *bvalue = *def_bvalue;
                  set_bit(i, job->hdr.item_present);

               } else {
                  Dmsg1(10, "Handler missing for job_items[%d]\n", i);
                  ASSERTD(0, "JobDefs -> Job handler missing\n");
               }
            }
         }
      }
      /*
       * Ensure that all required items are present
       */
      for (i=0; job_items[i].name; i++) {
         if (job_items[i].flags & ITEM_REQUIRED) {
               if (!bit_is_set(i, job->hdr.item_present)) {
                  Jmsg(NULL, M_ERROR_TERM, 0, _("\"%s\" directive in Job \"%s\" resource is required, but not found.\n"),
                    job_items[i].name, job->name());
                  OK = false;
                }
         }
         /* If this triggers, take a look at lib/parse_conf.h */
         if (i >= MAX_RES_ITEMS) {
            Emsg0(M_ERROR_TERM, 0, _("Too many items in Job resource\n"));
         }
      }
      if (!job->storage && !job->pool->storage) {
         Jmsg(NULL, M_FATAL, 0, _("No storage specified in Job \"%s\" nor in Pool.\n"),
            job->name());
         OK = false;
      }
   } /* End loop over Job res */

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
