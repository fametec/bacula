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
/**
 * Stream definitions.  Split from baconfig.h Nov 2010
 *
 *  Kern Sibbald, MM
 */

#ifndef __BSTREAMS_H
#define __BSTREAMS_H 1

/* Stream bits  -- these bits are new as of 24Nov10 */
#define STREAM_BIT_64                 (1<<30)    /* 64 bit stream (not yet implemented) */
#define STREAM_BIT_BITS               (1<<29)    /* Following bits may be set */
#define STREAM_BIT_PLUGIN             (1<<28)    /* Item written by a plugin */
#define STREAM_BIT_DELTA              (1<<27)    /* Stream contains delta data */
#define STREAM_BIT_OFFSETS            (1<<26)    /* Stream has data offset */
#define STREAM_BIT_PORTABLE_DATA      (1<<25)    /* Data is portable */

/* TYPE represents our current (old) stream types -- e.g. values 0 - 2047 */
#define STREAMBASE_TYPE                0         /* base for types */
#define STREAMBITS_TYPE               11         /* type bit size */
#define STREAMMASK_TYPE               (~((~0u)<< STREAMBITS_TYPE) << STREAMBASE_TYPE)
/*
 * Note additional base, bits, and masks can be defined for new     
 *  ranges or subranges of stream attributes.
 */

/**
 * Old, but currently used Stream definitions. Once defined these must NEVER
 *   change as they go on the storage media.
 * Note, the following streams are passed from the SD to the DIR
 *   so that they may be put into the catalog (actually only the
 *   stat packet part of the attr record is put in the catalog.
 *
 *   STREAM_UNIX_ATTRIBUTES
 *   STREAM_UNIX_ATTRIBUTES_EX
 *   STREAM_MD5_DIGEST
 *   STREAM_SHA1_DIGEST
 *   STREAM_SHA256_DIGEST
 *   STREAM_SHA512_DIGEST
 */
#define STREAM_NONE                         0    /* Reserved Non-Stream */
#define STREAM_UNIX_ATTRIBUTES              1    /* Generic Unix attributes */
#define STREAM_FILE_DATA                    2    /* Standard uncompressed data */
#define STREAM_MD5_SIGNATURE                3    /* deprecated */
#define STREAM_MD5_DIGEST                   3    /* MD5 digest for the file */
#define STREAM_GZIP_DATA                    4    /* GZip compressed file data */
#define STREAM_UNIX_ATTRIBUTES_EX           5    /* Extended Unix attr for Win32 EX - Deprecated */
#define STREAM_SPARSE_DATA                  6    /* Sparse data stream */
#define STREAM_SPARSE_GZIP_DATA             7    /* Sparse gzipped data stream */
#define STREAM_PROGRAM_NAMES                8    /* program names for program data */
#define STREAM_PROGRAM_DATA                 9    /* Data needing program */
#define STREAM_SHA1_SIGNATURE              10    /* deprecated */
#define STREAM_SHA1_DIGEST                 10    /* SHA1 digest for the file */
#define STREAM_WIN32_DATA                  11    /* Win32 BackupRead data */
#define STREAM_WIN32_GZIP_DATA             12    /* Gzipped Win32 BackupRead data */
#define STREAM_MACOS_FORK_DATA             13    /* Mac resource fork */
#define STREAM_HFSPLUS_ATTRIBUTES          14    /* Mac OS extra attributes */
#define STREAM_UNIX_ACCESS_ACL             15    /* Standard ACL attributes on UNIX - Deprecated */
#define STREAM_UNIX_DEFAULT_ACL            16    /* Default ACL attributes on UNIX - Deprecated */
#define STREAM_SHA256_DIGEST               17    /* SHA-256 digest for the file */
#define STREAM_SHA512_DIGEST               18    /* SHA-512 digest for the file */
#define STREAM_SIGNED_DIGEST               19    /* Signed File Digest, ASN.1, DER Encoded */
#define STREAM_ENCRYPTED_FILE_DATA         20    /* Encrypted, uncompressed data */
#define STREAM_ENCRYPTED_WIN32_DATA        21    /* Encrypted, uncompressed Win32 BackupRead data */
#define STREAM_ENCRYPTED_SESSION_DATA      22    /* Encrypted Session Data, ASN.1, DER Encoded */
#define STREAM_ENCRYPTED_FILE_GZIP_DATA    23    /* Encrypted, compressed data */
#define STREAM_ENCRYPTED_WIN32_GZIP_DATA   24    /* Encrypted, compressed Win32 BackupRead data */
#define STREAM_ENCRYPTED_MACOS_FORK_DATA   25    /* Encrypted, uncompressed Mac resource fork */
#define STREAM_PLUGIN_NAME                 26    /* Plugin "file" string */
#define STREAM_PLUGIN_DATA                 27    /* Plugin specific data */
#define STREAM_RESTORE_OBJECT              28    /* Plugin restore object */
/*
 * Non-gzip compressed streams. Those streams can handle arbitrary 
 *  compression algorithm data as an additional header is stored 
 *  at the beginning of the stream. See comp_stream_header definition 
 *  in ch.h for more details.
 */
#define STREAM_COMPRESSED_DATA                 29    /* Compressed file data */
#define STREAM_SPARSE_COMPRESSED_DATA          30    /* Sparse compressed data stream */
#define STREAM_WIN32_COMPRESSED_DATA           31    /* Compressed Win32 BackupRead data */
#define STREAM_ENCRYPTED_FILE_COMPRESSED_DATA  32    /* Encrypted, compressed data */
#define STREAM_ENCRYPTED_WIN32_COMPRESSED_DATA 33    /* Encrypted, compressed Win32 BackupRead data */

#define STREAM_ADATA_BLOCK_HEADER             200    /* Adata block header */
#define STREAM_ADATA_RECORD_HEADER            201    /* Adata record header */

/*
 * Additional Stream definitions. Once defined these must NEVER
 *   change as they go on the storage media.
 *
 * The Stream numbers from 1000-1999 are reserved for ACL and extended attribute streams.
 * Each different platform has its own stream id(s), if a platform supports multiple stream types
 * it should supply different handlers for each type it supports and this should be called
 * from the stream dispatch function. Currently in this reserved space we allocate the
 * different acl streams from 1000 on and the different extended attributes streams from
 * 1999 down. So the two naming spaces grows towards each other.
 *
 * Rationalize names a bit to correspond to the new XACL classes
 *
 */
#define STREAM_XACL_AIX_TEXT          1000    /* AIX string of acl_get */
#define STREAM_XACL_DARWIN_ACCESS     1001    /* Darwin (OSX) acl_t string of acl_to_text (POSIX acl) */
#define STREAM_XACL_FREEBSD_DEFAULT   1002    /* FreeBSD acl_t string of acl_to_text (POSIX acl) for default acls */
#define STREAM_XACL_FREEBSD_ACCESS    1003    /* FreeBSD acl_t string of acl_to_text (POSIX acl) for access acls */
#define STREAM_XACL_HPUX_ACL_ENTRY    1004    /* HPUX acl_entry string of acltostr (POSIX acl) */
#define STREAM_XACL_IRIX_DEFAULT      1005    /* IRIX acl_t string of acl_to_text (POSIX acl) for default acls */
#define STREAM_XACL_IRIX_ACCESS       1006    /* IRIX acl_t string of acl_to_text (POSIX acl) for access acls */
#define STREAM_XACL_LINUX_DEFAULT     1007    /* Linux acl_t string of acl_to_text (POSIX acl) for default acls */
#define STREAM_XACL_LINUX_ACCESS      1008    /* Linux acl_t string of acl_to_text (POSIX acl) for access acls */
#define STREAM_XACL_TRU64_DEFAULT     1009    /* Tru64 acl_t string of acl_to_text (POSIX acl) for default acls */
#define STREAM_XACL_TRU64_DEFAULT_DIR 1010    /* Tru64 acl_t string of acl_to_text (POSIX acl) for default acls */
#define STREAM_XACL_TRU64_ACCESS      1011    /* Tru64 acl_t string of acl_to_text (POSIX acl) for access acls */
#define STREAM_XACL_SOLARIS_POSIX     1012    /* Solaris aclent_t string of acltotext or acl_totext (POSIX acl) */
#define STREAM_XACL_SOLARIS_NFS4      1013    /* Solaris ace_t string of of acl_totext (NFSv4 or ZFS acl) */
#define STREAM_XACL_AFS_TEXT          1014    /* AFS string of pioctl */
#define STREAM_XACL_AIX_AIXC          1015    /* AIX string of aclx_printStr (POSIX acl) */
#define STREAM_XACL_AIX_NFS4          1016    /* AIX string of aclx_printStr (NFSv4 acl) */
#define STREAM_XACL_FREEBSD_NFS4      1017    /* FreeBSD acl_t string of acl_to_text (NFSv4 or ZFS acl) */
#define STREAM_XACL_HURD_DEFAULT      1018    /* GNU HURD acl_t string of acl_to_text (POSIX acl) for default acls */
#define STREAM_XACL_HURD_ACCESS       1019    /* GNU HURD acl_t string of acl_to_text (POSIX acl) for access acls */
#define STREAM_XACL_PLUGIN_ACL        1020    /* Plugin ACL data for plugin specific acls */
#define STREAM_XACL_PLUGIN_XATTR      1988    /* Plugin XATTR data for plugin specific xattrs */
#define STREAM_XACL_HURD_XATTR        1989    /* GNU HURD extended attributes */
#define STREAM_XACL_IRIX_XATTR        1990    /* IRIX extended attributes */
#define STREAM_XACL_TRU64_XATTR       1991    /* TRU64 extended attributes */
#define STREAM_XACL_AIX_XATTR         1992    /* AIX extended attributes */
#define STREAM_XACL_OPENBSD_XATTR     1993    /* OpenBSD extended attributes */
#define STREAM_XACL_SOLARIS_SYS_XATTR 1994    /* Solaris extensible attributes or
                                               * otherwise named extended system attributes. */
#define STREAM_XACL_SOLARIS_XATTR     1995    /* Solaris extented attributes */
#define STREAM_XACL_DARWIN_XATTR      1996    /* Darwin (OSX) extended attributes */
#define STREAM_XACL_FREEBSD_XATTR     1997    /* FreeBSD extended attributes */
#define STREAM_XACL_LINUX_XATTR       1998    /* Linux specific attributes */
#define STREAM_XACL_NETBSD_XATTR      1999    /* NetBSD extended attributes */

/* WARNING!!! do not define more than 2047 of these old types */

#endif /* __BSTREAMS_H */
