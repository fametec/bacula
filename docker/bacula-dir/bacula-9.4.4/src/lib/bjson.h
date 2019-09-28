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
 *
 *   Bacula Json library routines
 *
 *     Kern Sibbald, September MMXII
 *
 */

#ifndef __BJSON_H__
#define __BJSON_H__

/* Function codes for handler packet */
enum HFUNC {
  HF_STORE,
  HF_DISPLAY,
  HF_DEFAULT
};

/*
 * This structure defines the handler packet that is passed
 *  to the resource handler for display or store.
 */
struct HPKT {
   POOLMEM *edbuf;                    /* editing buffer */
   POOLMEM *edbuf2;                   /* editing buffer */
   RES_ITEM *ritem;                   /* RES_ITEM for call */
   RES *res;                          /* Pointer to resource header */
   HFUNC hfunc;                       /* Handler function to do */
   bool json;                         /* set to display Json */
   bool in_store_msg;                 /* set when doing store_msg */
   bool exclude;                      /* Include/Exclude flage */
   void (*sendit)(void *sock, const char *fmt, ...); /* print routine */
   LEX *lc;                           /* Lex packet */
   int index;                         /* Index item ITEM table */
   int pass;                          /* Store pass number */
   alist *list;                       /* alist to edit */
};

#endif /* __BJSON_H__ */
