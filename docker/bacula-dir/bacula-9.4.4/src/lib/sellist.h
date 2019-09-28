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
 *  Kern Sibbald, January  MMXII
 *
 *  Selection list. A string of integers separated by commas
 *   representing items selected. Ranges of the form nn-mm
 *   are also permitted.
 */

#ifndef __SELLIST_H_
#define __SELLIST_H_

/*
 * Loop var through each member of list
 */
#define foreach_sellist(var, list) \
        for((var)=(list)->first(); (var)>=0; (var)=(list)->next() )


class sellist : public SMARTALLOC {
   const char *errmsg;
   char *p, *e, *h;
   char esave, hsave;
   bool all;
   int64_t beg, end;
   int num_items;
   char *str;
   char *expanded;
public:
   sellist();
   ~sellist();
   bool set_string(const char *string, bool scan);
   bool is_all() { return all; };
   int64_t first();
   int64_t next();
   void begin();
   /* size() valid only if scan enabled on string */
   int size() const { return num_items; };
   char *get_list() { return str; };
   /* get the list of all jobids */
   char *get_expanded_list();
   /* if errmsg == NULL, no error */
   const char *get_errmsg() { return errmsg; };
};

/*
 * Initialize the list structure
 */
inline sellist::sellist()
{
   num_items = 0;
   expanded = NULL;
   str = NULL;
   e = NULL;
   errmsg = NULL;
}

/*
 * Destroy the list
 */
inline sellist::~sellist()
{
   if (str) {
      free(str);
      str = NULL;
   }
   if (expanded) {
      free(expanded);
      expanded = NULL;
   }
}

/*
 * Returns first item
 *   error if returns -1 and errmsg set
 *   end of items if returns -1 and errmsg NULL
 */
inline int64_t sellist::first()
{
   begin();
   return next();
}

/*
 * Reset to walk list from beginning
 */
inline void sellist::begin()
{
   e = str;
   end = 0;
   beg = 1;
   all = false;
   errmsg = NULL;
}

#endif /* __SELLIST_H_ */
