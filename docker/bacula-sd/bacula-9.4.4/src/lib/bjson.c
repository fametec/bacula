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
 *   Bacula Json library routines
 *
 *     Kern Sibbald, September MMXII
 *
 */

#include "bacula.h"
#include "lib/breg.h"

extern s_kw msg_types[];
extern RES_TABLE resources[];

union URES {
   MSGS  res_msgs;
   RES hdr;
};

#if defined(_MSC_VER)
extern "C" { // work around visual compiler mangling variables
   extern URES res_all;
}
#else
extern URES res_all;
#endif

struct display_filter
{
   /* default                   { { "Director": { "Name": aa, ...} }, { "Job": {..} */
   bool do_list;             /* [ {}, {}, ..] or { "aa": {}, "bb": {}, ...} */
   bool do_one;              /* { "Name": "aa", "Description": "test, ... } */
   bool do_only_data;        /* [ {}, {}, {}, ] */
   char *resource_type;
   char *resource_name;
   regex_t directive_reg;
};

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

void init_hpkt(HPKT &hpkt)
{
   memset(&hpkt, 0, sizeof(hpkt));
   hpkt.edbuf = get_pool_memory(PM_EMSG);
   hpkt.edbuf2 = get_pool_memory(PM_EMSG);
   hpkt.json = true;
   hpkt.hfunc = HF_DISPLAY;
   hpkt.sendit = sendit;
}

void term_hpkt(HPKT &hpkt)
{
   free_pool_memory(hpkt.edbuf);
   free_pool_memory(hpkt.edbuf2);
   memset(&hpkt, 0, sizeof(hpkt));
}

/*
 * Strip long options out of fo->opts string so that
 *   they will not give us false matches for regular
 *   1 or 2 character options.
 */
void strip_long_opts(char *out, const char *in)
{
   const char *p;
   for (p=in; *p; p++) {
      switch (*p) {
      /* V, C, J, and P are long options, skip them */
      case 'V':
      case 'C':
      case 'J':
      case 'P':
         while (*p != ':') {
            p++;       /* skip to after : */
         }
         break;
      /* Copy everything else */
      default:
         *out++ = *p;
         break;
      }
   }
   *out = 0;           /* terminate string */
}

void edit_alist(HPKT &hpkt)
{
   bool f = true;
   char *citem;

   pm_strcpy(hpkt.edbuf, " [");
   foreach_alist(citem, hpkt.list) {
      if (!f) {
         pm_strcat(hpkt.edbuf, ", ");
      }
      pm_strcat(hpkt.edbuf, quote_string(hpkt.edbuf2, citem));
      f = false;
   }
   pm_strcat(hpkt.edbuf, "]");
}

void edit_msg_types(HPKT &hpkt, DEST *dest)
{
   int i, j, count = 0;
   bool first_type = true;
   bool found;

   pm_strcpy(hpkt.edbuf, "[");
   for (i=1; i<M_MAX; i++) {
      if (bit_is_set(i, dest->msg_types)) {
         found = false;
         if (!first_type) pm_strcat(hpkt.edbuf, ",");
         first_type = false;
         for (j=0; msg_types[j].name; j++) {
            if ((int)msg_types[j].token == i) {
               pm_strcat(hpkt.edbuf, "\"");
               pm_strcat(hpkt.edbuf, msg_types[j].name);
               pm_strcat(hpkt.edbuf, "\"");
               found = true;
               break;
            }
         }
         if (!found) {
            sendit(NULL, "No find for type=%d\n", i);
         }
         count++;
      }
   }
   /*
    * Note, if we have more than half of the total items,
    *   redo using All and !item, which will give fewer items
    *   total.
    */
   if (count > M_MAX/2) {
      pm_strcpy(hpkt.edbuf, "[\"All\"");
      for (i=1; i<M_MAX; i++) {
         if (!bit_is_set(i, dest->msg_types)) {
            found = false;
            pm_strcat(hpkt.edbuf, ",");
            for (j=0; msg_types[j].name; j++) {
               if ((int)msg_types[j].token == i) {
                  pm_strcat(hpkt.edbuf, "\"!");
                  pm_strcat(hpkt.edbuf, msg_types[j].name);
                  pm_strcat(hpkt.edbuf, "\"");
                  found = true;
                  break;
               }
            }
            if (!found) {
               sendit(NULL, "No find for type=%d in second loop\n", i);
            }
         } else if (i == M_SAVED) {
            /* Saved is not set by default, users must explicitly use it
             * on the configuration line
             */
            pm_strcat(hpkt.edbuf, ",\"Saved\"");
         }
      }
   }
   pm_strcat(hpkt.edbuf, "]");
}

bool display_global_item(HPKT &hpkt)
{
   bool found = true;

   if (hpkt.ritem->handler == store_res) {
      display_res(hpkt);
   } else if (hpkt.ritem->handler == store_str ||
              hpkt.ritem->handler == store_name ||
              hpkt.ritem->handler == store_password ||
              hpkt.ritem->handler == store_strname ||
              hpkt.ritem->handler == store_dir) {
      display_string_pair(hpkt);
   } else if (hpkt.ritem->handler == store_int32 ||
              hpkt.ritem->handler == store_pint32 ||
              hpkt.ritem->handler == store_size32) {
      display_int32_pair(hpkt);
   } else if (hpkt.ritem->handler == store_size64 ||
              hpkt.ritem->handler == store_int64 ||
              hpkt.ritem->handler == store_time ||
              hpkt.ritem->handler == store_speed) {
      display_int64_pair(hpkt);
   } else if (hpkt.ritem->handler == store_bool) {
      display_bool_pair(hpkt);
   } else if (hpkt.ritem->handler == store_msgs) {
      display_msgs(hpkt);
   } else if (hpkt.ritem->handler == store_bit) {
      display_bit_pair(hpkt);
   } else if (hpkt.ritem->handler == store_alist_res) {
      found = display_alist_res(hpkt); /* In some cases, the list is null... */
   } else if (hpkt.ritem->handler == store_alist_str) {
      found = display_alist_str(hpkt); /* In some cases, the list is null... */
   } else {
      found = false;
   }

   return found;
}

/*
 * Called here for each store_msgs resource
 */
void display_msgs(HPKT &hpkt)
{
   MSGS *msgs = (MSGS *)hpkt.ritem->value;  /* Message res */
   DEST *dest;   /* destination chain */
   int first = true;

   if (!hpkt.in_store_msg) {
      hpkt.in_store_msg = true;
      sendit(NULL, "\n    \"Destinations\": [");
   }
   for (dest=msgs->dest_chain; dest; dest=dest->next) {
      if (dest->dest_code == hpkt.ritem->code) {
         if (!first) sendit(NULL, ",");
         first = false;
         edit_msg_types(hpkt, dest);
         switch (hpkt.ritem->code) {
         /* Output only message types */
         case MD_STDOUT:
         case MD_STDERR:
         case MD_SYSLOG:
         case MD_CONSOLE:
         case MD_CATALOG:
            sendit(NULL, "\n      {\n        \"Type\": \"%s\","
                         "\n        \"MsgTypes\": %s\n      }",
               hpkt.ritem->name, hpkt.edbuf);
            break;
         /* Output MsgTypes, Where */
         case MD_DIRECTOR:
         case MD_FILE:
         case MD_APPEND:
            sendit(NULL, "\n      {\n        \"Type\": \"%s\","
                         "\n        \"MsgTypes\": %s,\n",
               hpkt.ritem->name, hpkt.edbuf);
            sendit(NULL, "        \"Where\": [%s]\n      }",
               quote_where(hpkt.edbuf, dest->where));
            break;
         /* Now we edit MsgTypes, Where, and Command */
         case MD_MAIL:
         case MD_OPERATOR:
         case MD_MAIL_ON_ERROR:
         case MD_MAIL_ON_SUCCESS:
            sendit(NULL, "\n      {\n        \"Type\": \"%s\","
                         "\n        \"MsgTypes\": %s,\n",
               hpkt.ritem->name, hpkt.edbuf);
            sendit(NULL, "        \"Where\": [%s],\n",
               quote_where(hpkt.edbuf, dest->where));
            sendit(NULL, "        \"Command\": %s\n      }",
               quote_string(hpkt.edbuf, dest->mail_cmd));
            break;
         }
      }
   }
}

/*
 * Called here if the ITEM_LAST is set in flags,
 *  that means there are no more items to examine
 *  for this resource and that we can close any
 *  open json list.
 */
void display_last(HPKT &hpkt)
{
   if (hpkt.in_store_msg) {
      hpkt.in_store_msg = false;
      sendit(NULL, "\n    ]");
   }
}

void display_alist(HPKT &hpkt)
{
   edit_alist(hpkt);
   sendit(NULL, "%s", hpkt.edbuf);
}

bool display_alist_str(HPKT &hpkt)
{
   hpkt.list = (alist *)(*(hpkt.ritem->value));
   if (!hpkt.list) {
      return false;
   }
   sendit(NULL, "\n    \"%s\":", hpkt.ritem->name);
   display_alist(hpkt);
   return true;
}

bool display_alist_res(HPKT &hpkt)
{
   bool f = true;
   alist *list;
   RES *res;

   list = (alist *)(*(hpkt.ritem->value));
   if (!list) {
      return false;
   }
   sendit(NULL, "\n    \"%s\":", hpkt.ritem->name);
   sendit(NULL, " [");
   foreach_alist(res, list) {
      if (!f) {
         sendit(NULL, ", ");
      }
      sendit(NULL, "%s", quote_string(hpkt.edbuf, res->name));
      f = false;
   }
   sendit(NULL, "]");
   return true;
}

void display_res(HPKT &hpkt)
{
   RES *res;

   res = (RES *)*hpkt.ritem->value;
   sendit(NULL, "\n    \"%s\": %s", hpkt.ritem->name,
      quote_string(hpkt.edbuf, res->name));
}

void display_string_pair(HPKT &hpkt)
{
   sendit(NULL, "\n    \"%s\": %s", hpkt.ritem->name,
      quote_string(hpkt.edbuf, *hpkt.ritem->value));
}

void display_int32_pair(HPKT &hpkt)
{
   char ed1[50];
   sendit(NULL, "\n    \"%s\": %s", hpkt.ritem->name,
      edit_int64(*(int32_t *)hpkt.ritem->value, ed1));
}

void display_int64_pair(HPKT &hpkt)
{
   char ed1[50];
   sendit(NULL, "\n    \"%s\": %s", hpkt.ritem->name,
      edit_int64(*(int64_t *)hpkt.ritem->value, ed1));
}

void display_bool_pair(HPKT &hpkt)
{
   sendit(NULL, "\n    \"%s\": %s", hpkt.ritem->name,
      ((*(bool *)(hpkt.ritem->value)) == 0)?"false":"true");
}

void display_bit_pair(HPKT &hpkt)
{
   sendit(NULL, "\n    \"%s\": %s", hpkt.ritem->name,
      ((*(uint32_t *)(hpkt.ritem->value) & hpkt.ritem->code)
         == 0)?"false":"true");
}

bool byte_is_set(char *byte, int num)
{
   int i;
   bool found = false;
   for (i=0; i<num; i++) {
      if (byte[i]) {
         found = true;
         break;
      }
   }
   return found;
}

void display_bit_array(char *array, int num)
{
   int i;
   bool first = true;
   sendit(NULL, " [");
   for (i=0; i<num; i++) {
      if (bit_is_set(i, array)) {
         if (!first) sendit(NULL, ", ");
         first = false;
         sendit(NULL, "%d", i);
      }
   }
   sendit(NULL, "]");
}
