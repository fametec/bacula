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
 * See btime.c for defintions.
 *  Kern Sibbald, MM
 */


#ifndef __btime_INCLUDED
#define __btime_INCLUDED

/* New btime definition -- use this */
btime_t get_current_btime(void);
time_t btime_to_unix(btime_t bt);   /* bacula time to epoch time */
utime_t btime_to_utime(btime_t bt); /* bacula time to utime_t */

int tm_wom(int mday, int wday);
int tm_woy(time_t stime);
int tm_ldom(int month, int year);

char *bstrutime(char *dt, int maxlen, utime_t tim);
char *bstrftime(char *dt, int maxlen, utime_t tim);
char *bstrftimes(char *dt, int maxlen, utime_t tim);
char *bstrftime_ny(char *dt, int maxlen, utime_t tim);
char *bstrftime_nc(char *dt, int maxlen, utime_t tim);
char *bstrftime_dn(char *dt, int maxlen, utime_t tim);
utime_t str_to_utime(char *str);


/* =========================================================== */
/*        old code deprecated below. Do not use.               */

typedef float64_t fdate_t;             /* Date type */
typedef float64_t ftime_t;             /* Time type */

struct date_time {
    fdate_t julian_day_number;         /* Julian day number */
    ftime_t julian_day_fraction;       /* Julian day fraction */
};

/*  In arguments and results of the following functions,
    quantities are expressed as follows.

        year    Year in the Common Era.  The canonical
                date of adoption of the Gregorian calendar
                (October 5, 1582 in the Julian calendar)
                is assumed.

        month   Month index with January 0, December 11.

        day     Day number of month, 1 to 31.

*/


extern fdate_t date_encode(uint32_t year, uint8_t month, uint8_t day);
extern ftime_t time_encode(uint8_t hour, uint8_t minute, uint8_t second,
                          float32_t second_fraction);
extern void date_time_encode(struct date_time *dt,
                             uint32_t year, uint8_t month, uint8_t day,
                             uint8_t hour, uint8_t minute, uint8_t second,
                             float32_t second_fraction);

extern void date_decode(fdate_t date, uint32_t *year, uint8_t *month,
                        uint8_t *day);
extern void time_decode(ftime_t time, uint8_t *hour, uint8_t *minute,
                        uint8_t *second, float32_t *second_fraction);
extern void date_time_decode(struct date_time *dt,
                             uint32_t *year, uint8_t *month, uint8_t *day,
                             uint8_t *hour, uint8_t *minute, uint8_t *second,
                             float32_t *second_fraction);

extern int date_time_compare(struct date_time *dt1, struct date_time *dt2);

extern void tm_encode(struct date_time *dt, struct tm *tm);
extern void tm_decode(struct date_time *dt, struct tm *tm);
extern void get_current_time(struct date_time *dt);


#endif /* __btime_INCLUDED */
