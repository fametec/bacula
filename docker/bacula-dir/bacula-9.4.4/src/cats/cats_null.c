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
 * Null Bacula backend function replaced with the correct one
 *  at install time.
 */ 
 
#include  "bacula.h"
#include  "cats.h"
 
BDB *db_init_database(JCR *jcr, const char *db_driver, const char *db_name,
         const char *db_user, const char *db_password, const char *db_address,
         int db_port, const char *db_socket,
         const char *db_ssl_mode, const char *db_ssl_key,
         const char *db_ssl_cert, const char *db_ssl_ca,
         const char *db_ssl_capath, const char *db_ssl_cipher,
         bool mult_db_connections, bool disable_batch_insert)
{ 
   Jmsg(jcr, M_FATAL, 0, _("Please replace this null libbaccats library with a proper one.\n"));
   return NULL; 
} 
