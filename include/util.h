#ifndef UTIL_H
#define UTIL_H

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
#define EXTERNINLINE extern inline
#endif
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
	n = (n << 8) | (n >> 8);
	return n;
}

EXTERNINLINE uint32_t
intswap32 (uint32_t n)
{
	n = (n >> 16) | (n << 16);
	n = ((n >> 8) & 0x00FF00FF) | ((n & 0x00FF00FF) << 8);
	return n;
}

EXTERNINLINE uint64_t
intswap64 (uint64_t n)
{
	n = (n >> 32) | (n << 32);
	n = ((n >> 16) & INT64_C(0x0000FFFF0000FFFF)) | ((n & INT64_C(0x0000FFFF0000FFFF)) << 16);
	n = ((n >> 8) & INT64_C(0x00FF00FF00FF00FF)) | ((n & INT64_C(0x00FF00FF00FF00FF)) << 8);
	return n;
}

#endif

#define CRC32_RESIDUAL 0xdebb20e3

#define MFS_check_crc(data, size, crc) (mfs_check_crc ((unsigned char *)(data), (size), (unsigned int *)&(crc) - (unsigned int *)(data)))
#define MFS_update_crc(data, size, crc) (mfs_update_crc ((unsigned char *)(data), (size), (unsigned int *)&(crc) - (unsigned int *)(data)))

#endif
