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
 *   Master Configuration routines.
 *
 *   This file contains the common parts of the Bacula
 *   configuration routines.
 *
 *   Note, the configuration file parser consists of three parts
 *
 *   1. The generic lexical scanner in lib/lex.c and lib/lex.h
 *
 *   2. The generic config  scanner in lib/parse_conf.c and
 *      lib/parse_conf.h.
 *      These files contain the parser code, some utility
 *      routines, and the common store routines (name, int,
 *      string, time, int64, size, ...).
 *
 *   3. The daemon specific file, which contains the Resource
 *      definitions as well as any specific store routines
 *      for the resource records.
 *
 *    N.B. This is a two pass parser, so if you malloc() a string
 *         in a "store" routine, you must ensure to do it during
 *         only one of the two passes, or to free it between.
 *         Also, note that the resource record is malloced and
 *         saved in save_resource() during pass 1.  Anything that
 *         you want saved after pass two (e.g. resource pointers)
 *         must explicitly be done in save_resource. Take a look
 *         at the Job resource in src/dird/dird_conf.c to see how
 *         it is done.
 *
 *     Kern Sibbald, January MM
 *
 */


#include "bacula.h"

#if defined(HAVE_WIN32)
#include "shlobj.h"
#else
#define MAX_PATH  1024
#endif

/*
 * Define the Union of all the common resource structure definitions.
 */
union URES {
   MSGS  res_msgs;
   RES hdr;
};

#if defined(_MSC_VER)
// work around visual studio name mangling preventing external linkage since res_all
// is declared as a different type when instantiated.
extern "C" URES res_all;
#else
extern  URES res_all;
#endif

extern brwlock_t res_lock;            /* resource lock */


/* Forward referenced subroutines */
static void scan_types(LEX *lc, MSGS *msg, int dest, char *where, char *cmd);
static const char *get_default_configdir();

/* Common Resource definitions */

/*
 * Message resource directives
 * Note: keep all store_mesgs last in the list as they are all
 *   output in json as a list.
 *   Also, the list store_msgs item must have flags set to ITEM_LAST
 *   so that the list editor (bjson.c) knows when to stop.
 *
 *  name         handler      value       code   flags  default_value
 */
RES_ITEM msgs_items[] = {
   {"Name",        store_name,    ITEM(res_msgs.hdr.name),  0, ITEM_REQUIRED, 0},
   {"Description", store_str,     ITEM(res_msgs.hdr.desc),  0, 0, 0},
   {"MailCommand", store_str,     ITEM(res_msgs.mail_cmd),  0, ITEM_ALLOW_DUPS, 0},
   {"OperatorCommand", store_str, ITEM(res_msgs.operator_cmd), 0, ITEM_ALLOW_DUPS, 0},
   /* See comments above */
   {"Syslog",      store_msgs, ITEM(res_msgs), MD_SYSLOG,   0, 0},
   {"Mail",        store_msgs, ITEM(res_msgs), MD_MAIL,     0, 0},
   {"MailOnError", store_msgs, ITEM(res_msgs), MD_MAIL_ON_ERROR, 0, 0},
   {"MailOnSuccess", store_msgs, ITEM(res_msgs), MD_MAIL_ON_SUCCESS, 0, 0},
   {"File",        store_msgs, ITEM(res_msgs), MD_FILE,     0, 0},
   {"Append",      store_msgs, ITEM(res_msgs), MD_APPEND,   0, 0},
   {"Stdout",      store_msgs, ITEM(res_msgs), MD_STDOUT,   0, 0},
   {"Stderr",      store_msgs, ITEM(res_msgs), MD_STDERR,   0, 0},
   {"Director",    store_msgs, ITEM(res_msgs), MD_DIRECTOR, 0, 0},
   {"Console",     store_msgs, ITEM(res_msgs), MD_CONSOLE,  0, 0},
   {"Operator",    store_msgs, ITEM(res_msgs), MD_OPERATOR, 0, 0},
   {"Catalog",     store_msgs, ITEM(res_msgs), MD_CATALOG,  ITEM_LAST, 0},
   {NULL,          NULL,       {0},       0, 0, 0}
};

/* Various message types */
s_kw msg_types[] = {
   {"Debug",         M_DEBUG},  /* Keep 1st place */
   {"Saved",         M_SAVED},  /* Keep 2nd place */
   {"Abort",         M_ABORT},
   {"Fatal",         M_FATAL},
   {"Error",         M_ERROR},
   {"Warning",       M_WARNING},
   {"Info",          M_INFO},
   {"NotSaved",      M_NOTSAVED},
   {"Skipped",       M_SKIPPED},
   {"Mount",         M_MOUNT},
   {"Terminate",     M_TERM},
   {"Restored",      M_RESTORED},
   {"Security",      M_SECURITY},
   {"Alert",         M_ALERT},
   {"VolMgmt",       M_VOLMGMT},
   {"ErrorTerm",     M_ERROR_TERM},
   {"All",           M_MAX+1},
   {NULL,            0}
};


/*
 * Tape Label types permitted in Pool records
 *
 *   tape label      label code = token
 */
s_kw tapelabels[] = {
   {"Bacula",        B_BACULA_LABEL},
   {"ANSI",          B_ANSI_LABEL},
   {"IBM",           B_IBM_LABEL},
   {NULL,            0}
};


/* Simply print a message */
static void prtmsg(void *sock, const char *fmt, ...)
{
   va_list arg_ptr;

   va_start(arg_ptr, fmt);
   vfprintf(stdout, fmt, arg_ptr);
   va_end(arg_ptr);
}

const char *res_to_str(int rcode)
{
   if (rcode < r_first || rcode > r_last) {
      return _("***UNKNOWN***");
   } else {
      return resources[rcode-r_first].name;
   }
}

/*
 * Create a new res_head pointer to a list of res_heads
 */
void CONFIG::init_res_head(RES_HEAD ***rhead, int32_t rfirst, int32_t rlast)
{
   int num = rlast - rfirst + 1;
   RES *res = NULL;
   RES_HEAD **rh;
   rh = *rhead = (RES_HEAD **)malloc(num * sizeof(RES_HEAD));
   for (int i=0; i<num; i++) {
      rh[i] = (RES_HEAD *)malloc(sizeof(RES_HEAD));
      rh[i]->res_list = New(rblist(res, &res->link));
      rh[i]->first = NULL;
      rh[i]->last = NULL;
   }
}

/*
 * Insert the resource in res_all into the
 *  resource list.
 */
bool CONFIG::insert_res(int rindex, int size)
{
   RES *res;
   rblist *list = m_res_head[rindex]->res_list;
   res = (RES *)malloc(size);
   memcpy(res, m_res_all, size);
   if (list->empty()) {
      list->insert(res, res_compare);
      m_res_head[rindex]->first = res;
      m_res_head[rindex]->last = res;
   } else {
      RES *item, *prev;
      prev = m_res_head[rindex]->last;
      item = (RES *)list->insert(res, res_compare);
      if (item != res) {
         Mmsg(m_errmsg, _("Attempt to define second \"%s\" resource named \"%s\" is not permitted.\n"),
              resources[rindex].name, ((URES *)res)->hdr.name);
         return false;
      }
      prev->res_next = res;
      m_res_head[rindex]->last = res;
   }
   Dmsg2(900, _("Inserted res: %s index=%d\n"), ((URES *)res)->hdr.name, rindex);
   return true;
}

/*
 * Initialize the static structure to zeros, then
 *  apply all the default values.
 */
static void init_resource(CONFIG *config, int type, RES_ITEM *items, int pass)
{
   int i;
   int rindex = type - r_first;
   
   memset(config->m_res_all, 0, config->m_res_all_size);
   res_all.hdr.rcode = type;
   res_all.hdr.refcnt = 1;

   /* Set defaults in each item */
   for (i=0; items[i].name; i++) {
      Dmsg3(900, "Item=%s def=%s defval=%d\n", items[i].name,
            (items[i].flags & ITEM_DEFAULT) ? "yes" : "no",
            items[i].default_value);
      if (items[i].flags & ITEM_DEFAULT && items[i].default_value != 0) {
         if (items[i].handler == store_bit) {
            *(uint32_t *)(items[i].value) |= items[i].code;
         } else if (items[i].handler == store_bool) {
            *(bool *)(items[i].value) = items[i].default_value != 0;
         } else if (items[i].handler == store_pint32 ||
                    items[i].handler == store_int32 ||
                    items[i].handler == store_size32) {
            *(uint32_t *)(items[i].value) = items[i].default_value;
         } else if (items[i].handler == store_int64) {
            *(int64_t *)(items[i].value) = items[i].default_value;
         } else if (items[i].handler == store_size64) {
            *(uint64_t *)(items[i].value) = (uint64_t)items[i].default_value;
         } else if (items[i].handler == store_speed) {
            *(uint64_t *)(items[i].value) = (uint64_t)items[i].default_value;
         } else if (items[i].handler == store_time) {
            *(utime_t *)(items[i].value) = (utime_t)items[i].default_value;
         } else if (pass == 1 && items[i].handler == store_addresses) {
            init_default_addresses((dlist**)items[i].value, items[i].default_value);
         }
      }
      /* If this triggers, take a look at lib/parse_conf.h */
      if (i >= MAX_RES_ITEMS) {
         Emsg1(M_ERROR_TERM, 0, _("Too many directives in \"%s\" resource\n"), resources[rindex].name);
      }
   }
}

/* Initialize a resouce with default values */
bool init_resource(CONFIG *config, uint32_t type, void *res)
{
   RES_ITEM *items;
   for (int i=0; resources[i].name; i++) {
      if (resources[i].rcode == type) {
         items = resources[i].items;
         if (!items) {
            return false;
         }
         init_resource(config, type, items, 1);
         memcpy(res, config->m_res_all, config->m_res_all_size);
         return true;
      }
   }
   return false;
}

/*
 * Dump each resource of type
 */
void dump_each_resource(int type, void sendit(void *sock, const char *fmt, ...), void *sock)
{
   RES *res = NULL;

   if (type < 0) {                    /* no recursion */
      type = -type;
   }
   foreach_res(res, type) {
      dump_resource(-type, res, sendit, sock);
   }
}


/* Store Messages Destination information */
void store_msgs(LEX *lc, RES_ITEM *item, int index, int pass)
{
   int token;
   char *cmd;
   POOLMEM *dest;
   int dest_len;

   Dmsg2(900, "store_msgs pass=%d code=%d\n", pass, item->code);
   if (pass == 1) {
      switch (item->code) {
      case MD_STDOUT:
      case MD_STDERR:
      case MD_SYSLOG:              /* syslog */
      case MD_CONSOLE:
      case MD_CATALOG:
         scan_types(lc, (MSGS *)(item->value), item->code, NULL, NULL);
         break;
      case MD_OPERATOR:            /* send to operator */
      case MD_DIRECTOR:            /* send to Director */
      case MD_MAIL:                /* mail */
      case MD_MAIL_ON_ERROR:       /* mail if Job errors */
      case MD_MAIL_ON_SUCCESS:     /* mail if Job succeeds */
         if (item->code == MD_OPERATOR) {
            cmd = res_all.res_msgs.operator_cmd;
         } else {
            cmd = res_all.res_msgs.mail_cmd;
         }
         dest = get_pool_memory(PM_MESSAGE);
         dest[0] = 0;
         dest_len = 0;
         /* Pick up comma separated list of destinations */
         for ( ;; ) {
            token = lex_get_token(lc, T_NAME);   /* scan destination */
            dest = check_pool_memory_size(dest, dest_len + lc->str_len + 2);
            if (dest[0] != 0) {
               pm_strcat(dest, " ");  /* separate multiple destinations with space */
               dest_len++;
            }
            pm_strcat(dest, lc->str);
            dest_len += lc->str_len;
            Dmsg2(900, "store_msgs newdest=%s: dest=%s:\n", lc->str, NPRT(dest));
            token = lex_get_token(lc, T_SKIP_EOL);
            if (token == T_COMMA) {
               continue;           /* get another destination */
            }
            if (token != T_EQUALS) {
               scan_err1(lc, _("expected an =, got: %s"), lc->str);
               return;
            }
            break;
         }
         Dmsg1(900, "mail_cmd=%s\n", NPRT(cmd));
         scan_types(lc, (MSGS *)(item->value), item->code, dest, cmd);
         free_pool_memory(dest);
         Dmsg0(900, "done with dest codes\n");
         break;

      case MD_FILE:                /* file */
      case MD_APPEND:              /* append */
         dest = get_pool_memory(PM_MESSAGE);
         /* Pick up a single destination */
         token = lex_get_token(lc, T_NAME);   /* scan destination */
         pm_strcpy(dest, lc->str);
         dest_len = lc->str_len;
         token = lex_get_token(lc, T_SKIP_EOL);
         Dmsg1(900, "store_msgs dest=%s:\n", NPRT(dest));
         if (token != T_EQUALS) {
            scan_err1(lc, _("expected an =, got: %s"), lc->str);
            return;
         }
         scan_types(lc, (MSGS *)(item->value), item->code, dest, NULL);
         free_pool_memory(dest);
         Dmsg0(900, "done with dest codes\n");
         break;

      default:
         scan_err1(lc, _("Unknown item code: %d\n"), item->code);
         return;
      }
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
   Dmsg0(900, "Done store_msgs\n");
}

/*
 * Scan for message types and add them to the message
 * destination. The basic job here is to connect message types
 *  (WARNING, ERROR, FATAL, INFO, ...) with an appropriate
 *  destination (MAIL, FILE, OPERATOR, ...)
 */
static void scan_types(LEX *lc, MSGS *msg, int dest_code, char *where, char *cmd)
{
   int i;
   bool found, is_not;
   int msg_type = 0;
   char *str;

   for ( ;; ) {
      lex_get_token(lc, T_NAME);            /* expect at least one type */
      found = false;
      if (lc->str[0] == '!') {
         is_not = true;
         str = &lc->str[1];
      } else {
         is_not = false;
         str = &lc->str[0];
      }
      for (i=0; msg_types[i].name; i++) {
         if (strcasecmp(str, msg_types[i].name) == 0) {
            msg_type = msg_types[i].token;
            found = true;
            break;
         }
      }
      if (!found) {
         scan_err1(lc, _("message type: %s not found"), str);
         return;
      }

      if (msg_type == M_MAX+1) {         /* all? */
         for (i=2; i<=M_MAX; i++) {      /* yes set all types except Debug and Saved */
            add_msg_dest(msg, dest_code, msg_types[i].token, where, cmd);
         }
      } else if (is_not) {
         rem_msg_dest(msg, dest_code, msg_type, where);
      } else {
         add_msg_dest(msg, dest_code, msg_type, where, cmd);
      }
      if (lc->ch != ',') {
         break;
      }
      Dmsg0(900, "call lex_get_token() to eat comma\n");
      lex_get_token(lc, T_ALL);          /* eat comma */
   }
   Dmsg0(900, "Done scan_types()\n");
}


/*
 * This routine is ONLY for resource names
 *  Store a name at specified address.
 */
void store_name(LEX *lc, RES_ITEM *item, int index, int pass)
{
   POOLMEM *msg = get_pool_memory(PM_EMSG);

   lex_get_token(lc, T_NAME);
   if (!is_name_valid(lc->str, &msg)) {
      scan_err1(lc, "%s\n", msg);
      return;
   }
   free_pool_memory(msg);
   /* Store the name both pass 1 and pass 2 */
   if (*(item->value)) {
      scan_err5(lc, _("Attempt to redefine \"%s\" from \"%s\" to \"%s\" referenced on line %d : %s\n"),
         item->name, *(item->value), lc->str, lc->line_no, lc->line);
      return;
   }
   *(item->value) = bstrdup(lc->str);
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}


/*
 * Store a name string at specified address
 * A name string is limited to MAX_RES_NAME_LENGTH
 */
void store_strname(LEX *lc, RES_ITEM *item, int index, int pass)
{
   lex_get_token(lc, T_NAME);
   /* Store the name */
   if (pass == 1) {
      if (*(item->value)) {
         scan_err5(lc, _("Attempt to redefine \"%s\" from \"%s\" to \"%s\" referenced on line %d : %s\n"),
            item->name, *(item->value), lc->str, lc->line_no, lc->line);
         return;
      }
      *(item->value) = bstrdup(lc->str);
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}

/* Store a string at specified address */
void store_str(LEX *lc, RES_ITEM *item, int index, int pass)
{
   lex_get_token(lc, T_STRING);
   if (pass == 1) {
      if (*(item->value) && (item->flags & ITEM_ALLOW_DUPS)) {
         free(*(item->value));
         *(item->value) = NULL;
      }
      if (*(item->value)) {
         scan_err5(lc, _("Attempt to redefine \"%s\" from \"%s\" to \"%s\" referenced on line %d : %s\n"),
            item->name, *(item->value), lc->str, lc->line_no, lc->line);
         return;
      }
      *(item->value) = bstrdup(lc->str);
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}

/*
 * Store a directory name at specified address. Note, we do
 *   shell expansion except if the string begins with a vertical
 *   bar (i.e. it will likely be passed to the shell later).
 */
void store_dir(LEX *lc, RES_ITEM *item, int index, int pass)
{
   lex_get_token(lc, T_STRING);
   if (pass == 1) {
      if (lc->str[0] != '|') {
         do_shell_expansion(lc->str, sizeof_pool_memory(lc->str));
      }
#ifdef STANDARDIZED_DIRECTORY_USAGE
      // TODO ASX we should store all directory without the ending slash to
      // avoid the need of testing its presence
      int len=strlen(lc->str);
      if (len>0 && IsPathSeparator(lc->str[len-1])) {
         lc->str[len-1]='\0';
      }
#endif
      if (*(item->value)) {
         scan_err5(lc, _("Attempt to redefine \"%s\" from \"%s\" to \"%s\" referenced on line %d : %s\n"),
            item->name, *(item->value), lc->str, lc->line_no, lc->line);
         return;
      }
      *(item->value) = bstrdup(lc->str);
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}


/* Store a password specified address in MD5 coding */
void store_password(LEX *lc, RES_ITEM *item, int index, int pass)
{
   unsigned int i, j;
   struct MD5Context md5c;
   unsigned char digest[CRYPTO_DIGEST_MD5_SIZE];
   char sig[100];

   if (lc->options & LOPT_NO_MD5) {
      store_str(lc, item, index, pass);

   } else {
      lex_get_token(lc, T_STRING);
      if (pass == 1) {
         MD5Init(&md5c);
         MD5Update(&md5c, (unsigned char *) (lc->str), lc->str_len);
         MD5Final(digest, &md5c);
         for (i = j = 0; i < sizeof(digest); i++) {
            sprintf(&sig[j], "%02x", digest[i]);
            j += 2;
         }
         if (*(item->value)) {
            scan_err5(lc, _("Attempt to redefine \"%s\" from \"%s\" to \"%s\" referenced on line %d : %s\n"),
               item->name, *(item->value), lc->str, lc->line_no, lc->line);
            return;
         }
         *(item->value) = bstrdup(sig);
      }
      scan_to_eol(lc);
      set_bit(index, res_all.hdr.item_present);
   }
}


/* Store a resource at specified address.
 * If we are in pass 2, do a lookup of the
 * resource.
 */
void store_res(LEX *lc, RES_ITEM *item, int index, int pass)
{
   RES *res;

   lex_get_token(lc, T_NAME);
   if (pass == 2) {
      res = GetResWithName(item->code, lc->str);
      if (res == NULL) {
         scan_err3(lc, _("Could not find config Resource \"%s\" referenced on line %d : %s\n"),
            lc->str, lc->line_no, lc->line);
         return;
      }
      if (*(item->value)) {
         scan_err3(lc, _("Attempt to redefine resource \"%s\" referenced on line %d : %s\n"),
            item->name, lc->line_no, lc->line);
         return;
      }
      *(item->value) = (char *)res;
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}

/*
 * Store a resource pointer in an alist. default_value indicates how many
 *   times this routine can be called -- i.e. how many alists
 *   there are.
 * If we are in pass 2, do a lookup of the
 *   resource.
 */
void store_alist_res(LEX *lc, RES_ITEM *item, int index, int pass)
{
   RES *res;
   int count = item->default_value;
   int i = 0;
   alist *list;

   if (pass == 2) {
      if (count == 0) {               /* always store in item->value */
         i = 0;
         if ((item->value)[i] == NULL) {
            list = New(alist(10, not_owned_by_alist));
         } else {
            list = (alist *)(item->value)[i];
         }
      } else {
         /* Find empty place to store this directive */
         while ((item->value)[i] != NULL && i++ < count) { }
         if (i >= count) {
            scan_err4(lc, _("Too many %s directives. Max. is %d. line %d: %s\n"),
               lc->str, count, lc->line_no, lc->line);
            return;
         }
         list = New(alist(10, not_owned_by_alist));
      }

      for (;;) {
         lex_get_token(lc, T_NAME);   /* scan next item */
         res = GetResWithName(item->code, lc->str);
         if (res == NULL) {
            scan_err3(lc, _("Could not find config Resource \"%s\" referenced on line %d : %s\n"),
               lc->str, lc->line_no, lc->line);
            return;
         }
         Dmsg5(900, "Append %p to alist %p size=%d i=%d %s\n",
               res, list, list->size(), i, item->name);
         list->append(res);
         (item->value)[i] = (char *)list;
         if (lc->ch != ',') {         /* if no other item follows */
            if (!lex_check_eol(lc)) {
               /* found garbage at the end of the line */
               scan_err3(lc, _("Found unexpected characters resource list in Directive \"%s\" at the end of line %d : %s\n"),
                  item->name, lc->line_no, lc->line);
            }
            break;                    /* get out */
         }
         lex_get_token(lc, T_ALL);    /* eat comma */
      }
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}


/*
 * Store a string in an alist.
 */
void store_alist_str(LEX *lc, RES_ITEM *item, int index, int pass)
{
   alist *list;

   if (pass == 2) {
      if (*(item->value) == NULL) {
         list = New(alist(10, owned_by_alist));
         *(item->value) = (char *)list;
      } else {
         list = (alist *)(*(item->value));
      }
      for (;;) {
         lex_get_token(lc, T_STRING);   /* scan next item */
         Dmsg4(900, "Append %s to alist 0x%p size=%d %s\n",
            lc->str, list, list->size(), item->name);
         list->append(bstrdup(lc->str));
         if (lc->ch != ',') {         /* if no other item follows */
            if (!lex_check_eol(lc)) {
               /* found garbage at the end of the line */
               scan_err3(lc, _("Found unexpected characters in resource list in Directive \"%s\" at the end of line %d : %s\n"),
                  item->name, lc->line_no, lc->line);
            }
            break;                    /* get out */
         }
         lex_get_token(lc, T_ALL);    /* eat comma */
      }
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}



/*
 * Store default values for Resource from xxxDefs
 * If we are in pass 2, do a lookup of the
 * resource and store everything not explicitly set
 * in main resource.
 *
 * Note, here item points to the main resource (e.g. Job, not
 *  the jobdefs, which we look up).
 */
void store_defs(LEX *lc, RES_ITEM *item, int index, int pass)
{
   RES *res;

   lex_get_token(lc, T_NAME);
   if (pass == 2) {
     Dmsg2(900, "Code=%d name=%s\n", item->code, lc->str);
     res = GetResWithName(item->code, lc->str);
     if (res == NULL) {
        scan_err3(lc, _("Missing config Resource \"%s\" referenced on line %d : %s\n"),
           lc->str, lc->line_no, lc->line);
        return;
     }
   }
   scan_to_eol(lc);
}



/* Store an integer at specified address */
void store_int32(LEX *lc, RES_ITEM *item, int index, int pass)
{
   lex_get_token(lc, T_INT32);
   *(uint32_t *)(item->value) = lc->int32_val;
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}

/* Store a positive integer at specified address */
void store_pint32(LEX *lc, RES_ITEM *item, int index, int pass)
{
   lex_get_token(lc, T_PINT32);
   *(uint32_t *)(item->value) = lc->pint32_val;
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}


/* Store an 64 bit integer at specified address */
void store_int64(LEX *lc, RES_ITEM *item, int index, int pass)
{
   lex_get_token(lc, T_INT64);
   *(int64_t *)(item->value) = lc->int64_val;
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}

enum store_unit_type {
   STORE_SIZE,
   STORE_SPEED
} ;

/*
 * This routine stores either a 32 or a 64 bit value (size32)
 *  and either a size (in bytes) or a speed (bytes per second).
 */
static void store_int_unit(LEX *lc, RES_ITEM *item, int index, int pass,
                           bool size32, enum store_unit_type type)
{
   int token;
   uint64_t uvalue;
   char bsize[500];

   Dmsg0(900, "Enter store_unit\n");
   token = lex_get_token(lc, T_SKIP_EOL);
   errno = 0;
   switch (token) {
   case T_NUMBER:
   case T_IDENTIFIER:
   case T_UNQUOTED_STRING:
      bstrncpy(bsize, lc->str, sizeof(bsize));  /* save first part */
      /* if terminated by space, scan and get modifier */
      while (lc->ch == ' ') {
         token = lex_get_token(lc, T_ALL);
         switch (token) {
         case T_NUMBER:
         case T_IDENTIFIER:
         case T_UNQUOTED_STRING:
            bstrncat(bsize, lc->str, sizeof(bsize));
            break;
         }
      }
      if (type == STORE_SIZE) {
         if (!size_to_uint64(bsize, strlen(bsize), &uvalue)) {
            scan_err1(lc, _("expected a size number, got: %s"), lc->str);
            return;
         }
      } else {
         if (!speed_to_uint64(bsize, strlen(bsize), &uvalue)) {
            scan_err1(lc, _("expected a speed number, got: %s"), lc->str);
            return;
         }
      }
      if (size32) {
         *(uint32_t *)(item->value) = (uint32_t)uvalue;
      } else {
         *(uint64_t *)(item->value) = uvalue;
      }
      break;
   default:
      scan_err2(lc, _("expected a %s, got: %s"),
                (type == STORE_SIZE)?_("size"):_("speed"), lc->str);
      return;
   }
   if (token != T_EOL) {
      scan_to_eol(lc);
   }
   set_bit(index, res_all.hdr.item_present);
   Dmsg0(900, "Leave store_unit\n");
}

/* Store a size in bytes */
void store_size32(LEX *lc, RES_ITEM *item, int index, int pass)
{
   store_int_unit(lc, item, index, pass, true /* 32 bit */, STORE_SIZE);
}

/* Store a size in bytes */
void store_size64(LEX *lc, RES_ITEM *item, int index, int pass)
{
   store_int_unit(lc, item, index, pass, false /* not 32 bit */, STORE_SIZE);
}

/* Store a speed in bytes/s */
void store_speed(LEX *lc, RES_ITEM *item, int index, int pass)
{
   store_int_unit(lc, item, index, pass, false /* 64 bit */, STORE_SPEED);
}

/* Store a time period in seconds */
void store_time(LEX *lc, RES_ITEM *item, int index, int pass)
{
   int token;
   utime_t utime;
   char period[500];

   token = lex_get_token(lc, T_SKIP_EOL);
   errno = 0;
   switch (token) {
   case T_NUMBER:
   case T_IDENTIFIER:
   case T_UNQUOTED_STRING:
      bstrncpy(period, lc->str, sizeof(period));  /* get first part */
      /* if terminated by space, scan and get modifier */
      while (lc->ch == ' ') {
         token = lex_get_token(lc, T_ALL);
         switch (token) {
         case T_NUMBER:
         case T_IDENTIFIER:
         case T_UNQUOTED_STRING:
            bstrncat(period, lc->str, sizeof(period));
            break;
         }
      }
      if (!duration_to_utime(period, &utime)) {
         scan_err1(lc, _("expected a time period, got: %s"), period);
         return;
      }
      *(utime_t *)(item->value) = utime;
      break;
   default:
      scan_err1(lc, _("expected a time period, got: %s"), lc->str);
      return;
   }
   if (token != T_EOL) {
      scan_to_eol(lc);
   }
   set_bit(index, res_all.hdr.item_present);
}


/* Store a yes/no in a bit field */
void store_bit(LEX *lc, RES_ITEM *item, int index, int pass)
{
   lex_get_token(lc, T_NAME);
   if (strcasecmp(lc->str, "yes") == 0 || strcasecmp(lc->str, "true") == 0) {
      *(uint32_t *)(item->value) |= item->code;
   } else if (strcasecmp(lc->str, "no") == 0 || strcasecmp(lc->str, "false") == 0) {
      *(uint32_t *)(item->value) &= ~(item->code);
   } else {
      scan_err2(lc, _("Expect %s, got: %s"), "YES, NO, TRUE, or FALSE", lc->str); /* YES and NO must not be translated */
      return;
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}

/* Store a bool in a bit field */
void store_bool(LEX *lc, RES_ITEM *item, int index, int pass)
{
   lex_get_token(lc, T_NAME);
   if (strcasecmp(lc->str, "yes") == 0 || strcasecmp(lc->str, "true") == 0) {
      *(bool *)(item->value) = true;
   } else if (strcasecmp(lc->str, "no") == 0 || strcasecmp(lc->str, "false") == 0) {
      *(bool *)(item->value) = false;
   } else {
      scan_err2(lc, _("Expect %s, got: %s"), "YES, NO, TRUE, or FALSE", lc->str); /* YES and NO must not be translated */
      return;
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}


/*
 * Store Tape Label Type (Bacula, ANSI, IBM)
 *
 */
void store_label(LEX *lc, RES_ITEM *item, int index, int pass)
{
   int i;

   lex_get_token(lc, T_NAME);
   /* Store the label pass 2 so that type is defined */
   for (i=0; tapelabels[i].name; i++) {
      if (strcasecmp(lc->str, tapelabels[i].name) == 0) {
         *(uint32_t *)(item->value) = tapelabels[i].token;
         i = 0;
         break;
      }
   }
   if (i != 0) {
      scan_err1(lc, _("Expected a Tape Label keyword, got: %s"), lc->str);
      return;
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}


/* Parser state */
enum parse_state {
   p_none,
   p_resource
};

void CONFIG::init(
   const char *cf,
   LEX_ERROR_HANDLER *scan_error,
   int32_t err_type,
   void *vres_all,
   int32_t res_all_size,
   int32_t r_first,
   int32_t r_last,
   RES_TABLE *resources,
   RES_HEAD ***res_head)
{
   m_cf = cf;
   m_scan_error = scan_error;
   m_err_type = err_type;
   m_res_all = vres_all;
   m_res_all_size = res_all_size;
   m_r_first = r_first;
   m_r_last = r_last;
   m_resources = resources;
   init_res_head(res_head, r_first, r_last);
   m_res_head = *res_head;
}

/*********************************************************************
 *
 * Parse configuration file
 *
 * Return 0 if reading failed, 1 otherwise
 *  Note, the default behavior unless you have set an alternate
 *  scan_error handler is to die on an error.
 */
bool CONFIG::parse_config()
{
   LEX *lc = NULL;
   int token, i, pass;
   int res_type = 0;
   enum parse_state state = p_none;
   RES_ITEM *items = NULL;
   int level = 0;
   static bool first = true;
   int errstat;
   const char *cf = m_cf;
   LEX_ERROR_HANDLER *scan_error = m_scan_error;
   int err_type = m_err_type;
   //HPKT hpkt;

   if (first && (errstat=rwl_init(&res_lock)) != 0) {
      berrno be;
      Jmsg1(NULL, M_ABORT, 0, _("Unable to initialize resource lock. ERR=%s\n"),
            be.bstrerror(errstat));
   }
   first = false;

   char *full_path = (char *)alloca(MAX_PATH + 1);

   if (!find_config_file(cf, full_path, MAX_PATH +1)) {
      Jmsg0(NULL, M_ABORT, 0, _("Config filename too long.\n"));
   }
   cf = full_path;

   /* Make two passes. The first builds the name symbol table,
    * and the second picks up the items.
    */
   Dmsg0(900, "Enter parse_config()\n");
   for (pass=1; pass <= 2; pass++) {
      Dmsg1(900, "parse_config pass %d\n", pass);
      if ((lc = lex_open_file(lc, cf, scan_error)) == NULL) {
         berrno be;
         /* We must create a lex packet to print the error */
         lc = (LEX *)malloc(sizeof(LEX));
         memset(lc, 0, sizeof(LEX));
         lc->str = get_memory(5000);
         if (scan_error) {
            lc->scan_error = scan_error;
         } else {
            lex_set_default_error_handler(lc);
         }
         lex_set_error_handler_error_type(lc, err_type) ;
         pm_strcpy(lc->str, cf);
         lc->fname = lc->str;
         scan_err2(lc, _("Cannot open config file \"%s\": %s\n"),
            lc->str, be.bstrerror());
         free_pool_memory(lc->str);
         free(lc);
         return 0;
      }
      if (!m_encode_pass) {
         lex_store_clear_passwords(lc);
      }
      lex_set_error_handler_error_type(lc, err_type) ;
      while ((token=lex_get_token(lc, T_ALL)) != T_EOF) {
         Dmsg3(900, "parse state=%d pass=%d got token=%s\n", state, pass,
              lex_tok_to_str(token));
         switch (state) {
         case p_none:
            if (token == T_EOL) {
               break;
            } else if (token == T_UTF8_BOM) {
               /* We can assume the file is UTF-8 as we have seen a UTF-8 BOM */
               break;
            } else if (token == T_UTF16_BOM) {
               scan_err0(lc, _("Currently we cannot handle UTF-16 source files. "
                   "Please convert the conf file to UTF-8\n"));
               goto bail_out;
            } else if (token != T_IDENTIFIER) {
               scan_err1(lc, _("Expected a Resource name identifier, got: %s"), lc->str);
               goto bail_out;
            }
            for (i=0; resources[i].name; i++) {
               if (strcasecmp(resources[i].name, lc->str) == 0) {
                  items = resources[i].items;
                  if (!items) {
                     break;
                  }
                  state = p_resource;
                  res_type = resources[i].rcode;
                  init_resource(this, res_type, items, pass);
                  break;
               }
            }
            if (state == p_none) {
               scan_err1(lc, _("expected resource name, got: %s"), lc->str);
               goto bail_out;
            }
            break;
         case p_resource:
            switch (token) {
            case T_BOB:
               level++;
               break;
            case T_IDENTIFIER:
               if (level != 1) {
                  scan_err1(lc, _("not in resource definition: %s"), lc->str);
                  goto bail_out;
               }
               for (i=0; items[i].name; i++) {
                  //hpkt.pass = pass;
                  //hpkt.ritem = &items[i];
                  //hpkt.edbuf = NULL;
                  //hpkt.index = i;
                  //hpkt.lc = lc;
                  //hpkt.hfunc = HF_STORE;
                  if (strcasecmp(items[i].name, lc->str) == 0) {
                     /* If the ITEM_NO_EQUALS flag is set we do NOT
                      *   scan for = after the keyword  */
                     if (!(items[i].flags & ITEM_NO_EQUALS)) {
                        token = lex_get_token(lc, T_SKIP_EOL);
                        Dmsg1 (900, "in T_IDENT got token=%s\n", lex_tok_to_str(token));
                        if (token != T_EQUALS) {
                           scan_err1(lc, _("expected an equals, got: %s"), lc->str);
                           goto bail_out;
                        }
                     }
                     Dmsg1(800, "calling handler for %s\n", items[i].name);
                     /* Call item handler */
                     items[i].handler(lc, &items[i], i, pass);
                     i = -1;
                     break;
                  }
               }
               if (i >= 0) {
                  Dmsg2(900, "level=%d id=%s\n", level, lc->str);
                  Dmsg1(900, "Keyword = %s\n", lc->str);
                  scan_err1(lc, _("Keyword \"%s\" not permitted in this resource.\n"
                     "Perhaps you left the trailing brace off of the previous resource."), lc->str);
                  goto bail_out;
               }
               break;

            case T_EOB:
               level--;
               state = p_none;
               Dmsg0(900, "T_EOB => define new resource\n");
               if (res_all.hdr.name == NULL) {
                  scan_err0(lc, _("Name not specified for resource"));
                  goto bail_out;
               }
               if (!save_resource(this, res_type, items, pass)) {  /* save resource */
                  scan_err1(lc, "%s", m_errmsg);
                  goto bail_out;
               }
               break;

            case T_EOL:
               break;

            default:
               scan_err2(lc, _("unexpected token %d %s in resource definition"),
                  token, lex_tok_to_str(token));
               goto bail_out;
            }
            break;
         default:
            scan_err1(lc, _("Unknown parser state %d\n"), state);
            goto bail_out;
         }
      }
      if (state != p_none) {
         scan_err0(lc, _("End of conf file reached with unclosed resource."));
         goto bail_out;
      }
      if (chk_dbglvl(900) && pass == 2) {
         int i;
         for (i=m_r_first; i<=m_r_last; i++) {
            dump_each_resource(i, prtmsg, NULL);
         }
      }
      lc = lex_close_file(lc);
   }
   Dmsg0(900, "Leave parse_config()\n");
   return 1;
bail_out:
   if (lc) {
      lc = lex_close_file(lc);
   }
   return 0;
}

const char *get_default_configdir()
{
   return SYSCONFDIR;
}

#ifdef xxx_not_used
   HRESULT hr;
   static char szConfigDir[MAX_PATH + 1] = { 0 };
   if (!p_SHGetFolderPath) {
      bstrncpy(szConfigDir, DEFAULT_CONFIGDIR, sizeof(szConfigDir));
      return szConfigDir;
   }
   if (szConfigDir[0] == '\0') {
      hr = p_SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, 0, szConfigDir);
      if (SUCCEEDED(hr)) {
         bstrncat(szConfigDir, "\\Bacula", sizeof(szConfigDir));
      } else {
         bstrncpy(szConfigDir, DEFAULT_CONFIGDIR, sizeof(szConfigDir));
      }
   }
   return szConfigDir;
#endif


/*
 * Returns false on error
 *         true  on OK, with full_path set to where config file should be
 */
bool
find_config_file(const char *config_file, char *full_path, int max_path)
{
   int file_length = strlen(config_file) + 1;

   /* If a full path specified, use it */
   if (first_path_separator(config_file) != NULL) {
      if (file_length > max_path) {
         return false;
      }
      bstrncpy(full_path, config_file, file_length);
      return true;
   }

   /* config_file is default file name, now find default dir */
   const char *config_dir = get_default_configdir();
   int dir_length = strlen(config_dir);

   if ((dir_length + 1 + file_length) > max_path) {
      return false;
   }

   memcpy(full_path, config_dir, dir_length + 1);

   if (!IsPathSeparator(full_path[dir_length - 1])) {
      full_path[dir_length++] = '/';
   }

   memcpy(&full_path[dir_length], config_file, file_length);

   return true;
}

/*********************************************************************
 *
 *      Free configuration resources
 *
 */
void CONFIG::free_all_resources()
{
   RES *next, *res;
   if (m_res_head == NULL) {
      return;
   }
   /* Walk down chain of res_heads */
   for (int i=m_r_first; i<=m_r_last; i++) {
      if (m_res_head[i-m_r_first]) {
         next = m_res_head[i-m_r_first]->first;
         Dmsg2(500, "i=%d, next=%p\n", i, next);
         /* Walk down resource chain freeing them */
         for ( ; next; ) {
            res = next;
            next = res->res_next;
            free_resource(res, i);
         }
        free(m_res_head[i-m_r_first]->res_list);
        free(m_res_head[i-m_r_first]);
        m_res_head[i-m_r_first] = NULL;
      }
   }
}

CONFIG::CONFIG()
:  m_cf(NULL),
   m_scan_error(NULL),
   m_err_type(0),
   m_res_all(NULL),
   m_res_all_size(0),
   m_encode_pass(true),
   m_r_first(0),
   m_r_last(0),
   m_resources(NULL),
   m_res_head(NULL)
{
   m_errmsg = get_pool_memory(PM_EMSG);
   *m_errmsg = 0;
}

CONFIG::~CONFIG() {
   free_all_resources();
   free_pool_memory(m_errmsg);
}

void CONFIG::encode_password(bool a)
{
   m_encode_pass = a;
}
