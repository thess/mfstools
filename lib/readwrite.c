#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <linux/fs.h>
#include <linux/unistd.h>

/* For htonl() */
#include <netinet/in.h>

#include "mfs.h"

#ifdef __NR_readsectors
#ifndef NOTIVO
#define TIVO
#endif
#else
#undef TIVO
#define NOTIVO
#endif

#ifdef TIVO
#include <linux/ide-tivo.h>
#include <asm/page.h>

_syscall4 (static long, readsectors, unsigned int, fd, struct FsIovec *,buf, int, buf_len, struct FsIoRequest *, request)
_syscall4 (static long, writesectors, unsigned int, fd, struct FsIovec *,buf, int, buf_len, struct FsIoRequest *, request)
#endif

_syscall5(int,  _llseek,  uint,  fd, ulong, hi, ulong, lo,
          loff_t *, res, uint, wh);

/* Some static variables..  Really this should be a class and these */
/* private members. */
static int fake_write = 0;

/****************************************************************************/
/* Verify that a sector is writable.  This should be done for all groups of */
/* sectors to be written, since individual volumes can be opened RDONLY. */
int mfs_is_writable (unsigned int sector)
{
	struct volume_info *vol;

	vol = mfs_get_volume (sector);

	if (!vol || vol->vol_flags & VOL_RDONLY) {
		return fake_write;
	}

	return 1;
}

/*********************************************/
/* Preform byte-swapping in a block of data. */
void data_swab (void *data, int size)
{
	unsigned int *idata = data;

/* Do it 32 bits at a time if possible. */
	while (size > 3) {
		*idata = ((*idata << 8) & 0xff00ff00) | ((*idata & 0xff00ff00) >> 8);
		size -= 4;
		idata++;
	}

/* Swap the odd out bytes.  If theres a final odd out, just ignore it. */
/* Probably not the best solution for data integrity, but thats okay, */
/* this should never happen. */
	if (size > 1) {
		unsigned char *cdata = (unsigned char *)idata;
		unsigned char tmp;

		tmp = cdata[0];
		cdata[0] = cdata[1];
		cdata[1] = tmp;
	}
}

/*****************************************************************************/
/* Read data from the MFS volume set.  It must be in whole sectors, and must */
/* not cross a volume boundry. */
int mfs_read_data (void *buf, unsigned int sector, int count)
{
	struct volume_info *vol;
	loff_t result;
	int retval;

	vol = mfs_get_volume (sector);

/* If no volumes claim this sector, it's an IO error. */
	if (!vol) {
		errno = EIO;
		return -1;
	}

/* Make the sector number relative to this volume. */
	sector -= vol->start;

	if (sector + count > vol->sectors) {
		fprintf (stderr, "Attempt to read across volume boundry!");
		errno = EIO;
		return -1;
	}

/* Account for sector offset. */
	sector += vol->offset;

	if (count == 0) {
		return 0;
	}

#ifdef TIVO
/* If it is not a file, and this is for TiVo, use readsector. */
	if (!(vol->vol_flags & VOL_FILE)) {
		struct FsIovec vec;
		struct FsIoRequest req;

		vec.pb = buf;
		vec.cb = count * 512;
		req.sector = sector;
		req.num_sectors = count;
		req.deadline = 0;

		retval = readsectors (vol->fd, &vec, 1, &req);
		if (vol->vol_flags & VOL_SWAB) {
			data_swab (buf, count * 512);
		}
		return retval;
	}
#endif

/* A file, or not TiVo, use llseek and read. */
	if (_llseek (vol->fd, sector >> 23, sector << 9, &result, SEEK_SET) < 0) {
		return -1;
	}

	retval = read (vol->fd, buf, count * 512);
	if (vol->vol_flags & VOL_SWAB) {
		data_swab (buf, count * 512);
	}
	return retval;
}

/****************************************************************************/
/* Doesn't really belong here, but useful for debugging with MFS_FAKE_WRITE */
/* set, this gets called instead of writing. */
static void hexdump (unsigned char *buf, unsigned int sector)
{
    int ofs;

    for (ofs = 0; ofs < 512; ofs += 16) {
	unsigned char line[20];
	int myo;

	printf ("%05x:%03x ", sector, ofs);

	for (myo = 0; myo < 16; myo++) {
	    printf ("%02x%c", buf[myo + ofs], myo < 15 && (myo & 3) == 3? '-': ' ');
	    line[myo] = (isprint (buf[myo + ofs])? buf[myo + ofs]: '.');
	}

	printf ("|");
	line[16] = ':';
	line[17] = '\n';
	line[18] = 0;
	printf ("%s", line);
    }
}

/****************************************************************************/
/* Write data to the MFS volume set.  It must be in whole sectors, and must */
/* not cross a volume boundry. */
int mfs_write_data (void *buf, unsigned int sector, int count)
{
	struct volume_info *vol;
	loff_t result;
	int retval;

	vol = mfs_get_volume (sector);

/* If no volumes claim this sector, it's an IO error. */
	if (!vol) {
		errno = EIO;
		return -1;
	}

	if (fake_write) {
		int loop;
		for (loop = 0; loop < count; loop++) {
			hexdump ((unsigned char *)buf + loop * 512, sector + loop);
		}
		return count * 512;
	}

/* If the volume this sector is in was opened read-only, it's an error. */
/* Perhaps this should pretend to write, printing to stderr the attempt */
/* instead, useful for debug? */
	if (vol->vol_flags & VOL_RDONLY) {
		fprintf (stderr, "mfs_write_data: Attempt to write to read-only volume.\n");
		errno = EPERM;
		return -1;
	}

/* Make the sector number relative to this volume. */
	sector -= vol->start;

	if (sector + count > vol->sectors) {
		fprintf (stderr, "Attempt to write across volume boundry!");
		errno = EIO;
		return -1;
	}

/* Account for sector offset. */
	sector += vol->offset;

	if (count == 0) {
		return 0;
	}

#ifdef TIVO
/* If it is not a file, and this is for TiVo, use writesector. */
	if (!(vol->vol_flags & VOL_FILE)) {
		struct FsIovec vec;
		struct FsIoRequest req;

		vec.pb = buf;
		vec.cb = count * 512;
		req.sector = sector;
		req.num_sectors = count;
		req.deadline = 0;

		if (vol->vol_flags * VOL_SWAB) {
			data_swab (buf, count * 512);
		}
		retval = writesectors (vol->fd, &vec, 1, &req);
		if (vol->vol_flags * VOL_SWAB) {
/* Fix the data since we don't own it. */
			data_swab (buf, count * 512);
		}
		return retval;
	}
#endif

/* A file, or not TiVo, use llseek and write. */
	if (_llseek (vol->fd, sector >> 23, sector << 9, &result, SEEK_SET) < 0) {
		return -1;
	}

	if (vol->vol_flags * VOL_SWAB) {
		data_swab (buf, count * 512);
	}
	retval = write (vol->fd, buf, count * 512);
	if (vol->vol_flags * VOL_SWAB) {
/* Fix the data since we don't own it. */
		data_swab (buf, count * 512);
	}
	return retval;
}

int mfs_readwrite_init ()
{
        char *fake = getenv ("MFS_FAKE_WRITE");
 
        if (fake && *fake) {
                fake_write = 1;
        }

	return 0;
}
