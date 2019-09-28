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
 *   Generic base 64 input and output routines
 *
 *    Written by Kern E. Sibbald, March MM.
 */

/* Maximum size of len bytes after base64 encoding */
#define BASE64_SIZE(len) ((4 * len + 2) / 3 + 1)

// #define BASE64_SIZE(len) (((len + 3 - (len % 3)) / 3) * 4)
