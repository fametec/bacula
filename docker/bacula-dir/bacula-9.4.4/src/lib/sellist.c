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

#include "bacula.h"

/*
 * Returns next item
 *   error if returns -1 and errmsg set
 *   end of items if returns -1 and errmsg NULL
 */
int64_t sellist::next()
{
   errmsg = NULL;
   if (beg <= end) {     /* scan done? */
      //printf("Return %lld\n", beg);
      return beg++;
   }
   if (e == NULL) {
      goto bail_out;      /* nothing to scan */
   }
   /*
    * As we walk the list, we set EOF in
    *   the end of the next item to ease scanning,
    *   but save and then restore the character.
    */
   for (p=e; p && *p; p=e) {
      esave = hsave = 0;
      /* Check for list */
      e = strpbrk(p, ", ");
      if (e) {                       /* have list */
         esave = *e;
         *e++ = 0;
      }
      /* Check for range */
      h = strchr(p, '-');             /* range? */
      if (h == p) {
         errmsg = _("Negative numbers not permitted.\n");
         goto bail_out;
      }
      if (h) {                        /* have range */
         hsave = *h;
         *h++ = 0;
         if (!is_an_integer(h)) {
            errmsg = _("Range end is not integer.\n");
            goto bail_out;
         }
         skip_spaces(&p);
         if (!is_an_integer(p)) {
            errmsg = _("Range start is not an integer.\n");
            goto bail_out;
         }
         beg = str_to_int64(p);
         end = str_to_int64(h);
         //printf("beg=%lld end=%lld\n", beg, end);
         if (end <= beg) {
            errmsg = _("Range end not bigger than start.\n");
            goto bail_out;
         }
      } else {                           /* not list, not range */
         skip_spaces(&p);
         /* Check for abort (.) */
         if (*p == '.') {
            errmsg = _("User cancel requested.\n");
            goto bail_out;
         }
         /* Check for all keyword */
         if (strncasecmp(p, "all", 3) == 0) {
            all = true;
            errmsg = NULL;
            //printf("Return 0 i.e. all\n");
            return 0;
         }
         if (!is_an_integer(p)) {
            errmsg = _("Input value is not an integer.\n");
            goto bail_out;
         }
         beg = end = str_to_int64(p);
      }
      if (esave) {
         *(e-1) = esave;
      }
      if (hsave) {
         *(h-1) = hsave;
      }
      if (beg <= 0 || end <= 0) {
         errmsg = _("Selection items must be be greater than zero.\n");
         goto bail_out;
      }
      if (beg <= end) {
         //printf("Return %lld\n", beg);
         return beg++;
      }
   }
   //printf("Rtn=-1. End of items\n");
   /* End of items */
   begin();
   e = NULL;
   return -1;          /* No error */

bail_out:
   if (errmsg) {
      //printf("Bail out rtn=-1. p=%c err=%s\n", *p, errmsg);
   } else {
      //printf("Rtn=-1. End of items\n");
   }
   e = NULL;
   return -1;          /* Error, errmsg set */
}


/*
 * Set selection string and optionally scan it
 *   returns false on error in string
 *   returns true if OK
 */
bool sellist::set_string(const char *string, bool scan=true)
{
   bool ok = true;
   /*
    * Copy string, because we write into it,
    *  then scan through it once to find any
    *  errors.
    */
   if (expanded) {
      free(expanded);
      expanded = NULL;
   }
   if (str) {
      free(str);
   }
   str = bstrdup(string);
   begin();
   num_items = 0;
   if (scan) {
      while (next() >= 0) {
         num_items++;
      }
      ok = get_errmsg() == NULL;
   }
   if (ok) {
      begin();
   } else {
      e = NULL;
   }
   return ok;
}

/*
 * Get the expanded list of values separated by commas,
 * useful for SQL queries
 */
char *sellist::get_expanded_list()
{
   int32_t expandedsize = 512;
   int32_t len;
   int64_t val;
   char    *p, *tmp;
   char    ed1[50];

   if (!expanded) {
      p = expanded = (char *)malloc(expandedsize * sizeof(char));
      *p = 0;

      while ((val = next()) >= 0) {
         edit_int64(val, ed1);
         len = strlen(ed1);

         /* Alloc more space if needed */
         if ((p + len + 1) > (expanded + expandedsize)) {
            expandedsize = expandedsize * 2;

            tmp = (char *) realloc(expanded, expandedsize);

            /* Compute new addresses for p and expanded */
            p = tmp + (p - expanded);
            expanded = tmp;
         }

         /* If not at the begining of the string, add a "," */
         if (p != expanded) {
            strcpy(p, ",");
            p++;
         }

         strcpy(p, ed1);
         p += len;
      }
   }
   return expanded;
}

#ifndef TEST_PROGRAM
#define TEST_PROGRAM_A
#endif

#ifdef TEST_PROGRAM
#include "unittests.h"

struct test {
   const int nr;
   const char *sinp;
   const char *sout;
   const bool res;
};

static struct test tests[] = {
   { 1, "1,70", "1,70", true, },
   { 2, "1", "1", true, },
   { 3, "256", "256", true, },
   { 4, "1-5", "1,2,3,4,5", true, },
   { 5, "1-5,7", "1,2,3,4,5,7", true, },
   { 6, "1 10 20 30", "1,10,20,30", true, },
   { 7, "1-5,7,20 21", "1,2,3,4,5,7,20,21", true, },
   { 8, "all", "0", true, },
   { 9, "12a", "", false, },
   {10, "12-11", "", false, },
   {11, "12-13a", "", false, },
   {12, "a123", "", false, },
   {13, "1  3", "", false, },
   {0, "dummy", "dummy", false, },
};

#define ntests ((int)(sizeof(tests)/sizeof(struct test)))
#define MSGLEN 80

int main()
{
   Unittests sellist_test("sellist_test");
   const char *msg;
   sellist sl;
   char buf[MSGLEN];
   bool check_rc;

   for (int i = 0; i < ntests; i++) {
      if (tests[i].nr > 0){
         snprintf(buf, MSGLEN, "Checking test: %d - %s", tests[i].nr, tests[i].sinp);
         check_rc = sl.set_string(tests[i].sinp, true);
         msg = sl.get_expanded_list();
         ok(check_rc == tests[i].res && strcmp(msg, tests[i].sout) == 0, buf);
      }
   }

   return report();
}
#endif   /* TEST_PROGRAM */
