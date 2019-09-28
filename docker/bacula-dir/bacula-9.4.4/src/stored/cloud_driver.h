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
 * Routines for writing Cloud drivers
 *
 * Written by Kern Sibbald, May MMXVI
 */

#include "bacula.h"
#include "stored.h"
#include "cloud_parts.h"
#include "cloud_transfer_mgr.h"
#include "lib/bwlimit.h"

#ifndef _CLOUD_DRIVER_H_
#define _CLOUD_DRIVER_H_

#define NUM_UPLOAD_RETRIES 2
class cloud_dev;

enum {
   C_S3_DRIVER    = 1,
   C_FILE_DRIVER  = 2
};

/* Abstract class cannot be instantiated */
class cloud_driver: public SMARTALLOC {
public:
   cloud_driver() : max_upload_retries(NUM_UPLOAD_RETRIES) {};
   virtual ~cloud_driver() {};

   virtual bool copy_cache_part_to_cloud(transfer *xfer) = 0;
   virtual bool copy_cloud_part_to_cache(transfer *xfer) = 0;
   virtual bool truncate_cloud_volume(DCR *dcr, const char *VolumeName, ilist *trunc_parts, POOLMEM *&err) = 0;
   virtual bool init(JCR *jcr, cloud_dev *dev, DEVRES *device) = 0;
   virtual bool term(DCR *dcr) = 0;
   virtual bool start_of_job(DCR *dcr) = 0;
   virtual bool end_of_job(DCR *dcr) = 0;

   virtual bool get_cloud_volume_parts_list(DCR *dcr, const char* VolumeName, ilist *parts, POOLMEM *&err) = 0;
   virtual bool get_cloud_volumes_list(DCR* dcr, alist *volumes, POOLMEM *&err) = 0; /* TODO: Adapt the prototype to have a handler instead */

   bwlimit upload_limit;
   bwlimit download_limit;
   uint32_t max_upload_retries;
};

#endif /* _CLOUD_DRIVER_H_ */
