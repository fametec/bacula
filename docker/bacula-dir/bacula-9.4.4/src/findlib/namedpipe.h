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
   Basic abstraction for named pipe between windows and linux
 */

#ifndef NAMEDPIPE_H
#define NAMEDPIPE_H

#ifdef HAVE_WIN32
#include <windows.h>
#endif

#include <sys/types.h>

typedef struct {
#ifdef HAVE_WIN32
   HANDLE    fd;
#else
   char     *name;
   int       fd;
#endif
   int       ifd;
} NamedPipe;


void namedpipe_init(NamedPipe *self);
void namedpipe_free(NamedPipe *self);
int namedpipe_create(NamedPipe *self, const char *path, mode_t mode);
int namedpipe_open(NamedPipe *self, const char *path, mode_t mode);

#endif
