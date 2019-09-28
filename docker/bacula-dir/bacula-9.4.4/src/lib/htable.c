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
 *  Bacula hash table routines
 *
 *  htable is a hash table of items (pointers). This code is
 *    adapted and enhanced from code I wrote in 1982 for a
 *    relocatable linker.  At that time, the hash table size
 *    was fixed and a primary number, which essentially provides
 *    the randomness. In this program, the hash table can grow when
 *    it gets too full, so the table size here is a binary number. The
 *    hashing is provided using an idea from Tcl where the initial
 *    hash code is "randomized" using a simple calculation from
 *    a random number generator that multiplies by a big number
 *    (I multiply by a prime number, while Tcl did not)
 *    then shifts the result down and does the binary division
 *    by masking.  Increasing the size of the hash table is simple.
 *    Just create a new larger table, walk the old table and
 *    re-hash insert each entry into the new table.
 *
 *
 *   Kern Sibbald, July MMIII
 *
 */

#include "bacula.h"
#include "htable.h"

#define lli long long int
static const int dbglvl = 500;

/* ===================================================================
 *    htable
 */

#ifdef BIG_MALLOC
/*
 * This subroutine gets a big buffer.
 */
void htable::malloc_big_buf(int size)
{
   struct h_mem *hmem;

   hmem = (struct h_mem *)malloc(size);
   total_size += size;
   blocks++;
   hmem->next = mem_block;
   mem_block = hmem;
   hmem->mem = mem_block->first;
   hmem->rem = (char *)hmem + size - hmem->mem;
   Dmsg3(100, "malloc buf=%p size=%d rem=%d\n", hmem, size, hmem->rem);
}

/* This routine frees the whole tree */
void htable::hash_big_free()
{
   struct h_mem *hmem, *rel;

   for (hmem=mem_block; hmem; ) {
      rel = hmem;
      hmem = hmem->next;
      Dmsg1(100, "free malloc buf=%p\n", rel);
      free(rel);
   }
}

#endif

/*
 * Normal hash malloc routine that gets a
 *  "small" buffer from the big buffer
 */
char *htable::hash_malloc(int size)
{
#ifdef BIG_MALLOC
   char *buf;
   int asize = BALIGN(size);

   if (mem_block->rem < asize) {
      uint32_t mb_size;
      if (total_size >= 1000000) {
         mb_size = 1000000;
      } else {
         mb_size = 100000;
      }
      malloc_big_buf(mb_size);
   }
   mem_block->rem -= asize;
   buf = mem_block->mem;
   mem_block->mem += asize;
   return buf;
#else
   total_size += size;
   blocks++;
   return (char *)malloc(size);
#endif
}




/*
 * Create hash of key, stored in hash then
 *  create and return the pseudo random bucket index
 */
void htable::hash_index(char *key)
{
   hash = 0;
   for (char *p=key; *p; p++) {
      hash +=  ((hash << 5) | (hash >> (sizeof(hash)*8-5))) + (uint32_t)*p;
   }
   /* Multiply by large prime number, take top bits, mask for remainder */
   index = ((hash * 1103515249LL) >> rshift) & mask;
   Dmsg2(dbglvl, "Leave hash_index hash=0x%x index=%d\n", hash, index);
}

void htable::hash_index(uint64_t ikey)
{
   hash = ikey;           /* already have starting binary hash */
   /* Same algorithm as for char * */
   index = ((hash * 1103515249LL) >> rshift) & mask;
   Dmsg2(dbglvl, "Leave hash_index hash=0x%x index=%d\n", hash, index);
}

/*
 * tsize is the estimated number of entries in the hash table
 */
htable::htable(void *item, void *link, int tsize)
{
   init(item, link, tsize);
}

void htable::init(void *item, void *link, int tsize)
{
   int pwr;

   bmemzero(this, sizeof(htable));
   if (tsize < 31) {
      tsize = 31;
   }
   tsize >>= 2;
   for (pwr=0; tsize; pwr++) {
      tsize >>= 1;
   }
   loffset = (char *)link - (char *)item;
   mask = ~((~0)<<pwr);               /* 3 bits => table size = 8 */
   rshift = 30 - pwr;                 /* start using bits 28, 29, 30 */
   buckets = 1<<pwr;                  /* hash table size -- power of two */
   max_items = buckets * 4;           /* allow average 4 entries per chain */
   table = (hlink **)malloc(buckets * sizeof(hlink *));
   bmemzero(table, buckets * sizeof(hlink *));
#ifdef BIG_MALLOC
   malloc_big_buf(1000000);   /* ***FIXME*** need variable or some estimate */
#endif /* BIG_MALLOC */
}

uint32_t htable::size()
{
   return num_items;
}

/*
 * Take each hash link and walk down the chain of items
 *  that hash there counting them (i.e. the hits),
 *  then report that number.
 * Obiously, the more hits in a chain, the more time
 *  it takes to reference them. Empty chains are not so
 *  hot either -- as it means unused or wasted space.
 */
#define MAX_COUNT 20
void htable::stats()
{
   int hits[MAX_COUNT];
   int max = 0;
   int i, j;
   hlink *p;
   printf("\n\nNumItems=%d\nTotal buckets=%d\n", num_items, buckets);
   printf("Hits/bucket: buckets\n");
   for (i=0; i < MAX_COUNT; i++) {
      hits[i] = 0;
   }
   for (i=0; i<(int)buckets; i++) {
      p = table[i];
      j = 0;
      while (p) {
         p = (hlink *)(p->next);
         j++;
      }
      if (j > max) {
         max = j;
      }
      if (j < MAX_COUNT) {
         hits[j]++;
      }
   }
   for (i=0; i < MAX_COUNT; i++) {
      printf("%2d:           %d\n",i, hits[i]);
   }
   printf("buckets=%d num_items=%d max_items=%d\n", buckets, num_items, max_items);
   printf("max hits in a bucket = %d\n", max);
#ifdef BIG_MALLOC
   char ed1[100];
   edit_uint64(total_size, ed1);
   printf("total bytes malloced = %s\n", ed1);
   printf("total blocks malloced = %d\n", blocks);
#endif
}

void htable::grow_table()
{
   Dmsg1(100, "Grow called old size = %d\n", buckets);
   /* Setup a bigger table */
   htable *big = (htable *)malloc(sizeof(htable));
   memcpy(big, this, sizeof(htable));  /* start with original class data */
   big->loffset = loffset;
   big->mask = mask<<1 | 1;
   big->rshift = rshift - 1;
   big->num_items = 0;
   big->buckets = buckets * 2;
   big->max_items = big->buckets * 4;
   /* Create a bigger hash table */
   big->table = (hlink **)malloc(big->buckets * sizeof(hlink *));
   bmemzero(big->table, big->buckets * sizeof(hlink *));
   big->walkptr = NULL;
   big->walk_index = 0;
   /* Insert all the items in the new hash table */
   Dmsg1(100, "Before copy num_items=%d\n", num_items);
   /*
    * We walk through the old smaller tree getting items,
    * but since we are overwriting the colision links, we must
    * explicitly save the item->next pointer and walk each
    * colision chain ourselves.  We do use next() for getting
    * to the next bucket.
    */
   for (void *item=first(); item; ) {
      void *ni = ((hlink *)((char *)item+loffset))->next;  /* save link overwritten by insert */
      hlink *hp = (hlink *)((char *)item+loffset);
      if (hp->is_ikey) {
         Dmsg1(100, "Grow insert: %lld\n", hp->key.ikey);
         big->insert(hp->key.ikey, item);
      } else {
         Dmsg1(100, "Grow insert: %s\n", hp->key.key);
         big->insert(hp->key.key, item);
      }
      if (ni) {
         item = (void *)((char *)ni-loffset);
      } else {
         walkptr = NULL;
         item = next();
      }
   }
   Dmsg1(100, "After copy new num_items=%d\n", big->num_items);
   if (num_items != big->num_items) {
      Dmsg0(000, "****** Big problems num_items mismatch ******\n");
   }
   free(table);
   memcpy(this, big, sizeof(htable));  /* move everything across */
   free(big);
   Dmsg0(100, "Exit grow.\n");
}

bool htable::insert(char *key, void *item)
{
   hlink *hp;
   if (lookup(key)) {
      return false;                   /* already exists */
   }
   ASSERT(index < buckets);
   Dmsg2(dbglvl, "Insert: hash=%p index=%d\n", hash, index);
   hp = (hlink *)(((char *)item)+loffset);
   Dmsg4(dbglvl, "Insert hp=%p index=%d item=%p offset=%u\n", hp,
      index, item, loffset);
   hp->next = table[index];
   hp->hash = hash;
   hp->key.key = key;
   hp->is_ikey = false;
   table[index] = hp;
   Dmsg3(dbglvl, "Insert hp->next=%p hp->hash=0x%x hp->key=%s\n",
      hp->next, hp->hash, hp->key.key);

   if (++num_items >= max_items) {
      Dmsg2(dbglvl, "num_items=%d max_items=%d\n", num_items, max_items);
      grow_table();
   }
   Dmsg3(dbglvl, "Leave insert index=%d num_items=%d key=%s\n", index, num_items, key);
   return true;
}

void *htable::lookup(char *key)
{
   hash_index(key);
   for (hlink *hp=table[index]; hp; hp=(hlink *)hp->next) {
//    Dmsg2(100, "hp=%p key=%s\n", hp, hp->key.key);
      if (hash == hp->hash && strcmp(key, hp->key.key) == 0) {
         Dmsg1(dbglvl, "lookup return %p\n", ((char *)hp)-loffset);
         return ((char *)hp)-loffset;
      }
   }
   return NULL;
}

bool htable::insert(uint64_t ikey, void *item)
{
   hlink *hp;
   if (lookup(ikey)) {
      return false;                  /* already exists */
   }
   ASSERT(index < buckets);
   Dmsg2(dbglvl, "Insert: hash=%p index=%d\n", hash, index);
   hp = (hlink *)(((char *)item)+loffset);
   Dmsg4(dbglvl, "Insert hp=%p index=%d item=%p offset=%u\n", hp, index,
     item, loffset);
   hp->next = table[index];
   hp->hash = hash;
   hp->key.ikey = ikey;
   hp->is_ikey = true;
   table[index] = hp;
   Dmsg3(dbglvl, "Insert hp->next=%p hp->hash=0x%x hp->ikey=%lld\n", hp->next,
      hp->hash, hp->key.ikey);

   if (++num_items >= max_items) {
      Dmsg2(dbglvl, "num_items=%d max_items=%d\n", num_items, max_items);
      grow_table();
   }
   Dmsg3(dbglvl, "Leave insert index=%d num_items=%d key=%lld\n",
      index, num_items, ikey);
   return true;
}

void *htable::lookup(uint64_t ikey)
{
   hash_index(ikey);
   for (hlink *hp=table[index]; hp; hp=(hlink *)hp->next) {
//    Dmsg2(100, "hp=%p key=%lld\n", hp, hp->key.ikey);
      if (hash == hp->hash && ikey == hp->key.ikey) {
         Dmsg1(dbglvl, "lookup return %p\n", ((char *)hp)-loffset);
         return ((char *)hp)-loffset;
      }
   }
   return NULL;
}

void *htable::next()
{
   Dmsg1(dbglvl, "Enter next: walkptr=%p\n", walkptr);
   if (walkptr) {
      walkptr = (hlink *)(walkptr->next);
   }
   while (!walkptr && walk_index < buckets) {
      walkptr = table[walk_index++];
      if (walkptr) {
         Dmsg3(dbglvl, "new walkptr=%p next=%p inx=%d\n", walkptr,
            walkptr->next, walk_index-1);
      }
   }
   if (walkptr) {
      Dmsg2(dbglvl, "next: rtn %p walk_index=%d\n",
         ((char *)walkptr)-loffset, walk_index);
      return ((char *)walkptr)-loffset;
   }
   Dmsg0(dbglvl, "next: return NULL\n");
   return NULL;
}

void *htable::first()
{
   Dmsg0(dbglvl, "Enter first\n");
   walkptr = table[0];                /* get first bucket */
   walk_index = 1;                    /* Point to next index */
   while (!walkptr && walk_index < buckets) {
      walkptr = table[walk_index++];  /* go to next bucket */
      if (walkptr) {
         Dmsg3(dbglvl, "first new walkptr=%p next=%p inx=%d\n", walkptr,
            walkptr->next, walk_index-1);
      }
   }
   if (walkptr) {
      Dmsg1(dbglvl, "Leave first walkptr=%p\n", walkptr);
      return ((char *)walkptr)-loffset;
   }
   Dmsg0(dbglvl, "Leave first walkptr=NULL\n");
   return NULL;
}

/* Destroy the table and its contents */
void htable::destroy()
{
#ifdef BIG_MALLOC
   hash_big_free();
#else
   void *ni;
   void *li = first();

   while (li) {
      ni = next();
      free(li);
      li=ni;
   }
#endif

   free(table);
   table = NULL;
   Dmsg0(100, "Done destroy.\n");
}

#ifndef TEST_PROGRAM
#define TEST_PROGRAM_A
#endif


#ifdef TEST_PROGRAM
#include "unittests.h"

struct MYJCR {
   char *key;
   hlink link;
};

#define NITEMS 5000000

int main()
{
   Unittests htable_test("htable_test");
   char mkey[30];
   htable *jcrtbl;
   MYJCR *save_jcr = NULL, *item;
   MYJCR *jcr = NULL;
   int count = 0;
   int i;
   int len;
   bool check_cont;

   Pmsg0(0, "Initialize tests ...\n");
   jcrtbl = (htable *)malloc(sizeof(htable));
   jcrtbl->init(jcr, &jcr->link, NITEMS);
   ok(jcrtbl && jcrtbl->size() == 0, "Default initialization");

   Pmsg1(0, "Inserting %d items\n", NITEMS);
   for (i = 0; i < NITEMS; i++) {
      len = sprintf(mkey, "This is htable item %d", i) + 1;

      jcr = (MYJCR *)jcrtbl->hash_malloc(sizeof(MYJCR));
      jcr->key = (char *)jcrtbl->hash_malloc(len);
      memcpy(jcr->key, mkey, len);
      Dmsg2(100, "link=%p jcr=%p\n", jcr->link, jcr);

      jcrtbl->insert(jcr->key, jcr);
      if (i == 10) {
         save_jcr = jcr;
      }
   }
   ok(jcrtbl->size() == NITEMS, "Checking size");
   item = (MYJCR *)jcrtbl->lookup(save_jcr->key);
   ok(item != NULL, "Checking saved key lookup");
   ok(item != NULL && strcmp(save_jcr->key, item->key) == 0, "Checking key");

   /* some stats for human to consider */
   jcrtbl->stats();

   Pmsg0(0, "Walk the hash table\n");
   check_cont = true;
   for (i = 0; i < NITEMS; i++) {
      sprintf(mkey, "This is htable item %d", i);
      item = (MYJCR *)jcrtbl->lookup(mkey);
      if (!item){
         check_cont = false;
      }
   }
   ok(check_cont, "Checking htable content");

   foreach_htable (jcr, jcrtbl) {
#ifndef BIG_MALLOC
      free(jcr->key);
#endif
      count++;
   }
   ok(count == NITEMS, "Checking number of items");
   printf("Calling destroy\n");
   jcrtbl->destroy();
   free(jcrtbl);

   return report();
}
#endif
