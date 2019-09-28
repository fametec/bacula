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

#ifndef CMD_PARSER_H
#define CMD_PARSER_H

extern int parse_args(POOLMEM *cmd, POOLMEM **args, int *argc,
                      char **argk, char **argv, int max_args);
extern int parse_args_only(POOLMEM *cmd, POOLMEM **args, int *argc,
                           char **argk, char **argv, int max_args);

class cmd_parser {
public:
   POOLMEM *args;
   POOLMEM *cmd;                /* plugin command line */
   POOLMEM *org;                /* original command line */

   char **argk;                  /* Argument keywords */
   char **argv;                  /* Argument values */
   int argc;                    /* Number of arguments */
   int max_cmd;                 /* Max number of arguments */
   bool handle_plugin_name;     /* Search for : */

   cmd_parser(bool handle_plugin_name=true)
      : handle_plugin_name(handle_plugin_name)
   {
      org = get_pool_memory(PM_FNAME);
      args = get_pool_memory(PM_FNAME);
      cmd = get_pool_memory(PM_FNAME);
      *args = *org  = *cmd = 0;
      argc = 0;
      max_cmd = MAX_CMD_ARGS;
      argk = argv = NULL;
   };

   virtual ~cmd_parser() {
      free_pool_memory(org);
      free_pool_memory(cmd);
      free_pool_memory(args);
      if (argk) {
         free(argk);
      }
      if (argv) {
         free(argv);
      }
   }

   /*
    * Given a single keyword, find it in the argument list, but
    *   it must have a value
    * Returns: -1 if not found or no value
    *           list index (base 0) on success
    */
   int find_arg_with_value(const char *keyword)
   {
      for (int i=handle_plugin_name?1:0; i<argc; i++) {
         if (strcasecmp(keyword, argk[i]) == 0) {
            if (argv[i]) {
               return i;
            } else {
               return -1;
            }
         }
      }
      return -1;
   }

   /*
    * Given multiple keywords, find the next occurrence (after previous) in the
    *  argument list, but it must have a value
    * Returns: -1 if not found or no value
    *           list index (base 0) on success
    */
   int find_next_arg_with_value(const int previous, ...)
   {
      va_list vaargs;
      for (int i=previous + 1; i<argc; i++) {
         char *arg;
         va_start(vaargs, previous);
         while ((arg = va_arg(vaargs, char *)) != NULL) {
            if (strcasecmp(arg, argk[i]) == 0) {
               if (argv[i]) {
                  /* va_end should match indentation of va_start so we can't just return here */
                  break;
               }
            }
         }
         va_end(vaargs);
         if (arg) {
            return i;
         }
      }
      return -1;
   }

   /*
    * Given a single keyword, find it in the argument list
    * Returns: -1 if not found
    *           list index (base 0) on success
    */
   int find_arg(const char *keyword)
   {
      for (int i=handle_plugin_name?1:0; i<argc; i++) {
         if (strcasecmp(keyword, argk[i]) == 0) {
            return i;
         }
      }
      return -1;
   }

   /*
    * Build args, argc, argk from Plugin Restore|Backup command
    */

   bRC parse_cmd(const char *line)
   {
      char *a;
      int nbequal = 0;
      if (!line || *line == '\0') {
         return bRC_Error;
      }

      /* Same command line that before */
      if (!strcmp(line, org)) {
         return bRC_OK;
      }

      /*
       * line = delta:minsize=10002 param1=xxx
       *             |     backup command
       */
      pm_strcpy(org, line);
      pm_strcpy(cmd, line);

      if (handle_plugin_name) {
         if ((a = strchr(cmd, ':')) != NULL) {
            *a = ' ';              /* replace : by ' ' for command line processing */

         } else if (strchr(cmd, ' ') != NULL) { /* We have "word1 word2" where we expect "word1: word2" */
            return bRC_Error;
         }
      }

      for (char *p = cmd; *p ; p++) {
         if (*p == '=') {
            nbequal++;
         }
      }

      if (argk) {
         free(argk);
      }
      if (argv) {
         free(argv);
      }

      max_cmd = MAX(nbequal, MAX_CMD_ARGS) + 1;

      argk = (char **) malloc(sizeof(char **) * max_cmd);
      argv = (char **) malloc(sizeof(char **) * max_cmd);

      parse_args(cmd, &args, &argc, argk, argv, max_cmd);

      return bRC_OK;
   }

};

/* Special cmd_parser subclass to not look after plugin
 * names when decoding the line
 */
class arg_parser: public cmd_parser
{
public:
   arg_parser(): cmd_parser(false) { };
   virtual ~arg_parser() {};
};


#endif  /* CMD_PARSER_H */
