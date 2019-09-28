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

#ifndef __CONIO_H
#define __CONIO_H
extern int  input_line(char *line, int len);
extern void con_init(FILE *input);

extern void con_term();
extern void con_set_zed_keys();
extern void t_sendl(char *buf, int len);
extern void t_send(char *buf);
extern void t_char(char c);
#endif
