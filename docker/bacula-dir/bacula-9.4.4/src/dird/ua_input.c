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
/*
 *
 *   Bacula Director -- User Agent Input and scanning code
 *
 *     Kern Sibbald, October MMI
 *
 */

#include "bacula.h"
#include "dird.h"


/* Imported variables */


/* Exported functions */

/*
 * If subprompt is set, we send a BNET_SUB_PROMPT signal otherwise
 *   send a BNET_TEXT_INPUT signal.
 */
bool get_cmd(UAContext *ua, const char *prompt, bool subprompt)
{
   BSOCK *sock = ua->UA_sock;
   int stat;

   ua->cmd[0] = 0;
   if (!sock || ua->batch) {          /* No UA or batch mode */
      return false;
   }
   if (!subprompt && ua->api) {
      sock->signal(BNET_TEXT_INPUT);
   }
   sock->fsend("%s", prompt);
   if (!ua->api || subprompt) {
      sock->signal(BNET_SUB_PROMPT);
   }
   for ( ;; ) {
      stat = sock->recv();
      if (stat == BNET_SIGNAL) {
         continue;                    /* ignore signals */
      }
      if (sock->is_stop()) {
         return false;               /* error or terminate */
      }
      pm_strcpy(ua->cmd, sock->msg);
      strip_trailing_junk(ua->cmd);
      if (strcmp(ua->cmd, ".messages") == 0) {
         qmessagescmd(ua, ua->cmd);
      }
      /* Lone dot => break */
      if (ua->cmd[0] == '.' && ua->cmd[1] == 0) {
         return false;
      }
      break;
   }
   return true;
}

/*
 * Get a selection list
 *  We get a command from the user, scan it, then
 *  return when OK
 * Returns true if OK
 *         false if error
 */
bool get_selection_list(UAContext *ua, sellist &sl,
                        const char *prompt, bool subprompt)
{
   for ( ;; ) {
      if (!get_cmd(ua, prompt, subprompt)) {
         return false;
      }
      if (!sl.set_string(ua->cmd, true)) {
         ua->send_msg("%s", sl.get_errmsg());
         continue;
      }
      return true;
   }
}

/*
 * Get a positive integer
 *  Returns:  false if failure
 *            true  if success => value in ua->pint32_val
 */
bool get_pint(UAContext *ua, const char *prompt)
{
   double dval;
   ua->pint32_val = 0;
   ua->int64_val = 0;
   for (;;) {
      ua->cmd[0] = 0;
      if (!get_cmd(ua, prompt)) {
         return false;
      }
      /* Kludge for slots blank line => 0 */
      if (ua->cmd[0] == 0 && strncmp(prompt, _("Enter slot"), strlen(_("Enter slot"))) == 0) {
         return true;
      }
      if (!is_a_number(ua->cmd)) {
         ua->warning_msg(_("Expected a positive integer, got: %s\n"), ua->cmd);
         continue;
      }
      errno = 0;
      dval = strtod(ua->cmd, NULL);
      if (errno != 0 || dval < 0) {
         ua->warning_msg(_("Expected a positive integer, got: %s\n"), ua->cmd);
         continue;
      }
      ua->pint32_val = (uint32_t)dval;
      ua->int64_val = (int64_t)dval;
      return true;
   }
}

/*
 * Test a yes or no response
 *  Returns:  false if failure
 *            true  if success => ret == 1 for yes
 *                                ret == 0 for no
 */
bool is_yesno(char *val, int *ret)
{
   *ret = 0;
   if ((strcasecmp(val,   _("yes")) == 0) ||
       (strcasecmp(val, NT_("yes")) == 0))
   {
      *ret = 1;
   } else if ((strcasecmp(val,   _("no")) == 0) ||
              (strcasecmp(val, NT_("no")) == 0))
   {
      *ret = 0;
   } else {
      return false;
   }

   return true;
}

/*
 * Gets a yes or no response
 *  Returns:  false if failure
 *            true  if success => ua->pint32_val == 1 for yes
 *                                ua->pint32_val == 0 for no
 */
bool get_yesno(UAContext *ua, const char *prompt)
{
   int len;
   int ret;
   ua->pint32_val = 0;
   for (;;) {
      if (ua->api) ua->UA_sock->signal(BNET_YESNO);
      if (!get_cmd(ua, prompt)) {
         return false;
      }
      len = strlen(ua->cmd);
      if (len < 1 || len > 3) {
         continue;
      }
      if (is_yesno(ua->cmd, &ret)) {
         ua->pint32_val = ret;
         return true;
      }
      ua->warning_msg(_("Invalid response. You must answer yes or no.\n"));
   }
}

/*
 *  Gets an Enabled value => 0, 1, 2, yes, no, archived
 *  Returns: 0, 1, 2 if OK
 *           -1 on error
 */
int get_enabled(UAContext *ua, const char *val)
{
   int Enabled = -1;

   if (strcasecmp(val, "yes") == 0 || strcasecmp(val, "true") == 0) {
     Enabled = 1;
   } else if (strcasecmp(val, "no") == 0 || strcasecmp(val, "false") == 0) {
      Enabled = 0;
   } else if (strcasecmp(val, "archived") == 0) {
      Enabled = 2;
   } else {
      Enabled = atoi(val);
   }
   if (Enabled < 0 || Enabled > 2) {
      ua->error_msg(_("Invalid Enabled value, it must be yes, no, archived, 0, 1, or 2\n"));
      return -1;
   }
   return Enabled;
}

void parse_ua_args(UAContext *ua)
{
   parse_args(ua->cmd, &ua->args, &ua->argc, ua->argk, ua->argv, MAX_CMD_ARGS);
}

/*
 * Check if the comment has legal characters
 * If ua is non-NULL send the message
 */
bool is_comment_legal(UAContext *ua, const char *name)
{
   int len;
   const char *p;
   const char *forbid = "'<>&\\\"";

   /* Restrict the characters permitted in the comment */
   for (p=name; *p; p++) {
      if (!strchr(forbid, (int)(*p))) {
         continue;
      }
      if (ua) {
         ua->error_msg(_("Illegal character \"%c\" in a comment.\n"), *p);
      }
      return 0;
   }
   len = strlen(name);
   if (len >= MAX_NAME_LENGTH) {
      if (ua) {
         ua->error_msg(_("Comment too long.\n"));
      }
      return 0;
   }
   if (len == 0) {
      if (ua) {
         ua->error_msg(_("Comment must be at least one character long.\n"));
      }
      return 0;
   }
   return 1;
}
