/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2015 Kern Sibbald

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

#include "win32filter.h"

#define WIN32_STREAM_HEADER_SIZE 20 /* the size of the WIN32_STREAM_ID header without the name */

/* search in a record of a STREAM_WIN32_DATA for the true data
 * when found: return true, '*raw' is set at the beginning of the data
 * and *use_len is the length of data to read.
 * *raw_len is decremented and contains the amount of data that as not
 * been filtered yet.
 * For this STREAM_WIN32_DATA, you can call have_data() only one
 * per record.
 * If the stream where the data is can be spread all around the stream
 * you must call have_data() until *raw_len is zero and increment
 * *data before the next call.
 */
bool Win32Filter::have_data(char **raw, int64_t *raw_len, int64_t *use_len)
{
   int64_t size;
   char *orig=*raw;
   initialized = true;
   Dmsg1(100, "have_data(%lld)\n", *raw_len);
   while (*raw_len > 0) {
      /* In this rec, we could have multiple streams of data and headers
       * to handle before to reach the data, then we must iterate
       */

      Dmsg4(100, "s off=%lld len=%lld skip_size=%lld data_size=%lld\n", *raw-orig, *raw_len, skip_size, data_size);
      if (skip_size > 0) {
         /* skip what the previous header told us to skip */
         size = *raw_len < skip_size ? *raw_len : skip_size;
         skip_size -= size;
         *raw_len -= size;
         *raw += size;
      }

      Dmsg4(100, "h off=%lld len=%lld skip_size=%lld data_size=%lld\n", *raw-orig, *raw_len, skip_size, data_size);
      if (data_size == 0 && skip_size == 0 && *raw_len > 0) {
         /* read a WIN32_STREAM header, merge it with the part that was read
          * from the previous record, if any, if the header was split across
          * 2 records.
          */
         size = WIN32_STREAM_HEADER_SIZE - header_pos;
         if (*raw_len < size) {
            size = *raw_len;
         }
         memcpy((char *)&header + header_pos, *raw, size);
         header_pos += size;
         *raw_len -= size;
         *raw += size;
         if (header_pos == WIN32_STREAM_HEADER_SIZE) {
            Dmsg5(100, "header pos=%d size=%lld name_size=%d len=%lld StreamId=0x%x\n", header_pos, size,
                  header.dwStreamNameSize, header.Size, header.dwStreamId);
            header_pos = 0;
            skip_size = header.dwStreamNameSize; /* skip the name of the stream */
            if (header.dwStreamId == WIN32_BACKUP_DATA) {
               data_size = header.Size;
            } else {
               skip_size += header.Size; /* skip the all stream */
            }
         }
         Dmsg4(100, "H off=%lld len=%lld skip_size=%lld data_size=%lld\n", *raw-orig, *raw_len, skip_size, data_size);
      }

      Dmsg4(100, "d off=%lld len=%lld skip_size=%lld data_size=%lld\n", *raw - orig, *raw_len, skip_size, data_size);
      if (data_size > 0 && skip_size == 0 && *raw_len > 0) {
         /* some data to read */
         size = *raw_len < data_size ? *raw_len : data_size;
         data_size -= size;
         *raw_len -= size;
         *use_len = size;
         Dmsg5(100, "D off=%lld len=%lld use_len=%lld skip_size=%lld data_size=%lld\n", *raw-orig, *raw_len,
               *use_len, skip_size, data_size);
         return true;
      }
   }

   return false;
}
