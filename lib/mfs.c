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

/* Flags for vol_flags below */
#define VOL_FILE	1	/* This volume is really a file */
#define VOL_RDONLY	2	/* This volume is read-only */

/* Information about the list of volumes needed for reads */
struct volume_info {
	int fd;
	unsigned int start;
	unsigned int sectors;
	int vol_flags;
	struct volume_info *next;
};

/* Linked lists of zone maps for a certain type of map */
struct zone_map {
	zone_header *map;
	struct zone_map *next;
	struct zone_map *next_loaded;
};

/* Head of zone maps linked list, contains totals as well */
struct zone_map_head {
	unsigned int size;
	unsigned int free;
	struct zone_map *next;
};

/* Some static variables..  Really this should be a class and these */
/* private members. */
static struct volume_info *volumes = NULL;
static struct zone_map_head zones[ztMax] = {{0, 0, NULL}, {0, 0, NULL}, {0, 0, NULL}};
static struct zone_map *loaded_zones = NULL;
static volume_header vol_hdr;
static int fake_write = 0;

/* ANSI X3.66 CRC32 checksum */
static long crc32tab[] = { /* CRC polynomial 0xedb88320 */

0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

#define UPDC32(octet, crc) (crc32tab[((int)(crc) ^ octet) & 0xff] ^ (((crc) >> 8) & 0x00FFFFFF))

/**********************************************************************/
/* Compute the checksum, replacing the integer at off with 0xdeadf00d */
int mfs_compute_crc (unsigned char *data, unsigned int size, unsigned int off)
{
	unsigned int CRC=0;
	static const unsigned char deadfood[] = { 0xde, 0xad, 0xf0, 0x0d };
	off = off * 4 + 3;

	while (size) {
		if (off < 4) {
/* This replaces the checksum offset without actually modifying the data. */
			CRC = UPDC32 (deadfood[3 - off], CRC);
		} else {
			CRC = UPDC32 (*data, CRC);
		}
		data++;
		size--;
		off--;
	}

	return htonl (CRC);
}

/******************************/
/* Verify the CRC is correct. */
int mfs_check_crc (unsigned char *data, unsigned int size, unsigned int off)
{
	unsigned int target = *((unsigned int *)data + off);
	return target == mfs_compute_crc (data, size, off);
}

/*************************/
/* Make the CRC correct. */
void mfs_update_crc (unsigned char *data, unsigned int size, unsigned int off)
{
	*((unsigned int *)data + off) = mfs_compute_crc (data, size, off);
}

/***********************************************************************/
/* Translate a device name from the TiVo view of the world to reality, */
/* allowing relocating of MFS volumes by setting MFS_... variables. */
/* For example, MFS_HDA=/dev/hdb would make /dev/hda* into /dev/hdb* */
/* Note that it goes most specific first, so MFS_HDA10 would match before */
/* MFS_HDA on /dev/hda10.  Also note that MFS_HDA1 would match /dev/hda10, */
/* so be careful.  In addition to relocating, if a relocated device starts */
/* with RO: the device or file will be opened O_RDONLY no matter what the */
/* requested mode was. */
char *mfs_device_translate(char *dev)
{
	static char devname[1024];
	int dev_len = strcspn (dev, " ");

/* See if it is in /dev, to be relocated. */
	if (!strncmp (dev, "/dev/", 5)) {
		char dev_sub_var[128];
		char *dev_sub;
		int loop;

		strcpy (dev_sub_var, "MFS_");

/* Copy in the entire rest of the name after /dev/, uppercasing it. */
		for (loop = 0; dev[5 + loop] && dev[5 + loop] != ' '; loop++) {
			dev_sub_var[loop + 4] = toupper (dev[loop + 5]);
		}
		dev_sub_var[loop + 4] = 0;

/* Slowly eat off one character until it is MFS_X checking for a variable */
/* each time, breaking when one is found. */
		while (loop > 1 && !(dev_sub = getenv (dev_sub_var))) {
			dev_sub_var[--loop + 4] = 0;
		}

/* If the variable is found, substitute it, tacking on the remainder of the */
/* device name passed in. */
		if (dev_sub) {
			sprintf (devname, "%s%.*s", dev_sub, dev_len - (loop + 5), dev + loop + 5);

			return devname;
		}
	}

/* Otherwise, just copy out the name of this device.  This is always done */
/* instead of just returning the name since the list is space seperated. */
	sprintf (devname, "%.*s", dev_len, dev);
	return devname;
}

/***************************************************************************/
/* Add a volume to the internal list of open volumes.  Open it with flags. */
int mfs_add_volume (char *path, int flags)
{
	struct volume_info *newvol;
	struct volume_info **loop;

	newvol = calloc (sizeof (*newvol), 1);

	if (!newvol) {
		fprintf (stderr, "Out of memory!");
		return -1;
	}

/* Translate the device name to it's real world equivelent. */
	path = mfs_device_translate (path);

/* If the user requested RO, let them have it.  This may break a writer */
/* program, but thats what it is intended to do. */
	if (!strncmp (path, "RO:", 3)) {
		path += 3;
		flags = (flags & ~O_ACCMODE) | O_RDONLY;
	}

	newvol->fd = open (path, flags);

	if (newvol->fd < 0) {
		perror (path);
		return 0;
	}

/* If read-only was requested, make it so. */
	if ((flags & O_ACCMODE) == O_RDONLY) {
		newvol->vol_flags |= VOL_RDONLY;
	}

/* Find out the size of the device. */
	if (ioctl (newvol->fd, BLKGETSIZE, &newvol->sectors) < 0) {
		int tmperr = errno;
		loff_t tmp;

/* If BLKGETSIZE fails, the file may not be a block device.  Try llseek */
/* instead. */
		if (_llseek (newvol->fd, 0, 0, &tmp, SEEK_END) < 0) {
/* If llseek fails as well, it is something without random access.  Bail. */
			perror ("llseek");
			errno = tmperr;
			perror ("BLKGETSIZE");
			close (newvol->fd);
			free (newvol);
			return -1;
		}

/* Since llseek was used, the size is in bytes.  Divide it by sector size. */
/* The extra is truncated, but thats okay, since the size is truncated again */
/* below.  Also since llseek was used, mark it as a file so readsector is not */
/* used later. */
		newvol->sectors = tmp / 512;
		newvol->vol_flags |= VOL_FILE;
	}

/* TiVo rounds off the size of the partition to even counts of 1024 sectors. */
	newvol->sectors &= ~(MFS_PARTITION_ROUND - 1);

/* If theres nothing there, assume the worst. */
	if (!newvol->sectors) {
		fprintf (stderr, "Error: Empty partition %s.\n", path);
		free (newvol);
		return -1;
	}

/* Add it to the tail of the volume list. */
	for (loop = &volumes; *loop; loop = &(*loop)->next) {
		newvol->start = (*loop)->start + (*loop)->sectors;
	}

	*loop = newvol;

	return newvol->start;
}

/*************************************************/
/* Return the size of volume starting at sector. */
int mfs_volume_size (unsigned int sector)
{
	struct volume_info *vol;

/* Find the volume this sector is from in the table of open volumes. */
	for (vol = volumes; vol; vol = vol->next) {
		if (vol->start == sector) {
			break;
		}
	}

	if (vol) {
		return (vol->sectors);
	}

	return 0;
}

/**********************************************/
/* Return the size of all loaded volume sets. */
int mfs_volume_set_size ()
{
	struct volume_info *vol;
	int total = 0;

	for (vol = volumes; vol; vol = vol->next) {
		total += vol->sectors;
	}

	return total;
}

/****************************************************************************/
/* Verify that a sector is writable.  This should be done for all groups of */
/* sectors to be written, since individual volumes can be opened RDONLY. */
int mfs_is_writable (unsigned int sector)
{
	struct volume_info *vol;

/* Find the volume this sector is from in the table of open volumes. */
	for (vol = volumes; vol; vol = vol->next) {
		if (vol->start <= sector && vol->start + vol->sectors > sector) {
			break;
		}
	}

	if (!vol || vol->vol_flags & VOL_RDONLY) {
		return fake_write;
	}

	return 1;
}

/*****************************************************************************/
/* Read data from the MFS volume set.  It must be in whole sectors, and must */
/* not cross a volume boundry. */
int mfs_read_data (void *buf, unsigned int sector, int count)
{
	struct volume_info *vol;
	loff_t result;

/* Find the volume this sector is from in the table of open volumes. */
	for (vol = volumes; vol; vol = vol->next) {
		if (vol->start <= sector && vol->start + vol->sectors > sector) {
			break;
		}
	}

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

		return readsectors (vol->fd, &vec, 1, &req);
	}
#endif

/* A file, or not TiVo, use llseek and read. */
	if (_llseek (vol->fd, sector >> 23, sector << 9, &result, SEEK_SET) < 0) {
		return -1;
	}

	return read (vol->fd, buf, count * 512);
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

/* Find the volume this sector is from in the table of open volumes. */
	for (vol = volumes; vol; vol = vol->next) {
		if (vol->start <= sector && vol->start + vol->sectors > sector) {
			break;
		}
	}

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

		return writesectors (vol->fd, &vec, 1, &req);
	}
#endif

/* A file, or not TiVo, use llseek and write. */
	if (_llseek (vol->fd, sector >> 23, sector << 9, &result, SEEK_SET) < 0) {
		return -1;
	}

	return write (vol->fd, buf, count * 512);
}

/*****************************************************************************/
/* Return the count of inodes.  Each inode is 2 sectors, so the count is the */
/* size of the inode zone maps divided by 2. */
unsigned int mfs_inode_count ()
{
	return zones[ztInode].size / 2;
}

/****************************************/
/* Find the sector number for an inode. */
unsigned int mfs_inode_to_sector (unsigned int inode)
{
	struct zone_map *cur;

/* Don't bother if it's not a valid inode. */
	if (inode >= mfs_inode_count ()) {
		return 0;
	}

/* For ease of calculation, turn this into a sector offset into the inode */
/* maps. */
	inode *= 2;

/* Loop through each inode map, seeing if the current inode is within it. */
	for (cur = zones[ztInode].next; cur; cur = cur->next) {
		if (inode < htonl (cur->map->size)) {
			return (inode + htonl (cur->map->first));
		}

/* If not, subtract the size so the inode sector offset is now relative to */
/* the next inode zone. */
		inode -= htonl (cur->map->size);
	}

/* This should never happen. */ 
	fprintf (stderr, "Inode zones corrupt!  I don't know what to do.\n");
	return 0;
}

/*************************************/
/* Read an inode data and return it. */
mfs_inode *mfs_read_inode (unsigned int inode)
{
	mfs_inode *in = calloc (512, 1);
	int sector;

	if (!in) {
		return NULL;
	}

/* Find the sector number for this inode. */
	sector = mfs_inode_to_sector (inode);
	if (sector == 0) {
		free (in);
		return NULL;
	}

	if (mfs_read_data ((void *)in, sector, 1) != 512) {
		free (in);
		return NULL;
	}

/* If the CRC is good, don't bother reading the next inode. */
	if (MFS_check_crc (in, 512, in->checksum)) {
		return in;
	}

/* CRC is bad, try reading the backup on the next sector. */
	fprintf (stderr, "mfs_read_inode: Inode %d corrupt, trying backup.\n", inode);

	if (mfs_read_data ((void *)in, sector + 1, 1) != 512) {
		free (in);
		return NULL;
	}

	if (MFS_check_crc (in, 512, in->checksum)) {
		return in;
	}

	fprintf (stderr, "mfs_read_inode: Inode %d backup corrupt, giving up.\n", inode);
	return NULL;
}

/******************************************************************/
/* Read an inode data based on an fsid, scanning ahead as needed. */
mfs_inode *mfs_read_inode_by_fsid (unsigned int fsid)
{
	int inode = (fsid * MFS_FSID_HASH) & (mfs_inode_count () - 1);
	mfs_inode *cur = NULL;
	int inode_base = inode;

	do {
		if (cur) {
			free (cur);
		}

		cur = mfs_read_inode (inode);
/* Repeat until either the fsid matches, the CHAINED flag is unset, or */
/* every inode has been checked, which I hope I will not have to do. */
	} while (htonl (cur->fsid) != fsid && (htonl (cur->inode_flags) & INODE_CHAINED) && (inode = (inode + 1) & (mfs_inode_count () - 1)) != inode_base);

/* If cur is NULL or the fsid is correct and in use, then cur contains the */
/* right return. */
	if (!cur || (htonl (cur->fsid) == fsid && cur->refcount != 0)) {
		return cur;
	}

/* This is not the inode you are looking for.  Move along. */
	free (cur);
	return NULL;
}

/*************************************/
/* Read a portion of an inodes data. */
int mfs_read_inode_data_part (mfs_inode *inode, unsigned char *data, unsigned int start, unsigned int count)
{
	int totread = 0;

/* Parameter sanity check. */
	if (!data || !count || !inode) {
		return 0;
	}

/* If it doesn't fit in the sector find out where it is. */
	if (inode->numblocks) {
		int loop;

/* Loop through each block in the inode. */
		for (loop = 0; count && loop < htonl (inode->numblocks); loop++) {
/* For sanity sake, make these variables. */
			unsigned int blkstart = htonl (inode->datablocks[loop].sector);
			unsigned int blkcount = htonl (inode->datablocks[loop].count);
			int result;

/* If the start offset has not been reached, skip to it. */
			if (start) {
				if (blkcount >= start) {
/* If the start offset is not within this block, decrement the start and keep */
/* going. */
					start -= blkcount;
					continue;
				} else {
/* The start offset is within this block.  Adjust the block parameters a */
/* little, since this is just local variables. */
					blkstart += start;
					blkcount -= start;
					start = 0;
				}
			}

/* If the entire data is within this block, make this block look like it */
/* is no bigger than the data. */
			if (blkcount > count) {
				blkcount = count;
			}

			result = mfs_read_data (data, blkstart, blkcount);
			count -= blkcount;

/* Error - propogate it up. */
			if (result < 0) {
				return result;
			}

/* Add to the total. */
			totread += result;
			data += result;
/* If this is it, or if the amount read was truncated, return it. */
			if (result != blkcount * 512 || count == 0) {
				return totread;
			}
		}
	} else if (htonl (inode->size) < 512 - 0x3c && inode->type != tyStream) {
		memset (data, 0, 512);
		memcpy (data, (unsigned char *)inode + 0x3c, htonl (inode->size));
		return 512;
	}

/* They must have asked for more data than there was.  Return the total read. */
	return totread;
}

/******************************************************************************/
/* Read all the data from an inode, set size to how much was read.  This does */
/* not allow streams, since they are be so big. */
unsigned char *mfs_read_inode_data (mfs_inode *inode, int *size)
{
	unsigned char *data;
	int result;

	if (inode->type == tyStream || !inode || !size || !inode->size) {
		if (size) {
			*size = 0;
		}
		return NULL;
	}

	*size = htonl (inode->size);

	data = malloc ((*size + 511) & ~511);
	if (!data) {
		*size = 0;
		return NULL;
	}

	result = mfs_read_inode_data_part (inode, data, 0, (*size + 511) / 512);

	if (result < 0) {
		*size = result;
		free (data);
		return NULL;
	}

	return data;
}

/************************************************************************/
/* Return how big a new zone map would need to be for a given number of */
/* allocation blocks. */
static int mfs_new_zone_map_size (unsigned int blocks)
{
	int size = sizeof (zone_header) + 4;
	int order = 0;

/* Figure out the first order of 2 that is needed to have at least 1 bit for */
/* every block. */
	while ((1 << order) < blocks) {
		order++;
	}

/* Increment it by one for loops and math. */
	order++;

/* Start by adding in the sizes for all the bitmap headers. */
	size += (sizeof (bitmap_header) + sizeof (bitmap_header *)) * (order);

/* Estimate the size of the bitmap table for each order of 2. */
	while (order--) {
		int bits = 1 << order;
/* This produces the right results, oddly enough.  Every bitmap with 8 or */
/* more bits takes 1 int more than needed, and this produces that. */
		int tblints = (bits + 57) / 32;
		size += tblints * 4;
	}

	return size;
}

/****************************************************************************/
/* Create a new zone map at the requested sector, pointing to the requested */
/* sector, and link it in. */
static int mfs_new_zone_map (unsigned int sector, unsigned int backup, unsigned int first, unsigned int size, unsigned int minalloc, zone_type type)
{
	unsigned int blocks = size / minalloc;
	int zonesize = (mfs_new_zone_map_size (blocks) + 511) & ~511;
	unsigned char *buf;
	zone_header *zone;
	zone_header *last;
	struct zone_map *cur;
	int loop;
	int order = 0;
	int fsmem_base;
	unsigned int *fsmem_pointers;
	unsigned int *curofs;

/* Truncate the size to the nearest allocation sized block. */
	size = size & ~(minalloc - 1);

/* Find the last loaded zone. */
	for (cur = loaded_zones; cur->next_loaded; cur = cur->next_loaded);

	if (!cur) {
		return -1;
	}

	last = cur->map;

/* To get the pointer into fsmem, start with the first pointer from the */
/* previous zone map.  Subtract the header and all the fsmem pointers from */
/* it, and thats the base for that map.  Niw add in all the sectors from that */
/* map, plus 1 extra and 8 bytes. */
	fsmem_base = htonl (*(unsigned int *)(last + 1)) - (sizeof (*last) + htonl (last->num) * 4) + htonl (last->length) * 512 + 512 + 8;

	buf = malloc (zonesize);

	if (!buf) {
		return -1;
	}

/* Fill in everything with lots and lots of dead beef.  Hope theres no */
/* vegitarians or vegans in the crowd. */
	for (loop = 0; loop < zonesize; loop += 4) {
		*(int *)(buf + loop) = htonl (0xdeadbeef);
	}

/* Figure out the order of the blocks count. */
	while ((1 << order) < blocks) {
		order++;
	}

	order++;

	zone = (zone_header *)buf;

/* Fill in the header values. */
	zone->sector = htonl (sector);
	zone->sbackup = htonl (backup);
	zone->length = htonl (zonesize / 512);
	zone->next.sector = 0;
	zone->next.length = 0;
	zone->next.size = 0;
	zone->next.min = 0;
	zone->type = htonl (type);
	zone->transaction = 0;
	zone->checksum = htonl (0xdeadf00d);
	zone->first = htonl (first);
	zone->last = htonl (first + size - 1);
	zone->size = htonl (size);
	zone->min = htonl (minalloc);
	zone->free = htonl (size);
	zone->zero = 0;
	zone->num = htonl (order);

/* Grab a pointer to the array where fsmem pointers will go. */
	fsmem_pointers = (unsigned int *)(zone + 1);
	curofs = (unsigned int *)(zone + 1) + order;

/* Fill in the allocation bitmaps.  This is simpler than it sounds.  The */
/* bitmaps are regressing from the full 1 bit = min allocation block up to */
/* 1 bit = entire drive.  A bit means the block is free.  Free blocks are */
/* represented by the largest bit possible.  In a perfect power of 2, a */
/* completely free table is represented by 1 bit in the last table.  This */
/* may sound complex, but it's really easy to fill in an empty table. */
/* While filling in the size values for the headers for each bitmap, any */
/* time you have an odd number of active bits, set the last one, because */
/* it is not represented by any larger bits. */
	for (loop = 0; order-- > 0; loop++, blocks /= 2) {
		int nbits;
		int nints;
		bitmap_header *bitmap = (bitmap_header *)curofs;
		fsmem_pointers[loop] = htonl (fsmem_base + (char *)curofs - (char *)zone);

/* Set in the basic, constant header values.  The nbits is how many bits */
/* there are in the table, including extra inactive bits padding to the */
/* next power of 2.  The nints represents how many ints those bits take up. */
		nbits = 1 << order;
		bitmap->nbits = htonl (nbits);
		nints = (nbits + 31) / 32;
		bitmap->nints = htonl (nints);

/* Clear all the bits by default. */
		memset (curofs + sizeof (*bitmap) / 4, 0, nints * 4);

/* Set the rest of the header.  The last doesn't seem to have much use, but */
/* it may be an optimization, so I set it to the last bit if I set it.  The */
/* reason to set the last bit is that this is the last table that block */
/* will be represented in, so it needs to be marked free here.  The next */
/* table's bit is too big it overflows into the inactive area, so is itself */
/* inactive. */
		if (blocks & 1) {
			bitmap->last = htonl (blocks - 1);
			bitmap->freeblocks = htonl (1);
			curofs[4 + (blocks - 1) / 32] = htonl (1 << (31 - (blocks - 1) % 32));
		} else {
			bitmap->last = 0;
			bitmap->freeblocks = 0;
		}

/* Step past this table. */
		curofs += sizeof (*bitmap) / 4 + (nbits + 57) / 32;
	}

/* Copy the pointer into the current end of the zone list. */
	last->next.sector = zone->sector;
	last->next.sbackup = zone->sbackup;
	last->next.length = zone->length;
	last->next.size = zone->size;
	last->next.min = zone->min;

/* Update the CRC in the new zone, as well as the previous tail, since it's */
/* next pointer was updated. */
	MFS_update_crc (last, htonl (last->length) * 512, last->checksum);
	MFS_update_crc (zone, htonl (zone->length) * 512, zone->checksum);

/* Write the changes, with the changes to live MFS last.  This should use */
/* the journaling facilities, but I don't know how. */
	mfs_write_data (zone, htonl (zone->sector), htonl (zone->length));
	mfs_write_data (zone, htonl (zone->sbackup), htonl (zone->length));
	mfs_write_data (last, htonl (last->sector), htonl (last->length));
	mfs_write_data (last, htonl (last->sbackup), htonl (last->length));

	return 0;
}

/***********************************************************************/
/* Add a new set of partitions to the MFS volume set.  In other words, */
/* mfsadd. */
int mfs_add_volume_pair (char *app, char *media)
{
	struct zone_map *cur;
	int fdApp, fdMedia;
	int appstart, mediastart;
	int appsize, mediasize, mapsize;
	char *tmp;
	unsigned char foo[512];

/* Make sure the volumes being added don't overflow the 128 bytes. */
	if (strlen (vol_hdr.partitionlist) + strlen (app) + strlen (media) + 3 >= 128) {
		fprintf (stderr, "No space in volume list for new volumes.\n");
		return -1;
	}

/* Make sure block 0 is writable.  It wouldn't do to get all the way to */
/* the end and not be able to update the volume header. */
	if (!mfs_is_writable (0)) {
		fprintf (stderr, "mfs_add_volume_pair: Readonly volume set.\n");
 		return -1;
	}

/* Walk the list of zone maps to find the last loaded zone map. */
	for (cur = loaded_zones; cur && cur->next_loaded; cur = cur->next_loaded);

/* For cur to be null, it must have never been set. */
	if (!cur) {
		fprintf (stderr, "mfs_add_volume_pair: Zone maps not loaded?\n");
		return -1;
	}

/* Check that the last zone map is writable.  This is needed for adding the */
/* new pointer. */
	if (!mfs_is_writable (htonl (cur->map->sector))) {
		fprintf (stderr, "mfs_add_volume_pair: Readonly volume set.\n");
 		return -1;
	}

	tmp = mfs_device_translate (app);
	fdApp = open (tmp, O_RDWR);
	if (fdApp < 0) {
		perror (tmp);
		return -1;
	}

	tmp = mfs_device_translate (tmp);
	fdMedia = open (tmp, O_RDWR);
	if (fdMedia < 0) {
		perror (tmp);
		return -1;
	}

	close (fdApp);
	close (fdMedia);

	appstart = mfs_add_volume (app, O_RDWR);
	mediastart = mfs_add_volume (media, O_RDWR);

	if (appstart < 0 || mediastart < 0) {
		fprintf (stderr, "mfs_add_volume_pair: Error adding new volumes to set.\n");
		mfs_reinit (O_RDWR);
		return -1;
	}

	if (!mfs_is_writable (appstart) | !mfs_is_writable (mediastart)) {
		fprintf (stderr, "mfs_add_volume_pair: Could not add new volumes writable.\n");
		mfs_reinit (O_RDWR);
		return -1;
	}

	appsize = mfs_volume_size (appstart);
	mediasize = mfs_volume_size (mediastart);
	mapsize = (mfs_new_zone_map_size (mediasize / 0x800) + 511) / 512;

	if (mapsize * 2 + 2 > appsize) {
		fprintf (stderr, "mfs_add_volume_pair: New app size too small!  (Need %d more bytes)\n", (mapsize * 2 + 2 - appsize) * 512);
		mfs_reinit (O_RDWR);
		return -1;
	}

	if (mfs_new_zone_map (appstart + 1, appstart + appsize - mapsize - 1, mediastart, mediasize, 0x800, ztMedia) < 0) {
		fprintf (stderr, "mfs_add_volume_pair: Failed initializing new zone map.\n");
		mfs_reinit (O_RDWR);
		return -1;
	}

	sprintf (foo, "%s %s %s", vol_hdr.partitionlist, app, media);
	foo[127] = 0;
	strcpy (vol_hdr.partitionlist, foo);
	vol_hdr.total_sectors = htonl (mfs_volume_set_size ());
	MFS_update_crc (&vol_hdr, sizeof (vol_hdr), vol_hdr.checksum);

	memset (foo, 0, sizeof (foo));
	memcpy (foo, &vol_hdr, sizeof (vol_hdr));
	mfs_write_data (foo, 0, 1);
	mfs_write_data (foo, volumes->sectors - 1, 1);

	return 0;
}

/***********************************************/
/* Free space used by the volumes linked list. */
void mfs_cleanup_volumes ()
{
	while (volumes) {
		struct volume_info *cur;

		cur = volumes;
		volumes = volumes->next;

		close (cur->fd);
		free (cur);
	}
}

/**************************************/
/* Load and verify the volume header. */
int mfs_load_volume_header (int flags)
{
	unsigned char buf[512];
	unsigned char *volume_names;
	unsigned int total_sectors = 0;
	struct volume_info *vol;

/* Read in the volume header. */
	if (mfs_read_data (buf, 0, 1) != 512) {
		perror ("mfs_load_volume_header: mfs_read_data");
		return -1;
	}

/* Copy it into the static space.  This is needed since mfs_read_data must */
/* read even sectors. */
	memcpy ((void *)&vol_hdr, buf, sizeof (vol_hdr));

/* Verify the checksum. */
	if (!MFS_check_crc (&vol_hdr, sizeof (vol_hdr), vol_hdr.checksum)) {
		fprintf (stderr, "Primary volume header corrupt, trying backup.\n");
/* If the checksum doesn't match, try the backup. */
		if (mfs_read_data (buf, volumes->sectors - 1, 1) != 512) {
			perror ("mfs_load_volume_header: mfs_read_data");
			return -1;
		}

		memcpy ((void *)&vol_hdr, buf, sizeof (vol_hdr));

		if (!MFS_check_crc (&vol_hdr, sizeof (vol_hdr), vol_hdr.checksum)) {
/* Backup checksum doesn't match either.  It's the end of the world! */
			fprintf (stderr, "Secondary volume header corrupt, giving up.\n");
			fprintf (stderr, "mfs_load_volume_header: Bad checksum.\n");
			return -1;
		}
	}

/* Load the partition list from MFS. */
	volume_names = vol_hdr.partitionlist;

/* Skip the first volume since it's already loaded. */
	if (*volume_names) {
		volume_names += strcspn (volume_names, " \t\r\n");
		volume_names += strspn (volume_names, " \t\r\n");
	}

/* If theres more volumes, add each one in turn.  When mfs_add_volume calls */
/* mfs_device_translate, it will take care of seperating out one device. */
	while (*volume_names) {
		if (mfs_add_volume (volume_names, flags) < 0) {
			return -1;
		}

/* Skip the device just loaded. */
		volume_names += strcspn (volume_names, " \t\r\n");
		volume_names += strspn (volume_names, " \t\r\n");
	}

/* Count the total number of sectors in the volume set. */
	for (vol = volumes; vol; vol = vol->next) {
		total_sectors += vol->sectors;
	}

/* If the sectors mismatch, report it.. But continue anyway. */
	if (total_sectors != htonl (vol_hdr.total_sectors)) {
		fprintf (stderr, "mfs_load_volume_header: Total sectors(%u) mismatch with volume header (%d)\n", total_sectors, htonl (vol_hdr.total_sectors));
		fprintf (stderr, "mfs_load_volume_header: Loading anyway.\n");
	}

	return total_sectors;
}

/******************************************/
/* Free the memory used by the zone maps. */
void mfs_cleanup_zone_maps ()
{
	int loop;

	for (loop = 0; loop < ztMax; loop++) {
		while (zones[loop].next) {
			struct zone_map *map = zones[loop].next;

			zones[loop].next = map->next;
			free (map->map);
			free (map);
		}
	}

	loaded_zones = NULL;
}

/*************************************************************/
/* Load a zone map from the drive and verify it's integrity. */
static zone_header *mfs_load_zone_map (zone_map_ptr *ptr)
{
	zone_header *hdr = calloc (htonl (ptr->length), 512);

	if (!hdr) {
		return NULL;
	}

/* Read the map. */
	mfs_read_data ((unsigned char *)hdr, htonl (ptr->sector), htonl (ptr->length));

/* Verify the CRC matches. */
	if (!MFS_check_crc ((unsigned char *)hdr, htonl (ptr->length) * 512, hdr->checksum)) {
		fprintf (stderr, "mfs_load_zone_map: Primary zone map corrupt, loading backup.\n");
/* If the CRC doesn't match, try the backup map. */
		mfs_read_data ((unsigned char *)hdr, htonl (ptr->sbackup), htonl (ptr->length));
		if (!MFS_check_crc ((unsigned char *)hdr, htonl (ptr->length) * 512, hdr->checksum)) {
			fprintf (stderr, "mfs_load_zone_map: Secondary zone map corrupt, giving up.\n");
		
			fprintf (stderr, "mfs_load_zone_map: Zone map checksum error!\n");
			free (hdr);
			return NULL;
		}
	}

	return hdr;
}

/***************************/
/* Load the zone map list. */
int mfs_load_zone_maps ()
{
	zone_map_ptr *ptr = &vol_hdr.zonemap;
	zone_header *cur;
	struct zone_map **loaded_head = &loaded_zones;
	struct zone_map **cur_heads[ztMax];
	int loop;

/* Start clean. */
	mfs_cleanup_zone_maps ();
	memset (zones, 0, sizeof (zones));

	for (loop = 0; loop < ztMax; loop++) {
		cur_heads[loop] = &zones[loop].next;
	}

	loop = 0;

	while (ptr->sector && ptr->sbackup != htonl (0xdeadbeef)) {
		struct zone_map *newmap;

/* Read the map, verify it's checksum. */
		cur = mfs_load_zone_map (ptr);

		if (!cur) {
			return -1;
		}

		if (htonl (cur->type) < 0 || htonl (cur->type) >= ztMax) {
			fprintf (stderr, "mfs_load_zone_maps: Bad map type %d.\n", htonl (cur->type));
			free (cur);
			return -1;
		}

		newmap = calloc (sizeof (*newmap), 1);
		if (!newmap) {
			fprintf (stderr, "mfs_load_zone_maps: Out of memory.\n");
			free (cur);
			return -1;
		}

/* Link it into the proper map type pool. */
		newmap->map = cur;
		*cur_heads[htonl (cur->type)] = newmap;
		cur_heads[htonl (cur->type)] = &newmap->next;

/* Also link it into the loaded order. */
		*loaded_head = newmap;
		loaded_head = &newmap->next_loaded;
/* And add it to the totals. */
		zones[htonl (cur->type)].size += htonl (cur->size);
		zones[htonl (cur->type)].free += htonl (cur->free);
		loop++;

		ptr = &cur->next;
	}

	return loop;
}

/***********************************************************/
/* Initialize MFS, load the volume set, and all zone maps. */
/* TODO: If opened read-write, also replay journal and make sure real event */
/* switcher is not running. */
int mfs_init (int flags)
{
/* Bootstrap the first volume from MFS_DEVICE. */
	char *cur_volume = getenv ("MFS_DEVICE");

/* Only allow O_RDONLY or O_RDWR. */
	if ((flags & O_ACCMODE) == O_RDONLY) {
		flags = O_RDONLY;
	} else {
		flags = O_RDWR;
	}

/* If no volume is passed, assume hda10. */
	if (!cur_volume) {
		cur_volume = "/dev/hda10";
	}

/* Load the first volume by hand. */
	if (mfs_add_volume (cur_volume, flags) < 0) {
		mfs_cleanup_volumes ();
		return -1;
	}

/* Take care of loading the rest. */
	if (mfs_load_volume_header (flags) <= 0) {
		mfs_cleanup_volumes ();
		return -1;
	}

/* Load the zone maps. */
	if (mfs_load_zone_maps () < 0) {
		mfs_cleanup_volumes ();
		mfs_cleanup_zone_maps ();
		return -1;
	}

	cur_volume = getenv ("MFS_FAKE_WRITE");

	if (cur_volume && *cur_volume) {
		fake_write = 1;
	}

	return 0;
}

/************************************************/
/* Free all used memory and close opened files. */
void mfs_cleanup ()
{
	mfs_cleanup_zone_maps ();
	mfs_cleanup_volumes ();
}

/********************************/
/* Do a cleanup and init fresh. */
int mfs_reinit (int flags)
{
	mfs_cleanup ();
	return mfs_init (flags);
}
