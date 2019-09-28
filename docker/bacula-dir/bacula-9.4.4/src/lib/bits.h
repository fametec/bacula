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
/* Some elementary bit manipulations
 *
 *   Kern Sibbald, MM
 *
 *  NOTE:  base 0
 *
 */

#ifndef __BITS_H_
#define __BITS_H_

/* number of bytes to hold n bits */
#define nbytes_for_bits(n) ((((n)-1)>>3)+1)

/* test if bit is set */
#define bit_is_set(b, var) (((var)[(b)>>3] & (1<<((b)&0x7))) != 0)

/* set bit */
#define set_bit(b, var) ((var)[(b)>>3] |= (1<<((b)&0x7)))

/* clear bit */
#define clear_bit(b, var) ((var)[(b)>>3] &= ~(1<<((b)&0x7)))

/* clear all bits */
#define clear_all_bits(b, var) memset(var, 0, nbytes_for_bits(b))

/* set range of bits */
#define set_bits(f, l, var) { \
   int i; \
   for (i=f; i<=l; i++)  \
      set_bit(i, var); \
}

/* clear range of bits */
#define clear_bits(f, l, var) { \
   int i; \
   for (i=f; i<=l; i++)  \
      clear_bit(i, var); \
}

#endif /* __BITS_H_ */
