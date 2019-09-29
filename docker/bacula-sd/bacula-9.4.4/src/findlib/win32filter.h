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

/* Pulled from other files by Alain Spineux */

#ifndef WIN32FILTER_H_
#define WIN32FILTER_H_

#include "bacula.h"

/* this should physically correspond to WIN32_STREAM_ID
 * from winbase.h on Win32. We didn't inlcude cStreamName
 * as we don't use it and don't need it for a correct struct size.
 */

#define WIN32_BACKUP_DATA 1

typedef struct _BWIN32_STREAM_ID {
        int32_t        dwStreamId;
        int32_t        dwStreamAttributes;
        int64_t        Size;
        int32_t        dwStreamNameSize;
} BWIN32_STREAM_ID, *LPBWIN32_STREAM_ID ;

class Win32Filter
{
public:
   bool    initialized;
   int64_t skip_size;   /* how many bytes we have to skip before next header */
   int64_t data_size;   /* how many data are expected in the stream */
   int     header_pos;  /* the part of the header that was filled in by previous record */

   BWIN32_STREAM_ID header;

   Win32Filter() {
      init();
   };
   
   void init() {
      initialized = false;
      skip_size = 0;
      data_size = 0;
      header_pos = 0;
   };

   void copy(Win32Filter *f)
   {
      skip_size = f->skip_size;
      data_size = f->data_size;
      header_pos = f->header_pos;
      header = f->header;
      initialized = (skip_size != 0  ||
                     data_size != 0  ||
                     header_pos != 0);
   };

   /* If the stream is HHHDDDDD, you can call  have_data("HHHDDDDD", 8, <not used>)
    * and it will return  "DDDDD", 0, 5
    */
   bool have_data(char **raw, int64_t *raw_len, int64_t *data_len);
};

#endif
