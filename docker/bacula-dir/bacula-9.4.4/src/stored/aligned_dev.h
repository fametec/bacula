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
 * Inspired by vtape.h
 */

#ifndef _ALIGNED_DEV_H_
#define _ALIGNED_DEV_H_

class aligned_dev : public file_dev {
public:

   aligned_dev();
   ~aligned_dev();

   boffset_t get_adata_size(DCR *dcr);
   boffset_t align_adata_addr(DCR *dcr, boffset_t addr);
   boffset_t get_adata_addr(DCR *dcr);
   void set_adata_addr(DCR *dcr);
   void clear_adata_addr();

   /* DEVICE virtual functions that we redefine */
   void setVolCatName(const char *name);
   void setVolCatStatus(const char *status);
   void free_dcr_blocks(DCR *dcr);
   void new_dcr_blocks(DCR *dcr);
   void updateVolCatBytes(uint64_t);
   void updateVolCatBlocks(uint32_t);
   void updateVolCatWrites(uint32_t);
   void updateVolCatReads(uint32_t);
   void updateVolCatReadBytes(uint64_t);
   void updateVolCatPadding(uint64_t);
   bool setVolCatAdataBytes(uint64_t bytes);
   void updateVolCatHoleBytes(uint64_t bytes);
   void device_specific_open(DCR *dcr);
   void set_volcatinfo_from_dcr(DCR *dcr);
   bool allow_maxbytes_concurrency(DCR *dcr);
   bool flush_before_eos(DCR *dcr);
   void set_nospace();
   void set_append();
   void set_read();
   void clear_nospace();
   void clear_append();
   void clear_read();
   void device_specific_init(JCR *jcr, DEVRES *device);
   int d_close(int fd);
   int d_open(const char *pathname, int flags);
   int d_ioctl(int fd, ioctl_req_t request, char *mt_com);
   ssize_t d_read(int fd, void *buffer, size_t count);
   ssize_t d_write(int, const void *buffer, size_t count);
   boffset_t lseek(DCR *dcr, off_t offset, int whence);
   bool rewind(DCR *dcr);
   bool reposition(DCR *dcr, uint64_t raddr);
   bool open_device(DCR *dcr, int omode);
   bool truncate(DCR *dcr);
   bool close(DCR *dcr);
   void term(DCR *dcr);
   bool eod(DCR *dcr);
   bool update_pos(DCR *dcr);
   bool mount_file(int mount, int dotimeout);
   bool is_indexed() { return !adata; };
   int read_dev_volume_label(DCR *dcr);
   const char *print_type();
   DEVICE *get_dev(DCR *dcr);
   uint32_t get_hi_addr();
   uint32_t get_low_addr();
   uint64_t get_full_addr();
   uint64_t get_full_addr(boffset_t addr);
   bool do_size_checks(DCR *dcr, DEV_BLOCK *block);
   bool write_volume_label_to_block(DCR *dcr);
   bool write_volume_label_to_dev(DCR *dcr,
          const char *VolName, const char *PoolName,
          bool relabel, bool no_prelabel);
   bool write_adata_label(DCR *dcr, DEV_RECORD *rec);
   void write_adata(DCR *dcr, DEV_RECORD *rec);
   void write_cont_adata(DCR *dcr, DEV_RECORD *rec);
   int  write_adata_rechdr(DCR *dcr, DEV_RECORD *rec);
   bool read_adata_record_header(DCR *dcr, DEV_BLOCK *block, DEV_RECORD *rec);
   void read_adata_block_header(DCR *dcr);
   int read_adata(DCR *dcr, DEV_RECORD *rec);
   bool have_adata_header(DCR *dcr, DEV_RECORD *rec, int32_t  FileIndex,
                        int32_t  Stream, uint32_t VolSessionId);
   void select_data_stream(DCR *dcr, DEV_RECORD *rec);
   bool flush_block(DCR *dcr);
   bool do_pre_write_checks(DCR *dcr, DEV_RECORD *rec);



   /*
    * Locking and blocking calls
    */
#ifdef DEV_DEBUG_LOCK
   void dbg_Lock(const char *, int);
   void dbg_Unlock(const char *, int);
   void dbg_rLock(const char *, int, bool locked=false);
   void dbg_rUnlock(const char *, int);
#else
   void Lock();
   void Unlock();
   void rLock(bool locked=false);
   void rUnlock();
#endif

#ifdef  SD_DEBUG_LOCK
   void dbg_Lock_acquire(const char *, int);
   void dbg_Unlock_acquire(const char *, int);
   void dbg_Lock_read_acquire(const char *, int);
   void dbg_Unlock_read_acquire(const char *, int);
   void dbg_Lock_VolCatInfo(const char *, int);
   void dbg_Unlock_VolCatInfo(const char *, int);
#else
   void Lock_acquire();
   void Unlock_acquire();
   void Lock_read_acquire();
   void Unlock_read_acquire();
   void Lock_VolCatInfo();
   void Unlock_VolCatInfo();
#endif

   void dblock(int why);                  /* in lock.c */
   void dunblock(bool locked=false);

};

#endif /* _ALIGNED_DEV_H_ */
