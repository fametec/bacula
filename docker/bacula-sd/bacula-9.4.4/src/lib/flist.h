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
 *  Kern Sibbald, August 2014.
 */

/* Second arg of init */
enum {
  owned_by_flist = true,
  not_owned_by_flist = false
};

/*
 * Fifo list -- derived from alist
 */
class flist : public SMARTALLOC {
   void **items;
   int num_items;
   int max_items;
   int add_item;
   int get_item;
   bool own_items;
public:
   flist(int num = 10, bool own=false);
   ~flist();
   void init(int num = 10, bool own=false);
   bool queue(void *item);
   void *dequeue();
   bool empty() const;
   bool full() const;
   int size() const;
   void destroy();
};

inline bool flist::empty() const
{
   return num_items == 0;
}

inline bool flist::full() const
{
   return num_items == max_items;
}

/*
 * This allows us to do explicit initialization,
 *   allowing us to mix C++ classes inside malloc'ed
 *   C structures. Define before called in constructor.
 */
inline void flist::init(int num, bool own)
{
   items = NULL;
   num_items = 0;
   max_items = num;
   own_items = own;
   add_item = 0;
   get_item = 0;
   items = (void **)malloc(max_items * sizeof(void *));
}

/* Constructor */
inline flist::flist(int num, bool own)
{
   init(num, own);
}

/* Destructor */
inline flist::~flist()
{
   destroy();
}



/* Current size of list */
inline int flist::size() const
{
   return num_items;
}
