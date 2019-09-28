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
 * tls.h TLS support functions
 *
 * Author: Landon Fuller <landonf@threerings.net>
 *
 * This file was contributed to the Bacula project by Landon Fuller
 * and Three Rings Design, Inc.
 *
 * Three Rings Design, Inc. has been granted a perpetual, worldwide,
 * non-exclusive, no-charge, royalty-free, irrevocable copyright
 * license to reproduce, prepare derivative works of, publicly
 * display, publicly perform, sublicense, and distribute the original
 * work contributed by Three Rings Design, Inc. and its employees to
 * the Bacula project in source or object form.
 *
 * If you wish to license contributions from Three Rings Design, Inc,
 * under an alternate open source license please contact
 * Landon Fuller <landonf@threerings.net>.
 */

#ifndef __TLS_H_
#define __TLS_H_

/*
 * Opaque TLS Context Structure.
 * New TLS Connections are manufactured from this context.
 */
typedef struct TLS_Context TLS_CONTEXT;

/* Opaque TLS Connection Structure */
typedef struct TLS_Connection TLS_CONNECTION;

#endif /* __TLS_H_ */
