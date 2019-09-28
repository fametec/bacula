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
 * Routines for writing to a file from the Cloud device.
 *
 *  This is for testing purposes only.
 #
 *  NOTE!!! This cloud driver is not compatible with
 *   any disk-changer script for changing Volumes.
 *   It does however work with Bacula Virtual autochangers.
 *
 * Written by Kern Sibbald, May MMXVI
 *
 */

#include "file_driver.h"

static const int dbglvl = 100;

#include <fcntl.h>

/* Imported functions */
const char *mode_to_str(int mode);
int breaddir(DIR *dirp, POOLMEM *&dname);

/* Forward referenced functions */

/* Const and Static definitions */

/*
 * Put a cache object into the cloud (i.e. local disk)
 *  or visa-versa.
 */
bool file_driver::put_object(transfer *xfer, const char *in_fname, const char *out_fname, bwlimit *limit)
{
   struct stat statbuf;
   char *p, *f;
   char save_separator;
   ssize_t rbytes, wbytes;
   uint32_t read_len;
   int64_t obj_len;
   FILE *infile=NULL, *outfile=NULL;
   POOLMEM *buf = get_memory(buf_len);

   Enter(dbglvl);
   Dmsg2(dbglvl, "Put from: %s to %s\n", in_fname, out_fname);

   /*
    * First work on output file
    */
   /* Split out_fname into path + file */
   for (p=f=const_cast<char*>(out_fname); *p; p++) {
      if (IsPathSeparator(*p)) {
         f = p;                       /* set pos of last slash */
      }
   }
   if (!IsPathSeparator(*f)) {         /* did we find a slash? */
      Mmsg1(xfer->m_message, "Could not find path name for output file: %s\n", out_fname);
      goto get_out;
   }
   save_separator = *f;
   *f = 0;                         /* terminate path */

   /* const_cast should not be necessary here but is due the makedir interface */
   if (!makedir(NULL, const_cast<char*>(out_fname), 0740)) {
      Mmsg1(xfer->m_message, "Could not makedir output directory: %s\n", out_fname);
      *f = save_separator;
      goto get_out;
   }
   *f = save_separator;

   if (lstat(out_fname, &statbuf) == -1) {
       outfile = bfopen(out_fname, "w");
   } else {
      /* append to existing file */
      outfile = bfopen(out_fname, "r+");
   }

   if (!outfile) {
      berrno be;
      Mmsg2(xfer->m_message, "Could not open output file %s. ERR=%s\n",
            out_fname, be.bstrerror());
      goto get_out;
   }

   /*
    * Now work on input file
    */
   if (lstat(in_fname, &statbuf) == -1) {
      berrno be;
      Mmsg2(xfer->m_message, "Failed to stat input file %s. ERR=%s\n",
         in_fname, be.bstrerror());
      goto get_out;
   }

   obj_len = statbuf.st_size;
   Dmsg1(dbglvl, "Object length to copy is: %lld bytes.\n", obj_len);
   if (obj_len == 0) {  /* Not yet created nothing to do */
      goto get_out;
   }

   infile = bfopen(in_fname, "r");

   if (!infile) {
      berrno be;
      Mmsg2(xfer->m_message, "Failed to open input file %s. ERR=%s\n",
         in_fname, be.bstrerror());
      goto get_out;
   }

   while (obj_len > 0) {
      if (xfer->is_cancelled()) {
         Mmsg(xfer->m_message, "Job is canceled.\n");
         goto get_out;
      }
      read_len = (obj_len > buf_len) ? buf_len : obj_len;
      Dmsg3(dbglvl, "obj_len=%d buf_len=%d read_len=%d\n", obj_len, buf_len, read_len);
      rbytes = fread(buf, 1, read_len, infile);
      Dmsg1(dbglvl, "Read %d bytes.\n", rbytes);
      if (rbytes <= 0) {
         berrno be;
         Mmsg2(xfer->m_message, "Error reading input file %s. ERR=%s\n",
            in_fname, be.bstrerror());
         goto get_out;
      }
      wbytes = fwrite(buf, 1, rbytes, outfile);
      Dmsg2(dbglvl, "Wrote: %d bytes wanted %d bytes.\n", wbytes, rbytes);
      if (wbytes < 0) {
         berrno be;
         Mmsg2(xfer->m_message, "Error writing output file %s. ERR=%s\n",
            out_fname, be.bstrerror());
      }
      obj_len -= rbytes;
      if (limit->use_bwlimit()) {
         limit->control_bwlimit(rbytes);
      }
   }

get_out:
   free_memory(buf);
   if (infile) {
      fclose(infile);
   }
   if (outfile) {
      fclose(outfile);
      /* Get stats on the result part and fill the xfer res */
      if (lstat(out_fname, &statbuf) == -1) {
         berrno be;
         Mmsg2(xfer->m_message, "Failed to stat file %s: %s\n", out_fname, be.bstrerror());
      } else {
         xfer->m_res_size  = statbuf.st_size;
         xfer->m_res_mtime = statbuf.st_mtime;
      }
   }
   Leave(dbglvl);
   return (xfer->m_message[0] == 0);
}

bool file_driver::get_cloud_object(transfer *xfer, const char *cloud_fname, const char *cache_fname)
{
   return put_object(xfer, cloud_fname, cache_fname, &download_limit);
}

bool file_driver::truncate_cloud_volume(DCR *dcr, const char *VolumeName, ilist *trunc_parts, POOLMEM *&err)
{
   bool rtn = true;
   int i;
   POOLMEM *filename = get_pool_memory(PM_FNAME);
   for (i=1; (i <= (int)trunc_parts->last_index()); i++) {
      if (!trunc_parts->get(i)) {
         continue;
      }
      make_cloud_filename(filename, VolumeName, i);
      if (unlink(filename) != 0 && errno != ENOENT) {
         berrno be;
         Mmsg2(err, "Unable to delete %s. ERR=%s\n", filename, be.bstrerror());
         Dmsg1(dbglvl, "%s", err);
         Qmsg(dcr->jcr, M_INFO, 0, "%s", err);
         rtn = false;
      } else {
         Dmsg1(dbglvl, "Unlink file %s\n", filename);
      }
   }

   free_pool_memory(filename);
   return rtn;
}

void file_driver::make_cloud_filename(POOLMEM *&filename,
       const char *VolumeName, uint32_t part)
{
   Enter(dbglvl);

   pm_strcpy(filename, hostName);
   dev->add_vol_and_part(filename, VolumeName, "part", part);
   Dmsg1(dbglvl, "make_cloud_filename: %s\n", filename);
}

/*
 * Copy a single cache part to the cloud (local disk)
 */
bool file_driver::copy_cache_part_to_cloud(transfer *xfer)
{
   Enter(dbglvl);
   POOLMEM *cloud_fname = get_pool_memory(PM_FNAME);
   make_cloud_filename(cloud_fname, xfer->m_volume_name, xfer->m_part);
   Dmsg2(dbglvl, "Call put_object: %s, %s\n", xfer->m_cache_fname, cloud_fname);
   bool rtn = put_object(xfer, xfer->m_cache_fname, cloud_fname, &upload_limit);
   free_pool_memory(cloud_fname);
   return rtn;
}

/*
 * Copy a single object (part) from the cloud to the cache
 */
bool file_driver::copy_cloud_part_to_cache(transfer *xfer)
{
   Enter(dbglvl);
   POOLMEM *cloud_fname = get_pool_memory(PM_FNAME);
   make_cloud_filename(cloud_fname, xfer->m_volume_name, xfer->m_part);
   bool rtn = put_object(xfer, cloud_fname, xfer->m_cache_fname, &download_limit);
   free_pool_memory(cloud_fname);
   return rtn;
}

/*
 * NOTE: The SD Cloud resource has the following items

   RES   hdr;
   char *host_name;
   char *bucket_name;
   char *access_key;
   char *secret_key;
   int32_t protocol;
   int32_t uri_style;
   uint32_t driver_type;
   uint32_t trunc_opt;
   uint32_t upload_opt;
*/

bool file_driver::init(JCR *jcr, cloud_dev *adev, DEVRES *adevice)
{
   dev = adev;            /* copy cloud device pointer */
   device = adevice;      /* copy device resource pointer */
   cloud = device->cloud; /* local pointer to cloud definition */

   /* File I/O buffer */
   buf_len = dev->max_block_size;
   if (buf_len == 0) {
      buf_len = DEFAULT_BLOCK_SIZE;
   }

   hostName = cloud->host_name;
   bucketName = cloud->bucket_name;
   protocol = cloud->protocol;
   uriStyle = cloud->uri_style;
   accessKeyId = cloud->access_key;
   secretAccessKey = cloud->secret_key;

   return true;
}

bool file_driver::start_of_job(DCR *dcr)
{
   Jmsg(dcr->jcr, M_INFO, 0, _("Using File cloud driver Host=%s Bucket=%s\n"),
      hostName, bucketName);
   return true;
}

bool file_driver::end_of_job(DCR *dcr)
{
   return true;
}

/*
 * Note, dcr may be NULL
 */
bool file_driver::term(DCR *dcr)
{
   return true;
}

bool file_driver::get_cloud_volume_parts_list(DCR *dcr, const char* VolumeName, ilist *parts, POOLMEM *&err)
{
   Enter(dbglvl);

   if (parts == NULL || strlen(VolumeName) == 0) {
      pm_strcpy(err, "Invalid argument");
      return false;
   }

   POOLMEM *vol_dir = get_pool_memory(PM_NAME);

   pm_strcpy(vol_dir, hostName);

   if (!IsPathSeparator(vol_dir[strlen(vol_dir)-1])) {
      pm_strcat(vol_dir, "/");
   }
   pm_strcat(vol_dir, VolumeName);

   DIR* dp = NULL;
   struct dirent *entry = NULL;
   struct stat statbuf;
   int name_max;
   bool ok = false;
   POOL_MEM dname(PM_FNAME);
   int status = 0;

   Dmsg1(dbglvl, "Searching for parts in: %s\n", vol_dir);

   if (!(dp = opendir(vol_dir))) {
      berrno be;
      Mmsg2(err, "Cannot opendir to get parts list. Volume %s does not exist. ERR=%s",
        VolumeName, be.bstrerror());
      Dmsg1(dbglvl, "%s\n", err);
      if (errno == ENOENT) {
         ok=true;                  /* No volume, so no part */
      }
      goto get_out;
   }

   name_max = pathconf(".", _PC_NAME_MAX);
   if (name_max < 1024) {
      name_max = 1024;
   }

   entry = (struct dirent *)malloc(sizeof(struct dirent) + name_max + 1000);

   for ( ;; ) {
      if (dcr->jcr->is_canceled()) {
         pm_strcpy(err, "Job canceled");
         goto get_out;
      }
      errno = 0;
      status = breaddir(dp, dname.addr());
      if (status != 0) {
         if (status > 0) {
            Mmsg1(err, "breaddir failed: status=%d", status);
            Dmsg1(dbglvl, "%s\n", err);
         }
         break;
      }
      /* Always ignore . and .. */
      if (strcmp(".", dname.c_str()) == 0 || strcmp("..", dname.c_str()) == 0) {
         continue;
      }

      /* Look only for part files */
      if (strncmp("part.", dname.c_str(), 5) != 0) {
         continue;
      }

      char *ext = strrchr (dname.c_str(), '.');
      if (!ext || strlen(ext) < 2) {
         continue;
      }

      cloud_part *part = (cloud_part*) malloc(sizeof(cloud_part));

      /* save extension (part number) to cloud_part struct index*/
      part->index = atoi(&ext[1]);

      POOLMEM *part_path = get_pool_memory(PM_NAME);
      pm_strcpy(part_path,vol_dir);
      if (!IsPathSeparator(part_path[strlen(vol_dir)-1])) {
         pm_strcat(part_path, "/");
      }
      pm_strcat(part_path, dname.c_str());

      /* Get size of part */
      if (lstat(part_path, &statbuf) == -1) {
         berrno be;
         Mmsg(err, "Failed to stat file %s: %s", part_path, be.bstrerror());
         Dmsg1(dbglvl, "%s\n", err);
         free_pool_memory(part_path);
         free(part);
         goto get_out;
      }
      free_pool_memory(part_path);

      part->size  = statbuf.st_size;
      part->mtime = statbuf.st_mtime;
      parts->put(part->index, part);
   }
   ok = true;

get_out:
   if (dp) {
      closedir(dp);
   }
   if (entry) {
      free(entry);
   }

   free_pool_memory(vol_dir);

   return ok;
}

bool file_driver::get_cloud_volumes_list(DCR *dcr, alist *volumes, POOLMEM *&err)
{
   if (!volumes) {
      pm_strcpy(err, "Invalid argument");
      return false;
   }

   Enter(dbglvl);

   DIR* dp = NULL;
   struct dirent *entry = NULL;
   struct stat statbuf;
   int name_max;
   bool ok = false;
   POOLMEM *fullpath = get_pool_memory(PM_NAME);
   POOL_MEM dname(PM_FNAME);
   int status = 0;

   if (!(dp = opendir(hostName))) {
      berrno be;
      Mmsg2(err, "Cannot opendir to get volumes list. host_name %s does not exist. ERR=%s",
        hostName, be.bstrerror());
      Dmsg1(dbglvl, "%s\n", err);
      if (errno == ENOENT) {
         ok=true;                  /* No volume, so no part */
      }
      goto get_out;
   }

   name_max = pathconf(".", _PC_NAME_MAX);
   if (name_max < 1024) {
      name_max = 1024;
   }

   entry = (struct dirent *)malloc(sizeof(struct dirent) + name_max + 1000);

   for ( ;; ) {
      if (dcr->jcr->is_canceled()) {
         goto get_out;
      }
      errno = 0;
      status = breaddir(dp, dname.addr());
      if (status != 0) {
         if (status > 0) {
            Mmsg1(err, "breaddir failed: status=%d", status);
            Dmsg1(dbglvl, "%s\n", err);
         }
         break;
      }
      /* Always ignore . and .. */
      if (strcmp(".", dname.c_str()) == 0 || strcmp("..", dname.c_str()) == 0) {
         continue;
      }


      pm_strcpy(fullpath, hostName);
      if (!IsPathSeparator(fullpath[strlen(fullpath)-1])) {
         pm_strcat(fullpath, "/");
      }
      pm_strcat(fullpath, dname.c_str());

      if (lstat(fullpath, &statbuf) != 0) {
         berrno be;
         Dmsg2(dbglvl, "Failed to stat file %s: %s\n",
            fullpath, be.bstrerror());
         continue;
      }

      if (S_ISDIR(statbuf.st_mode)) {
         volumes->append(bstrdup(dname.c_str()));
      }
   }
   ok = true;

get_out:
   if (dp) {
      closedir(dp);
   }
   if (entry) {
      free(entry);
   }

   free_pool_memory(fullpath);

   return ok;
}
