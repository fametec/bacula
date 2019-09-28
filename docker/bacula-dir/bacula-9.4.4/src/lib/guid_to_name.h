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
 * Written by Kern Sibbald, July 2007 to replace idcache.c
 *
 *  Program to convert uid and gid into names, and cache the results
 *   for preformance reasons.
 */

class guid_list {
public:
   dlist *uid_list;
   dlist *gid_list;

   char *uid_to_name(uid_t uid, char *name, int maxlen);
   char *gid_to_name(gid_t gid, char *name, int maxlen);
};

guid_list *new_guid_list();
void free_guid_list(guid_list *list);
