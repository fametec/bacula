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
 * Routines for writing Cloud drivers
 *
 * Written by Kern Sibbald, May MMXVI
 */

#include "cloud_parts.h"

bool operator==(const cloud_part& lhs, const cloud_part& rhs)
{
   return (lhs.index == rhs.index &&
           lhs.mtime == rhs.mtime &&
           lhs.size  == rhs.size);
}

bool operator!=(const cloud_part& lhs, const cloud_part& rhs)
{
   return !operator==(lhs, rhs);
}

bool operator==(const cloud_part& lhs, const uint32_t& rhs)
{
   return (lhs.index == rhs);
}

bool operator!=(const cloud_part& lhs, const uint32_t& rhs)
{
   return !operator==(lhs, rhs);
}

/* compares the all cloud_part, according to the operator==() above*/
bool list_contains_part(ilist *parts, cloud_part *p)
{
   if (parts && p) {
      cloud_part *ap = (cloud_part *)parts->get(p->index);
      if (ap && *ap == *p) {
         return true;
      }
   }
   return false;
}

/* only checks if the part_idx part exists in the parts lst*/
bool list_contains_part(ilist *parts, uint32_t part_idx)
{
   if (parts && part_idx > 0) {
      return parts->get(part_idx) != NULL;
   }
   return false;
}

bool identical_lists(ilist *parts1, ilist *parts2)
{
   if (parts1 && parts2) {
      /* Using indexed ilist forces us to treat it differently (foreach not working b.e.) */
      int max_size = parts1->last_index();
      if (parts2->last_index() > parts1->last_index()) {
         max_size = parts2->last_index();
      }
      for(int index=0; index<=max_size; index++ ) {
         cloud_part *p1 = (cloud_part *)parts1->get(index);
         cloud_part *p2 = (cloud_part *)parts2->get(index);
         if (!p1) {
            if (p2) return false;
         } else if (!p2) {
            if (p1) return false;
         } else if (*p1 != *p2) {
            return false;
         }
      }
      return true;
   }
   return false;
}

/* cloud_parts present in source but not in dest are appended to diff.
 * there's no cloud_part copy made.
 * Diff only holds references and shoudn't own them */
bool diff_lists(ilist *source, ilist *dest, ilist *diff)
{
   if (source && dest && diff) {
      /* Using indexed list forces us to treat it differently (foreach not working b.e.) */
      int max_size = source->last_index();
      if (dest->last_index() > source->last_index()) {
         max_size = dest->last_index();
      }
      for(int index=0; index<=max_size; index++ ) {
         cloud_part *p1 = (cloud_part *)source->get(index);
         cloud_part *p2 = (cloud_part *)dest->get(index);
         if (!p1) {
            if (p2) diff->put(index, p1);
         } else if (!p2) {
            if (p1) diff->put(index, p1);
         } else if (*p1 != *p2) {
            diff->put(index, p1);
         }
      }
      return true;
   }
   return false;
}

/*=================================================
 * cloud_proxy definitions
  ================================================= */

cloud_proxy * cloud_proxy::m_pinstance=NULL;
uint64_t cloud_proxy::m_count=0;

/* hash table node structure */
typedef struct {
   hlink hlnk;
   ilist *parts_lst;
   char *key_name;
} VolHashItem;

/* constructor
 * size: the default hash size
 * owns: determines if the ilists own the cloud_parts or not */
cloud_proxy::cloud_proxy(uint32_t size, bool owns)
{
   pthread_mutex_init(&m_mutex, 0);
   VolHashItem *hitem=NULL;
   m_hash = New(htable(hitem, &hitem->hlnk, size));
   m_owns = owns;
}

/* destructor
 * we need to go thru each htable node and manually delete
 * the associated alist before deleting the htable itself */
cloud_proxy::~cloud_proxy()
{
   VolHashItem *hitem;
   foreach_htable(hitem, m_hash) {
      delete hitem->parts_lst;
      free (hitem->key_name);
   }
   delete m_hash;
   pthread_mutex_destroy(&m_mutex);
}

/* insert the cloud_part into the proxy.
 * create the volume ilist if necessary */
bool cloud_proxy::set(const char *volume, cloud_part *part)
{
   if (part) {
      return set(volume, part->index, part->mtime, part->size);
   }
   return false;
}

bool cloud_proxy::set(const char *volume, uint32_t index, utime_t mtime, uint64_t size)
{
   if (!volume || index < 1) {
      return false;
   }
   lock_guard lg(m_mutex);
   /* allocate new part */
   cloud_part *part = (cloud_part*) malloc(sizeof(cloud_part));
   /* fill it with result info from the transfer */
   part->index = index;
   part->mtime = mtime;
   part->size = size;

   VolHashItem *hitem = (VolHashItem*)m_hash->lookup(const_cast<char*>(volume));
   if (hitem) { /* when the node already exist, put the cloud_part into the vol list */
      /* free the existing part */
      if (hitem->parts_lst->get(index)) {
         free(hitem->parts_lst->get(index));
      }
      hitem->parts_lst->put(index, part);
      return true;
   } else { /* if the node doesnt exist for this key, create it */
      ilist *new_lst = New(ilist(100,m_owns));
      new_lst->put(part->index, part);
      /* use hashtable helper malloc */
      VolHashItem *new_hitem = (VolHashItem *) m_hash->hash_malloc(sizeof(VolHashItem));
      new_hitem->parts_lst = new_lst;
      new_hitem->key_name = bstrdup(volume);
      return m_hash->insert(new_hitem->key_name, new_hitem);
   }
   return false;
}

/* retrieve the cloud_part for the volume name at index part idx
 * can return NULL */
cloud_part *cloud_proxy::get(const char *volume, uint32_t index)
{
   lock_guard lg(m_mutex);
   if (volume) {
      VolHashItem *hitem = (VolHashItem *)m_hash->lookup(const_cast<char*>(volume));
      if (hitem) {
         ilist * ilst = hitem->parts_lst;
         if (ilst) {
            return (cloud_part*)ilst->get(index);
         }
      }
   }
   return NULL;
}

uint64_t cloud_proxy::get_size(const char *volume, uint32_t part_idx)
{
   cloud_part *cld_part = get(volume, part_idx);
   return cld_part ? cld_part->size:0;
}

/* Check if the volume entry exists and return true if it's the case */
bool cloud_proxy::volume_lookup(const char *volume)
{
   lock_guard lg(m_mutex);
   return ( (volume) && m_hash->lookup(const_cast<char*>(volume)) );
}

/* reset the volume list content with the content of part_list */
bool cloud_proxy::reset(const char *volume, ilist *part_list)
{
   lock_guard lg(m_mutex);
   if (volume && part_list) {
      VolHashItem *hitem = (VolHashItem*)m_hash->lookup(const_cast<char*>(volume));
      if (hitem) { /* when the node already exist, recycle it */
         delete hitem->parts_lst;
      } else { /* create the node */
         hitem = (VolHashItem *) m_hash->hash_malloc(sizeof(VolHashItem));
         hitem->key_name = bstrdup(volume);
         if (!m_hash->insert(hitem->key_name, hitem)) {
            return false;
         }
      }
      /* re-create the volume list */
      hitem->parts_lst = New(ilist(100, m_owns));
      /* feed it with cloud_part elements */
      for(int index=1; index<=part_list->last_index(); index++ ) {
         cloud_part *part = (cloud_part *)part_list->get(index);
         if (part) {
            hitem->parts_lst->put(index, part);
         }
      }
      return true;
   }
   return false;
}

uint32_t cloud_proxy::last_index(const char *volume)
{
   lock_guard lg(m_mutex);
   if (volume) {
      VolHashItem *hitem = (VolHashItem*)m_hash->lookup(const_cast<char*>(volume));
      if (hitem && hitem->parts_lst) {
         return hitem->parts_lst->last_index();
      }
   }
   return 0;
}

ilist *cloud_proxy::exclude(const char *volume, ilist *exclusion_lst)
{
   if (volume && exclusion_lst) {
      VolHashItem *hitem = (VolHashItem*)m_hash->lookup(const_cast<char*>(volume));
      if (hitem) {
         ilist *res_lst = New(ilist(100, false));
         if (diff_lists(hitem->parts_lst, exclusion_lst, res_lst)) {
            return res_lst;
         }
      }
   }
   return NULL;
}
cloud_proxy *cloud_proxy::get_instance()
{
   if (!m_pinstance) {
      m_pinstance = New(cloud_proxy());
   }
   ++m_count;
   return m_pinstance;
}

void cloud_proxy::release()
{
   if (--m_count == 0) {
      delete m_pinstance;
      m_pinstance = NULL;
   }
}

void cloud_proxy::dump()
{
   VolHashItem *hitem;
   foreach_htable(hitem, m_hash) {
      Dmsg2(0, "proxy (%d) Volume:%s\n", m_hash->size(), hitem->hlnk.key.key);
      for(int index=0; index<=hitem->parts_lst->last_index(); index++ ) {
         cloud_part *p = (cloud_part *)hitem->parts_lst->get(index);
         if (p) {
            Dmsg1(0, "part.%d\n", p->index);
         }
      }
   }
}

//=================================================
#ifdef TEST_PROGRAM
int main (int argc, char *argv[])
{
   pthread_attr_t attr;

   void * start_heap = sbrk(0);
   (void)start_heap;

   setlocale(LC_ALL, "");
   bindtextdomain("bacula", LOCALEDIR);
   textdomain("bacula");
   init_stack_dump();
   my_name_is(argc, argv, "cloud_parts_test");
   init_msg(NULL, NULL);
   daemon_start_time = time(NULL);
   set_thread_concurrency(150);
   lmgr_init_thread(); /* initialize the lockmanager stack */
   pthread_attr_init(&attr);
   berrno be;

   printf("Test0\n");
   {
   cloud_part p1, p2, p3, p4;

   p1.index = 1;
   p1.mtime = 1000;
   p1.size = 1000;

   p2.index = 2;
   p2.mtime = 2000;
   p2.size = 2020;

   p3.index = 3;
   p3.mtime = 3000;
   p3.size = 3030;

   p4.index = 4;
   p4.mtime = 4000;
   p4.size = 4040;

   ilist l(10,false);
   l.put(p1.index,&p1);
   l.put(p2.index,&p2);
   l.put(p3.index,&p3);

   ASSERT(list_contains_part(&l, &p1));
   ASSERT(list_contains_part(&l, &p2));
   ASSERT(list_contains_part(&l, &p3));
   ASSERT(!list_contains_part(&l, &p4));

   ASSERT(list_contains_part(&l, 3));
   ASSERT(list_contains_part(&l, 1));
   ASSERT(list_contains_part(&l, 2));
   ASSERT(!list_contains_part(&l, 4));
   }

   printf("Test1\n");
   {
   cloud_part p1, p2, p3;

   p1.index = 1;
   p1.mtime = 1000;
   p1.size = 1000;

   p2.index = 2;
   p2.mtime = 2000;
   p2.size = 2020;

   p3.index = 3;
   p3.mtime = 3000;
   p3.size = 3030;

   ilist cloud(10,false);
   cloud.put(p1.index, &p1);
   cloud.put(p2.index, &p2);

   ilist cache(10,false);
   cache.put(p3.index, &p3);

   ASSERT(!identical_lists(&cloud, &cache));

   cache.put(p1.index, &p1);
   ASSERT(!identical_lists(&cloud, &cache));
   }

   printf("Test2\n");
   {
   cloud_part p1, p2, p3, p4;

   p1.index = 1;
   p1.mtime = 1000;
   p1.size = 1000;

   p2.index = 2;
   p2.mtime = 2000;
   p2.size = 2020;

   p3.index = 3;
   p3.mtime = 3000;
   p3.size = 3030;

   p4.index = 4;
   p4.mtime = 4000;
   p4.size = 4040;

   ilist cloud(10,false);
   cloud.put(p1.index, &p1);
   cloud.put(p2.index, &p2);

   ilist cache(10,false);
   cloud.put(p3.index, &p3);
   cloud.put(p4.index, &p4);

   ASSERT(!identical_lists(&cloud, &cache));

   cache.put(p1.index, &p1);
   ASSERT(!identical_lists(&cloud, &cache));
   }

   printf("Test3\n");
   {
   cloud_part p1, p2, p3;

   p1.index = 1;
   p1.mtime = 1000;
   p1.size = 1000;

   p2.index = 2;
   p2.mtime = 2000;
   p2.size = 2020;

   p3.index = 3;
   p3.mtime = 3000;
   p3.size = 3030;

   ilist cloud(10,false);
   cloud.put(p1.index, &p1);
   cloud.put(p2.index, &p2);
   cloud.put(p3.index, &p3);

   ilist cache(10,false);
   cache.put(p3.index, &p3);
   cache.put(p1.index, &p1);
   cache.put(p2.index, &p2);

   ASSERT(identical_lists(&cloud, &cache));
   }

   printf("Test4\n");
   {
   cloud_part p1, p2, p3;

   p1.index = 1;
   p1.mtime = 1000;
   p1.size = 1000;

   p2.index = 2;
   p2.mtime = 2000;
   p2.size = 2020;

   p3.index = 3;
   p3.mtime = 3000;
   p3.size = 3030;

   ilist cloud(10,false);
   cloud.put(p1.index, &p1);
   cloud.put(p2.index, &p2);
   cloud.put(p3.index, &p3);

   ilist cache(10,false);
   cache.put(p2.index, &p2);
   cache.put(p1.index, &p1);

   ASSERT(!identical_lists(&cloud, &cache));
   ilist diff(10,false);
   ASSERT(diff_lists(&cloud, &cache, &diff));
   ASSERT(diff.size() == 1);
   cloud_part *dp = (cloud_part *)diff.get(3);
   ASSERT(*dp == p3);
   }

   printf("Test proxy set\\get\n");
   {
   cloud_part p1, p2, p3;

   p1.index = 1;
   p1.mtime = 1000;
   p1.size = 1000;

   p2.index = 2;
   p2.mtime = 2000;
   p2.size = 2020;

   p3.index = 3;
   p3.mtime = 3000;
   p3.size = 3030;

   cloud_proxy *prox = cloud_proxy::get_instance();

   /* add to the cloud proxy with no error */
   /* in volume1 */
   ASSERT(prox->set("volume1", &p1));
   ASSERT(prox->set("volume1", &p2));
   /* in volume2 */
   ASSERT(prox->set("volume2", &p3));

   /* retrieve the correct elements */
   ASSERT(prox->get("volume1", 1) != NULL);
   ASSERT(prox->get("volume1", 1)->mtime == 1000);
   ASSERT(prox->get("volume1", 1)->size == 1000);
   ASSERT(prox->get("volume1", 2) != NULL);
   ASSERT(prox->get("volume1", 2)->mtime == 2000);
   ASSERT(prox->get("volume1", 2)->size == 2020);
   /* part3 is in volume2, not in volume1 */
   ASSERT(prox->get("volume1", 3) == NULL);
   ASSERT(prox->get("volume2", 3) != NULL);
   ASSERT(prox->get("volume2", 3)->mtime == 3000);
   ASSERT(prox->get("volume2", 3)->size == 3030);
   /* there's no volume3 */
   ASSERT(prox->get("volume3", 1) == NULL);
   /* there's no volume3 nor part4 */
   ASSERT(prox->get("volume3", 4) == NULL);
   }
   printf("Test proxy reset\n");
   {
   cloud_part p1, p2, p3, p4, p5;

   p1.index = 1;
   p1.mtime = 1000;
   p1.size = 1000;

   p2.index = 2;
   p2.mtime = 2000;
   p2.size = 2020;

   p3.index = 3;
   p3.mtime = 3000;
   p3.size = 3030;

   cloud_proxy *prox = cloud_proxy::get_instance();

   /* add to the cloud proxy with no error */
   /* in volume1 */
   ASSERT(prox->set("volume1", &p1));
   ASSERT(prox->set("volume1", &p2));
   /* in volume2 */
   ASSERT(prox->set("volume2", &p3));

   p4.index = 3;
   p4.mtime = 4000;
   p4.size = 4040;

   p5.index = 50;
   p5.mtime = 5000;
   p5.size = 5050;

   ilist part_list(10,false);
   part_list.put(p4.index, &p4);
   part_list.put(p5.index, &p5);

   /* reset volume 1 */
   prox->reset("volume1", &part_list);
   /* old elements are gone */
   ASSERT(prox->get("volume1", 1) == NULL);
   ASSERT(prox->get("volume1", 2) == NULL);
   /* new elements are at the correct index */
   ASSERT(prox->get("volume1", 3) != NULL);
   ASSERT(prox->get("volume1", 3)->mtime == 4000);
   ASSERT(prox->get("volume1", 3)->size == 4040);
   ASSERT(prox->get("volume1", 50) != NULL);
   ASSERT(prox->get("volume1", 50)->mtime == 5000);
   ASSERT(prox->get("volume1", 50)->size == 5050);
   /* part3 is still in volume2 */
   ASSERT(prox->get("volume2", 3) != NULL);
   ASSERT(prox->get("volume2", 3)->mtime == 3000);
   ASSERT(prox->get("volume2", 3)->size == 3030);
   /* there's no volume3 */
   ASSERT(prox->get("volume3", 1) == NULL);
   /* there's no volume3 nor part.index 4 */
   ASSERT(prox->get("volume3", 4) == NULL);
   prox->dump();
   }


   return 0;

}

#endif /* TEST_PROGRAM */
