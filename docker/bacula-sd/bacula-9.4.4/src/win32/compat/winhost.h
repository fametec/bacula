/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2019 Kern Sibbald

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
 * Define Host machine
 */


#include "host.h"
#undef HOST_OS
#undef DISTNAME
#undef DISTVER

#ifdef HAVE_MINGW
extern char win_os[];
#define HOST_OS  "Linux"
#define DISTNAME "Cross-compile"
#ifndef BACULA
#define BACULA "Bacula"
#endif
#ifdef _WIN64
# define DISTVER "Win64"
#else
# define DISTVER "Win32"
#endif

#else

extern DLL_IMP_EXP char WIN_VERSION_LONG[];
extern DLL_IMP_EXP char WIN_VERSION[];

#define HOST_OS  WIN_VERSION_LONG
#define DISTNAME "MVS"
#define DISTVER  WIN_VERSION

#endif
