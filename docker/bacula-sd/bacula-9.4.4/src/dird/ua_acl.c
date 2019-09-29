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
 *   Bacula Director -- User Agent Access Control List (ACL) handling
 *
 *     Kern Sibbald, January MMIV
 */

#include "bacula.h"
#include "dird.h"

/*
 * Check if access is permitted to item in acl
 */
bool acl_access_ok(UAContext *ua, int acl, const char *item)
{
   return acl_access_ok(ua, acl, item, strlen(item));
}

bool acl_access_client_ok(UAContext *ua, const char *name, int32_t jobtype)
{
   if (acl_access_ok(ua, Client_ACL, name)) {
      return true;
   }
   if (jobtype == JT_BACKUP && acl_access_ok(ua, BackupClient_ACL, name)) {
      return true;
   }
   if (jobtype == JT_RESTORE && acl_access_ok(ua, RestoreClient_ACL, name)) {
      return true;
   }
   /* Some commands such as "status client" are for both Backup and Restore */
   if (jobtype == JT_BACKUP_RESTORE &&
       (acl_access_ok(ua, RestoreClient_ACL, name) ||
        acl_access_ok(ua, BackupClient_ACL, name)))
   {
      return true;
   }
   return false;
}



/* This version expects the length of the item which we must check. */
bool acl_access_ok(UAContext *ua, int acl, const char *item, int len)
{
   /* The resource name contains nasty characters */
   if (acl != Where_ACL && !is_name_valid(item, NULL)) {
      Dmsg1(1400, "Access denied for item=%s\n", item);
      return false;
   }

   /* If no console resource => default console and all is permitted */
   if (!ua || !ua->cons) {
      Dmsg0(1400, "Root cons access OK.\n");
      return true;                    /* No cons resource -> root console OK for everything */
   }

   alist *list = ua->cons->ACL_lists[acl];
   if (!list) {                       /* empty list */
      if (len == 0 && acl == Where_ACL) {
         return true;                 /* Empty list for Where => empty where */
      }
      return false;                   /* List empty, reject everything */
   }

   /* Special case *all* gives full access */
   if (list->size() == 1 && strcasecmp("*all*", (char *)list->get(0)) == 0) {
      return true;
   }

   /* Search list for item */
   for (int i=0; i<list->size(); i++) {
      if (strcasecmp(item, (char *)list->get(i)) == 0) {
         Dmsg3(1400, "ACL found %s in %d %s\n", item, acl, (char *)list->get(i));
         return true;
      }
   }
   return false;
}

/*
 * Return  true if we have a restriction on the ACL
 *        false if there is no ACL restriction
 */
bool have_restricted_acl(UAContext *ua, int acl)
{
   alist *list;

   /* If no console resource => default console and all is permitted */
   if (!ua || !ua->cons) {
      return false;       /* no restrictions */
   }

   list = ua->cons->ACL_lists[acl];
   if (!list) {
      return false;
   }
   /* Special case *all* gives full access */
   if (list->size() == 1 && strcasecmp("*all*", (char *)list->get(0)) == 0) {
      return false;
   }
   return list->size() > 0;
}
