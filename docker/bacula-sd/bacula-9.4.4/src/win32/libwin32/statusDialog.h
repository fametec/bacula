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
 * Bacula Status Dialog header file
 *
 *  Kern Sibbald, August 2007
 *
 */

#ifndef __STATUS_DIALOG_H_
#define __STATUS_DIALOG_H_

class statusDialog
{
public:
   statusDialog() { m_visible = false; m_textWin = NULL; };
   ~statusDialog() { };

   void display();

   void show(bool show);

   void resize(HWND win, int width, int height);

   bool m_visible;
   HWND m_textWin;
};

#endif
