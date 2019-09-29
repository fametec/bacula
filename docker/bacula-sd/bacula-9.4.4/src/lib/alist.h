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
 *  Kern Sibbald, June MMIII
 */


extern bool is_null(const void *ptr);

/*
 * There is a lot of extra casting here to work around the fact
 * that some compilers (Sun and Visual C++) do not accept
 * (void *) as an lvalue on the left side of an equal.
 *
 * Loop var through each member of list
 */
#ifdef HAVE_TYPEOF
#define foreach_alist(var, list) \
        for((var)=(typeof(var))(list)->first(); (var); (var)=(typeof(var))(list)->next() )
#else
#define foreach_alist(var, list) \
    for((*((void **)&(var))=(void*)((list)->first())); \
         (var); \
         (*((void **)&(var))=(void*)((list)->next())))
#endif

#ifdef HAVE_TYPEOF
#define foreach_alist_index(inx, var, list) \
        for(inx=0; ((var)=(typeof(var))(list)->get(inx)); inx++ )
#else
#define foreach_alist_index(inx, var, list) \
    for(inx=0; ((*((void **)&(var))=(void*)((list)->get(inx)))); inx++ )
#endif




/* Second arg of init */
enum {
  owned_by_alist = true,
  not_owned_by_alist = false
};

/*
 * Array list -- much like a simplified STL vector
 *   array of pointers to inserted items. baselist is
 *   the common code between alist and ilist
 */
class baselist : public SMARTALLOC {
protected:
   void **items;                /* from  0..n-1 */
   int num_items;               /* from  1..n   */
   int last_item;               /* maximum item index (1..n) */
   int max_items;               /* maximum possible items (array size) (1..n) */
   int num_grow;
   int cur_item;                /* from 1..n */
   bool own_items;
   void grow_list(void);
   void *remove_item(int index);
public:
   baselist(int num = 100, bool own=true);
   ~baselist();
   void init(int num = 100, bool own=true);
   void append(void *item);
   void *get(int index);
   bool empty() const;
   int last_index() const { return last_item; };
   int max_size() const { return max_items; };
   void * operator [](int index) const;
   int current() const { return cur_item; };
   int size() const;
   void destroy();
   void grow(int num);

   /* Use it as a stack, pushing and poping from the end */
   void push(void *item) { append(item); };
   void *pop() { return remove_item(num_items-1); };
};

class alist: public baselist
{
public:
   alist(int num = 100, bool own=true): baselist(num, own) {};
   void *prev();
   void *next();
   void *last();
   void *first();
   void prepend(void *item);
   void *remove(int index) { return remove_item(index);};
};

/*
 * Indexed list -- much like a simplified STL vector
 *   array of pointers to inserted items
 */
class ilist : public baselist {
public:
   ilist(int num = 100, bool own=true): baselist(num, own) {};
    /* put() is not compatible with remove(), prepend() or foreach_alist */
   void put(int index, void *item);
};

/*
 * Define index operator []
 */
inline void * baselist::operator [](int index) const {
   if (index < 0 || index >= max_items) {
      return NULL;
   }
   return items[index];
}

inline bool baselist::empty() const
{
   return num_items == 0;
}

/*
 * This allows us to do explicit initialization,
 *   allowing us to mix C++ classes inside malloc'ed
 *   C structures. Define before called in constructor.
 */
inline void baselist::init(int num, bool own)
{
   items = NULL;
   num_items = 0;
   last_item = 0;
   max_items = 0;
   num_grow = num;
   own_items = own;
}

/* Constructor */
inline baselist::baselist(int num, bool own)
{
   init(num, own);
}

/* Destructor */
inline baselist::~baselist()
{
   destroy();
}

/* Current size of list */
inline int baselist::size() const
{
   if (is_null(this)) return 0;
   return num_items;
}

/* How much to grow by each time */
inline void baselist::grow(int num)
{
   num_grow = num;
}
