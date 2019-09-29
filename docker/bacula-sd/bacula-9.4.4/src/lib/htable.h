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
 * Written by Kern Sibbald, MMIV
 */

#ifndef _HTABLE_H_
#define _HTABLE_H_

/*
 *
 * Written by Kern Sibbald, MMIV
 *
 */

/* ========================================================================
 *
 *   Hash table class -- htable
 *
 */

/* 
 * BIG_MALLOC is to provide a large malloc service to htable
 */
#define BIG_MALLOC

/*
 * Loop var through each member of table
 */
#ifdef HAVE_TYPEOF
#define foreach_htable(var, tbl) \
        for((var)=(typeof(var))((tbl)->first()); \
           (var); \
           (var)=(typeof(var))((tbl)->next()))
#else
#define foreach_htable(var, tbl) \
        for((*((void **)&(var))=(void *)((tbl)->first())); \
            (var); \
            (*((void **)&(var))=(void *)((tbl)->next())))
#endif

union key_val {
   char *key;                   /* char key */
   uint64_t ikey;               /* integer key */
}; 

struct hlink {
   void *next;                        /* next hash item */
   uint64_t hash;                     /* 64 bit hash for this key */
   union key_val key;                 /* key value this key */
   bool is_ikey;                      /* set if integer key */
};

struct h_mem {
   struct h_mem *next;                /* next buffer */
   char   *mem;                       /* memory pointer */
   int64_t rem;                       /* remaining bytes in big_buffer */
   char first[1];                     /* first byte */
};

class htable : public SMARTALLOC {
   hlink **table;                     /* hash table */
   uint64_t hash;                     /* temp storage */
   uint64_t total_size;               /* total bytes malloced */
   int loffset;                       /* link offset in item */
   uint32_t num_items;                /* current number of items */
   uint32_t max_items;                /* maximum items before growing */
   uint32_t buckets;                  /* size of hash table */
   uint32_t index;                    /* temp storage */
   uint32_t mask;                     /* "remainder" mask */
   uint32_t rshift;                   /* amount to shift down */
   hlink *walkptr;                    /* table walk pointer */
   uint32_t walk_index;               /* table walk index */
   uint32_t blocks;                   /* blocks malloced */
#ifdef BIG_MALLOC
   struct h_mem *mem_block;           /* malloc'ed memory block chain */
   void malloc_big_buf(int size);     /* Get a big buffer */
#endif
   void hash_index(char *key);        /* produce hash key,index */
   void hash_index(uint64_t ikey);    /* produce hash key,index */
   void grow_table();                 /* grow the table */

public:
   htable(void *item, void *link, int tsize = 31);
   ~htable() { destroy(); }
   void  init(void *item, void *link, int tsize = 31);
   bool  insert(char *key, void *item);      /* char key */
   bool  insert(uint64_t ikey, void *item);  /* 64 bit key */
   void *lookup(char *key);                  /* char key */
   void *lookup(uint64_t ikey);              /* 64 bit key */
   void *first();                     /* get first item in table */
   void *next();                      /* get next item in table */
   void  destroy();
   void  stats();                     /* print stats about the table */
   uint32_t size();                   /* return size of table */
   char *hash_malloc(int size);       /* malloc bytes for a hash entry */
#ifdef BIG_MALLOC
   void hash_big_free();              /* free all hash allocated big buffers */
#endif
};

#endif
