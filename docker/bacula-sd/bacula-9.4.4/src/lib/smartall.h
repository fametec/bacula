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

        Definitions for the smart memory allocator

*/

#ifndef SMARTALLOC_H
#define SMARTALLOC_H

extern uint64_t DLL_IMP_EXP sm_max_bytes;
extern uint64_t DLL_IMP_EXP sm_bytes;
extern uint32_t DLL_IMP_EXP sm_max_buffers;
extern uint32_t DLL_IMP_EXP sm_buffers;

#ifdef  SMARTALLOC
#undef  SMARTALLOC
#define SMARTALLOC SMARTALLOC


/* Avoid aggressive GCC optimization */
extern void *bmemset(void *s, int c, size_t n);

extern void *sm_malloc(const char *fname, int lineno, unsigned int nbytes),
            *sm_calloc(const char *fname, int lineno,
                unsigned int nelem, unsigned int elsize),
            *sm_realloc(const char *fname, int lineno, void *ptr, unsigned int size),
            *actuallymalloc(unsigned int size),
            *actuallycalloc(unsigned int nelem, unsigned int elsize),
            *actuallyrealloc(void *ptr, unsigned int size);
extern void sm_free(const char *fname, int lineno, void *fp);
extern void actuallyfree(void *cp),
            sm_dump(bool bufdump, bool in_use=false), sm_static(int mode);
extern void sm_new_owner(const char *fname, int lineno, char *buf);
extern void sm_get_owner(int64_t dbglvl, char *buf);
#ifdef SMCHECK
#define Dsm_check(lvl) if ((lvl)<=debug_level) sm_check(__FILE__, __LINE__, true)
extern void sm_check(const char *fname, int lineno, bool bufdump);
extern int sm_check_rtn(const char *fname, int lineno, bool bufdump);
#else
#define Dsm_check(lvl)
#define sm_check(f, l, fl)
#define sm_check_rtn(f, l, fl) 1
#endif


/* Redefine standard memory allocator calls to use our routines
   instead. */

#define free(x)        sm_free(__FILE__, __LINE__, (void *)(x))
#define cfree(x)       sm_free(__FILE__, __LINE__, (void *)(x))
#define malloc(x)      sm_malloc(__FILE__, __LINE__, (x))
#define calloc(n,e)    sm_calloc(__FILE__, __LINE__, (n), (e))
#define realloc(p,x)   sm_realloc(__FILE__, __LINE__, (p), (x))

#else

/* If SMARTALLOC is disabled, define its special calls to default to
   the standard routines.  */

#define actuallyfree(x)      free(x)
#define actuallymalloc(x)    malloc(x)
#define actuallycalloc(x,y)  calloc(x,y)
#define actuallyrealloc(x,y) realloc(x,y)
inline void sm_dump(int x, int y=0) {} /* with default arguments, we can't use a #define */
#define sm_static(x)
#define sm_new_owner(a, b, c)
#define sm_get_owner(a,b)
#define sm_malloc(f, l, n)     malloc(n)
#define sm_free(f, l, n)       free(n)
#define sm_check(f, l, fl)
#define sm_check_rtn(f, l, fl) 1

extern void *b_malloc(const char *file, int line, size_t size);
#define malloc(x) b_malloc(__FILE__, __LINE__, (x))

#define Dsm_check(lvl)
#define sm_check(f, l, fl)
#define sm_check_rtn(f, l, fl) 1

#endif

#ifdef SMARTALLOC

#define New(type) new(__FILE__, __LINE__) type

/* We do memset(0) because it's not possible to memset a class when
 * using subclass with virtual functions
 */

class SMARTALLOC
{
public:

void *operator new(size_t s, const char *fname, int line)
{
   size_t size =  s > sizeof(int) ? (unsigned int)s : sizeof(int);
   void *p = sm_malloc(fname, line, size);
   return bmemset(p, 0, size);   /* return memset() result to avoid GCC 6.1 issue */
}
void *operator new[](size_t s, const char *fname, int line)
{
   size_t size =  s > sizeof(int) ? (unsigned int)s : sizeof(int);
   void *p = sm_malloc(fname, line, size);
   return bmemset(p, 0, size);  /* return memset() result to avoid GCC 6.1 issue */
}

void  operator delete(void *ptr)
{
   free(ptr);
}
void  operator delete[](void *ptr, size_t /*i*/)
{
   free(ptr);
}

void  operator delete(void *ptr, const char * /*fname*/, int /*line*/)
{
   free(ptr);
}
void  operator delete[](void *ptr, size_t /*i*/,
                        const char * /*fname*/, int /*line*/)
{
   free(ptr);
}

private:
void *operator new(size_t s) throw() { (void)s; return 0; }
void *operator new[](size_t s) throw() { (void)s; return 0; }
};

#else

#define New(type) new type

class SMARTALLOC
{
   public:
      void *operator new(size_t s) {
         void *p = malloc(s);
         bmemset(p, 0, s);
         return p;
      }
      void *operator new[](size_t s) {
         void *p = malloc(s);
         bmemset(p, 0, s);
         return p;
      }
      void  operator delete(void *ptr) {
          free(ptr);
      }
      void  operator delete[](void *ptr, size_t i) {
          free(ptr);
      }
};
#endif  /* SMARTALLOC */
#endif  /* !SMARTALLOC_H */
