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
 *
 * Written by John Walker, MM
 *
 */

/*  Serialisation support functions from serial.c.  */

extern void serial_int16(uint8_t * * const ptr, const int16_t v);
extern void serial_uint16(uint8_t * * const ptr, const uint16_t v);
extern void serial_int32(uint8_t * * const ptr, const int32_t v);
extern void serial_uint32(uint8_t * * const ptr, const uint32_t v);
extern void serial_int64(uint8_t * * ptr, int64_t v);
extern void serial_uint64(uint8_t * * const ptr, const uint64_t v);
extern void serial_btime(uint8_t * * const ptr, const btime_t v);
extern void serial_float64(uint8_t * * const ptr, const float64_t v);
extern void serial_string(uint8_t * * const ptr, const char * const str);

extern int16_t unserial_int16(uint8_t * * const ptr);
extern uint16_t unserial_uint16(uint8_t * * const ptr);
extern int32_t unserial_int32(uint8_t * * const ptr);
extern uint32_t unserial_uint32(uint8_t * * const ptr);
extern int64_t unserial_int64(uint8_t * * const ptr);
extern uint64_t unserial_uint64(uint8_t * * const ptr);
extern btime_t unserial_btime(uint8_t * * const ptr);
extern float64_t unserial_float64(uint8_t * * const ptr);
extern void unserial_string(uint8_t * * const ptr, char * const str, int max);

/*

                         Serialisation Macros

    These macros use a uint8_t pointer, ser_ptr, which must be
    defined by the code which uses them.

*/

#ifndef __SERIAL_H_
#define __SERIAL_H_ 1

/*  ser_declare  --  Declare ser_ptr locally within a function.  */
#define ser_declare     uint8_t *ser_ptr
#define unser_declare   uint8_t *ser_ptr

/*  ser_begin(x, s)  --  Begin serialisation into a buffer x of size s.  */
#define ser_begin(x, s) ser_ptr = ((uint8_t *)(x))
#define unser_begin(x, s) ser_ptr = ((uint8_t *)(x))

/*  ser_length  --  Determine length in bytes of serialised into a
                    buffer x.  */
#define ser_length(x)  ((uint32_t)(ser_ptr - (uint8_t *)(x)))
#define unser_length(x) ((uint32_t)(ser_ptr - (uint8_t *)(x)))

/*  ser_end(x, s)  --  End serialisation into a buffer x of size s.  */
#define ser_end(x, s)   ASSERT(ser_length(x) <= ((uint32_t)(s)))
#define unser_end(x, s)   ASSERT(unser_length(x) <= ((uint32_t)(s)))

/*  ser_check(x, s)  --  Verify length of serialised data in buffer x is
                         expected length s.  */
#define ser_check(x, s) ASSERT(ser_length(x) == ((uint32_t)(s)))
#define unser_check(x, s) ASSERT(unser_length(x) == ((uint32_t)(s)))

/*  ser_assign(ptr, len) -- assign current position to ptr and go len bytes forward  */
#define ser_assign(ptr, len) { ptr = (typeof(ptr))ser_ptr; ser_ptr += (len); }
#define unser_assign(ptr, len) { ptr = (typeof(ptr))ser_ptr; ser_ptr += (len); }

/*                          Serialisation                   */

/*  8 bit signed integer  */
#define ser_int8(x)     *ser_ptr++ = (x)
/*  8 bit unsigned integer  */
#define ser_uint8(x)    *ser_ptr++ = (x)

/*  16 bit signed integer  */
#define ser_int16(x)    serial_int16(&ser_ptr, x)
/*  16 bit unsigned integer  */
#define ser_uint16(x)   serial_uint16(&ser_ptr, x)

/*  32 bit signed integer  */
#define ser_int32(x)    serial_int32(&ser_ptr, x)
/*  32 bit unsigned integer  */
#define ser_uint32(x)   serial_uint32(&ser_ptr, x)

/*  64 bit signed integer  */
#define ser_int64(x)    serial_int64(&ser_ptr, x)
/*  64 bit unsigned integer  */
#define ser_uint64(x)   serial_uint64(&ser_ptr, x)

/* btime -- 64 bit unsigned integer */
#define ser_btime(x)    serial_btime(&ser_ptr, x)


/*  64 bit IEEE floating point number  */
#define ser_float64(x)  serial_float64(&ser_ptr, x)

/*  128 bit signed integer  */
#define ser_int128(x)   memcpy(ser_ptr, x, sizeof(int128_t)), ser_ptr += sizeof(int128_t)

/*  Binary byte stream len bytes not requiring serialisation  */
#define ser_bytes(x, len) memcpy(ser_ptr, (x), (len)), ser_ptr += (len)

/*  Binary byte stream not requiring serialisation (length obtained by sizeof)  */
#define ser_buffer(x)   ser_bytes((x), (sizeof (x)))

/* Binary string not requiring serialization */
#define ser_string(x)   serial_string(&ser_ptr, (x))

/*                         Unserialisation                  */

/*  8 bit signed integer  */
#define unser_int8(x)   (x) = *ser_ptr++
/*  8 bit unsigned integer  */
#define unser_uint8(x)  (x) = *ser_ptr++

/*  16 bit signed integer  */
#define unser_int16(x)  (x) = unserial_int16(&ser_ptr)
/*  16 bit unsigned integer  */
#define unser_uint16(x) (x) = unserial_uint16(&ser_ptr)

/*  32 bit signed integer  */
#define unser_int32(x)  (x) = unserial_int32(&ser_ptr)
/*  32 bit unsigned integer  */
#define unser_uint32(x) (x) = unserial_uint32(&ser_ptr)

/*  64 bit signed integer  */
#define unser_int64(x)  (x) = unserial_int64(&ser_ptr)
/*  64 bit unsigned integer  */
#define unser_uint64(x) (x) = unserial_uint64(&ser_ptr)

/* btime -- 64 bit unsigned integer */
#define unser_btime(x) (x) = unserial_btime(&ser_ptr)

/*  64 bit IEEE floating point number  */
#define unser_float64(x)(x) = unserial_float64(&ser_ptr)

/*  128 bit signed integer  */
#define unser_int128(x) memcpy(ser_ptr, x, sizeof(int128_t)), ser_ptr += sizeof(int128_t)

/*  Binary byte stream len bytes not requiring serialisation  */
#define unser_bytes(x, len) memcpy((x), ser_ptr, (len)), ser_ptr += (len)

/*  Binary byte stream not requiring serialisation (length obtained by sizeof)  */
#define unser_buffer(x)  unser_bytes((x), (sizeof (x)))

/* Binary string not requiring serialization (length obtained from max) */
#define unser_nstring(x,max) unserial_string(&ser_ptr, (x), (int)(max))

/*  Binary string not requiring serialisation (length obtained by sizeof)  */
#define unser_string(x) unserial_string(&ser_ptr, (x), sizeof(x))

#endif /* __SERIAL_H_ */
