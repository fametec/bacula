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
 * Support routines for Unit Tests.
 */

#ifndef _UNITTESTS_H_
#define _UNITTESTS_H_

// Test success if value x is not zero
#define ok(x, label) _ok(__FILE__, __LINE__, #x, (x), label)
// Test success if value x is zero
#define nok(x, label) _nok(__FILE__, __LINE__, #x, (x), label)

/* TODO: log() ported from BEE it should be updated. */
#ifdef RTEST_LOG_THREADID
#define log(format, ...)  do { \
   pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); \
   printf("%p: " format "\n", (void *)pthread_self(),  ##__VA_ARGS__ ); \
   pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);                 \
 } while (0)
#else
#define log(format, ...)  do { \
   printf("\n------------------------------------------\n" format "\n------------------------------------------\n", ##__VA_ARGS__ ); \
 } while (0)
#endif

void _ok(const char *file, int l, const char *op, int value, const char *label);
void _nok(const char *file, int l, const char *op, int value, const char *label);
int report();
void terminate(int sig);
void prolog(const char *name, bool lmgr=false, bool motd=true);
void epilog();

/* The class based approach for C++ geeks */
class Unittests
{
public:
   Unittests(const char *name, bool lmgr=false, bool motd=true)
   {
      prolog(name, lmgr, motd);
   };
   ~Unittests() { epilog(); };
};

#endif /* _UNITTESTS_H_ */