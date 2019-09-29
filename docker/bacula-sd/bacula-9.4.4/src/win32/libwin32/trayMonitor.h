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

#ifndef __TRAY_MONITOR_H_
#define __TRAY_MONITOR_H_ 1

#define WM_TRAYNOTIFY WM_USER+1

/* Define the trayMonitor class */
class trayMonitor
{
public:
   trayMonitor();
  ~trayMonitor();

   void show(bool show);
   void install();
   void update(int bacstat);
   void sendMessage(DWORD msg, int bacstat);

   static LRESULT CALLBACK trayWinProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);

   bool m_visible;
   bool m_installed;
   UINT m_tbcreated_msg;

   aboutDialog m_about;
   statusDialog m_status;

   HWND  m_hwnd;
   HMENU m_hmenu;
   NOTIFYICONDATA m_nid;
   HICON m_idle_icon;
   HICON m_running_icon;
   HICON m_error_icon;
   HICON m_warn_icon;
};

#endif /* __TRAY_MONITOR_H_ */
