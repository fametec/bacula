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
 * vtape.h - Emulate the Linux st (scsi tape) driver on file.
 * for regression and bug hunting purpose
 *
 */

#ifndef VTAPE_H
#define VTAPE_H

#include <stdarg.h>
#include <stddef.h>
#include "bacula.h"
#include "tape_dev.h"

void vtape_debug(int level);

#ifdef USE_VTAPE

#define FTAPE_MAX_DRIVE 50

#define VTAPE_MAX_BLOCK 20*1024*2048;      /* 20GB */

typedef enum {
   VT_READ_EOF,                 /* Need to read the entire EOF struct */
   VT_SKIP_EOF                  /* Have already read the EOF byte */
} VT_READ_FM_MODE;

class vtape: public tape_dev {
private:
   int         fd;              /* Our file descriptor */
   int         lockfd;          /* File descriptor for the lock file */

   boffset_t   file_block;      /* size */
   boffset_t   max_block;

   boffset_t   last_FM;         /* last file mark (last file) */
   boffset_t   next_FM;         /* next file mark (next file) */
   boffset_t   cur_FM;          /* current file mark */

   bool        atEOF;           /* End of file */
   bool        atEOT;           /* End of media */
   bool        atEOD;           /* End of data */
   bool        atBOT;           /* Begin of tape */
   bool        online;          /* volume online */
   bool        needEOF;         /* check if last operation need eof */

   int32_t     last_file;       /* last file of the volume */
   int32_t     current_file;    /* current position */
   int32_t     current_block;   /* current position */
   char *      lockfile;        /* Name of the lock file */

   void destroy();
   int truncate_file();
   void check_eof() { if(needEOF) weof();};
   void update_pos();
   bool read_fm(VT_READ_FM_MODE readfirst);

public:
   int fsf();
   int fsr(int count);
   int weof();
   int bsf();
   int bsr(int count);

   vtape();
   ~vtape();
   int get_fd();
   void dump();

   int tape_op(struct mtop *mt_com);
   int tape_get(struct mtget *mt_com);
   int tape_pos(struct mtpos *mt_com);

   /* DEVICE virtual interfaces that we redefine */
   int d_close(int);
   int d_open(const char *pathname, int flags);
   int d_ioctl(int fd, ioctl_req_t request, char *op=NULL);
   ssize_t d_read(int, void *buffer, size_t count);
   ssize_t d_write(int, const void *buffer, size_t count);
   bool offline(DCR *dcr);

   boffset_t lseek(DCR *dcr, off_t offset, int whence) { return -1; }
   boffset_t lseek(int fd, off_t offset, int whence)
      { return ::lseek(fd, offset, whence); }
   const char *print_type();
};


#else  /*!USE_VTAPE */

class vtape: public DEVICE {
public:

   vtape();
   ~vtape();
   int d_open(const char *pathname, int flags) { return -1; }
   ssize_t d_read(int fd, void *buffer, size_t count) { return -1; }
   ssize_t d_write(int fd, const void *buffer, size_t count) { return -1; }
   int d_close(int) { return -1; }
   int d_ioctl(int fd, ioctl_req_t request, char *mt=NULL) { return -1; }
   boffset_t lseek(DCR *dcr, off_t offset, int whence) { return -1; }
   bool open_device(DCR *dcr, int omode) { return true; } 
   const char *print_type();
};

#endif  /* USE_VTAPE */

#endif /* !VTAPE_H */
