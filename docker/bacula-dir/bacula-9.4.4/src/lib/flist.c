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
 *  Bacula fifo list routines
 *
 *  flist is a simple malloc'ed array of pointers. Derived from alist.
 *
 *   Kern Sibbald, August 2014
 *
 */

#include "bacula.h"

void *flist::dequeue()
{
   void *item;

   if (num_items == 0) {
      return NULL;
   }
   num_items--;
   item = items[get_item];
   items[get_item++] = NULL;
   if (get_item >= max_items) {
      get_item = 0;
   }
   return item;
}


/*
 * Queue an item to the list
 */
bool flist::queue(void *item)
{
   if (num_items == max_items) {
      return false;
   }
   num_items++;
   items[add_item++] = item;
   if (add_item >= max_items) {
      add_item = 0;
   }
   return true;
}

/* Destroy the list and its contents */
void flist::destroy()
{
   if (num_items && own_items) {
      for (int i=0; i<num_items; i++) {
         if (items[i]) {
            free(items[i]);
            items[i] = NULL;
         }
      }
   }
   free(items);
   items = NULL;
}

#ifndef TEST_PROGRAM
#define TEST_PROGRAM_A
#endif

#ifdef TEST_PROGRAM
#include "unittests.h"

#define NUMITEMS        20

struct FILESET {
   flist mylist;
};

int main()
{
   Unittests flist_test("flist_test");
   FILESET *fileset;
   char buf[30];
   char buftmp[30];
   flist *mlist;
   char *p, *q;
   int i;
   int dn;
   bool check_dq;
   bool check_qa;

   Pmsg0(0, "Initialize tests ...\n");
   fileset = (FILESET *)malloc(sizeof(FILESET));
   bmemzero(fileset, sizeof(FILESET));
   fileset->mylist.init();
   ok(fileset && fileset->mylist.empty(), "Default initialization");

   Pmsg0(0, "Manual allocation/destruction of flist:\n");
   fileset->mylist.queue((void*)"first");
   fileset->mylist.queue((void*)"second");
   fileset->mylist.queue((void*)"third");
   q = (char*)fileset->mylist.dequeue();
   ok(strcmp("first", q) == 0, "Checking first dequeue");
   q = (char*)fileset->mylist.dequeue();
   ok(strcmp("second", q) == 0, "Checking second dequeue");
   q = (char*)fileset->mylist.dequeue();
   ok(strcmp("third", q) == 0, "Checking third dequeue");
   ok(fileset->mylist.empty(), "Checking if empty");

   Pmsg0(0, "automatic allocation/destruction of flist:\n");

   dn = 0;
   check_dq = true;
   check_qa = true;
   for (i = dn; i < NUMITEMS; i++) {
      sprintf(buf, "This is item %d", i);
      p = bstrdup(buf);
      if (!fileset->mylist.queue(p)) {
         q = (char *)fileset->mylist.dequeue();
         sprintf(buftmp, "This is item %d", dn++);
         if (strcmp(q, buftmp) != 0){
            check_dq = false;
         }
         free(q);
         if (!fileset->mylist.queue(p)) {
            check_qa = false;
         }
      }
   }
   ok(check_dq, "Checking first out dequeue");
   ok(check_qa, "Checking queue again after dequeue");
   /* dequeue rest of the list */
   check_dq = true;
   while ((q=(char *)fileset->mylist.dequeue())) {
      sprintf(buftmp, "This is item %d", dn++);
      if (strcmp(q, buftmp) != 0){
         check_dq = false;
      }
      free(q);
   }
   ok(check_dq, "Checking dequeue rest of the list");
   ok(fileset->mylist.empty(), "Checking if list is empty");
   ok(fileset->mylist.dequeue() == NULL, "Checking empty dequeue");
   fileset->mylist.destroy();
   ok(fileset->mylist.size() == 0, "Check size after destroy");
   free(fileset);

   Pmsg0(0, "Allocation/destruction using new delete\n");

   mlist = New(flist(10));
   ok(mlist && mlist->empty(), "Constructor initialization");

   for (i = 0; i < NUMITEMS; i++) {
      sprintf(buf, "This is item %d", i);
      p = bstrdup(buf);
      if (!mlist->queue(p)) {
         free(p);
         break;
      }
   }
   ok(mlist->size() == 10, "Checking list size");
   dn = 0;
   check_dq = true;
   for (i=1; !mlist->empty(); i++) {
      p = (char *)mlist->dequeue();
      sprintf(buf, "This is item %d", dn++);
      if (strcmp(p, buf) != 0){
         check_dq = false;
      }
      free(p);
   }
   ok(check_dq, "Checking dequeue list");
   ok(mlist->empty(), "Checking list empty");

   delete mlist;

   return report();
}
#endif   /* TEST_PROGRAM */
