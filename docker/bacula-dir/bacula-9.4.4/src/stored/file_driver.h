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

#ifndef _FILE_DRV_H
#define _FILE_DRV_H

#include "bacula.h"
#include "stored.h"
#include "cloud_driver.h"   /* get base class definitions */

class file_driver: public cloud_driver {
public:
   cloud_dev *dev;              /* device that is calling us */
   DEVRES *device;
   CLOUD *cloud;                /* Pointer to CLOUD resource */
   alist *objects;
   uint32_t buf_len;


   /* Stuff directly from Cloud resource */
   char *hostName;
   char *bucketName;
   char *accessKeyId;
   char *secretAccessKey;
   int32_t protocol;
   int32_t uriStyle;


private:
   void make_cloud_filename(POOLMEM *&filename, const char *VolumeName, uint32_t part);
   bool init(JCR *jcr, cloud_dev *dev, DEVRES *device);
   bool start_of_job(DCR *dcr);
   bool end_of_job(DCR *dcr);
   bool term(DCR *dcr);
   bool truncate_cloud_volume(DCR *dcr, const char *VolumeName, ilist *trunc_parts, POOLMEM *&err);
   bool copy_cache_part_to_cloud(transfer *xfer);
   bool copy_cloud_part_to_cache(transfer *xfer);
   bool get_cloud_volume_parts_list(DCR *dcr, const char* VolumeName, ilist *parts, POOLMEM *&err);
   bool get_cloud_volumes_list(DCR* dcr, alist *volumes, POOLMEM *&err);

   bool put_object(transfer *xfer, const char *cache_fname, const char *cloud_fname, bwlimit *limit);
   bool get_cloud_object(transfer *xfer, const char *cloud_fname, const char *cache_fname);

public:
   file_driver() {
   };
   ~file_driver() {
   };
};

#endif /* _FILE_DRV_H */
