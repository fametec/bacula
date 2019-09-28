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
 * Handle simple configuration file such as "ini" files.
 * key1 = val     # comment
 * key2 = val     # <type>
 *
 */

#include "bacula.h"
#include "ini.h"

#define bfree_and_null_const(a) do{if(a){free((void *)a); (a)=NULL;}} while(0)
static int dbglevel = 100;

/* We use this structure to associate a key to the function */
struct ini_store {
   const char *key;
   const char *comment;
   INI_ITEM_HANDLER *handler;
};

static struct ini_store funcs[] = {
   {"@INT32@",  "Integer",          ini_store_int32},
   {"@PINT32@", "Integer",          ini_store_pint32},
   {"@PINT64@", "Positive Integer", ini_store_pint64},
   {"@INT64@",  "Integer",          ini_store_int64},
   {"@NAME@",   "Simple String",    ini_store_name},
   {"@STR@",    "String",           ini_store_str},
   {"@BOOL@",   "on/off",           ini_store_bool},
   {"@ALIST@",  "String list",      ini_store_alist_str},
   {"@DATE@",   "Date",             ini_store_date},
/* TODO: Add protocol for the FD @ASKFD@ */
   {NULL,       NULL,               NULL}
};

/*
 * Get handler code from handler @
 */
const char *ini_get_store_code(INI_ITEM_HANDLER *handler)
{
   for (int i = 0; funcs[i].key ; i++) {
      if (funcs[i].handler == handler) {
         return funcs[i].key;
      }
   }
   return NULL;
}

/*
 * Get handler function from handler name
 */
INI_ITEM_HANDLER *ini_get_store_handler(const char *key)
{
   for (int i = 0; funcs[i].key ; i++) {
      if (!strcmp(funcs[i].key, key)) {
         return funcs[i].handler;
      }
   }
   return NULL;
}

/*
 * Format a scanner error message
 */
static void s_err(const char *file, int line, LEX *lc, const char *msg, ...)
{
   ConfigFile *ini = (ConfigFile *)(lc->caller_ctx);
   va_list arg_ptr;
   char buf[MAXSTRING];

   va_start(arg_ptr, msg);
   bvsnprintf(buf, sizeof(buf), msg, arg_ptr);
   va_end(arg_ptr);

#ifdef TEST_PROGRAM
   printf("ERROR: Config file error: %s\n"
          "            : Line %d, col %d of file %s\n%s\n",
      buf, lc->line_no, lc->col_no, lc->fname, lc->line);
#endif

   if (ini->jcr) {              /* called from core */
      Jmsg(ini->jcr, M_ERROR, 0, _("Config file error: %s\n"
                              "            : Line %d, col %d of file %s\n%s\n"),
         buf, lc->line_no, lc->col_no, lc->fname, lc->line);

//   } else if (ini->ctx) {       /* called from plugin */
//      ini->bfuncs->JobMessage(ini->ctx, __FILE__, __LINE__, M_FATAL, 0,
//                    _("Config file error: %s\n"
//                      "            : Line %d, col %d of file %s\n%s\n"),
//                            buf, lc->line_no, lc->col_no, lc->fname, lc->line);
//
   } else {                     /* called from ??? */
      e_msg(file, line, M_ERROR, 0,
            _("Config file error: %s\n"
              "            : Line %d, col %d of file %s\n%s\n"),
            buf, lc->line_no, lc->col_no, lc->fname, lc->line);
   }
}

/* Reset free items */
void ConfigFile::clear_items()
{
   if (!items) {
      return;
   }

   for (int i=0; items[i].name; i++) {
      if (items[i].found) {
         /* special members require delete or free */
         if (items[i].handler == ini_store_str) {
            free(items[i].val.strval);
            items[i].val.strval = NULL;

         } else if (items[i].handler == ini_store_alist_str) {
            delete items[i].val.alistval;
            items[i].val.alistval = NULL;
         }
         items[i].found = false;
      }
   }
}

void ConfigFile::free_items()
{
   if (items_allocated) {
      for (int i=0; items[i].name; i++) {
         bfree_and_null_const(items[i].name);
         bfree_and_null_const(items[i].comment);
         bfree_and_null_const(items[i].default_value);
      }
   }
   if (items) {
      free(items);
   }
   items = NULL;
   items_allocated = false;
}

/* Get a particular item from the items list */
int ConfigFile::get_item(const char *name)
{
   if (!items) {
      return -1;
   }

   for (int i=0; i < MAX_INI_ITEMS && items[i].name; i++) {
      if (strcasecmp(name, items[i].name) == 0) {
         return i;
      }
   }
   return -1;
}

/* Dump a buffer to a file in the working directory
 * Needed to unserialise() a config
 */
bool ConfigFile::dump_string(const char *buf, int32_t len)
{
   FILE *fp;
   bool ret=false;

   if (!out_fname) {
      out_fname = get_pool_memory(PM_FNAME);
      make_unique_filename(&out_fname, (int)(intptr_t)this, (char*)"configfile");
   }

   fp = bfopen(out_fname, "wb");
   if (!fp) {
      return ret;
   }

   if (fwrite(buf, len, 1, fp) == 1) {
      ret = true;
   }

   fclose(fp);
   return ret;
}

/* Dump the item table format to a text file (used by plugin) */
bool ConfigFile::serialize(const char *fname)
{
   FILE *fp;
   POOLMEM *tmp;
   int32_t len;
   bool ret = false;

   if (!items) {
      return ret;
   }

   fp = bfopen(fname, "w");
   if (!fp) {
      return ret;
   }

   tmp = get_pool_memory(PM_MESSAGE);
   len = serialize(&tmp);
   if (fwrite(tmp, len, 1, fp) == 1) {
      ret = true;
   }
   free_pool_memory(tmp);

   fclose(fp);
   return ret;
}

/* Dump the item table format to a text file (used by plugin) */
int ConfigFile::serialize(POOLMEM **buf)
{
   int len;
   POOLMEM *tmp, *tmp2;
   if (!items) {
      **buf = 0;
      return 0;
   }

   len = Mmsg(buf, "# Plugin configuration file\n# Version %d\n", version);

   tmp = get_pool_memory(PM_MESSAGE);
   tmp2 = get_pool_memory(PM_MESSAGE);

   for (int i=0; items[i].name ; i++) {
      if (items[i].comment) {
         Mmsg(tmp, "OptPrompt=%s\n", quote_string(tmp2, items[i].comment));
         pm_strcat(buf, tmp);
      }
      if (items[i].default_value) {
         Mmsg(tmp, "OptDefault=%s\n",
              quote_string(tmp2, items[i].default_value));
         pm_strcat(buf, tmp);
      }
      if (items[i].required) {
         Mmsg(tmp, "OptRequired=yes\n");
         pm_strcat(buf, tmp);
      }

      Mmsg(tmp, "%s=%s\n\n",
           items[i].name, ini_get_store_code(items[i].handler));

      /* variable = @INT64@ */
      len = pm_strcat(buf, tmp);
   }
   free_pool_memory(tmp);
   free_pool_memory(tmp2);

   return len ;
}

/* Dump the item table content to a text file (used by director) */
int ConfigFile::dump_results(POOLMEM **buf)
{
   int len;
   POOLMEM *tmp, *tmp2;
   if (!items) {
      **buf = 0;
      return 0;
   }
   len = Mmsg(buf, "# Plugin configuration file\n# Version %d\n", version);

   tmp = get_pool_memory(PM_MESSAGE);
   tmp2 = get_pool_memory(PM_MESSAGE);

   for (int i=0; items[i].name ; i++) {
      bool process= items[i].found;
      if (items[i].found) {
         items[i].handler(NULL, this, &items[i]);
      }
      if (!items[i].found && items[i].required && items[i].default_value) {
         pm_strcpy(this->edit, items[i].default_value);
         process = true;
      }
      if (process) {
         if (items[i].comment && *items[i].comment) {
            Mmsg(tmp, "# %s\n", items[i].comment);
            pm_strcat(buf, tmp);
         }
         if (items[i].handler == ini_store_str  ||
             items[i].handler == ini_store_name ||
             items[i].handler == ini_store_date)
         {
            Mmsg(tmp, "%s=%s\n\n",
                 items[i].name,
                 quote_string(tmp2, this->edit));

         } else {
            Mmsg(tmp, "%s=%s\n\n", items[i].name, this->edit);
         }
         len = pm_strcat(buf, tmp);
      }
   }
   free_pool_memory(tmp);
   free_pool_memory(tmp2);

   return len ;
}

bool ConfigFile::parse()
{
   int token, i;
   bool ret = false;
   bool found;

   lc->options |= LOPT_NO_EXTERN;
   lc->caller_ctx = (void *)this;

   while ((token=lex_get_token(lc, T_ALL)) != T_EOF) {
      if (token == T_EOL) {
         continue;
      }
      found = false;
      for (i=0; items[i].name; i++) {
         if (strcasecmp(items[i].name, lc->str) == 0) {
            if ((token = lex_get_token(lc, T_EQUALS)) == T_ERROR) {
               Dmsg2(dbglevel, "in T_IDENT got token=%s str=%s\n", lex_tok_to_str(token), lc->str);
               break;
            }
            Dmsg2(dbglevel, "parse got token=%s str=%s\n", lex_tok_to_str(token), lc->str);
            Dmsg1(dbglevel, "calling handler for %s\n", items[i].name);
            /* Call item handler */
            ret = items[i].found = items[i].handler(lc, this, &items[i]);
            found = true;
            break;
         }
      }
      if (!found) {
         Dmsg1(dbglevel, "Unfound keyword=%s\n", lc->str);
         scan_err1(lc, "Keyword %s not found", lc->str);
         /* We can raise an error here */
         break;
      } else {
         Dmsg1(dbglevel, "Found keyword=%s\n", items[i].name);
      }
      if (!ret) {
         Dmsg1(dbglevel, "Error getting value for keyword=%s\n", items[i].name);
         break;
      }
      Dmsg0(dbglevel, "Continue with while(token) loop\n");
   }

   for (i=0; items[i].name; i++) {
      if (items[i].required && !items[i].found) {
         scan_err1(lc, "%s required but not found", items[i].name);
         ret = false;
      }
   }

   lc = lex_close_file(lc);
   return ret;
}

/* Parse a config file used by Plugin/Director */
bool ConfigFile::parse(const char *fname)
{
   if (!items) {
      return false;
   }

   if ((lc = lex_open_file(lc, fname, s_err)) == NULL) {
      berrno be;
      Emsg2(M_ERROR, 0, _("Cannot open config file %s: %s\n"),
            fname, be.bstrerror());
      return false;
   }
   return parse();          /* Parse file */
}

/* Parse a config buffer used by Plugin/Director */
bool ConfigFile::parse_buf(const char *buffer)
{
   if (!items) {
      return false;
   }

   if ((lc = lex_open_buf(lc, buffer, s_err)) == NULL) {
      Emsg0(M_ERROR, 0, _("Cannot open lex\n"));
      return false;
   }
   return parse();         /* Parse memory buffer */
}


/* Analyse the content of a ini file to build the item list
 * It uses special syntax for datatype. Used by Director on Restore object
 *
 * OptPrompt = "Variable1"
 * OptRequired
 * OptDefault = 100
 * Variable1 = @PINT32@
 * ...
 */
bool ConfigFile::unserialize(const char *fname)
{
   int token, i, nb = 0;
   bool ret=false;
   const char **assign;

   /* At this time, we allow only 32 different items */
   int s = MAX_INI_ITEMS * sizeof (struct ini_items);

   items = (struct ini_items *) malloc (s);
   memset(items, 0, s);
   items_allocated = true;

   /* parse the file and generate the items structure on the fly */
   if ((lc = lex_open_file(lc, fname, s_err)) == NULL) {
      berrno be;
      Emsg2(M_ERROR, 0, _("Cannot open config file %s: %s\n"),
            fname, be.bstrerror());
      return false;
   }
   lc->options |= LOPT_NO_EXTERN;
   lc->caller_ctx = (void *)this;

   while ((token=lex_get_token(lc, T_ALL)) != T_EOF) {
      Dmsg1(dbglevel, "parse got token=%s\n", lex_tok_to_str(token));

      if (token == T_EOL) {
         continue;
      }

      ret = false;
      assign = NULL;

      if (nb >= MAX_INI_ITEMS) {
         break;
      }

      if (strcasecmp("optprompt", lc->str) == 0) {
         assign = &(items[nb].comment);

      } else if (strcasecmp("optdefault", lc->str) == 0) {
         assign = &(items[nb].default_value);

      } else if (strcasecmp("optrequired", lc->str) == 0) {
         items[nb].required = true;               /* Don't use argument */
         scan_to_eol(lc);
         continue;

      } else {
         items[nb].name = bstrdup(lc->str);
      }

      token = lex_get_token(lc, T_ALL);
      Dmsg1(dbglevel, "in T_IDENT got token=%s\n", lex_tok_to_str(token));

      if (token != T_EQUALS) {
         scan_err1(lc, "expected an equals, got: %s", lc->str);
         break;
      }

      /* We may allow blank variable */
      if (lex_get_token(lc, T_STRING) == T_ERROR) {
         break;
      }

      if (assign) {
         *assign = bstrdup(lc->str);

      } else {
         if ((items[nb].handler = ini_get_store_handler(lc->str)) == NULL) {
            scan_err1(lc, "expected a data type, got: %s", lc->str);
            break;
         }
         nb++;
      }
      scan_to_eol(lc);
      ret = true;
   }

   if (!ret) {
      for (i = 0; i < nb ; i++) {
         bfree_and_null_const(items[i].name);
         bfree_and_null_const(items[i].comment);
         bfree_and_null_const(items[i].default_value);
         items[i].handler = NULL;
         items[i].required = false;
      }
   }

   lc = lex_close_file(lc);
   return ret;
}

/* ----------------------------------------------------------------
 * Handle data type. Import/Export
 * ----------------------------------------------------------------
 */
bool ini_store_str(LEX *lc, ConfigFile *inifile, ini_items *item)
{
   if (!lc) {
      Mmsg(inifile->edit, "%s", item->val.strval);
      return true;
   }
   if (lex_get_token(lc, T_STRING) == T_ERROR) {
      return false;
   }
   /* If already allocated, free first */
   if (item->found && item->val.strval) {
      free(item->val.strval);
   }
   item->val.strval = bstrdup(lc->str);
   scan_to_eol(lc);
   return true;
}

bool ini_store_name(LEX *lc, ConfigFile *inifile, ini_items *item)
{
   if (!lc) {
      Mmsg(inifile->edit, "%s", item->val.nameval);
      return true;
   }
   if (lex_get_token(lc, T_NAME) == T_ERROR) {
      Dmsg0(dbglevel, "Want token=T_NAME got T_ERROR\n");
      return false;
   }
   Dmsg1(dbglevel, "ini_store_name: %s\n", lc->str);
   strncpy(item->val.nameval, lc->str, sizeof(item->val.nameval));
   scan_to_eol(lc);
   return true;
}

bool ini_store_alist_str(LEX *lc, ConfigFile *inifile, ini_items *item)
{
   alist *list = item->val.alistval;
   if (!lc) {
      /* TODO, write back the alist to edit buffer */
      return true;
   }

   for (;;) {
      if (lex_get_token(lc, T_STRING) == T_ERROR) {
         return false;
      }

      if (list == NULL) {
         list = New(alist(10, owned_by_alist));
      }
      list->append(bstrdup(lc->str));

      if (lc->ch != ',') {         /* if no other item follows */
         if (!lex_check_eol(lc)) {
            /* found garbage at the end of the line */
            return false;
         }
         break;                    /* get out */
      }
      lex_get_token(lc, T_ALL);    /* eat comma */
   }

   item->val.alistval = list;
   scan_to_eol(lc);
   return true;
}

bool ini_store_pint64(LEX *lc, ConfigFile *inifile, ini_items *item)
{
   if (!lc) {
      Mmsg(inifile->edit, "%lld", item->val.int64val);
      return true;
   }
   if (lex_get_token(lc, T_PINT64) == T_ERROR) {
      return false;
   }
   item->val.int64val = lc->pint64_val;
   scan_to_eol(lc);
   return true;
}

bool ini_store_int64(LEX *lc, ConfigFile *inifile, ini_items *item)
{
   if (!lc) {
      Mmsg(inifile->edit, "%lld", item->val.int64val);
      return true;
   }
   if (lex_get_token(lc, T_INT64) == T_ERROR) {
      return false;
   }
   item->val.int64val = lc->int64_val;
   scan_to_eol(lc);
   return true;
}

bool ini_store_pint32(LEX *lc, ConfigFile *inifile, ini_items *item)
{
   if (!lc) {
      Mmsg(inifile->edit, "%d", item->val.int32val);
      return true;
   }
   if (lex_get_token(lc, T_PINT32) == T_ERROR) {
      return false;
   }
   item->val.int32val = lc->pint32_val;
   scan_to_eol(lc);
   return true;
}

bool ini_store_int32(LEX *lc, ConfigFile *inifile, ini_items *item)
{
   if (!lc) {
      Mmsg(inifile->edit, "%d", item->val.int32val);
      return true;
   }
   if (lex_get_token(lc, T_INT32) == T_ERROR) {
      return false;
   }
   item->val.int32val = lc->int32_val;
   scan_to_eol(lc);
   return true;
}

bool ini_store_bool(LEX *lc, ConfigFile *inifile, ini_items *item)
{
   if (!lc) {
      Mmsg(inifile->edit, "%s", item->val.boolval?"yes":"no");
      return true;
   }
   if (lex_get_token(lc, T_NAME) == T_ERROR) {
      return false;
   }
   if (strcasecmp(lc->str, "yes")  == 0 ||
       strcasecmp(lc->str, "true") == 0 ||
       strcasecmp(lc->str, "on")   == 0 ||
       strcasecmp(lc->str, "1")    == 0)
   {
      item->val.boolval = true;

   } else if (strcasecmp(lc->str, "no")    == 0 ||
              strcasecmp(lc->str, "false") == 0 ||
              strcasecmp(lc->str, "off")   == 0 ||
              strcasecmp(lc->str, "0")     == 0)
   {
      item->val.boolval = false;

   } else {
      /* YES and NO must not be translated */
      scan_err2(lc, _("Expect %s, got: %s"), "YES, NO, ON, OFF, 0, 1, TRUE, or FALSE", lc->str);
      return false;
   }
   scan_to_eol(lc);
   return true;
}

bool ini_store_date(LEX *lc, ConfigFile *inifile, ini_items *item)
{
   if (!lc) {
      bstrutime(inifile->edit,sizeof_pool_memory(inifile->edit),item->val.btimeval);
      return true;
   }
   if (lex_get_token(lc, T_STRING) == T_ERROR) {
      return false;
   }
   item->val.btimeval = str_to_utime(lc->str);
   if (item->val.btimeval == 0) {
      return false;
   }
   scan_to_eol(lc);
   return true;
}

#ifndef TEST_PROGRAM
#define TEST_PROGRAM_A
#endif

/* ---------------------------------------------------------------- */
#ifdef TEST_PROGRAM
#include "unittests.h"
/* make ini_test
 * export LD_LIBRARY_PATH=.libs/
 * ./.libs/ini_test
 */

struct ini_items membuf_items[] = {
   /* name          handler           comment             req */
   {"client",      ini_store_name,    "Client name",       0},
   {"serial",      ini_store_int32,   "Serial number",     1},
   {"max_clients", ini_store_int32,   "Max Clients",       0},
   {NULL,          NULL,              NULL,                0}
};

struct ini_items test_items[] = {
   /* name          handler           comment             req */
   {"datastore",   ini_store_name,    "Target Datastore", 0},
   {"newhost",     ini_store_str,     "New Hostname",     1},
   {"int64val",    ini_store_int64,   "Int64",            1},
   {"list",        ini_store_alist_str, "list",           0},
   {"bool",        ini_store_bool,    "Bool",             0},
   {"pint64",      ini_store_pint64,  "pint",             0},
   {"int32",       ini_store_int32,   "int 32bit",        0},
   {"plugin.test", ini_store_str,     "test with .",      0},
   {"adate",       ini_store_date,    "test with date",   0},
   {NULL,          NULL,              NULL,               0}
};

/* In order to link with libbaccfg */
int32_t r_last;
int32_t r_first;
RES_HEAD **res_head;
bool save_resource(RES_HEAD **rhead, int type, RES_ITEM *items, int pass){return false;}
bool save_resource(CONFIG*, int, RES_ITEM*, int) {return false;}
void dump_resource(int type, RES *ares, void sendit(void *sock, const char *fmt, ...), void *sock){}
void free_resource(RES *rres, int type){}
union URES {};
RES_TABLE resources[] = {};
URES res_all;

int main()
{
   Unittests ini_test("ini_test");
   FILE *fp;
   int pos, size;
   ConfigFile *ini = new ConfigFile();
   POOLMEM *buf = get_pool_memory(PM_BSOCK);
   char buffer[2000];

   //debug_level=500;
   Pmsg0(0, "Begin Memory buffer Test\n");
   ok(ini->register_items(membuf_items, sizeof(struct ini_items)), "Check sizeof ini_items");

   if ((fp = bfopen("test.cfg", "w")) == NULL) {
      exit (1);
   }
   fprintf(fp, "client=JohnDoe\n");
   fprintf(fp, "serial=2\n");
   fprintf(fp, "max_clients=3\n");
   fflush(fp);
   fseek(fp, 0, SEEK_END);
   size = ftell(fp);
   fclose(fp);

   if ((fp = bfopen("test.cfg", "rb")) == NULL) {
      printf("Could not open file test.cfg\n");
      exit (1);
   }

   size = fread(buffer, 1, size, fp);
   buffer[size] = 0;
   //printf("size of read buffer is: %d\n", size);
   //printf("====buf=%s\n", buffer);

   ok(ini->parse_buf(buffer), "Test memory read with all members");

   ini->clear_items();
   ini->free_items();
   fclose(fp);

   //debug_level = 0;
   Pmsg0(0, "Begin Original Full Tests\n");
   nok(ini->register_items(test_items, 5), "Check bad sizeof ini_items");
   ok(ini->register_items(test_items, sizeof(struct ini_items)), "Check sizeof ini_items");

   if ((fp = bfopen("test.cfg", "w")) == NULL) {
      exit (1);
   }
   fprintf(fp, "# this is a comment\ndatastore=datastore1\nnewhost=\"host1\"\n");
   fflush(fp);

   nok(ini->parse("test.cfg"), "Test missing member");
   ini->clear_items();

   fprintf(fp, "int64val=12 # with a comment\n");
   fprintf(fp, "int64val=10 # with a comment\n");
   fprintf(fp, "int32=100\n");
   fprintf(fp, "bool=yes\n");
   fprintf(fp, "plugin.test=parameter\n");
   fprintf(fp, "adate=\"1970-01-02 12:00:00\"\n");

   fflush(fp);

   ok(ini->parse("test.cfg"), "Test with all members");

   ok(ini->items[0].found, "Test presence of char[]");
   ok(!strcmp(ini->items[0].val.nameval, "datastore1"), "Test char[]");
   ok(ini->items[1].found, "Test presence of char*");
   ok(!strcmp(ini->items[1].val.strval, "host1"), "Test char*");
   ok(ini->items[2].found, "Test presence of int");
   ok(ini->items[2].val.int64val == 10, "Test int");
   ok(ini->items[4].val.boolval == true, "Test bool");
   ok(ini->items[6].val.int32val == 100, "Test int 32");
   ok(ini->items[6].val.btimeval != 126000, "Test btime");

   alist *list = ini->items[3].val.alistval;
   nok(ini->items[3].found, "Test presence of alist");

   fprintf(fp, "list=a\nlist=b\nlist=c,d,e\n");
   fflush(fp);

   ini->clear_items();
   ok(ini->parse("test.cfg"), "Test with all members");

   list = ini->items[3].val.alistval;
   ok(ini->items[3].found, "Test presence of alist");
   ok(list != NULL, "Test list member");
   ok(list->size() == 5, "Test list size");

   ok(!strcmp((char *)list->get(0), "a"), "Testing alist[0]");
   ok(!strcmp((char *)list->get(1), "b"), "Testing alist[1]");
   ok(!strcmp((char *)list->get(2), "c"), "Testing alist[2]");

   system("cp -f test.cfg test3.cfg");

   fprintf(fp, "pouet='10, 11, 12'\n");
   fprintf(fp, "pint=-100\n");
   fprintf(fp, "int64val=-100\n"); /* TODO: fix negative numbers */
   fflush(fp);

   ini->clear_items();
   ok(ini->parse("test.cfg"), "Test with errors");
   nok(ini->items[5].found, "Test presence of positive int");

   fclose(fp);
   ini->clear_items();
   ini->free_items();

   /* Test  */
   if ((fp = bfopen("test2.cfg", "w")) == NULL) {
      exit (1);
   }
   fprintf(fp,
           "# this is a comment\n"
           "optprompt=\"Datastore Name\"\n"
           "datastore=@NAME@\n"
           "optprompt=\"New Hostname to create\"\n"
           "newhost=@STR@\n"
           "optprompt=\"Some 64 integer\"\n"
           "optrequired=yes\n"
           "int64val=@INT64@\n"
           "list=@ALIST@\n"
           "bool=@BOOL@\n"
           "pint64=@PINT64@\n"
           "pouet=@STR@\n"
           "int32=@INT32@\n"
           "plugin.test=@STR@\n"
           "adate=@DATE@\n"
      );
   fclose(fp);

   ok(ini->unserialize("test2.cfg"), "Test dynamic parse");
   ok(ini->serialize("test4.cfg"), "Try to dump the item table in a file");
   ok(ini->serialize(&buf) > 0, "Try to dump the item table in a buffer");
   ok(ini->parse("test3.cfg"), "Parse test file with dynamic grammar");

   ok((pos = ini->get_item("datastore")) == 0, "Check datastore definition");
   ok(ini->items[pos].found, "Test presence of char[]");
   ok(!strcmp(ini->items[pos].val.nameval, "datastore1"), "Test char[]");
   ok(!strcmp(ini->items[pos].comment, "Datastore Name"), "Check comment");
   ok(ini->items[pos].required == false, "Check required");

   ok((pos = ini->get_item("newhost")) == 1, "Check newhost definition");
   ok(ini->items[pos].found, "Test presence of char*");
   ok(!strcmp(ini->items[pos].val.strval, "host1"), "Test char*");
   ok(ini->items[pos].required == false, "Check required");

   ok((pos = ini->get_item("int64val")) == 2, "Check int64val definition");
   ok(ini->items[pos].found, "Test presence of int");
   ok(ini->items[pos].val.int64val == 10, "Test int");
   ok(ini->items[pos].required == true, "Check required");

   ok((pos = ini->get_item("bool")) == 4, "Check bool definition");
   ok(ini->items[pos].val.boolval == true, "Test bool");

   ok((pos = ini->get_item("adate")) == 9, "Check adate definition");
   ok(ini->items[pos].val.btimeval == 126000, "Test date");

   ok(ini->dump_results(&buf), "Test to dump results");
   printf("<%s>\n", buf);

   ini->clear_items();
   ini->free_items();
   delete(ini);
   free_pool_memory(buf);
   /* clean after tests */
   unlink("test.cfg");
   unlink("test2.cfg");
   unlink("test3.cfg");
   unlink("test4.cfg");

   return report();
}
#endif   /* TEST_PROGRAM */
