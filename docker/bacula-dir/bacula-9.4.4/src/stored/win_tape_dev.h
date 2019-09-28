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
 * Inspired by vtape.h
 */
#ifndef __WIN_TAPE_DEV_H_
#define __WIN_TAPE_DEV_H_

class win_tape_dev : public DEVICE {
public:

   win_tape_dev() { };
   ~win_tape_dev() { };

   /* interface from DEVICE */
   int d_close(int);
   int d_open(const char *pathname, int flags);
   int d_ioctl(int fd, ioctl_req_t request, char *op=NULL);
   ssize_t d_read(int, void *buffer, size_t count);
   ssize_t d_write(int, const void *buffer, size_t count);

   boffset_t lseek(DCR *dcr, off_t offset, int whence) { return -1; };
   boffset_t lseek(int fd, off_t offset, int whence);
   bool open_device(DCR *dcr, int omode);

   int tape_op(struct mtop *mt_com);
   int tape_get(struct mtget *mt_com);
   int tape_pos(struct mtpos *mt_com);

};

#endif /* __WIN_TAPE_DEV_H_ */
