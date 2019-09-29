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

#include "bacula.h"
#include "dird.h"

static char CreateSnap[] = "CatReq Job=%127s new_snapshot name=%127s volume=%s device=%s tdate=%d type=%127s retention=%50s";
static char ListSnap[] = "CatReq Job=%127s list_snapshot name=%127s volume=%s device=%s tdate=%d type=%127s before=%50s after=%50s";
static char DelSnap[] = "CatReq Job=%127s del_snapshot name=%127s device=%s";
static char snapretentioncmd[] = "snapshot retention=%s\n";


static void send_list(void *ctx, const char *msg)
{
   BSOCK *bs = (BSOCK *)ctx;
   bs->fsend("%s", msg);
}


/* Scan command line for common snapshot arguments */
static void snapshot_scan_cmdline(UAContext *ua, int start, SNAPSHOT_DBR *snapdbr)
{
   for (int j=start; j<ua->argc; j++) {
      if (strcasecmp(ua->argk[j], NT_("device")) == 0 && ua->argv[j]) {
         snapdbr->Device = bstrdup(ua->argv[j]);
         snapdbr->need_to_free = true;

      } else if (strcasecmp(ua->argk[j], NT_("jobid")) == 0 && ua->argv[j]) {
         snapdbr->JobId = str_to_int64(ua->argv[j]);

      } else if (strcasecmp(ua->argk[j], NT_("type")) == 0 && ua->argv[j]) {
         bstrncpy(snapdbr->Type, ua->argv[j], sizeof(snapdbr->Type));

      } else if (strcasecmp(ua->argk[j], NT_("client")) == 0 && ua->argv[j]) {
         bstrncpy(snapdbr->Client, ua->argv[j], sizeof(snapdbr->Client));

      } else if (strcasecmp(ua->argk[j], NT_("snapshotid")) == 0 && ua->argv[j]) {
         snapdbr->SnapshotId = str_to_int64(ua->argv[j]);

      } else if (strcasecmp(ua->argk[j], NT_("snapshot")) == 0 && ua->argv[j]) {
         bstrncpy(snapdbr->Name, ua->argv[j], sizeof(snapdbr->Name));

      } else if (strcasecmp(ua->argk[j], NT_("volume")) == 0 && ua->argv[j]) {
         snapdbr->Volume = bstrdup(ua->argv[j]);
         snapdbr->need_to_free = true;

      } else if (strcasecmp(ua->argk[j], NT_("createdate")) == 0 && ua->argv[j]) {
         bstrncpy(snapdbr->CreateDate, ua->argv[j], sizeof(snapdbr->CreateDate));
         snapdbr->CreateTDate = str_to_utime(ua->argv[j]);

      } else if (strcasecmp(ua->argk[j], NT_("createtdate")) == 0 && ua->argv[j]) {
         snapdbr->CreateTDate = str_to_uint64(ua->argv[j]);
         bstrutime(snapdbr->CreateDate, sizeof(snapdbr->CreateDate), snapdbr->CreateTDate);

      } else if (strcasecmp(ua->argk[j], NT_("name")) == 0 && ua->argv[j]) {
         bstrncpy(snapdbr->Name, ua->argv[j], sizeof(snapdbr->Name));

      } else if (strcasecmp(ua->argk[j], NT_("size")) == 0 && ua->argv[j]) {
         snapdbr->Size = str_to_uint64(ua->argv[j]);

      } else if (strcasecmp(ua->argk[j], NT_("status")) == 0 && ua->argv[j]) {
         snapdbr->status = str_to_uint64(ua->argv[j]);

      } else if (strcasecmp(ua->argk[j], NT_("error")) == 0 && ua->argv[j]) {
         snapdbr->errmsg = bstrdup(ua->argv[j]);
         unbash_spaces(snapdbr->errmsg);
         snapdbr->need_to_free = true;

      } else {
         continue;
      }
   }
}

/* Get a snapshot record, and check that the current UA can access to the Client/FileSet */
static int get_snapshot_record(UAContext *ua, SNAPSHOT_DBR *snapdbr)
{
   if (!open_client_db(ua)) {
      Dmsg0(10, "Unable to open database\n");
      return 0;
   }
   if (!db_get_snapshot_record(ua->jcr, ua->db, snapdbr)) {
      Dmsg0(10, "Unable to get snapshot record\n");
      return 0;
   }
   /* Need to check if the client is authorized */
   if (!acl_access_client_ok(ua, snapdbr->Client, JT_BACKUP_RESTORE)) {
      Dmsg0(10, "Client access denied\n");
      return 0;
   }
   if (snapdbr->FileSetId && !acl_access_ok(ua, FileSet_ACL, snapdbr->FileSet)) {
      Dmsg0(10, "Fileset access denied\n");
      return 0;
   }
   return 1;
}

static int check_response(UAContext *ua, BSOCK *sd, const char *resp, const char *cmd)
{
   if (sd->errors) {
      return 0;
   }
   if (bget_msg(sd) > 0) {
      unbash_spaces(sd->msg);
      if (strcmp(sd->msg, resp) == 0) {
         return 1;
      }
   }
   if (sd->is_error()) {
      ua->error_msg(_("Comm error with SD. bad response to %s. ERR=%s\n"),
                    cmd, sd->bstrerror());
   } else {
      ua->error_msg(_("Bad response from SD to %s command. Wanted %s, got %s len=%ld\n"),
                    cmd, resp, sd->msg, sd->msglen);
   }
   return 0;
}

bool send_snapshot_retention(JCR *jcr, utime_t val)
{
   BSOCK *fd = jcr->file_bsock;
   char ed1[50];
   if (val > 0 && jcr->FDVersion >= 13) {
      fd->fsend(snapretentioncmd, edit_uint64(val, ed1));
      if (!response(jcr, fd, (char*)"2000 Snapshot retention\n", "set Snapshot Retention", DISPLAY_ERROR)) {
         jcr->snapshot_retention = 0;      /* can't set snapshot retention */
         return false;
      }
   }
   return true;
}

/* Called from delete_cmd() in ua_cmd.c */
int delete_snapshot(UAContext *ua)
{
   POOL_MEM     buf;
   POOLMEM     *out;
   SNAPSHOT_DBR snapdbr;
   CLIENT      *client;
   BSOCK       *fd;

   if (!open_new_client_db(ua)) {
      return 1;
   }

   /* If the client or the fileset are not authorized,
    * the function will fail.
    */
   if (!select_snapshot_dbr(ua, &snapdbr)) {
      ua->error_msg(_("Snapshot not found\n"));
      snapdbr.debug(0);
      return 0;
   }

   client = (CLIENT *)GetResWithName(R_CLIENT, snapdbr.Client);
   if (!client) {
      ua->error_msg(_("Client resource not found\n"));
      return 0;
   }

   /* Connect to File daemon */
   ua->jcr->client = client;

   /* Try to connect for 15 seconds */
   ua->send_msg(_("Connecting to Client %s at %s:%d\n"),
                client->name(), client->address(buf.addr()), client->FDport);
   if (!connect_to_file_daemon(ua->jcr, 1, 15, 0)) {
      ua->error_msg(_("Failed to connect to Client.\n"));
      ua->jcr->client = NULL;
      return 0;
   }

   fd = ua->jcr->file_bsock;
   out = get_pool_memory(PM_FNAME);
   fd->fsend("snapshot del %s\n", snapdbr.as_arg(&out));
   free_pool_memory(out);

   /* If the snapshot is not found, still delete ours */
   if (check_response(ua, fd, "2000 Snapshot deleted ERR=\n", "Snapshot")) {
      ua->send_msg(_("Snapshot \"%s\" deleted from client %s\n"), snapdbr.Name,
                   snapdbr.Client);
   }

   ua->jcr->file_bsock->signal(BNET_TERMINATE);
   free_bsock(ua->jcr->file_bsock);
   ua->jcr->client = NULL;

   db_delete_snapshot_record(ua->jcr, ua->db, &snapdbr);
   ua->send_msg(_("Snapshot \"%s\" deleted from catalog\n"), snapdbr.Name);
   return 1;
}

/* Called from menu, if snap_list is valid, the snapshot
 * list will be stored in this list. (not_owned_by_alist)
 */
int list_snapshot(UAContext *ua, alist *snap_list)
{
   POOL_MEM     tmp;
   SNAPSHOT_DBR snap;
   POOLMEM     *buf;
   CLIENT      *client;
   BSOCK       *fd;

   client = select_client_resource(ua, JT_BACKUP_RESTORE);
   if (!client) {
      return 0;
   }

   /* Connect to File daemon */
   ua->jcr->client = client;

   /* Try to connect for 15 seconds */
   ua->send_msg(_("Connecting to Client %s at %s:%d\n"),
                client->name(), client->address(tmp.addr()), client->FDport);

   if (!connect_to_file_daemon(ua->jcr, 1, 15, 0)) {
      ua->error_msg(_("Failed to connect to Client.\n"));
      return 0;
   }

   fd = ua->jcr->file_bsock;

   /* The command line can have filters */
   snapshot_scan_cmdline(ua, 0, &snap);
   buf = get_pool_memory(PM_FNAME);

   fd->fsend("snapshot list %s\n", snap.as_arg(&buf));
   while (fd->recv() >= 0) {
      if (snap_list) {
         SNAPSHOT_DBR *snapr = new SNAPSHOT_DBR();
         parse_args(fd->msg, &ua->args, &ua->argc, ua->argk, ua->argv, MAX_CMD_ARGS);
         snapshot_scan_cmdline(ua, 0, snapr);
         bstrncpy(snapr->Client, client->name(), sizeof(snapr->Client));
         snap_list->append(snapr);
         snapr->debug(0);
      } else {
         ua->send_msg("%s", fd->msg);
      }
   }

   /* Reset the UA arg list */
   parse_args(ua->cmd, &ua->args, &ua->argc, ua->argk, ua->argv, MAX_CMD_ARGS);

   ua->jcr->file_bsock->signal(BNET_TERMINATE);
   free_bsock(ua->jcr->file_bsock);
   ua->jcr->client = NULL;
   free_pool_memory(buf);
   return 1;
}

static void storeit(void *ctx, const char *msg)
{
   char ed1[51];
   alist *lst = (alist *)ctx;
   if (sscanf(msg, "snapshotid=%50s", ed1) == 1) {
      lst->append((void *)(intptr_t) str_to_int64(ed1));
   }
}

int prune_snapshot(UAContext *ua)
{
   /* First, we get the snapshot list that can be pruned */
   CLIENT *client = NULL;
   BSOCK  *fd = NULL;
   POOLMEM *buf = NULL;
   POOL_MEM tmp;
   SNAPSHOT_DBR snapdbr;
   alist *lst;
   intptr_t id;

   snapshot_scan_cmdline(ua, 0, &snapdbr);
   snapdbr.expired = true;
   if (!open_client_db(ua)) {
      Dmsg0(10, "Unable to open database\n");
      return 0;
   }

   buf = get_pool_memory(PM_FNAME);
   lst = New(alist(10, not_owned_by_alist));
   db_list_snapshot_records(ua->jcr, ua->db, &snapdbr, storeit, lst, ARG_LIST);
   foreach_alist(id, lst) {
      snapdbr.reset();
      snapdbr.SnapshotId = id;
      if (get_snapshot_record(ua, &snapdbr)) {

         ua->send_msg(_("Snapshot \"%s\" on Client %s\n"), snapdbr.Name, snapdbr.Client);
         if (!confirm_retention_yesno(ua, snapdbr.Retention, "Snapshot")) {
            continue;
         }

         if (client && strcmp(client->hdr.name, snapdbr.Client) != 0) {
            ua->jcr->file_bsock->signal(BNET_TERMINATE);
            free_bsock(ua->jcr->file_bsock);
            ua->jcr->client = NULL;
            client = NULL;
         }

         if (!client) {
            client = (CLIENT *)GetResWithName(R_CLIENT, snapdbr.Client);
            if (!client) {
               continue;
            }

            /* Connect to File daemon */
            ua->jcr->client = client;

            /* Try to connect for 15 seconds */
            ua->send_msg(_("Connecting to Client %s at %s:%d\n"),
                         client->name(), client->address(tmp.addr()), client->FDport);
            if (!connect_to_file_daemon(ua->jcr, 1, 15, 0)) {
               ua->error_msg(_("Failed to connect to Client.\n"));
               free_bsock(ua->jcr->file_bsock);
               ua->jcr->client = NULL;
               client = NULL;
               continue;
            }

            fd = ua->jcr->file_bsock;
         }

         fd->fsend("snapshot del %s\n", snapdbr.as_arg(&buf));

         fd->recv();
         if (strncmp(fd->msg, "2000", 4) == 0) {
            ua->send_msg("Snapshot %s deleted\n", snapdbr.Volume);
            db_delete_snapshot_record(ua->jcr, ua->db, &snapdbr);
         } else {
            unbash_spaces(fd->msg);
            ua->send_msg("%s", fd->msg);
         }
      }
   }

   if (ua->jcr->file_bsock) {
      ua->jcr->file_bsock->signal(BNET_TERMINATE);
      free_bsock(ua->jcr->file_bsock);
      ua->jcr->client = NULL;
   }

   free_pool_memory(buf);
   delete lst;
   return 1;
}


/* Called from the FD, in catreq.c */
int snapshot_catreq(JCR *jcr, BSOCK *bs)
{
   SNAPSHOT_DBR snapdbr;
   char Job[MAX_NAME_LENGTH], ed1[50];
   POOLMEM *vol = get_memory(bs->msglen);
   POOLMEM *dev = get_memory(bs->msglen);
   POOLMEM *err = get_pool_memory(PM_MESSAGE);
   int n, ret = 1, expired;
   *vol = *dev = 0;

   Dmsg1(DT_SNAPSHOT|10, "Get snapshot catalog request %s\n", bs->msg);

   /* We need to create a snapshot record in the catalog */
   n = sscanf(bs->msg, CreateSnap, Job, snapdbr.Name, vol, dev, 
              &snapdbr.CreateTDate, snapdbr.Type, ed1);
   if (n == 7) {
      snapdbr.Volume = vol;
      snapdbr.Device = dev;
      snapdbr.JobId = jcr->JobId;
      unbash_spaces(snapdbr.Name);
      unbash_spaces(snapdbr.Volume);
      unbash_spaces(snapdbr.Device);
      snapdbr.Retention = str_to_uint64(ed1);
      bstrftimes(snapdbr.CreateDate, sizeof(snapdbr.CreateDate), snapdbr.CreateTDate);
      unbash_spaces(snapdbr.Type);
      bstrncpy(snapdbr.Client, jcr->client->hdr.name, sizeof(snapdbr.Client));
      bstrncpy(snapdbr.FileSet, (jcr->fileset)?jcr->fileset->hdr.name:"", sizeof(snapdbr.FileSet));

      Dmsg1(DT_SNAPSHOT|10, "Creating snapshot %s\n", snapdbr.Name);
      snapdbr.debug(20);

      /* We lock the db before to keep the error message */
      db_lock(jcr->db);
      ret = db_create_snapshot_record(jcr, jcr->db, &snapdbr);
      pm_strcpy(err, jcr->db->errmsg);
      db_unlock(jcr->db);

      if (ret) {
         bs->fsend("1000 Snapshot created\n");

      } else {
         bs->fsend("1999 Snapshot not created ERR=%s\n", err);
      }
      goto bail_out;
   }

   n = sscanf(bs->msg, ListSnap, Job, snapdbr.Name, vol, dev, &snapdbr.CreateTDate, snapdbr.Type,
              snapdbr.created_before, snapdbr.created_after, &expired);
   if (n == 8) {
      snapdbr.Volume = vol;
      snapdbr.Device = dev;
      unbash_spaces(snapdbr.Name);
      unbash_spaces(snapdbr.Volume);
      unbash_spaces(snapdbr.Device);
      bstrftimes(snapdbr.CreateDate, sizeof(snapdbr.CreateDate), snapdbr.CreateTDate);
      unbash_spaces(snapdbr.Type);
      unbash_spaces(snapdbr.created_before);
      unbash_spaces(snapdbr.created_after);
      bstrncpy(snapdbr.Client, jcr->client->hdr.name, sizeof(snapdbr.Client));
      snapdbr.expired = (expired != 0);
      Dmsg0(DT_SNAPSHOT|10, "List snapshots\n");
      snapdbr.debug(20);
      db_list_snapshot_records(jcr, jcr->db, &snapdbr, send_list, bs, ARG_LIST);
      bs->signal(BNET_EOD);
      goto bail_out;
   }

   n = sscanf(bs->msg, DelSnap, Job, snapdbr.Name, dev);
   if (n == 3) {
      snapdbr.Device = dev;
      unbash_spaces(snapdbr.Name);
      unbash_spaces(snapdbr.Device);
      bstrncpy(snapdbr.Client, jcr->client->hdr.name, sizeof(snapdbr.Client));
      Dmsg2(DT_SNAPSHOT|10, "Delete snapshot %s from %s\n", snapdbr.Name, snapdbr.Client);
      snapdbr.debug(20);      

      /* We lock the db before to keep the error message */
      db_lock(jcr->db);
      ret = db_delete_snapshot_record(jcr, jcr->db, &snapdbr);
      pm_strcpy(err, jcr->db->errmsg);
      db_unlock(jcr->db);

      if (ret) {
         bs->fsend("1000 Snapshot deleted\n");

      } else {
         bs->fsend("1999 Snapshot not deleted ERR=%s\n", err);
      }
      goto bail_out;
   }
   ret = 0;

bail_out:
   free_pool_memory(vol);
   free_pool_memory(dev);
   free_pool_memory(err);
   return ret;
}

/* List snapshots, allow to use some parameters from the command line */
void snapshot_list(UAContext *ua, int i, DB_LIST_HANDLER *sendit, e_list_type llist)
{
   SNAPSHOT_DBR snapdbr;
   snapshot_scan_cmdline(ua, i, &snapdbr);
   if (open_new_client_db(ua)) {
      db_list_snapshot_records(ua->jcr, ua->db, &snapdbr, sendit, ua, llist);
   }
}

static int list_client_snapshot(UAContext *ua, bool sync)
{
   SNAPSHOT_DBR *s, stemp;
   alist *lst;
//   char ed1[50];

   if (sync) {
      if (!open_new_client_db(ua)) {
         return 1;
      }
   }

   lst = New(alist(10, not_owned_by_alist));
   if (list_snapshot(ua, lst)) {
      foreach_alist(s, lst) {
         ua->send_msg(_(
            "Snapshot      %s:\n"
            "  Volume:     %s\n"
            "  Device:     %s\n"
            "  CreateDate: %s\n"
//            "  Size:       %sB\n",
            "  Type:       %s\n"
            "  Status:     %s\n"
            "  Error:      %s\n"),
            s->Name, NPRT(s->Volume), NPRT(s->Device),
            s->CreateDate,
//          edit_uint64_with_suffix(s->Size, ed1),
                      s->Type, s->status?_("OK"):_("Error"), s->errmsg);
         if (sync && s->Device && *s->Name) {
            stemp.reset();
            stemp.Device = s->Device;
            bstrncpy(stemp.Name, s->Name, sizeof(stemp.Name));
            if (!db_get_snapshot_record(ua->jcr, ua->db, &stemp)) {
               if (db_create_snapshot_record(ua->jcr, ua->db, s)) {
                  ua->send_msg(_("Snapshot added in Catalog\n"));
               }
            }
         }
      }
      if (lst->size() == 0) {
         ua->send_msg(_("No snapshot found\n"));
      }
   }
   /* Cleanup the list */
   foreach_alist (s, lst) {
      delete s;
   }
   delete lst;
   return 1;
}

/* Ask client to create/prune/delete a snapshot via the command line */
int snapshot_cmd(UAContext *ua, const char *cmd)
{
   SNAPSHOT_DBR snapdbr;
   for (int i=0; i<ua->argc; i++) {
      if (strcasecmp(ua->argk[i], NT_("purge")) == 0) {

      } else if (strcasecmp(ua->argk[i], NT_("prune")) == 0) {
         return prune_snapshot(ua);

      } else if (strcasecmp(ua->argk[i], NT_("listclient")) == 0) {
         return list_client_snapshot(ua, false);

      } else if (strcasecmp(ua->argk[i], NT_("list")) == 0) {
         snapshot_list(ua, 0, prtit, HORZ_LIST);
         return 1;

      } else if (strcasecmp(ua->argk[i], NT_("create")) == 0) {
         /* We need a job definition, or a client */

      } else if (strcasecmp(ua->argk[i], NT_("delete")) == 0) {
         return delete_snapshot(ua);

      } else if (strcasecmp(ua->argk[i], NT_("status")) == 0) {

      } else if (strcasecmp(ua->argk[i], NT_("sync")) == 0) {
         return list_client_snapshot(ua, true);

      } else if (strcasecmp(ua->argk[i], NT_("update")) == 0) {
         return update_snapshot(ua);

      } else {
         continue;
      }
   }

   for ( ;; ) {

      start_prompt(ua, _("Snapshot choice: \n"));
      add_prompt(ua, _("List snapshots in Catalog"));
      add_prompt(ua, _("List snapshots on Client"));
      add_prompt(ua, _("Prune snapshots"));
      add_prompt(ua, _("Delete snapshot"));
      add_prompt(ua, _("Update snapshot parameters"));
      add_prompt(ua, _("Update catalog with Client snapshots"));
      add_prompt(ua, _("Done"));

      switch(do_prompt(ua, "", _("Select action to perform on Snapshot Engine"), NULL, 0)) {
      case 0:                         /* list catalog */
         snapshot_list(ua, 0, prtit, HORZ_LIST);
         break;
      case 1:                         /* list client */
         list_client_snapshot(ua, false);
         break;
      case 2:                   /* prune */
         prune_snapshot(ua);
         break;
      case 3:                   /* delete */
         delete_snapshot(ua);
         break;
      case 4:                      /* update snapshot */
         update_snapshot(ua);
         break;
      case 5:                      /* sync snapshot */
         list_client_snapshot(ua, true);
         break;
      case 6:                   /* done */
      default:
         ua->info_msg(_("Selection terminated.\n"));
         return 1;
      }
   }
   return 1;
}

/* Select a Snapshot record from the database, might be in ua_select.c */
int select_snapshot_dbr(UAContext *ua, SNAPSHOT_DBR *sr)
{
   int   ret = 0;
   char *p;
   POOLMEM *err = get_pool_memory(PM_FNAME);
   *err=0;

   sr->reset();
   snapshot_scan_cmdline(ua, 0, sr);

   if (sr->SnapshotId == 0 && (sr->Name[0] == 0 || sr->Client[0] == 0)) {
      CLIENT_DBR cr;
      memset(&cr, 0, sizeof(cr));
      /* Get the pool from client=<client-name> */
      if (!get_client_dbr(ua, &cr, JT_BACKUP_RESTORE)) {
         goto bail_out;
      }
      sr->ClientId = cr.ClientId;
      db_list_snapshot_records(ua->jcr, ua->db, sr, prtit, ua, HORZ_LIST);
      if (!get_cmd(ua, _("Enter a SnapshotId: "))) {
         goto bail_out;
      }
      p = ua->cmd;
      if (*p == '*') {
         p++;
      }
      if (is_a_number(p)) {
         sr->SnapshotId = str_to_int64(p);
      } else {
         goto bail_out;
      }
   }

   if (!get_snapshot_record(ua, sr)) {
      ua->error_msg(_("Unable to get Snapshot record.\n"));
      goto bail_out;
   }

   ret = 1;

bail_out:
   if (!ret && *err) {
      ua->error_msg("%s", err);
   }
   free_pool_memory(err);
   return ret;
}

/* This part should be in ua_update.c */
static void update_snapretention(UAContext *ua, char *val, SNAPSHOT_DBR *sr)
{
   char ed1[150];
   POOL_MEM tmp(PM_MESSAGE);
   bool ret;
   if (!duration_to_utime(val, &sr->Retention)) {
      ua->error_msg(_("Invalid retention period specified: %s\n"), val);
      return;
   }

   db_lock(ua->db);
   if (!(ret = db_update_snapshot_record(ua->jcr, ua->db, sr))) {
      pm_strcpy(tmp, db_strerror(ua->db));
   }
   db_unlock(ua->db);

   if (!ret) {
      ua->error_msg("%s", tmp.c_str());

   } else {
      ua->info_msg(_("New retention period is: %s\n"),
         edit_utime(sr->Retention, ed1, sizeof(ed1)));
   }
}

/* This part should be in ua_update.c */
static void update_snapcomment(UAContext *ua, char *val, SNAPSHOT_DBR *sr)
{
   POOL_MEM tmp(PM_MESSAGE);
   bool ret;

   bstrncpy(sr->Comment, val, sizeof(sr->Comment));

   db_lock(ua->db);
   if (!(ret = db_update_snapshot_record(ua->jcr, ua->db, sr))) {
      pm_strcpy(tmp, db_strerror(ua->db));
   }
   db_unlock(ua->db);

   if (!ret) {
      ua->error_msg("%s", tmp.c_str());

   } else {
      ua->info_msg(_("New Comment is: %s\n"), sr->Comment);
   }
}

/* This part should be in ua_update.c */
bool update_snapshot(UAContext *ua)
{
   SNAPSHOT_DBR sr;
   POOL_MEM ret;
   char ed1[130];
   bool done = false;
   int i;
   const char *kw[] = {
      NT_("Retention"),                /* 0 */
      NT_("Comment"),                  /* 1 */
      NULL };

   for (i=0; kw[i]; i++) {
      int j;
      if ((j=find_arg_with_value(ua, kw[i])) > 0) {
         /* If all from pool don't select a media record */
         if (!select_snapshot_dbr(ua, &sr)) {
            return 0;
         }
         switch (i) {
         case 0:
            update_snapretention(ua, ua->argv[j], &sr);
            break;
         case 1:
            update_snapcomment(ua, ua->argv[j], &sr);
            break;
         default:
            break;
         }
         done = true;
      }
   }

   for ( ; !done; ) {
      start_prompt(ua, _("Parameters to modify:\n"));
      add_prompt(ua, _("Snapshot Retention Period"));  /* 0 */
      add_prompt(ua, _("Snapshot Comment"));           /* 1 */
      add_prompt(ua, _("Done"));                       /* 2 */
      i = do_prompt(ua, "", _("Select parameter to modify"), NULL, 0);
      if (i == 2) {
         return 0;
      }

      if (!select_snapshot_dbr(ua, &sr)) {  /* Get Snapshot record */
         return 0;
      }
      ua->info_msg(_("Updating Snapshot \"%s\" on \"%s\"\n"), sr.Name, sr.Client);

      switch (i) {
      case 0:                         /* Snapshot retention */
         ua->info_msg(_("Current retention period is: %s\n"),
            edit_utime(sr.Retention, ed1, sizeof(ed1)));
         if (!get_cmd(ua, _("Enter Snapshot Retention period: "))) {
            return 0;
         }
         update_snapretention(ua, ua->cmd, &sr);
         break;
      case 1:
         ua->info_msg(_("Current comment is: %s\n"), NPRTB(sr.Comment));
         if (!get_cmd(ua, _("Enter Snapshot comment: "))) {
            return 0;
         }
         update_snapcomment(ua, ua->cmd, &sr);
         break;
      default:                        /* Done or error */
         ua->info_msg(_("Selection terminated.\n"));
         return 1;
      }
   }
   return 1;
}
