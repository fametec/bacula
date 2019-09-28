#ifndef _COMBOUTIL_H_
#define _COMBOUTIL_H_
/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2016 Kern Sibbald

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
 *   Combobox helpers - Riccardo Ghetta, May 2008 
 */

class QComboBox;
class QString;
class QStringList;

/* selects value val on combo, if exists */
void comboSel(QComboBox *combo, const QString &val);

/* if the combo has selected something different from "Any" uses the selection
 * to build a condition on field fldname and adds it to the condition list */
void comboCond(QStringList &cndlist, const QComboBox *combo, const char *fldname);

/* these helpers are used to give an uniform content to common combos.
 * There are two routines per combo type:
 * - XXXXComboFill fills the combo with values.
 * - XXXXComboCond checks the combo and, if selected adds a condition
 *   on the field fldName to the list of conditions cndList
 */

/* boolean combo (yes/no) */
void boolComboFill(QComboBox *combo);
void boolComboCond(QStringList &cndlist, const QComboBox *combo, const char *fldname);

/* backup level combo */
void levelComboFill(QComboBox *combo);
void levelComboCond(QStringList &cndlist, const QComboBox *combo, const char *fldname);

/* job status combo */
void jobStatusComboFill(QComboBox *combo);
void jobStatusComboCond(QStringList &cndlist, const QComboBox *combo, const char *fldname);

#endif /* _COMBOUTIL_H_ */
