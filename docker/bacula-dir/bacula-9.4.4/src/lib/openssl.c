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
 * openssl.c OpenSSL support functions
 *
 * Author: Landon Fuller <landonf@opendarwin.org>
 *
 * This file was contributed to the Bacula project by Landon Fuller.
 *
 * Landon Fuller has been granted a perpetual, worldwide, non-exclusive,
 * no-charge, royalty-free, irrevocable copyright license to reproduce,
 * prepare derivative works of, publicly display, publicly perform,
 * sublicense, and distribute the original work contributed by Landon Fuller
 * to the Bacula project in source or object form.
 *
 * If you wish to license these contributions under an alternate open source
 * license please contact Landon Fuller <landonf@opendarwin.org>.
 */


#include "bacula.h"
#include <assert.h>

#ifdef HAVE_OPENSSL

/* Are we initialized? */
static int crypto_initialized = false;
/*
 * ***FIXME*** this is a sort of dummy to avoid having to
 *   change all the existing code to pass either a jcr or
 *   a NULL.  Passing a NULL causes the messages to be
 *   printed by the daemon -- not very good :-(
 */
void openssl_post_errors(int code, const char *errstring)
{
   openssl_post_errors(NULL, code, errstring);
}

/*
 * Post all per-thread openssl errors
 */
void openssl_post_errors(JCR *jcr, int code, const char *errstring)
{
   char buf[512];
   unsigned long sslerr;

   /* Pop errors off of the per-thread queue */
   while((sslerr = ERR_get_error()) != 0) {
      /* Acquire the human readable string */
      ERR_error_string_n(sslerr, buf, sizeof(buf));
      Dmsg3(50, "jcr=%p %s: ERR=%s\n", jcr, errstring, buf);
      Qmsg2(jcr, M_ERROR, 0, "%s: ERR=%s\n", errstring, buf);
   }
}

#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || defined(LIBRESSL_VERSION_NUMBER)
/* Array of mutexes for use with OpenSSL static locking */
static pthread_mutex_t *mutexes;

/* OpenSSL dynamic locking structure */
struct CRYPTO_dynlock_value {
   pthread_mutex_t mutex;
};

/*
 * Return an OpenSSL thread ID
 *  Returns: thread ID
 *
 */
static unsigned long get_openssl_thread_id(void)
{
#ifdef HAVE_WIN32
   return (unsigned long)GetCurrentThreadId();
#else
   /*
    * Comparison without use of pthread_equal() is mandated by the OpenSSL API
    *
    * Note: this creates problems with the new Win32 pthreads
    *   emulation code, which defines pthread_t as a structure.
    */
   return ((unsigned long)pthread_self());
#endif
}

/*
 * Allocate a dynamic OpenSSL mutex
 */
static struct CRYPTO_dynlock_value *openssl_create_dynamic_mutex (const char *file, int line)
{
   struct CRYPTO_dynlock_value *dynlock;
   int stat;

   dynlock = (struct CRYPTO_dynlock_value *)malloc(sizeof(struct CRYPTO_dynlock_value));

   if ((stat = pthread_mutex_init(&dynlock->mutex, NULL)) != 0) {
      berrno be;
      Jmsg1(NULL, M_ABORT, 0, _("Unable to init mutex: ERR=%s\n"), be.bstrerror(stat));
   }

   return dynlock;
}

static void openssl_update_dynamic_mutex(int mode, struct CRYPTO_dynlock_value *dynlock, const char *file, int line)
{
   if (mode & CRYPTO_LOCK) {
      P(dynlock->mutex);
   } else {
      V(dynlock->mutex);
   }
}

static void openssl_destroy_dynamic_mutex(struct CRYPTO_dynlock_value *dynlock, const char *file, int line)
{
   int stat;

   if ((stat = pthread_mutex_destroy(&dynlock->mutex)) != 0) {
      berrno be;
      Jmsg1(NULL, M_ABORT, 0, _("Unable to destroy mutex: ERR=%s\n"), be.bstrerror(stat));
   }

   free(dynlock);
}

/*
 * (Un)Lock a static OpenSSL mutex
 */
static void openssl_update_static_mutex (int mode, int i, const char *file, int line)
{
   if (mode & CRYPTO_LOCK) {
      P(mutexes[i]);
   } else {
      V(mutexes[i]);
   }
}

/*
 * Initialize OpenSSL thread support
 *  Returns: 0 on success
 *           errno on failure
 */
static int openssl_init_threads (void)
{
   int i, numlocks;
   int stat;

   /* Set thread ID callback */
   CRYPTO_set_id_callback(get_openssl_thread_id);

   /* Initialize static locking */
   numlocks = CRYPTO_num_locks();
   mutexes = (pthread_mutex_t *) malloc(numlocks * sizeof(pthread_mutex_t));
   for (i = 0; i < numlocks; i++) {
      if ((stat = pthread_mutex_init(&mutexes[i], NULL)) != 0) {
         berrno be;
         Jmsg1(NULL, M_FATAL, 0, _("Unable to init mutex: ERR=%s\n"), be.bstrerror(stat));
         return stat;
      }
   }

   /* Set static locking callback */
   CRYPTO_set_locking_callback(openssl_update_static_mutex);

   /* Initialize dyanmic locking */
   CRYPTO_set_dynlock_create_callback(openssl_create_dynamic_mutex);
   CRYPTO_set_dynlock_lock_callback(openssl_update_dynamic_mutex);
   CRYPTO_set_dynlock_destroy_callback(openssl_destroy_dynamic_mutex);

   return 0;
}

/*
 * Clean up OpenSSL threading support
 */
static void openssl_cleanup_threads(void)
{
   int i, numlocks;
   int stat;

   /* Unset thread ID callback */
   CRYPTO_set_id_callback(NULL);

   /* Deallocate static lock mutexes */
   numlocks = CRYPTO_num_locks();
   for (i = 0; i < numlocks; i++) {
      if ((stat = pthread_mutex_destroy(&mutexes[i])) != 0) {
         berrno be;
         /* We don't halt execution, reporting the error should be sufficient */
         Jmsg1(NULL, M_ERROR, 0, _("Unable to destroy mutex: ERR=%s\n"),
               be.bstrerror(stat));
      }
   }

   /* Unset static locking callback */
   CRYPTO_set_locking_callback(NULL);

   /* Free static lock array */
   free(mutexes);

   /* Unset dynamic locking callbacks */
   CRYPTO_set_dynlock_create_callback(NULL);
   CRYPTO_set_dynlock_lock_callback(NULL);
   CRYPTO_set_dynlock_destroy_callback(NULL);
}

#endif

/*
 * Seed OpenSSL PRNG
 *  Returns: 1 on success
 *           0 on failure
 */
static int openssl_seed_prng (void)
{
   const char *names[]  = { "/dev/urandom", "/dev/random", NULL };
   int i;

   // ***FIXME***
   // Win32 Support
   // Read saved entropy?

   for (i = 0; names[i]; i++) {
      if (RAND_load_file(names[i], 1024) != -1) {
         /* Success */
         return 1;
      }
   }

   /* Fail */
   return 0;
}

/*
 * Save OpenSSL Entropy
 *  Returns: 1 on success
 *           0 on failure
 */
static int openssl_save_prng (void)
{
   // ***FIXME***
   // Implement PRNG state save
   return 1;
}

/*
 * Perform global initialization of OpenSSL
 * This function is not thread safe.
 *  Returns: 0 on success
 *           errno on failure
 */
int init_crypto (void)
{
   int stat = 0;

#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || defined(LIBRESSL_VERSION_NUMBER)
   if ((stat = openssl_init_threads()) != 0) {
      berrno be;
      Jmsg1(NULL, M_ABORT, 0,
        _("Unable to init OpenSSL threading: ERR=%s\n"), be.bstrerror(stat));
   }

   /* Load libssl and libcrypto human-readable error strings */
   SSL_load_error_strings();

   /* Initialize OpenSSL SSL  library */
   SSL_library_init();

   /* Register OpenSSL ciphers and digests */
   OpenSSL_add_all_algorithms();
#endif

   if (!openssl_seed_prng()) {
      Jmsg0(NULL, M_ERROR_TERM, 0, _("Failed to seed OpenSSL PRNG\n"));
   }

   crypto_initialized = true;

   return stat;
}

/*
 * Perform global cleanup of OpenSSL
 * All cryptographic operations must be completed before calling this function.
 * This function is not thread safe.
 *  Returns: 0 on success
 *           errno on failure
 */
int cleanup_crypto (void)
{
   /*
    * Ensure that we've actually been initialized; Doing this here decreases the
    * complexity of client's termination/cleanup code.
    */
   if (!crypto_initialized) {
      return 0;
   }

   if (!openssl_save_prng()) {
      Jmsg0(NULL, M_ERROR, 0, _("Failed to save OpenSSL PRNG\n"));
   }

#if (OPENSSL_VERSION_NUMBER < 0x10100000L) || defined(LIBRESSL_VERSION_NUMBER)
   openssl_cleanup_threads();

   /* Free libssl and libcrypto error strings */
   ERR_free_strings();

   /* Free all ciphers and digests */
   EVP_cleanup();

   /* Free memory used by PRNG */
   RAND_cleanup();
#endif

   crypto_initialized = false;

   return 0;
}

#else

/* Dummy routines */
int init_crypto (void) { return 0; }
int cleanup_crypto (void) { return 0; }

#endif /* HAVE_OPENSSL */
