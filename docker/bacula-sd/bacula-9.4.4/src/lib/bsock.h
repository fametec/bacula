/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2019 Kern Sibbald

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
 * Bacula Sock Class definition
 *   Note, the old non-class code is in bnet.c, and the
 *   new class code associated with this file is in bsock.c
 *
 * The new code inherit common functions from BSOCKCORE class
 * and implement BSOCK/Bacula specific protocols and data flow.
 *
 * Kern Sibbald, May MM
 *
 * Major refactoring of BSOCK code written by:
 *
 * Radosław Korzeniewski, MMXVIII
 * radoslaw@korzeniewski.net, radekk@inteos.pl
 * Inteos Sp. z o.o. http://www.inteos.pl/
 *
 * Zero msglen from other end indicates soft eof (usually
 *   end of some binary data stream, but not end of conversation).
 *
 * Negative msglen, is special "signal" (no data follows).
 *   See below for SIGNAL codes.
 */

#ifndef __BSOCK_H_
#define __BSOCK_H_

#define BSOCK_TIMEOUT  3600 * 24 * 200;  /* default 200 days */

class BSOCK: public BSOCKCORE {
public:
   FILE *m_spool_fd;                  /* spooling file */
   POOLMEM *cmsg;                     /* Compress buffer */

private:
   boffset_t m_data_end;              /* offset of data written */
   boffset_t m_last_data_end;         /* offset of last valid data written */
   int32_t m_FileIndex;               /* attr spool FI */
   int32_t m_lastFileIndex;           /* last valid attr spool FI */
   int32_t m_lastFlushIndex;          /* Last FileIndex flushed */
   bool m_spool: 1;                   /* set for spooling */
   bool m_compress: 1;                /* set to use comm line compression */
   uint64_t m_CommBytes;              /* Bytes sent */
   uint64_t m_CommCompressedBytes;    /* Compressed bytes sent */

   bool open(JCR *jcr, const char *name, char *host, char *service,
               int port, utime_t heart_beat, int *fatal);
   void init();
   void _destroy();
   int32_t write_nbytes(char *ptr, int32_t nbytes);

public:
   BSOCK();
   BSOCK(int sockfd);
   ~BSOCK();
  // int32_t recv(int /*len*/) { return recv(); };
   int32_t recv();
   bool send() { return send(0); };
   bool send(int flags);
   bool signal(int signal);
   void close();              /* close connection and destroy packet */
   bool comm_compress();               /* in bsock.c */
   bool despool(void update_attr_spool_size(ssize_t size), ssize_t tsize);
   bool authenticate_director(const char *name, const char *password,
           TLS_CONTEXT *tls_ctx, char *response, int response_len);

   /* Inline functions */
   bool is_spooling() const { return m_spool; };
   bool can_compress() const { return m_compress; };
   void set_data_end(int32_t FileIndex) {
          if (m_spool && FileIndex > m_FileIndex) {
              m_lastFileIndex = m_FileIndex;
              m_last_data_end = m_data_end;
              m_FileIndex = FileIndex;
              m_data_end = ftello(m_spool_fd);
           }
        };
   void set_lastFlushIndex(int32_t FlushIndex) { m_lastFlushIndex = FlushIndex; };
   boffset_t get_last_data_end() { return m_last_data_end; };
   uint32_t get_lastFileIndex() { return m_lastFileIndex; };
   uint32_t get_lastFlushIndex() { return m_lastFlushIndex; };
   uint32_t CommBytes() { return m_CommBytes; };
   uint32_t CommCompressedBytes() { return m_CommCompressedBytes; };
   void set_spooling() { m_spool = true; };
   void clear_spooling() { m_spool = false; };
   void set_compress() { m_compress = true; };
   void clear_compress() { m_compress = false; };
   void dump();
};

/*
 *  Signal definitions for use in bsock->signal()
 *  Note! These must be negative.  There are signals that are generated
 *   by the bsock software not by the OS ...
 */
enum {
   BNET_EOD            = -1,          /* End of data stream, new data may follow */
   BNET_EOD_POLL       = -2,          /* End of data and poll all in one */
   BNET_STATUS         = -3,          /* Send full status */
   BNET_TERMINATE      = -4,          /* Conversation terminated, doing close() */
   BNET_POLL           = -5,          /* Poll request, I'm hanging on a read */
   BNET_HEARTBEAT      = -6,          /* Heartbeat Response requested */
   BNET_HB_RESPONSE    = -7,          /* Only response permited to HB */
   BNET_xxxxxxPROMPT   = -8,          /* No longer used -- Prompt for subcommand */
   BNET_BTIME          = -9,          /* Send UTC btime */
   BNET_BREAK          = -10,         /* Stop current command -- ctl-c */
   BNET_START_SELECT   = -11,         /* Start of a selection list */
   BNET_END_SELECT     = -12,         /* End of a select list */
   BNET_INVALID_CMD    = -13,         /* Invalid command sent */
   BNET_CMD_FAILED     = -14,         /* Command failed */
   BNET_CMD_OK         = -15,         /* Command succeeded */
   BNET_CMD_BEGIN      = -16,         /* Start command execution */
   BNET_MSGS_PENDING   = -17,         /* Messages pending */
   BNET_MAIN_PROMPT    = -18,         /* Server ready and waiting */
   BNET_SELECT_INPUT   = -19,         /* Return selection input */
   BNET_WARNING_MSG    = -20,         /* Warning message */
   BNET_ERROR_MSG      = -21,         /* Error message -- command failed */
   BNET_INFO_MSG       = -22,         /* Info message -- status line */
   BNET_RUN_CMD        = -23,         /* Run command follows */
   BNET_YESNO          = -24,         /* Request yes no response */
   BNET_START_RTREE    = -25,         /* Start restore tree mode */
   BNET_END_RTREE      = -26,         /* End restore tree mode */
   BNET_SUB_PROMPT     = -27,         /* Indicate we are at a subprompt */
   BNET_TEXT_INPUT     = -28,         /* Get text input from user */
   BNET_EXT_TERMINATE  = -29,         /* A Terminate condition has been met and
                                         already reported somewhere else */
   BNET_FDCALLED       = -30          /* The FD should keep the connection for a new job */
};

/*
 * These bits ares set in the packet length field.  Attempt to
 *  keep the number of bits to a minimum and instead use the new
 *  flag field for passing bits using the BNET_HDR_EXTEND bit.
 *  Note: we must not set the high bit as that indicates a signal.
 */
#define BNET_COMPRESSED (1<<30)       /* set for lz4 compressed data */
#define BNET_HDR_EXTEND (1<<29)       /* extended header */

/*
 * The following bits are kept in flags.  The high 16 bits are
 *  for flags, and the low 16 bits are for other info such as
 *  compressed data offset (BNET_OFFSET)
 */
#define BNET_IS_CMD           (1<<28)     /* set for command data */
#define BNET_OFFSET           (1<<27)     /* Data compression offset specified */
#define BNET_NOCOMPRESS       (1<<25)     /* Disable compression */
#define BNET_DATACOMPRESSED   (1<<24)     /* Data compression */

#define BNET_SETBUF_READ  1           /* Arg for bnet_set_buffer_size */
#define BNET_SETBUF_WRITE 2           /* Arg for bnet_set_buffer_size */

/*
 * Return status from bnet_recv()
 * Note, the HARDEOF and ERROR refer to comm status/problems
 *  rather than the BNET_xxx above, which are software signals.
 */
enum {
   BNET_SIGNAL         = -1,
   BNET_HARDEOF        = -2,
   BNET_ERROR          = -3,
   BNET_COMMAND        = -4
};

/*
 * Inter-daemon commands
 * When BNET_IS_CMD is on, the next int32 is a command
 */
#define BNET_CMD_SIZE sizeof(int32_t)

enum {
   BNET_CMD_NONE       =  0, /* reserved */
   BNET_CMD_ACK_HASH   =  1, /* backup  SD->FD  SD already know this hash, don't need the block */
   BNET_CMD_UNK_HASH   =  2, /* restore SD->FD  hash is unknown */
   BNET_CMD_GET_HASH   =  3, /* backup  SD->FD  SD ask FD to send the corresponding block */
                             /* restore FD->SD  FD ask SD to send the corresponding block */
   BNET_CMD_STO_BLOCK  =  4, /* backup  FD->SD  FD send requested block */
   BNET_CMD_REC_ACK    =  5, /* restore FD->SD  FD has consumed records from the buffer */
   BNET_CMD_STP_THREAD =  6, /* restore FD->SD  SD must stop thread */
   BNET_CMD_STP_FLOWCTRL = 7 /* backup FD->SD  SD must stop sending flowcontrol information */
};

/*
 * TLS enabling values. Value is important for comparison, ie:
 * if (tls_remote_need < BNET_TLS_REQUIRED) { ... }
 */
enum {
   BNET_TLS_NONE        = 0,          /* cannot do TLS */
   BNET_TLS_OK          = 1,          /* can do, but not required on my end */
   BNET_TLS_REQUIRED    = 2           /* TLS is required */
};

const char *bnet_cmd_to_name(int val);

BSOCK *new_bsock();
/*
 * Completely release the socket packet, and NULL the pointer
 */
#define free_bsock(a) free_bsockcore(a)

/*
 * Does the socket exist and is it open?
 */
#define is_bsock_open(a) is_bsockcore_open(a)

#endif /* __BSOCK_H_ */
