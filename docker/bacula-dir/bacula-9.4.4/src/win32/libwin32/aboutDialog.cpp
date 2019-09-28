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
 *
 * Kern Sibbald, August 2007
 *
 *
*/

#include "bacula.h"
#include "win32.h"

static BOOL CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
   /* Get the dialog class pointer from USERDATA */
   aboutDialog *about;

   switch (uMsg) {
   case WM_INITDIALOG:
      /* save the dialog class pointer */
      SetWindowLong(hwnd, GWL_USERDATA, lParam);
      about = (aboutDialog *)lParam;

      /* Show the dialog */
      SetForegroundWindow(hwnd);
      about->m_visible = true;
      return true;

   case WM_COMMAND:
      switch (LOWORD(wParam)) {
      case IDCANCEL:
      case IDOK:
         EndDialog(hwnd, true);
         about = (aboutDialog *)GetWindowLong(hwnd, GWL_USERDATA);
         about->m_visible = false;
         return true;
      }
      break;

   case WM_DESTROY:
      EndDialog(hwnd, false);
      about = (aboutDialog *)GetWindowLong(hwnd, GWL_USERDATA);
      about->m_visible = false;
      return true;
   }
   return false;
}

void aboutDialog::show(bool show)
{
   if (show && !m_visible) {
      DialogBoxParam(appInstance, MAKEINTRESOURCE(IDD_ABOUT), NULL,
         (DLGPROC)DialogProc, (LPARAM)this);
   }
}
