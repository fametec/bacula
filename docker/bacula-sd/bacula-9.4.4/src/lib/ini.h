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

#ifndef INI_H
#define INI_H

/*
 * Plugin has a internal C structure that describes the configuration:
 * struct ini_items[]
 *
 * The ConfigFile object can generate a text file that describes the C
 * structure. This text format is saved as RestoreObject in the catalog.
 *
 *   struct ini_items[]  -> register_items()  -> serialize() -> RestoreObject R1
 *
 * On the Director side, at the restore time, we can analyse this text to
 * get the C structure.
 *
 * RestoreObject R1 -> write to disk -> unserialize() -> struct ini_items[]
 *
 * Once done, we can ask questions to the user at the restore time and fill
 * the C struct with answers. The Director can send back as a RestoreObject
 * the result of the questionnaire.
 *
 * struct ini_items[] -> UAContext -> dump_result() -> FD as RestoreObject R2
 *
 * On the Plugin side, it can get back the C structure and use it.
 * RestoreObject R2 -> parse() -> struct ini_items[]
 */

class ConfigFile;
struct ini_items;

/* Used to store result */
typedef union {
   char    *strval;
   char    nameval[MAX_NAME_LENGTH];
   int64_t int64val;
   int32_t int32val;
   btime_t btimeval;
   alist   *alistval;
   bool    boolval;
} item_value;

/* These functions are used to convert a string to the appropriate value */
typedef
bool (INI_ITEM_HANDLER)(LEX *lc, ConfigFile *inifile,
                        struct ini_items *item);

/* If no items are registred at the scan time, we detect this list from
 * the file itself
 */
struct ini_items {
   const char *name;            /* keyword name */
   INI_ITEM_HANDLER *handler;   /* type accepted */
   const char *comment;         /* comment associated, used in prompt */

   int required;                /* optional required or not */
   const char *default_value;   /* optional default value */

   const char *re_value;        /* optional regexp associated */
   const char *in_values;       /* optional list of values */

   bool found;                  /* if val is set */
   item_value val;              /* val contains the value */
};

/* When reading a ini file, we limit the number of items that we
 *  can create
 */
#define MAX_INI_ITEMS 32

/* Special RestoreObject name used to get user input at restore time */
#define INI_RESTORE_OBJECT_NAME    "RestoreOptions"

/* Can be used to set re_value, in_value, default_value, found and val to 0
 * G++ looks to allow partial declaration, let see with an other compiler
 */
#define ITEMS_DEFAULT    NULL,NULL,NULL,0,{0}

/*
 * Handle simple configuration file such as "ini" files.
 * key1 = val               # comment
 * OptPrompt=comment
 * key2 = val
 *
 * For usage example, see ini.c TEST_PROGRAM
 */

class ConfigFile
{
private:
   LEX *lc;                     /* Lex parser */
   bool items_allocated;

public:
   JCR *jcr;                    /* JCR needed for Jmsg */
   int version;                 /* Internal version check */
   int sizeof_ini_items;        /* Extra check when using dynamic loading */
   bool unlink_temp_file;       /* Unlink temp file when destroying object */
   struct ini_items *items;     /* Structure of the config file */
   POOLMEM *out_fname;          /* Can be used to dump config to disk */
   POOLMEM *edit;               /* Can be used to build result file */
   char *plugin_name;           /* Used to store owner of this ConfigFile */

   ConfigFile() {
      lc = NULL;
      jcr = NULL;
      items = NULL;
      out_fname = NULL;
      plugin_name = NULL;

      version = 1;
      items_allocated = false;
      unlink_temp_file = true;
      edit = get_pool_memory(PM_FNAME);
      edit[0] = 0;
      sizeof_ini_items = sizeof(struct ini_items);
   }

   virtual ~ConfigFile() {
      if (lc) {
         lex_close_file(lc);
      }
      if (edit) {
         free_pool_memory(edit);
      }
      if (out_fname) {
         if (unlink_temp_file) {
            unlink(out_fname);
         }
         free_pool_memory(out_fname);
      }
      if (plugin_name) {
         free(plugin_name);
      }
      clear_items();
      free_items();
   }

   /* Dump a config string to out_fname */
   bool dump_string(const char *buf, int32_t len);

   void set_plugin_name(char *n) {
      if (plugin_name) {
         free(plugin_name);
      }
      plugin_name = bstrdup(n);
   }

   /* JCR needed for Jmsg */
   void set_jcr(JCR *ajcr) {
      jcr = ajcr;
   }

   void set_unlink_temp_file(bool val) {
      unlink_temp_file = val;
   }

   /* Free malloced items such as char* or alist or items */
   void free_items();

   /* Clear items member */
   void clear_items();

   /* Dump the item table to a file (used on plugin side) */
   bool serialize(const char *fname);

   /* Dump the item table format to a buffer (used on plugin side)
    * returns the length of the buffer, -1 if error
    */
   int serialize(POOLMEM **buf);

   /* Dump the item table content to a buffer */
   int dump_results(POOLMEM **buf);

   /* Get item position in items list (useful when dynamic) */
   virtual int get_item(const char *name);

   /* Register config file structure, if size doesn't match */
   bool register_items(struct ini_items *aitems, int size) {
      int i;
      if (sizeof_ini_items == size) {
         for (i = 0; aitems[i].name ; i++);
         items = (struct ini_items*) malloc((i+1) * size); /* NULL terminated */
         memcpy(items, aitems, (i+1) * size);
         items_allocated = false; /* we copy only pointers, don't free them */
         return true;
      }
      return false;
   }

   /* Parse an ini file with a item list previously registred (plugin side) */
   bool parse(const char *filename);

   /* Parse an ini buffer */
   bool parse_buf(const char *buf);

   /* Parse file or buffer already setup */
   bool parse();

   /* Create a item list from a ini file (director side) */
   bool unserialize(const char *filename);

   /* Get Menu for an entry */
   char *get_menu(int index, POOLMEM **dest);

   /* Check if an entry is a part of a menu */
   bool is_in_menu(int index, const char *menu);
};

/*
 * Standard global parsers defined in ini.c
 * When called with lc=NULL, it converts the item value back in inifile->edit
 * buffer.
 */
bool ini_store_str(LEX *lc, ConfigFile *inifile, ini_items *item);
bool ini_store_name(LEX *lc, ConfigFile *inifile, ini_items *item);
bool ini_store_alist_str(LEX *lc, ConfigFile *inifile, ini_items *item);
bool ini_store_pint64(LEX *lc, ConfigFile *inifile, ini_items *item);
bool ini_store_int64(LEX *lc, ConfigFile *inifile, ini_items *item);
bool ini_store_pint32(LEX *lc, ConfigFile *inifile, ini_items *item);
bool ini_store_int32(LEX *lc, ConfigFile *inifile, ini_items *item);
bool ini_store_bool(LEX *lc, ConfigFile *inifile, ini_items *item);
bool ini_store_date(LEX *lc, ConfigFile *inifile, ini_items *item);

/* Get handler code from handler @ */
const char *ini_get_store_code(INI_ITEM_HANDLER *handler);

/* Get handler function from handler name */
INI_ITEM_HANDLER *ini_get_store_handler(const char *);

#endif
