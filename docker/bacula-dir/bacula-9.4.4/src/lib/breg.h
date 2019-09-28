/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2018 Kern Sibbald

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
 * Bacula BREGEXP Structure definition for FileDaemon
 * Eric Bollengier March 2007
 */


#ifndef __BREG_H_
#define __BREG_H_ 1

//#undef HAVE_REGEX_H

#ifndef HAVE_REGEX_H
#include "bregex.h"
#else
#include <regex.h>
#endif

/* Usage:
 *
 * #include "lib/breg.h"
 *
 * BREGEXP *breg = new_bregexp("!/prod!/test!");
 * char *filename = breg->replace("/prod/data.dat");
 *   or
 * char *filename = breg->result;
 * free_bregexp(breg);
 */

#define BREG_NREGS 11

/*
 * Structure for BREGEXP ressource
 */
class BREGEXP {
public:
   POOLMEM *result;             /* match result */
   bool success;                /* match is ok */

   char *replace(const char *fname, struct stat *sp=NULL); /* return this.result */
   void debug();

   /* private */
   POOLMEM *expr;               /* search epression */
   POOLMEM *subst;              /* substitution */
   regex_t preg;                /* regex_t result of regcomp() */
   regmatch_t regs[BREG_NREGS]; /* contains match */
   char *eor;                   /* end of regexp in expr */

   char *return_fname(const char *fname, int len); /* return fname as result */
   char *edit_subst(const char *fname, struct stat *sp, regmatch_t breg[]);
   int compute_dest_len(const char *fname, regmatch_t breg[]);
   bool extract_regexp(const char *motif);
};

/* create new BREGEXP and compile regex_t */
BREGEXP *new_bregexp(const char *motif);

/* launch each bregexp on filename */
int run_bregexp(alist *bregexps, const char *fname);

/* free BREGEXP (and all POOLMEM) */
void free_bregexp(BREGEXP *script);

/* fill an alist with BREGEXP from where */
alist *get_bregexps(const char *where);

/* apply every regexps from the alist */
bool apply_bregexps(const char *fname, struct stat *sp, alist *bregexps, char **result);
bool apply_bregexps(const char *fname, alist *bregexps, char **result);

/* foreach_alist free RUNSCRIPT */
void free_bregexps(alist *bregexps); /* you have to free alist */

/* get regexp size */
int bregexp_get_build_where_size(char *strip_prefix,
                                 char *add_prefix,
                                 char *add_suffix);

/* get a bregexp string from user arguments
 * you must allocate it with bregexp_get_build_where_size();
 */
char *bregexp_build_where(char *dest, int str_size,
                          char *strip_prefix,
                          char *add_prefix,
                          char *add_suffix);

/* escape a string to regexp format (sep and \)
 * dest must be long enough (dest = 2*src + 1)
 */
char *bregexp_escape_string(char *dest, const char *src, const char sep);

#endif /* __BREG_H_ */
