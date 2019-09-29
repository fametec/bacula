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
 * Kern Sibbald, August 2007
 *
 * This file is pulled in by certain generic routines in libwin32
 *   to define the names of the daemon that is being built.
 */

#define APP_NAME "Bacula-dir"
#define LC_APP_NAME "bacula-dir"
#define APP_DESC "Bacula Director Service"

#define terminate_app(x) terminate_dird(x)
extern void terminate_dird(int sig);

#define VSSInit()
