#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _LARGEFILE64_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <sys/param.h>
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#ifdef HAVE_LINUX_UNISTD_H
#include <linux/unistd.h>
#endif

/* #include "mfs.h" */
#include "macpart.h"

#ifdef __NR_readsectors
# ifndef NOTIVO
#  define TIVO
# endif
#else
# undef TIVO
# define NOTIVO
#endif

#ifdef TIVO
# include <linux/ide-tivo.h>
# include <asm/page.h>

_syscall4 (static long, readsectors, unsigned int, fd, struct FsIovec *, buf, int, buf_len, struct FsIoRequest *, request) _syscall4 (static long, writesectors, unsigned int, fd, struct FsIovec *, buf, int, buf_len, struct FsIoRequest *, request)
#endif
#ifndef HAVE_LSEEK64
#ifdef __NR__llseek
static _syscall5 (int, _llseek, uint, fd, ulong, hi, ulong, lo, loff_t *, res, uint, wh);
#define USE__LLSEEK
#endif
#endif

/*********************************************/
/* Preform byte-swapping in a block of data. */
void
data_swab (void *data, int size)
{
	unsigned int *idata = data;

/* Do it 32 bits at a time if possible. */
	while (size > 3)
	{
#if TARGET_OS_MAC
		*idata = Endian32_Swap (*idata);
#else
		*idata = ((*idata << 8) & 0xff00ff00) | ((*idata & 0xff00ff00) >> 8);
#endif
		size -= 4;
		idata++;
	}

/* Swap the odd out bytes.  If theres a final odd out, just ignore it. */
/* Probably not the best solution for data integrity, but thats okay, */
/* this should never happen. */
	if (size > 1)
	{
		unsigned char *cdata = (unsigned char *) idata;
		unsigned char tmp;

		tmp = cdata[0];
		cdata[0] = cdata[1];
		cdata[1] = tmp;
	}
}

/*****************************************************************************/
/* Read data from the MFS volume set.  It must be in whole sectors, and must */
/* not cross a volume boundry. */
int
tivo_partition_read (tpFILE * file, void *buf, uint64_t sector, int count)
{
#ifdef USE__LLSEEK
	loff_t result;
#endif
	int retval;

	//patch submitted by terativo (http://mfslive.org/forums/viewtopic.php?f=4&t=955)
	unsigned int orig_sector = sector;

	if (sector + count > tivo_partition_size (file))
	{
		fprintf (stderr, "Attempt to read across partition boundry!");
		errno = EIO;
		return -1;
	}

/* Account for sector offset. */
	sector += tivo_partition_offset (file);

	if (count == 0)
	{
		return 0;
	}

#ifdef TIVO
/* If it is not a file, and this is for TiVo, use readsector. */
	if (_tivo_partition_isdevice (file))
	{
		struct FsIovec vec;
		struct FsIoRequest req;

		vec.pb = buf;
		vec.cb = count * 512;
		req.sector = sector;
		req.num_sectors = count;
		req.deadline = 0;

		retval = readsectors (_tivo_partition_fd (file), &vec, 1, &req);
		if (_tivo_partition_swab (file))
		{
			data_swab (buf, count * 512);
		}
		return retval;
	}
#endif

/* A file, or not TiVo, use llseek and read. */
#ifdef USE__LLSEEK
	if (_llseek (_tivo_partition_fd (file), sector >> 23, sector << 9, &result, SEEK_SET) < 0)
#elif HAVE_LSEEK64
	if (lseek64 (_tivo_partition_fd (file), (off64_t)sector << 9, SEEK_SET) != (off64_t)sector << 9)
#else
	if (lseek (_tivo_partition_fd (file), (off_t)sector << 9, SEEK_SET) != (off_t)sector << 9)
#endif
	{
		return -1;
	}

	retval = read (_tivo_partition_fd (file), buf, count * 512);
	
	/* rescue begin by terativo(http://mfslive.org/forums/viewtopic.php?f=4&t=955)*/
	if (retval < 0 && errno == EIO)
	{
		int error = errno;
		if (count == 1)
		{
			fprintf(stderr, "read failure at sector %" PRIu64 "(%#" PRIx64 ") of %d(%#x): %s; zeroing sector\n",
			sector, sector, count*512, count*512, strerror(errno));
			memset(buf, 0, count * 512);
			retval = count * 512;
		}
		else
		{
			int i;
			
			fprintf(stderr, "read failure at sector %" PRIu64 "(%#" PRIx64 ") of %d(%#x): %s; trying sector-by-sector\n",
			sector, sector, count*512, count*512, strerror(errno));
			for (i = 0; i < count; i++)
			{
				int r;
				r = tivo_partition_read(file, (char *) buf + i * 512, orig_sector + i, 1);
				if (r < 0)
				{
					 fprintf(stderr, "sector scan failed\n");
					 errno = error;
					 break;
				}
			}

			if (i >= count)
			{
				fprintf(stderr, "sector scan done\n");
				retval = count * 512;
			}
		}
	}
	/* rescue end */


	if (_tivo_partition_swab (file))
	{
		data_swab (buf, count * 512);
	}
	return retval;
}

/****************************************************************************/
/* Write data to the MFS volume set.  It must be in whole sectors, and must */
/* not cross a volume boundry. */
int
tivo_partition_write (tpFILE * file, void *buf, uint64_t sector, int count)
{
#ifdef USE__LLSEEK
	loff_t result;
#endif
	int retval;

	if (sector + count > tivo_partition_size (file))
	{
		fprintf (stderr, "Attempt to write across partition boundry!\n");
		errno = EIO;
		return -1;
	}

/* Account for sector offset. */
	sector += tivo_partition_offset (file);

	if (count == 0)
	{
		return 0;
	}

#ifdef TIVO
/* If it is not a file, and this is for TiVo, use writesector. */
	if (_tivo_partition_isdevice (file))
	{
		struct FsIovec vec;
		struct FsIoRequest req;

		vec.pb = buf;
		vec.cb = count * 512;
		req.sector = sector;
		req.num_sectors = count;
		req.deadline = 0;

		if (_tivo_partition_swab (file))
		{
			data_swab (buf, count * 512);
		}
		retval = writesectors (_tivo_partition_fd (file), &vec, 1, &req);
		if (_tivo_partition_swab (file))
		{
/* Fix the data since we don't own it. */
			data_swab (buf, count * 512);
		}
		return retval;
	}
#endif

/* A file, or not TiVo, use llseek and write. */
#ifdef USE__LLSEEK
	if (_llseek (_tivo_partition_fd (file), sector >> 23, sector << 9, &result, SEEK_SET) < 0)
#elif HAVE_LSEEK64
	if (lseek64 (_tivo_partition_fd (file), (off64_t)sector << 9, SEEK_SET) != (off64_t)sector << 9)
#else
	if (lseek (_tivo_partition_fd (file), (off_t)sector << 9, SEEK_SET) != (off_t)sector << 9)
#endif
	{
		return -1;
	}

	if (_tivo_partition_swab (file))
	{
		data_swab (buf, count * 512);
	}
	retval = write (_tivo_partition_fd (file), buf, count * 512);
	if (_tivo_partition_swab (file))
	{
/* Fix the data since we don't own it. */
		data_swab (buf, count * 512);
	}
	return retval;
}
