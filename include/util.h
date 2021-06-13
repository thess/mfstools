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
EXTERNINLINE uint16_t
Endian16_Swap (uint16_t var)
{
	var = (uint16_t) ((var << 8) | (var >> 8));
	return var;
}
#endif

#if !HAVE_ENDIAN32_SWAP
EXTERNINLINE uint32_t
Endian32_Swap (uint32_t var)
{
	var = (var << 16) | (var >> 16);
	var = ((var & 0xff00ff00) >> 8) | ((var << 8) & 0xff00ff00);
	return var;
}
#endif

#if !HAVE_ENDIAN64_SWAP
EXTERNINLINE uint64_t
Endian64_Swap (uint64_t var)
{
	var = (var >> 32) | (var << 32);
	var = ((var >> 16) & INT64_C(0x0000FFFF0000FFFF)) | ((var & INT64_C(0x0000FFFF0000FFFF)) << 16);
	var = ((var >> 8) & INT64_C(0x00FF00FF00FF00FF)) | ((var & INT64_C(0x00FF00FF00FF00FF)) << 8);
	return var;
}
#endif

// Historically, the drive was accessed as big endian (MSB), however newer platforms (Roamio) are mipsel based, hence the numeric values are little endian (LSB).
extern int mfsLSB; /* Drive is little endian */
extern int partLSB; 

// If the running architecture doesn't match the required endianness, then a conversion will needed
#if BYTE_ORDER == BIG_ENDIAN
#define archLSB 0
#else
#define archLSB 1
#endif

/* If byte order is not set, assume whatever platform it is doesn't have byteorder.h, and is probably x86 based */

// Fix endianness in the MFS
EXTERNINLINE uint16_t
intswap16 (uint16_t n)
{
	if (mfsLSB == archLSB)
		return n;
	return Endian16_Swap (n);
}

EXTERNINLINE uint32_t
intswap32 (uint32_t n)
{
	if (mfsLSB == archLSB)
		return n;
	return Endian32_Swap (n);
}

EXTERNINLINE uint64_t
intswap64 (uint64_t n)
{
	if (mfsLSB == archLSB)
		return n;
	return Endian64_Swap (n);
}

EXTERNINLINE uint64_t
sectorswap64(uint64_t n)
{
	uint64_t ret;
// *NOTE*  Little endian drives (Roamio) have reversed hi an lo 32 bits
	if (mfsLSB == archLSB)
		ret = n;
	else
		ret = Endian64_Swap (n);

	if (mfsLSB == 1)
		ret = (ret >> 32) | (ret << 32);

	return ret;
}

// Fix endianness in the TiVo Partiion Map
EXTERNINLINE uint16_t
partintswap16(uint16_t n)
{
	if (partLSB == archLSB)
		return n;
	return Endian16_Swap (n);
}

EXTERNINLINE uint32_t
partintswap32(uint32_t n)
{
	if (partLSB == archLSB)
		return n;
	return Endian32_Swap (n);
}

EXTERNINLINE uint64_t
partintswap64(uint64_t n)
{
	if (partLSB == archLSB)
		return n;
	return Endian64_Swap (n);
}

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
