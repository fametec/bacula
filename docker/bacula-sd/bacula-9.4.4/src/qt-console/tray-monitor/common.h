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

#ifndef TRAY_MONITOR_COMMON_H
#define TRAY_MONITOR_COMMON_H

#if defined(HAVE_WIN32)
#if !defined(_STAT_H)
#define _STAT_H       /* don't pull in MinGW stat.h */
#endif
#ifndef _STAT_DEFINED 
#define _STAT_DEFINED /* don't pull in MinGW stat.h */
#endif
#endif

#if defined(HAVE_WIN32)
#if defined(HAVE_MINGW)
#include "mingwconfig.h"
#else
#include "winconfig.h"
#endif
#else
#include "config.h"
#endif
#define __CONFIG_H


#if QT_VERSION >= 0x050000
#include <QtWidgets>
#else
#include <QtGui>
#endif
#include <QtCore>

#include "bacula.h"

class DbgLine
{
public:
   const char *funct;
   DbgLine(const char *fun): funct(fun) {
      Dmsg2(0, "[%p] -> %s\n", bthread_get_thread_id(), funct);
   }
   ~DbgLine() {
      Dmsg2(0, "[%p] <- %s\n", bthread_get_thread_id(), funct);
   }
};

#ifdef Enter
#undef Enter
#endif
//#define Enter()    DbgLine _enter(__PRETTY_FUNCTION__)
#define Enter()

#endif
