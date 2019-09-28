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
 *
 *   record-util.c -- Utilities for record handling
 *
 *            Kern Sibbald, October MMXII
 *
 */

#include "bacula.h"
#include "stored.h"

/*
 * Convert a FileIndex into a printable
 *   ASCII string.  Not reentrant.
 * If the FileIndex is negative, it flags the
 *   record as a Label, otherwise it is simply
 *   the FileIndex of the current file.
 */
const char *FI_to_ascii(char *buf, int fi)
{
   if (fi >= 0) {
      sprintf(buf, "%d", fi);
      return buf;
   }
   switch (fi) {
   case PRE_LABEL:
      return "PRE_LABEL";
   case VOL_LABEL:
      return "VOL_LABEL";
   case EOM_LABEL:
      return "EOM_LABEL";
   case SOS_LABEL:
      return "SOS_LABEL";
   case EOS_LABEL:
      return "EOS_LABEL";
   case EOT_LABEL:
      return "EOT_LABEL";
      break;
   case SOB_LABEL:
      return "SOB_LABEL";
      break;
   case EOB_LABEL:
      return "EOB_LABEL";
      break;
   default:
     sprintf(buf, _("unknown: %d"), fi);
     return buf;
   }
}

/*
 * Convert a Stream ID into a printable
 * ASCII string.  Not reentrant.

 * A negative stream number represents
 *   stream data that is continued from a
 *   record in the previous block.
 * If the FileIndex is negative, we are
 *   dealing with a Label, hence the
 *   stream is the JobId.
 */
const char *stream_to_ascii(char *buf, int stream, int fi)
{

   if (fi < 0) {
      sprintf(buf, "%d", stream);
      return buf;
   }
   if (stream < 0) {
      stream = -stream;
      stream &= STREAMMASK_TYPE;
      /* Stream was negative => all are continuation items */
      switch (stream) {
      case STREAM_UNIX_ATTRIBUTES:
         return "contUATTR";
      case STREAM_FILE_DATA:
         return "contDATA";
      case STREAM_WIN32_DATA:
         return "contWIN32-DATA";
      case STREAM_WIN32_GZIP_DATA:
         return "contWIN32-GZIP";
      case STREAM_WIN32_COMPRESSED_DATA:
         return "contWIN32-COMPRESSED";
      case STREAM_MD5_DIGEST:
         return "contMD5";
      case STREAM_SHA1_DIGEST:
         return "contSHA1";
      case STREAM_GZIP_DATA:
         return "contGZIP";
      case STREAM_COMPRESSED_DATA:
         return "contCOMPRESSED";
      case STREAM_UNIX_ATTRIBUTES_EX:
         return "contUNIX-ATTR-EX";
      case STREAM_RESTORE_OBJECT:
         return "contRESTORE-OBJECT";
      case STREAM_SPARSE_DATA:
         return "contSPARSE-DATA";
      case STREAM_SPARSE_GZIP_DATA:
         return "contSPARSE-GZIP";
      case STREAM_SPARSE_COMPRESSED_DATA:
         return "contSPARSE-COMPRESSED";
      case STREAM_PROGRAM_NAMES:
         return "contPROG-NAMES";
      case STREAM_PROGRAM_DATA:
         return "contPROG-DATA";
      case STREAM_MACOS_FORK_DATA:
         return "contMACOS-RSRC";
      case STREAM_HFSPLUS_ATTRIBUTES:
         return "contHFSPLUS-ATTR";
      case STREAM_SHA256_DIGEST:
         return "contSHA256";
      case STREAM_SHA512_DIGEST:
         return "contSHA512";
      case STREAM_SIGNED_DIGEST:
         return "contSIGNED-DIGEST";
      case STREAM_ENCRYPTED_SESSION_DATA:
         return "contENCRYPTED-SESSION-DATA";
      case STREAM_ENCRYPTED_FILE_DATA:
         return "contENCRYPTED-FILE";
      case STREAM_ENCRYPTED_FILE_GZIP_DATA:
         return "contENCRYPTED-GZIP";
      case STREAM_ENCRYPTED_FILE_COMPRESSED_DATA:
         return "contENCRYPTED-COMPRESSED";
      case STREAM_ENCRYPTED_WIN32_DATA:
         return "contENCRYPTED-WIN32-DATA";
      case STREAM_ENCRYPTED_WIN32_GZIP_DATA:
         return "contENCRYPTED-WIN32-GZIP";
      case STREAM_ENCRYPTED_WIN32_COMPRESSED_DATA:
         return "contENCRYPTED-WIN32-COMPRESSED";
      case STREAM_ENCRYPTED_MACOS_FORK_DATA:
         return "contENCRYPTED-MACOS-RSRC";
      case STREAM_PLUGIN_NAME:
         return "contPLUGIN-NAME";
      case STREAM_ADATA_BLOCK_HEADER:
         return "contADATA-BLOCK-HEADER";
      case STREAM_ADATA_RECORD_HEADER:
         return "contADATA-RECORD-HEADER";

      default:
         sprintf(buf, "%d", -stream);
         return buf;
      }
   }

   switch (stream & STREAMMASK_TYPE) {
   case STREAM_UNIX_ATTRIBUTES:
      return "UATTR";
   case STREAM_FILE_DATA:
      return "DATA";
   case STREAM_WIN32_DATA:
      return "WIN32-DATA";
   case STREAM_WIN32_GZIP_DATA:
      return "WIN32-GZIP";
   case STREAM_WIN32_COMPRESSED_DATA:
      return "WIN32-COMPRESSED";
   case STREAM_MD5_DIGEST:
      return "MD5";
   case STREAM_SHA1_DIGEST:
      return "SHA1";
   case STREAM_GZIP_DATA:
      return "GZIP";
   case STREAM_COMPRESSED_DATA:
      return "COMPRESSED";
   case STREAM_UNIX_ATTRIBUTES_EX:
      return "UNIX-ATTR-EX";
   case STREAM_RESTORE_OBJECT:
      return "RESTORE-OBJECT";
   case STREAM_SPARSE_DATA:
      return "SPARSE-DATA";
   case STREAM_SPARSE_GZIP_DATA:
      return "SPARSE-GZIP";
   case STREAM_SPARSE_COMPRESSED_DATA:
      return "SPARSE-COMPRESSED";
   case STREAM_PROGRAM_NAMES:
      return "PROG-NAMES";
   case STREAM_PROGRAM_DATA:
      return "PROG-DATA";
   case STREAM_PLUGIN_NAME:
      return "PLUGIN-NAME";
   case STREAM_MACOS_FORK_DATA:
      return "MACOS-RSRC";
   case STREAM_HFSPLUS_ATTRIBUTES:
      return "HFSPLUS-ATTR";
   case STREAM_SHA256_DIGEST:
      return "SHA256";
   case STREAM_SHA512_DIGEST:
      return "SHA512";
   case STREAM_SIGNED_DIGEST:
      return "SIGNED-DIGEST";
   case STREAM_ENCRYPTED_SESSION_DATA:
      return "ENCRYPTED-SESSION-DATA";
   case STREAM_ENCRYPTED_FILE_DATA:
      return "ENCRYPTED-FILE";
   case STREAM_ENCRYPTED_FILE_GZIP_DATA:
      return "ENCRYPTED-GZIP";
   case STREAM_ENCRYPTED_FILE_COMPRESSED_DATA:
      return "ENCRYPTED-COMPRESSED";
   case STREAM_ENCRYPTED_WIN32_DATA:
      return "ENCRYPTED-WIN32-DATA";
   case STREAM_ENCRYPTED_WIN32_GZIP_DATA:
      return "ENCRYPTED-WIN32-GZIP";
   case STREAM_ENCRYPTED_WIN32_COMPRESSED_DATA:
      return "ENCRYPTED-WIN32-COMPRESSED";
   case STREAM_ENCRYPTED_MACOS_FORK_DATA:
      return "ENCRYPTED-MACOS-RSRC";
   case STREAM_ADATA_BLOCK_HEADER:
      return "ADATA-BLOCK-HEADER";
   case STREAM_ADATA_RECORD_HEADER:
      return "ADATA-RECORD-HEADER";
   default:
      sprintf(buf, "%d", stream);
      return buf;
   }
}

const char *stream_to_ascii_ex(char *buf, int stream, int fi)
{
   if (fi < 0) {
      return stream_to_ascii(buf, stream, fi);
   }
   const char *p = stream_to_ascii(buf, stream, fi);
   return p;
}
/*
 * Return a new record entity
 */
DEV_RECORD *new_record(void)
{
   DEV_RECORD *rec;

   rec = (DEV_RECORD *)get_memory(sizeof(DEV_RECORD));
   memset(rec, 0, sizeof(DEV_RECORD));
   rec->data = get_pool_memory(PM_MESSAGE);
   rec->wstate = st_none;
   rec->rstate = st_none;
   return rec;
}

void empty_record(DEV_RECORD *rec)
{
   rec->RecNum = 0;
   rec->StartAddr = rec->Addr = 0;
   rec->VolSessionId = rec->VolSessionTime = 0;
   rec->FileIndex = rec->Stream = 0;
   rec->data_len = rec->remainder = 0;
   rec->state_bits &= ~(REC_PARTIAL_RECORD|REC_ADATA_EMPTY|REC_BLOCK_EMPTY|REC_NO_MATCH|REC_CONTINUATION);
   rec->FileOffset = 0;
   rec->wstate = st_none;
   rec->rstate = st_none;
   rec->VolumeName = NULL;
}

/*
 * Free the record entity
 *
 */
void free_record(DEV_RECORD *rec)
{
   Dmsg0(950, "Enter free_record.\n");
   if (rec->data) {
      free_pool_memory(rec->data);
   }
   Dmsg0(950, "Data buf is freed.\n");
   free_pool_memory((POOLMEM *)rec);
   Dmsg0(950, "Leave free_record.\n");
}

void dump_record(DEV_RECORD *rec)
{
   char buf[32];
   Dmsg11(100|DT_VOLUME, "Dump record %s 0x%p:\n\tStart=%lld addr=%lld #%d\n"
         "\tVolSess: %ld:%ld\n"
         "\tFileIndex: %ld\n"
         "\tStream: 0x%lx\n\tLen: %ld\n\tData: %s\n",
         rec, NPRT(rec->VolumeName),
         rec->StartAddr, rec->Addr, rec->RecNum,
         rec->VolSessionId, rec->VolSessionTime, rec->FileIndex,
         rec->Stream, rec->data_len,
         asciidump(rec->data, rec->data_len, buf, sizeof(buf)));
}

/*
 * Test if we can write whole record to the block
 *
 *  Returns: false on failure
 *           true  on success (all bytes can be written)
 */
bool can_write_record_to_block(DEV_BLOCK *block, DEV_RECORD *rec)
{
   uint32_t remlen;

   remlen = block->buf_len - block->binbuf;
   if (rec->remainder == 0) {
      if (remlen >= WRITE_RECHDR_LENGTH) {
         remlen -= WRITE_RECHDR_LENGTH;
         rec->remainder = rec->data_len;
      } else {
         return false;
      }
   } else {
      return false;
   }
   if (rec->remainder > 0 && remlen < rec->remainder) {
      return false;
   }
   return true;
}

uint64_t get_record_address(DEV_RECORD *rec)
{
     return rec->Addr;
}

uint64_t get_record_start_address(DEV_RECORD *rec)
{
     return rec->StartAddr;
}
