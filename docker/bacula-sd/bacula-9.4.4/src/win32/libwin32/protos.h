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
 * Kern Sibbald, August 2007
 *
 */

#define log_error_message(msg) LogLastErrorMsg((msg), __FILE__, __LINE__)

extern int BaculaAppMain();
extern void LogLastErrorMsg(const char *msg, const char *fname, int lineno);

extern int BaculaMain(int argc, char *argv[]);
extern BOOL ReportStatus(DWORD state, DWORD exitcode, DWORD waithint);
extern void d_msg(const char *, int, int, const char *, ...);
extern char *bac_status(char *buf, int buf_len);


/* service.cpp */
bool postToBacula(UINT message, WPARAM wParam, LPARAM lParam);
bool isAService();
int installService(const char *svc);
int removeService();
int stopRunningBacula();
int baculaServiceMain();


/* Globals */
extern DWORD service_thread_id;
extern DWORD service_error;
extern bool opt_debug;
extern bool have_service_api;
extern HINSTANCE appInstance;
extern int bacstat;
