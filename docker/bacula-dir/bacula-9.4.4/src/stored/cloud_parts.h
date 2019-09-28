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
 * Routines for managing Volumes contains and comparing parts
 *
 * Written by Norbert Bizet, May MMXVI
 */
#ifndef _CLOUD_PARTS_H_
#define _CLOUD_PARTS_H_

#include "bacula.h"
#include "stored.h"


struct cloud_part
{
   uint32_t    index;
   utime_t     mtime;
   uint64_t    size;
};

/* equality operators for cloud_part structure */
bool operator==(const cloud_part& lhs, const cloud_part& rhs);
bool operator!=(const cloud_part& lhs, const cloud_part& rhs);
/* more equality operators: when compared to int, we match only index */
bool operator==(const cloud_part& lhs, const uint32_t& rhs);
bool operator!=(const cloud_part& lhs, const uint32_t& rhs);

/* Check if a part p is contained in a parts list */
bool list_contains_part(ilist *parts, cloud_part *p);
/* Check if a part index is contained in a parts list */
bool list_contains_part(ilist *parts, uint32_t part_idx);
/* if parts1 and parts2 are synced, return true. false otherwise */
bool identical_lists(ilist *parts1, ilist *parts2);
/* cloud_parts present in source but not in dest are appended to diff.
 * there's no cloud_part copy made.
 * Diff only holds references and shoudn't own them */
bool diff_lists(ilist *source, ilist *dest, ilist *diff);


/* A proxy view of the cloud, providing existing parts
 * index/size/date of modification without accessing the cloud itself.
 * The basic proxy structure is a hash table of ilists:
  root
   |
   -[volume001]-----ilist
   |                   |
   |                 [01]-->cloud_part
   |                 [03]-->cloud_part
   |                   |
   |
   -[volume002]-----ilist
   |                   |
   |                 [01]-->cloud_part
                     [02]-->cloud_part
                       |
 */
class cloud_proxy : public SMARTALLOC
{
private:
   htable *m_hash;               /* the root htable */
   bool m_owns;                  /* determines if ilist own the cloud_parts */
   pthread_mutex_t m_mutex;      /* protect access*/   
   static cloud_proxy *m_pinstance; /* singleton instance */
   static uint64_t m_count;      /* static refcount */
   
   ~cloud_proxy();

public:
   /* size: the default hash size
    * owns: determines if the ilists own the cloud_parts or not */
   cloud_proxy(uint32_t size=100, bool owns=true);

   /* each time a part is added to the cloud, the corresponding cloud_part
    * should be set here */
   /* either using a part ptr (part can be disposed afterward)... */
   bool set(const char *volume, cloud_part *part);
   /* ...or by passing basic part parameters (part is constructed internally) */
   bool set(const char *volume, uint32_t index, utime_t mtime, uint64_t size);

   /* one can retrieve the proxied cloud_part using the get method */
   cloud_part *get(const char *volume, uint32_t part_idx);
   /* direct access to part size */
   uint64_t get_size(const char *volume, uint32_t part_idx);

   /* Check if the volume entry exists and return true if it's the case */
   bool volume_lookup(const char *volume);
   
   /* reset the volume list content with the content of part_list */
   bool reset(const char *volume, ilist *part_list);

   /* get the current last (max) index for a given volume */
   uint32_t last_index(const char *volume);

   /* returns a ilist of elements present in the proxy but not in the exclusion list */
   ilist *exclude(const char* volume, ilist *exclusion_lst);

   /* refcounted singleton */
   static cloud_proxy *get_instance();
   /* instead of deleting, release the cloud_proxy*/
   void release();

   void dump();
};

#endif /* _CLOUD_PARTS_H_ */
