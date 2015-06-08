#ifndef UTIL_H
#define UTIL_H

#if HAVE_STDDEF_H
#include <stddef.h>
#endif

#if HAVE_BYTEORDER_H
#include <byteorder.h>
#endif

#if HAVE_STDINT_H
#include <stdint.h>
#endif

#ifndef EXTERNINLINE
#if DEBUG
#define EXTERNINLINE static inline
#else
#define EXTERNINLINE static inline
#endif
#endif

#if !HAVE_ENDIAN16_SWAP
EXTERNINLINE u_int16_t
Endian16_Swap (u_int16_t var)
{
	var = (u_int16_t) ((var << 8) | (var >> 8));
	return var;
}
#endif

#if !HAVE_ENDIAN32_SWAP
EXTERNINLINE u_int32_t
Endian32_Swap (u_int32_t var)
{
	var = (var << 16) | (var >> 16);
	var = ((var & 0xff00ff00) >> 8) | ((var << 8) & 0xff00ff00);
	return var;
}
#endif

#if !HAVE_ENDIAN64_SWAP
EXTERNINLINE u_int64_t
Endian64_Swap (u_int64_t var)
{
	var = (var >> 32) | (var << 32);
	var = ((var >> 16) & INT64_C(0x0000FFFF0000FFFF)) | ((var & INT64_C(0x0000FFFF0000FFFF)) << 16);
	var = ((var >> 8) & INT64_C(0x00FF00FF00FF00FF)) | ((var & INT64_C(0x00FF00FF00FF00FF)) << 8);
	return var;
}
#endif

#if BYTE_ORDER == BIG_ENDIAN
#define intswap16(n) (n)
#define intswap32(n) (n)
#define intswap64(n) (n)
#else
/* If byte order is not set, assume whatever platform it is doesn't have byteorder.h, and is probably x86 based */

EXTERNINLINE uint16_t
intswap16 (uint16_t n)
{
	return Endian16_Swap (n);
}

EXTERNINLINE uint32_t
intswap32 (uint32_t n)
{
	return Endian32_Swap (n);
}

EXTERNINLINE uint64_t
intswap64 (uint64_t n)
{
	return Endian64_Swap (n);
}

#endif

#ifndef offsetof
#define offsetof(struc,field) ((size_t)(&((struc *)0)->field))
#endif

#define CRC32_RESIDUAL 0xdebb20e3

unsigned int compute_crc (unsigned char *data, unsigned int size, unsigned int crc);
unsigned int mfs_compute_crc (unsigned char *data, unsigned int size, unsigned int off);
unsigned int mfs_check_crc (unsigned char *data, unsigned int size, unsigned int off);
void mfs_update_crc (unsigned char *data, unsigned int size, unsigned int off);

#define MFS_check_crc(data, size, crc) (mfs_check_crc ((unsigned char *)(data), (size), (unsigned int *)&(crc) - (unsigned int *)(data)))
#define MFS_update_crc(data, size, crc) (mfs_update_crc ((unsigned char *)(data), (size), (unsigned int *)&(crc) - (unsigned int *)(data)))

#endif
