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
 *     Kern Sibbald, January MM
 *
 */
#pragma once
/* Used for certain keyword tables */
struct s_kw {
   const char *name;
   int token;
};

struct RES_ITEM;                   /* Declare forward referenced structure */
struct RES_ITEM1;
struct RES_ITEM2;                  /* Declare forward referenced structure */
class RES;                         /* Declare forward referenced structure */
struct HPKT;                       /* Declare forward referenced structure */
typedef void (RES_HANDLER)(HPKT &hpkt);
typedef void (MSG_RES_HANDLER)(LEX *lc, RES_ITEM *item, int index, int pass);
/* The INC_RES handler has an extra argument */
typedef void (INC_RES_HANDLER)(LEX *lc, RES_ITEM2 *item, int index, int pass, bool exclude);

/* This is the structure that defines
 * the record types (items) permitted within each
 * resource. It is used to define the configuration
 * tables.
 */
struct RES_ITEM {
   const char *name;                  /* Resource name i.e. Director, ... */
   MSG_RES_HANDLER *handler;          /* Routine storing the resource item */
   union {
      char **value;                   /* Where to store the item */
      char **charvalue;
      uint32_t ui32value;
      int32_t i32value;
      uint64_t ui64value;
      int64_t i64value;
      bool boolvalue;
      utime_t utimevalue;
      RES *resvalue;
      RES **presvalue;
   };
   int32_t  code;                     /* item code/additional info */
   uint32_t  flags;                   /* flags: default, required, ... */
   int32_t  default_value;            /* default value */
};

/*
 * This handler takes only the RPKT as an argument
 */
struct RES_ITEM1 {
   const char *name;                  /* Resource name i.e. Director, ... */
   RES_HANDLER *handler;              /* Routine storing/displaying the resource */
   union {
      char **value;                   /* Where to store the item */
      char **charvalue;
      uint32_t ui32value;
      int32_t i32value;
      uint64_t ui64value;
      int64_t i64value;
      bool boolvalue;
      utime_t utimevalue;
      RES *resvalue;
      RES **presvalue;
   };
   int32_t  code;                     /* item code/additional info */
   uint32_t  flags;                   /* flags: default, required, ... */
   int32_t  default_value;            /* default value */
};

/* INC_RES_HANDLER has exclude argument */
struct RES_ITEM2 {
   const char *name;                  /* Resource name i.e. Director, ... */
   INC_RES_HANDLER *handler;          /* Routine storing the resource item */
   union {
      char **value;                   /* Where to store the item */
      char **charvalue;
      uint32_t ui32value;
      int32_t i32value;
      uint64_t ui64value;
      int64_t i64value;
      bool boolvalue;
      utime_t utimevalue;
      RES *resvalue;
      RES **presvalue;
   };
   int32_t  code;                     /* item code/additional info */
   uint32_t  flags;                   /* flags: default, required, ... */
   int32_t  default_value;            /* default value */
};


/* For storing name_addr items in res_items table */
#define ITEM(x) {(char **)&res_all.x}

#define MAX_RES_ITEMS 100             /* maximum resource items per RES */

class RES_HEAD {
public:
   rblist *res_list;                  /* Resource list */
   RES *first;                        /* First RES item in list */
   RES *last;                         /* Last RES item inserted */
};

/*
 * This is the universal header that is
 * at the beginning of every resource
 * record.
 */
class RES {
public:
   rblink link;                       /* red-black link */
   RES *res_next;                     /* pointer to next resource of this type */
   char *name;                        /* resource name */
   char *desc;                        /* resource description */
   uint32_t rcode;                    /* resource id or type */
   int32_t  refcnt;                   /* reference count for releasing */
   char  item_present[MAX_RES_ITEMS]; /* set if item is present in conf file */
};


/*
 * Master Resource configuration structure definition
 * This is the structure that defines the
 * resources that are available to this daemon.
 */
struct RES_TABLE {
   const char *name;                  /* resource name */
   RES_ITEM *items;                   /* list of resource keywords */
   uint32_t rcode;                    /* code if needed */
};

/* Common Resource definitions */

#define MAX_RES_NAME_LENGTH MAX_NAME_LENGTH-1       /* maximum resource name length */

/* Permitted bits in Flags field */
#define ITEM_REQUIRED    (1<<0)       /* item required */
#define ITEM_DEFAULT     (1<<1)       /* default supplied */
#define ITEM_NO_EQUALS   (1<<2)       /* Don't scan = after name */
#define ITEM_LAST        (1<<3)       /* Last item in list */
#define ITEM_ALLOW_DUPS  (1<<4)       /* Allow duplicate directives */

/* Message Resource */
class MSGS {
public:
   RES   hdr;
   char *mail_cmd;                    /* mail command */
   char *operator_cmd;                /* Operator command */
   DEST *dest_chain;                  /* chain of destinations */
   char send_msg[nbytes_for_bits(M_MAX+1)];  /* bit array of types */

private:
   bool m_in_use;                     /* set when using to send a message */
   bool m_closing;                    /* set when closing message resource */

public:
   /* Methods */
   char *name() const;
   void clear_in_use() { lock(); m_in_use=false; unlock(); }
   void set_in_use() { wait_not_in_use(); m_in_use=true; unlock(); }
   void set_closing() { m_closing=true; }
   bool get_closing() { return m_closing; }
   void clear_closing() { lock(); m_closing=false; unlock(); }
   bool is_closing() { lock(); bool rtn=m_closing; unlock(); return rtn; }

   void wait_not_in_use();            /* in message.c */
   void lock();                       /* in message.c */
   void unlock();                     /* in message.c */
};

inline char *MSGS::name() const { return hdr.name; }

/*
 * New C++ configuration routines
 */

class CONFIG: public SMARTALLOC {
public:
   const char *m_cf;                   /* config file */
   LEX_ERROR_HANDLER *m_scan_error;    /* error handler if non-null */
   int32_t m_err_type;                 /* the way to terminate on failure */
   void *m_res_all;                    /* pointer to res_all buffer */
   int32_t m_res_all_size;             /* length of buffer */
   bool  m_encode_pass;                /* Encode passwords with MD5 or not */

   /* The below are not yet implemented */
   int32_t m_r_first;                  /* first daemon resource type */
   int32_t m_r_last;                   /* last daemon resource type */
   RES_TABLE *m_resources;             /* pointer to table of permitted resources */
   RES_HEAD **m_res_head;              /* pointer to list of resources this type */
   brwlock_t m_res_lock;               /* resource lock */
   POOLMEM *m_errmsg;

   /* functions */
   void init(
      const char *cf,
      LEX_ERROR_HANDLER *scan_error,
      int32_t err_type,
      void *vres_all,
      int32_t res_all_size,
      int32_t r_first,
      int32_t r_last,
      RES_TABLE *resources,
      RES_HEAD ***res_head);

   CONFIG();
   ~CONFIG();
   void encode_password(bool encode);
   bool parse_config();
   void free_all_resources();
   bool insert_res(int rindex, int size);
   RES_HEAD **save_resources();
   RES_HEAD **new_res_head();
   void init_res_head(RES_HEAD ***rhead, int32_t first, int32_t last);
};

/* Resource routines */
int res_compare(void *item1, void *item2);
RES *GetResWithName(int rcode, const char *name);
RES *GetNextRes(int rcode, RES *res);
RES *GetNextRes(RES_HEAD **rhead, int rcode, RES *res);
void b_LockRes(const char *file, int line);
void b_UnlockRes(const char *file, int line);
void dump_resource(int type, RES *res, void sendmsg(void *sock, const char *fmt, ...), void *sock);
void dump_each_resource(int type, void sendmsg(void *sock, const char *fmt, ...), void *sock);
void free_resource(RES *res, int type);
bool init_resource(CONFIG *config, uint32_t type, void *res);
bool save_resource(CONFIG *config, int type, RES_ITEM *item, int pass);
void unstrip_password(RES_TABLE *resources); /* Used for json stuff */
void strip_password(RES_TABLE *resources);   /* Used for tray monitor */
const char *res_to_str(int rcode);
bool find_config_file(const char *config_file, char *full_path, int max_path);

/* Loop through each resource of type, returning in var */
#ifdef HAVE_TYPEOF
#define foreach_res(var, type) \
        for((var)=NULL; ((var)=(typeof(var))GetNextRes((type), (RES *)var));)
#else
#define foreach_res(var, type) \
    for(var=NULL; (*((void **)&(var))=(void *)GetNextRes((type), (RES *)var));)
#endif


/*
 * Standard global parsers defined in parse_config.c
 */
void store_str(LEX *lc, RES_ITEM *item, int index, int pass);
void store_dir(LEX *lc, RES_ITEM *item, int index, int pass);
void store_clear_password(LEX *lc, RES_ITEM *item, int index, int pass);
void store_password(LEX *lc, RES_ITEM *item, int index, int pass);
void store_name(LEX *lc, RES_ITEM *item, int index, int pass);
void store_strname(LEX *lc, RES_ITEM *item, int index, int pass);
void store_res(LEX *lc, RES_ITEM *item, int index, int pass);
void store_alist_res(LEX *lc, RES_ITEM *item, int index, int pass);
void store_alist_str(LEX *lc, RES_ITEM *item, int index, int pass);
void store_int32(LEX *lc, RES_ITEM *item, int index, int pass);
void store_pint32(LEX *lc, RES_ITEM *item, int index, int pass);
void store_msgs(LEX *lc, RES_ITEM *item, int index, int pass);
void store_int64(LEX *lc, RES_ITEM *item, int index, int pass);
void store_bit(LEX *lc, RES_ITEM *item, int index, int pass);
void store_bool(LEX *lc, RES_ITEM *item, int index, int pass);
void store_time(LEX *lc, RES_ITEM *item, int index, int pass);
void store_size64(LEX *lc, RES_ITEM *item, int index, int pass);
void store_size32(LEX *lc, RES_ITEM *item, int index, int pass);
void store_speed(LEX *lc, RES_ITEM *item, int index, int pass);
void store_defs(LEX *lc, RES_ITEM *item, int index, int pass);
void store_label(LEX *lc, RES_ITEM *item, int index, int pass);

/* ***FIXME*** eliminate these globals */
extern int32_t r_first;
extern int32_t r_last;
extern RES_TABLE resources[];
extern RES_HEAD **res_head;
extern int32_t res_all_size;
