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
 * Routines for writing to the Cloud using S3 protocol.
 *
 * Written by Kern Sibbald, May MMXVI
 */

#ifndef _S3_DRV_H
#define _S3_DRV_H

#include "bacula.h"
#include "stored.h"

#ifdef HAVE_LIBS3
#include <libs3.h>
#include "cloud_driver.h"   /* get base class definitions */

class s3_driver: public cloud_driver {
private:
   S3BucketContext s3ctx;       /* Main S3 bucket context */
   uint32_t buf_len;

public:
   cloud_dev *dev;              /* device that is calling us */
   DEVRES *device;
   DCR *dcr;                    /* dcr set during calls to S3_xxx */
   CLOUD *cloud;                /* Pointer to CLOUD resource */

   s3_driver() {
   };
   ~s3_driver() {
   };

   void make_cloud_filename(POOLMEM *&filename, const char *VolumeName, uint32_t part);
   bool init(JCR *jcr, cloud_dev *dev, DEVRES *device);
   bool start_of_job(DCR *dcr);
   bool term(DCR *dcr);
   bool end_of_job(DCR *dcr);
   bool truncate_cloud_volume(DCR *dcr, const char *VolumeName, ilist *trunc_parts, POOLMEM *&err);
   bool copy_cache_part_to_cloud(transfer *xfer);
   bool copy_cloud_part_to_cache(transfer *xfer);
   bool get_cloud_volume_parts_list(DCR *dcr, const char* VolumeName, ilist *parts, POOLMEM *&err);
   bool get_cloud_volumes_list(DCR* dcr, alist *volumes, POOLMEM *&err);
   S3Status put_object(transfer *xfer, const char *cache_fname, const char *cloud_fname);
   bool retry_put_object(S3Status status);
   bool get_cloud_object(transfer *xfer, const char *cloud_fname, const char *cache_fname);
};

#endif  /* HAVE_LIBS3 */
#endif /* _S3_DRV_H */
