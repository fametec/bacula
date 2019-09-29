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
 * NULL driver -- normally only used for performance testing
 */

#ifndef __NULL_DEV_
#define __NULL_DEV_

class null_dev : public file_dev {
public:

   null_dev() { };
   ~null_dev() { };
   const char *print_type();
};

#endif /* __NULL_DEV_ */
