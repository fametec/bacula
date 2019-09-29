/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

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
 * Bacula zlib compression wrappers
 *
 */

#include "bacula.h"
#ifdef HAVE_LIBZ
#include <zlib.h>
#endif

/*
 * Deflate or compress and input buffer.  You must supply an
 *  output buffer sufficiently long and the length of the
 *  output buffer. Generally, if the output buffer is the
 *  same size as the input buffer, it should work (at least
 *  for text).
 */
int Zdeflate(char *in, int in_len, char *out, int &out_len)
{
#ifdef HAVE_LIBZ
   z_stream strm;
   int ret;

   /* allocate deflate state */
   strm.zalloc = Z_NULL;
   strm.zfree = Z_NULL;
   strm.opaque = Z_NULL;
   ret = deflateInit(&strm, 9);
   if (ret != Z_OK) {
      Dmsg0(200, "deflateInit error\n");
      (void)deflateEnd(&strm);
      return ret;
   }

   strm.next_in = (Bytef *)in;
   strm.avail_in = in_len;
   Dmsg1(200, "In: %d bytes\n", strm.avail_in);
   strm.avail_out = out_len;
   strm.next_out = (Bytef *)out;
   ret = deflate(&strm, Z_FINISH);
   out_len = out_len - strm.avail_out;
   Dmsg1(200, "compressed=%d\n", out_len);
   (void)deflateEnd(&strm);
   return ret;
#else
   return 1;
#endif
}

/*
 * Inflate or uncompress an input buffer.  You must supply
 *  and output buffer and an output length sufficiently long
 *  or there will be an error.  This uncompresses in one call.
 */
int Zinflate(char *in, int in_len, char *out, int &out_len)
{
#ifdef HAVE_LIBZ
   z_stream strm;
   int ret;

   /* allocate deflate state */
   strm.zalloc = Z_NULL;
   strm.zfree = Z_NULL;
   strm.opaque = Z_NULL;
   strm.next_in = (Bytef *)in;
   strm.avail_in = in_len;
   ret = inflateInit(&strm);
   if (ret != Z_OK) {
      Dmsg0(200, "inflateInit error\n");
      (void)inflateEnd(&strm);
      return ret;
   }

   Dmsg1(200, "In len: %d bytes\n", strm.avail_in);
   strm.avail_out = out_len;
   strm.next_out = (Bytef *)out;
   ret = inflate(&strm, Z_FINISH);
   out_len -= strm.avail_out;
   Dmsg1(200, "Uncompressed=%d\n", out_len);
   (void)inflateEnd(&strm);
   return ret;
#else
   return 1;
#endif
}
