#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#if HAVE_MALLOC_H
#include <malloc.h>
#endif
#if HAVE_SYS_MALLOC_H
#include <sys/malloc.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#include <sys/types.h>
#ifdef HAVE_ASM_TYPES_H
#include <asm/types.h>
#endif
#include <fcntl.h>
#include <zlib.h>
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#include <sys/param.h>
#include <string.h>
#include <sys/ioctl.h>

#include "mfs.h"
#include "macpart.h"
#include "log.h"

#define RESTORE
#include "backup.h"

/**
 * Tivo device names.
 * Defaults to /dev/hd{a,b}.  For a Premier backup, we'll replace these
 * with /dev/sd{a,b}.
 */
extern char* tivo_devnames[];


/*************************************************/
/* Initializes the backup structure for restore. */
struct backup_info *
init_restore (unsigned int flags)
{
	struct backup_info *info;

	flags &= RF_FLAGS;

	info = calloc (sizeof (*info), 1);

	if (!info)
	{
		return 0;
	}

	info->state_machine = &restore_v1;
	while (info->state < bsMax && (*info->state_machine)[info->state] == NULL)
		info->state++;

	info->nsectors = 1;
	info->back_flags = flags;

	info->crc = ~0;

	return info;
}

void
restore_set_varsize (struct backup_info *info, int size)
{
	info->varsize = size;
}

void
restore_set_swapsize (struct backup_info *info, int size)
{
	info->swapsize = size;
}

void
restore_set_bswap (struct backup_info *info, int bswap)
{
	info->bswap = bswap;
}

void
restore_set_mfs_type (struct backup_info *info, int bits)
{
	info->bitsize = bits;
}

/***************************************************************************/
/* State handlers - return val -1 = error, 0 = more data needed, 1 = go to */
/* next state. */

/***********************************/
/* Generic handler for header data */
enum backup_state_ret
restore_read_header (struct backup_info *info, void *data, unsigned size, unsigned *consumed, void *dest, unsigned total, unsigned datasize)
{
	unsigned count = total * datasize - info->state_val1;

	if (size == 0)
	{
		info->err_msg = "Internal error: Restore buffer empty";
		return bsError;
	}

/* Copy as much as possible */
	if (count + info->shared_val1 > size * 512)
	{
		count = size * 512 - info->shared_val1;
	}

	memcpy ((char *)dest + info->state_val1, (char *)data + info->shared_val1, count);

	info->state_val1 += count;
	info->shared_val1 += count;
	*consumed = info->shared_val1 / 512;
	info->shared_val1 &= 511;

	if (info->state_val1 < total * datasize)
		return bsMoreData;

	return bsNextState;
}

/***********************************/
/* Read partition info from backup */
/* state_val1 = offset of last copied partition */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
restore_state_partition_info (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	return restore_read_header (info, data, size, consumed, info->parts, info->nparts, sizeof (struct backup_partition));
}

/************************/
/* Read MFS volume list */
/* state_val1 = offset of last copied MFS partition */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
restore_state_mfs_partition_info (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	return restore_read_header (info, data, size, consumed, info->mfsparts, info->nmfs, sizeof (struct backup_partition));
}

/********************************/
/* Finish off the backup header */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
restore_state_info_end (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	if (size == 0)
	{
		info->err_msg = "Internal error: Restore buffer empty";
		return bsError;
	}

	if (info->shared_val1 > 0)
	{
		*consumed = 1;
		info->shared_val1 = 0;
	}

	if (info->back_flags & RF_ENDIAN)
	{
		unsigned loop;

		for (loop = 0; loop < info->nparts; loop++)
		{
			info->parts[loop].sectors = Endian32_Swap (info->parts[loop].sectors);
		}

		for (loop = 0; loop < info->nblocks; loop++)
		{
			info->blocks[loop].firstsector = Endian32_Swap (info->blocks[loop].firstsector);
			info->blocks[loop].sectors = Endian32_Swap (info->blocks[loop].sectors);
		}

		for (loop = 0; loop < info->nmfs; loop++)
		{
			info->mfsparts[loop].sectors = Endian32_Swap (info->mfsparts[loop].sectors);
		}

		for (loop = 0; loop < info->nzones; loop++)
		{
			info->zonemaps[loop].map_length = Endian32_Swap (info->zonemaps[loop].map_length);
			info->zonemaps[loop].zone_type = Endian32_Swap (info->zonemaps[loop].zone_type);
			info->zonemaps[loop].fsmem_base = Endian32_Swap (info->zonemaps[loop].fsmem_base);
			info->zonemaps[loop].min_au = Endian32_Swap (info->zonemaps[loop].min_au);
			info->zonemaps[loop].size = Endian64_Swap (info->zonemaps[loop].size);
		}
	}


	if (info->back_flags & BF_PREMIERE) 
	{
		info->back_flags |= RF_SWAPV1;  /* premiere doesn't support V0 swap */
		tivo_devnames[0] = "/dev/sda";
		tivo_devnames[1] = "/dev/sdb";
	}

	return bsNextState;
}

/**************************/
/* Restore the boot block */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
restore_state_boot_block (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	if (size == 0)
	{
		info->err_msg = "Internal error: Restore buffer empty";
		return bsError;
	}

	if (tivo_partition_write_bootsector (info->devs[0].devname, data) != 512)
	{
		info->err_msg = "Error writing boot block: %s";
		info->err_arg1 = (size_t)strerror (errno);
		return bsError;
	}

	*consumed = 1;

	return bsNextState;
}

/****************************************/
/* Restore the raw (non-MFS) partitions */
/* state_val1 = current partition index */
/* state_val2 = offset within current partition */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
restore_state_partitions (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	tpFILE *file;
	int tocopy = info->parts[info->state_val1].sectors - info->state_val2;

	if (size == 0)
	{
		info->err_msg = "Internal error: Restore buffer empty";
		return bsError;
	}

/* The partition is larger than the buffer, read as much as possible. */
	if (tocopy > size)
	{
		tocopy = size;
	}

/* Get the file for this partition from the info structure. */
	file = info->devs[(int)info->parts[info->state_val1].devno].files[(int)info->parts[info->state_val1].partno];

/* If the file isn't opened, open it. */
	if (!file)
	{
		file = tivo_partition_open_direct (info->devs[(int)info->parts[info->state_val1].devno].devname, info->parts[info->state_val1].partno, O_RDWR);
/* The sick part, is most of this line is an lvalue. */
		info->devs[(int)info->parts[info->state_val1].devno].files[(int)info->parts[info->state_val1].partno] = file;

		if (!file)
		{
			info->err_msg = "Internal error opening partition %s%d for writing";
			info->err_arg1 = (size_t)info->devs[(int)info->parts[info->state_val1].devno].devname;
			info->err_arg2 = (size_t)info->parts[info->state_val1].partno;
			return bsError;
		}
	}

	if (tivo_partition_write (file, data, info->state_val2, tocopy) < 0)
	{
		info->err_msg = "%s backing up partitions";
		if (errno)
			info->err_arg1 = (size_t)strerror (errno);
		else
			info->err_arg1 = (size_t)"Unknown error";
		return bsError;
	}

	*consumed = tocopy;
	info->state_val2 += tocopy;

	if (info->state_val2 >= info->parts[info->state_val1].sectors)
	{
		info->state_val1++;
		info->state_val2 = 0;
	}

	if (info->state_val1 < info->nparts)
		return bsMoreData;

	return bsNextState;
}

/*********************************/
/* Initialize the MFS Volume set */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
restore_state_mfs_init (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	int loop;

	if (size == 0)
	{
		info->err_msg = "Internal error: Restore buffer empty";
		return bsError;
	}

/* Load the volumes to prepare for MFS bootstrap */
	for (loop = 0; loop < info->nmfs; loop++)
	{
		char devname[MAXPATHLEN];
		int devno = info->mfsparts[loop].devno;
		int partno = info->mfsparts[loop].partno;

		sprintf (devname, "%s%d", tivo_devnames[devno], partno);
		mfsvol_add_volume (info->vols, devname, O_RDWR);
	}

	return bsNextState;
}

/*****************************************************************************/
/* Return the next sectors in the backup.  This is where all the data in the */
/* backup originates.  If it's backed up, it came from here.  This only */
/* reads the data from the info structure.  Compression is handled */
/* elsewhere. */
static unsigned int
restore_next_sectors (struct backup_info *info, unsigned char *buf, int sectors)
{
	enum backup_state_ret ret;
	unsigned consumed;

	unsigned restore_blocks = 0;

	while (sectors > 0 && info->state < bsMax && info->state >= bsBegin)
	{
		if (info->state > bsInfoEnd && !(info->back_flags & RF_INITIALIZED))
		{
			break;
		}

		consumed = 0;

		ret = ((*info->state_machine)[info->state]) (info, buf, sectors, &consumed);

		if (consumed > sectors)
		{
			info->err_msg = "Internal error: State %d consumed too much buffer";
			info->err_arg1 = (size_t)info->state;
			return -1;
		}

/* Handle return codes */
		switch (ret)
		{
		case bsError:
/* Error message should be set by handler */
			return -1;
			break;

/* Nothing to do */
		case bsMoreData:
			break;

/* Find the next valid state */
		case bsNextState:
			while (info->state < bsMax && (*info->state_machine)[++info->state] == NULL) ;
/* Initialize per-state values */
			info->state_val1 = 0;
			info->state_val2 = 0;
			info->state_ptr1 = NULL;
			break;

		default:
			info->err_msg = "Internal error: State %d returned %d";
			info->err_arg1 = (size_t)info->state;
			info->err_arg2 = (size_t)ret;
			return -1;
		}

/* Deal with consumed buffer */
		if (consumed > 0)
		{
/* Probably should be before for restore, but some day this may be merged */
/* with backup, so keep the code identical */
		  info->crc = compute_crc ( (unsigned char *)buf, consumed * 512, info->crc);
			info->cursector += consumed;
			restore_blocks += consumed;
			sectors -= consumed;
			buf += consumed * 512;
		}
	}

	return restore_blocks;
}

/*************************************************************************/
/* Pass the data to the front-end program.  This handles compression and */
/* all that fun stuff. */
unsigned int
restore_write (struct backup_info *info, unsigned char *buf, unsigned int size)
{
	unsigned int retval = 0;

	if (info->back_flags & BF_COMPRESSED)
	{
/* The first sector is never compressed.  But thats okay, because the backup */
/* flags will have not been read yet. */
		if (!info->comp)
		{
			return retval;
		}

		info->comp->avail_in = size;
		info->comp->next_in = (unsigned char *) buf;
		while ((info->comp && info->comp->avail_in > 0) ||
			(((info->back_flags & RF_NOMORECOMP) || !(info->back_flags & RF_INITIALIZED)) &&
			(size_t)info->comp->next_out - (size_t)info->comp_buf > 512))
		{
			if ((size_t)info->comp->next_out - (size_t)info->comp_buf > 512)
			{
				int nread = restore_next_sectors (info, info->comp_buf, ((size_t)info->comp->next_out - (size_t)info->comp_buf) / 512);
				if (nread < 0)
				{
					return -1;
				}
				if (nread == 0)
				{
					break;
				}

				nread *= 512;
				if ((size_t)info->comp->next_out - (size_t)info->comp_buf > nread)
				{
					int nleft = (size_t)info->comp->next_out - (size_t)info->comp_buf - nread;

					memmove (info->comp_buf, info->comp->next_out - nleft, nleft);
				}
				info->comp->avail_out += nread;
				info->comp->next_out -= nread;
			}
			else if (!(info->back_flags & RF_NOMORECOMP))
			{
				int zres = inflate (info->comp, 0);

				switch (zres) {
				case Z_STREAM_END:
					info->back_flags |= RF_NOMORECOMP;
					continue;
				case Z_OK:
					break;
				case Z_NEED_DICT:
					info->err_msg = "No dict to feed hungry inflate";
					return -1;
				case Z_ERRNO:
					info->err_msg = "Inflate is doing things it shouldn't be";
					return -1;
				case Z_STREAM_ERROR:
					info->err_msg = "Internal error: zlib structures corrupt";
					return -1;
				case Z_DATA_ERROR:
					info->err_msg = "Error in compressed data stream";
					return -1;
				case Z_MEM_ERROR:
					info->err_msg = "Decompression out of memory";
					return -1;
				case Z_BUF_ERROR:
#if DEBUG
					fprintf (stderr, "Non-fatal Z_BUF_ERROR from inflate()\n");
#endif
					return retval;
				default:
					info->err_msg = "Unknown zlib_error %d";
					info->err_arg1 = (size_t)(size_t)zres;
					break;
				}
			}
			else
			{
				break;
			}
		}
		if (info->comp)
		{
			retval += size - info->comp->avail_in;
		}
	}
	else
	{
		if (size < 512)
		{
			info->err_msg = "Short read from backup";
			return -1;
		}

		if (info->cursector == 0)
		{
			int nwrit = restore_next_sectors (info, buf, 1);
			if (nwrit != 1)
				return -1;

			size -= 512;
			buf += 512;
			retval += 512;

			if (info->back_flags & BF_COMPRESSED)
			{
				info->comp_buf = calloc (2048, 512);
				if (!info->comp_buf)
				{
					info->err_msg = "Memory exhausted";
					return -1;
				}
				info->comp = calloc (sizeof (*info->comp), 1);
				if (!info->comp)
				{
					free (info->comp_buf);
					info->err_msg = "Memory exhausted";
					return -1;
				}

				info->comp->zalloc = Z_NULL;
				info->comp->zfree = Z_NULL;
				info->comp->opaque = Z_NULL;
				info->comp->next_in = Z_NULL;
				info->comp->avail_in = 0;
				info->comp->next_out = info->comp_buf;
				info->comp->avail_out = 512 * 2048;

				if (inflateInit (info->comp) != Z_OK)
				{
					free (info->comp_buf);
					free (info->comp);
					info->err_msg = "Deompression error";
					return -1;
				}

				if (size > 0)
				{
					retval = restore_write (info, buf, size);

					return retval < 0? retval: retval + 512;
				}
			}
			else
			{
				if (size < 512)
				{
					return retval;
				}
			}
		}
		return retval + restore_next_sectors (info, buf, size / 512) * 512;
	}

	return retval;
}

/* Tries to figure out the best arrangement of partitions to use disk space */
/* appropriately. */
static int
find_optimal_partitions (struct backup_info *info, unsigned int min1, unsigned int secs1, unsigned int secs2)
{
/* a12a13 a14a15 b2b3 b4b5 b6b7 b8b9 b10b11 b12b13 b14b15 */
	int bestorder = -1;
	unsigned int bestleft = secs1 - min1;
	int loop, loop2, loop3;
	int count;
	char *err = 0;
	int partlimit = 16;

#if DEBUG
	fprintf (stderr, "find_optimal_partitions (..., %d, %d, %d)\n", min1, secs1, secs2);
#endif

	if (info->back_flags & RF_NOFILL)
		partlimit = 14;

	if (info->back_flags & RF_BALANCE)
	{
		bestleft = secs1;
	}

/* This is a very simple process since there are only 2 drives max; a */
/* partition is either on one drive or the other.  If we call them 1 and 0, */
/* suddenly the problem is turned into a bitfield.  Take one bit for each */
/* partition, and count through all the numbers from 000... to 111... */
/* Of course, media partitions stick with app partition pairs.  Should work */
/* without this, but eh, why bother.  Spread the work out and all. */
	for (loop = 0; loop < (1 << (info->nmfs / 2 - 1)); loop++)
	{
/* Partitions 10 and 11, the first MFS pair, are always first.  It has the */
/* info where to find the others.  Therefore, the current highest partition */
/* on drive 1 is 11.  On drive 2 there is just the hypothetical partition */
/* table, which is always partition 1. */
		int max1 = 11;
		int max2 = 1;
/* Again, free space is calculated, partition 1 has some unknown space used, */
/* which is passed in.  Partition two, once again, has the partition table. */
		int free1 = secs1 - min1;
		int free2 = secs2;

/* All these loops.  Maybe I should re-think my naming convention.  Well, */
/* loop is the current bitfeild map, loop2 is a walker which parses the map */
/* and adds up the space used on each drive, and loop3 is the current MFS */
/* pair index. */
		for (loop2 = 1 << (info->nmfs / 2 - 2), loop3 = 2; loop2 && free1 >= 0 && free2 >= 0; loop2 >>= 1, loop3 += 2)
		{
/* This looks at which drive the partition is on and adds it to the */
/* appropriate total. */
			if (loop & loop2)
			{
				max1 += 2;
				free1 -= info->mfsparts[loop3].sectors + info->mfsparts[loop3 + 1].sectors;
			}
			else
			{
				max2 += 2;
				free2 -= info->mfsparts[loop3].sectors + info->mfsparts[loop3 + 1].sectors;
			}
		}

/* Check to make sure there are not too many partitions.  Linux can handle */
/* up to 128, but TiVo only has devices for 1-16.  But it is moot, since */
/* TiVo only has storage for 12 partitions (128 bytes) in MFS.  I suppose */
/* you could munge device names to get more on there, like putting them in */
/* / instead of /dev and naming them like /ha11 /ha12 /ha13 or whatever. */
/* But..  nah. */
		if (max1 < partlimit && max2 < partlimit && free1 >= 0 && free2 >= 0)
		{
/* If the partitions are being balanced, a filled drive is one with */
/* the closest data to either the end or the middle. */
			if (info->back_flags & RF_BALANCE)
			{
				unsigned int left = secs1 / 2 > free1? secs1 / 2 - free1: free1 - secs1 / 2;
				if (left <= bestleft)
				{
					if ((max1 - 9) * 11 + (max2 - 1) * 10 < 128)
					{
						bestorder = loop;
						bestleft = left;
					}
					else
						err = "Too many MFS partitions";
				}
			} else if (free1 <= bestleft)
			{
				if ((max1 - 9) * 11 + (max2 - 1) * 10 < 128)
				{
					bestorder = loop;
					bestleft = free1;
				}
				else
					err = "Too many MFS partitions";
			}
		}
	}

	if (bestorder < 0)
	{
		info->err_msg = err? err: "Unable to fit backup onto drives";
		return -1;
	}

/* Now redefining.  The bitfeild is bestorder, which is now known to be the */
/* best order, loop is the walker, loop2 is the mfs partition index, and */
/* loop3 is the next available partition on drive A.  Only drive A at the */
/* moment. */
	count = 10;
	for (loop = 1 << (info->nmfs / 2 - 1), loop2 = 0, loop3 = 12; loop; loop >>= 1, loop2 += 2)
	{
/* Add to the partitions to be created list if it is on this device. */
		if (bestorder & loop)
		{
			info->newparts[count].devno = 0;
			info->mfsparts[loop2].devno = 0;
			info->newparts[count].partno = loop3;
			info->mfsparts[loop2].partno = loop3;
			info->newparts[count].sectors = info->mfsparts[loop2].sectors;
			info->newparts[count + 1].devno = 0;
			info->mfsparts[loop2 + 1].devno = 0;
			info->newparts[count + 1].partno = loop3 + 1;
			info->mfsparts[loop2 + 1].partno = loop3 + 1;
			info->newparts[count + 1].sectors = info->mfsparts[loop2 + 1].sectors;
			info->devs[0].nparts += 2;

			count += 2;
			loop3 += 2;
			if (info->back_flags & BF_PREMIERE && loop3 == 14)
			{
				loop3++;
			}
		}
	}

/* Do the same for device 1.  The numbers look a little fishy here if you */
/* are paying attention.  They don't match above.  It is just a minor */
/* optimization - the first partition will always be on drive 1. */
	for (loop = 1 << (info->nmfs / 2 - 2), loop2 = 2, loop3 = 2; loop; loop >>= 1, loop2 += 2)
	{
		if (!(bestorder & loop))
		{
			info->newparts[count].devno = 1;
			info->mfsparts[loop2].devno = 1;
			info->newparts[count].partno = loop3;
			info->mfsparts[loop2].partno = loop3;
			info->newparts[count].sectors = info->mfsparts[loop2].sectors;
			info->newparts[count + 1].devno = 1;
			info->mfsparts[loop2 + 1].devno = 1;
			info->newparts[count + 1].partno = loop3 + 1;
			info->mfsparts[loop2 + 1].partno = loop3 + 1;
			info->newparts[count + 1].sectors = info->mfsparts[loop2 + 1].sectors;
			count += 2;
			loop3 += 2;
			info->devs[1].nparts += 2;
		}
	}

	err = NULL;

	return 0;
}

/* Test a device to see if the MFS will fit on it.  This is non-destructive */
/* but the restore can not start without it.  It also cannot be done again */
/* once it succeeds. */
int
restore_trydev (struct backup_info *info, char *dev1, char *dev2)
{
	uint64_t secs1 = 0;
	uint64_t secs2 = 0;
	int swab1 = 0;
	int swab2 = 0;
	uint64_t min1 = 0;
	uint64_t count;
	int loop;

/* Make sure this is a first potentially sucessful run. */
	if (info->back_flags & RF_INITIALIZED || info->state - 1 > bsInfoEnd)
	{
		info->err_msg = "Internal error 4 attempt to re-initialize restore";
		return -1;
	}
/* Make sure there is at least 1 device. */
	if (!dev1 || !*dev1)
	{
		info->err_msg = "No restore target device";
		return -1;
	}
/* Make sure the MFS set is an even number. */
	if ((info->nmfs & 1) == 1)
	{
		info->err_msg = "Internal error 5: Odd number of MFS partitions in backup file";
		return -1;
	}

	if (info->bswap)
	{
		swab1 = info->bswap > 0;
		swab2 = swab1;
	}
	else
	{
		if (info->back_flags & BF_NOBSWAP)
			swab1 = 0;
		else
			swab1 = 1;
		swab2 = swab1;
	}

	if (info->vols)
		mfsvol_cleanup (info->vols);
	info->vols = mfsvol_init (dev1, dev2);
	if (!info->vols)
	{
		info->err_msg = "Out of memory";
		return -1;
	}

	if (tivo_partition_devswabbed (dev1))
		swab1 ^= 1;
	if (dev2 && *dev2 && tivo_partition_devswabbed (dev2))
		swab2 ^= 1;

/* Try to initialize the drive partition table. */
	if (tivo_partition_table_init (dev1, swab1) < 0)
	{
		info->err_msg = "Unable to open %s for writing";
		info->err_arg1 = (size_t)dev1;
		return -1;
	}

/* Get the size, so fittingness will be known, and any non device will be */
/* detected. */
	secs1 = tivo_partition_total_free (dev1);

#if DEBUG
	fprintf (stderr, "Drive 1 size: %lld\n", secs1);
#endif

/* If there is a second device, do the same. */
	if (dev2 && *dev2)
	{
		if (tivo_partition_table_init (dev2, swab2) < 0)
		{
			info->err_msg = "Unable to open %s for writing";
			info->err_arg1 = (size_t)dev2;
			return -1;
		}

		secs2 = tivo_partition_total_free (dev2);

#if DEBUG
		fprintf (stderr, "Drive 2 size: %lld\n", secs2);
#endif
	}

/* Check for consistency in the partitions. */
	for (loop = 0, count=0; loop < info->nparts; loop++)
	{
/* All partitions should be device 0 - it is up to restore to decide. */
/* Furthermore, partitions less than 2 are special. */
		if (info->parts[loop].devno != 0 || info->parts[loop].partno < 2)
		{
			info->err_msg = "Format error in backup file partition list";
			return (-1);
		}

/* If /var was in the backup, and a varsize was given, make sure they match. */
		if (info->parts[loop].partno > 7 && info->parts[loop].partno == 9)
		{
			if (info->varsize && info->varsize != info->parts[loop].sectors)
			{
				info->err_msg = "Varsize in backup (%d) mis-matches requested varsize(%d)";
				info->err_arg1 = (size_t)(size_t)(info->varsize / 2048);
				info->err_arg2 = (size_t)(size_t)(info->parts[loop].sectors / 2048);
				return -1;
			}

			info->varsize = info->parts[loop].sectors;
			count++;
		} else {
/* If it's not /var, count it in the total, /var will be handles later. */
			min1 += info->parts[loop].sectors;
			count++;
		}
	}

/* If there are 3 partitions, double it.  No backup currently has both */
/* sets of root.  If they do in the future, this will be changed. */
	if (count <= 5)
	{
#if DEBUG
		fprintf (stderr, "Size of non-var partitions in backup: %lld\n", min1);
#endif
		min1 *= 2;
	}

/* Set the default swapsize and varsize. */
	if (info->swapsize == 0)
	{
		info->swapsize = 64 * 1024 * 2;
	}
	if (info->varsize == 0)
	{
		info->varsize = 128 * 1024 * 2;
	}

/* Account for misc generated partitions and first MFS pair, which is always */
/* on the first drive. */
/* (Boot sector, partition table uncounted) swap, var, mfs set 1 */
	min1 += info->swapsize + info->varsize + info->mfsparts[0].sectors + info->mfsparts[1].sectors;

#if DEBUG
	fprintf (stderr, "Minimum drive 1 size: %" PRId64 "\n", min1);
#endif

/* Make sure the first drive is big enough for the basics. */
	if (min1 > secs1)
	{
		info->err_msg = "First target drive too small (%dmb) require %dmb minimum";
		info->err_arg1 = (size_t)(secs1 / 2048);
		info->err_arg2 = (size_t)((min1 + 2047) / 2048);
		return -1;
	}

/* Allocate the new partition list.  If this is not the first call, it will */
/* already be allocated. */
	if (info->newparts == NULL)
	{
		info->nnewparts = 8 + info->nmfs;
		if (info->back_flags & BF_PREMIERE)
		{
		  info->nnewparts++;
		}
		info->newparts = calloc (32, sizeof (struct backup_partition));
		if (!info->newparts)
		{
			info->err_msg = "Memory exhausted";
			return -1;
		}
	}

/* Figure out the total size needed. */
	for (count = min1, loop = 2; loop < info->nmfs; loop++)
	{
		count += info->mfsparts[loop].sectors;
	}

#if DEBUG
	fprintf (stderr, "Size needed for single drive restore: %lld\n", count);
#endif

/* Initialize the initial new partitions.  The values are just defaults and */
/* could be overridden by the backup information. */
	bzero (info->newparts, info->nnewparts * sizeof (struct backup_partition));
	info->newparts[0].partno = 2;
	info->newparts[0].sectors = 4096;
	info->newparts[1].partno = 3;
	info->newparts[1].sectors = 4096;
	info->newparts[2].partno = 4;
	info->newparts[2].sectors = 128 * 1024 * 2;
	info->newparts[3].partno = 5;
	info->newparts[3].sectors = 4096;
	info->newparts[4].partno = 6;
	info->newparts[4].sectors = 4096;
	info->newparts[5].partno = 7;
	info->newparts[5].sectors = 128 * 1024 * 2;
	info->newparts[6].partno = 8;
	info->newparts[6].sectors = info->swapsize;
	info->newparts[7].partno = 9;
	info->newparts[7].sectors = info->varsize;
	if (info->back_flags & BF_PREMIERE) 
	{
		info->newparts[8].partno = 14;
	}

/* Override for the odd size partitions. */
	for (loop = 0; loop < info->nparts; loop++)
	{
		if (info->parts[loop].partno == 14)
		{
			info->newparts[8] = info->parts[loop];
		} 
		else
		{
		info->newparts[info->parts[loop].partno - 2] = info->parts[loop];
		}

/* If it's part of a partition set and only one was backed up, set the */
/* alternate set size. */
		if (info->nparts < 6 && info->parts[loop].partno < 8)
		{
			if (info->parts[loop].partno < 5)
				info->newparts[info->parts[loop].partno + 3 - 2].sectors = info->parts[loop].sectors;
			else
				info->newparts[info->parts[loop].partno - 3 - 2].sectors = info->parts[loop].sectors;
		}
	}
/* First MFS pair. */
	int firstmfs = 8;
	if (info->back_flags & BF_PREMIERE) 
	{
		firstmfs = 9;
	}

	info->newparts[firstmfs].partno = 10;
	info->newparts[firstmfs].sectors = info->mfsparts[0].sectors;
	info->newparts[firstmfs+1].partno = 11;
	info->newparts[firstmfs+1].sectors = info->mfsparts[1].sectors;

/* Quick and dirty hack for expanded V3 restores */
	if (info->format == bfV3)
	{
		uint64_t left = secs1 - min1 + info->mfsparts[1].sectors - info->mfsparts[2].sectors - 2048;
		info->newparts[firstmfs+1].sectors = left/2;
		info->newparts[firstmfs+2].partno = 12;
		info->newparts[firstmfs+2].sectors = info->mfsparts[2].sectors;
		info->newparts[firstmfs+3].partno = 13;
		info->newparts[firstmfs+3].sectors = left/2;
		info->nnewparts = firstmfs+4;

		if (info->devs)
			free (info->devs);

		info->ndevs = 1;
		if (secs2 > 0)
		  info->ndevs++;
		
		info->devs = calloc (1, sizeof (struct device_info));

		if (!info->devs)
		{
			info->err_msg = "Memory exhausted";
			return -1;
		}

/* Basic info for the device. */
		info->devs->nparts = info->nnewparts;
		info->devs->devname = dev1;
		info->devs->sectors = secs1;
		info->devs->swab = swab1;

/* Add the MFS partitions to the partition table. */
		info->nmfs = 4;
		for (loop = 0; loop < info->nmfs; loop++)
		{
			info->mfsparts[loop].devno = 0;
			info->mfsparts[loop].partno = 10 + loop;
		}

		if (info->ndevs > 1) 
		{
			info->devs[1].nparts = 2;
			info->devs[1].devname = dev2;
			info->devs[1].sectors = secs2;
			info->devs[1].swab = swab2;

			info->newparts[info->nnewparts].devno = 1;
			info->newparts[info->nnewparts].partno = 2;
			info->newparts[info->nnewparts].sectors = 2048;
			info->newparts[++info->nnewparts].devno = 1;
			info->newparts[info->nnewparts].partno = 3;
			info->newparts[info->nnewparts].sectors = secs2 - 2*2048;
			info->nnewparts++;

/* Add the MFS partitions to the partition table on second drive. */	
			info->mfsparts[info->nmfs].devno = 1;
			info->mfsparts[info->nmfs].partno = 2;
			info->mfsparts[++info->nmfs].devno = 1;
			info->mfsparts[info->nmfs].partno = 3;
			++info->nmfs;
		}

/* In business. */
		return 1;
	}

/* If it fits on the first drive, yay us. */
	if (count <= secs1 && info->nmfs <= 6 && (!(info->back_flags & RF_NOFILL) || info->nmfs <= 4))
	{
		if (info->devs)
			free (info->devs);

		info->ndevs = 1;
		info->devs = calloc ( info->ndevs, sizeof (struct device_info));

		if (!info->devs)
		{
			info->err_msg = "Memory exhausted";
			return -1;
		}

/* Basic info for the device. */
		info->devs[0].nparts = info->nnewparts;
		info->devs[0].devname = dev1;
		info->devs[0].sectors = secs1;
		info->devs[0].swab = swab1;
		

/* Add the MFS partitions to the partition table. */
		for (loop = 0; loop < info->nmfs; loop++)
		{
			info->mfsparts[loop].devno = 0;
			info->mfsparts[loop].partno = 10 + loop;
			info->newparts[loop + firstmfs].sectors = info->mfsparts[loop].sectors;
			info->newparts[loop + firstmfs].partno = loop + 10;
			if ( (info->back_flags & BF_PREMIERE)  &&
			     (info->newparts[loop + firstmfs].partno >= 14) )
			{
				info->newparts[loop + firstmfs].partno++;
				info->mfsparts[loop].partno++;
			}
		}

/* In business. */
		return 1;
	}

/* First drive not big enough.  If there is no second drive, bail. */
	if (secs2 < 1)
	{
		info->err_msg = "Backup target not large enough for entire backup by itself";
		return -1;
	}

	if (info->devs)
		free (info->devs);
	info->ndevs = 2;
	info->devs = calloc (2, sizeof (struct device_info));

	if (!info->devs)
	{
		info->err_msg = "Memory exhausted";
		return -1;
	}

/* Basic info.  Partition count not yet known per device. */
	info->devs[0].nparts = 11;
	info->devs[0].devname = dev1;
	info->devs[0].sectors = secs1;
	info->devs[0].swab = swab1;
	info->devs[1].nparts = 1;
	info->devs[1].devname = dev2;
	info->devs[1].sectors = secs2;
	info->devs[1].swab = swab2;

/* Do the grunt work. */
	if (find_optimal_partitions (info, min1, secs1, secs2) < 0)
	{
		free (info->devs);
		info->devs = 0;
		return -1;
	}

/* In business. */
	return 1;
}

static const char *partition_strings [2][16][2] =
{
	{
		{"", ""},
		{"Apple", "Apple_partition_map"},
		{"Bootstrap 1", "Image"},
		{"Kernel 1", "Image"},
		{"Root 1", "Ext2"},
		{"Bootstrap 2", "Image"},
		{"Kernel 2", "Image"},
		{"Root 2", "Ext2"},
		{"Linux swap", "Swap"},
		{"/var", "Ext2"},
		{"MFS application a10", "MFS"},
		{"MFS media a11", "MFS"},
		{"MFS application a12", "MFS"},
		{"MFS media a13", "MFS"},
		{"MFS application a14", "MFS"},
		{"MFS media a15", "MFS"},
	},
	{
		{"", ""},
		{"Apple", "Apple_partition_map"},
		{"MFS application b2", "MFS"},
		{"MFS media b3", "MFS"},
		{"MFS application b4", "MFS"},
		{"MFS media b5", "MFS"},
		{"MFS application b6", "MFS"},
		{"MFS media b7", "MFS"},
		{"MFS application b8", "MFS"},
		{"MFS media b9", "MFS"},
		{"MFS application b10", "MFS"},
		{"MFS media b11", "MFS"},
		{"MFS application b12", "MFS"},
		{"MFS media b13", "MFS"},
		{"MFS application b14", "MFS"},
		{"MFS media b15", "MFS"},
	}
};

static char *mfsnames[12] =
{
	"MFS application region",
	"MFS media region",
	"Second MFS application region",
	"Second MFS media region",
	"Third MFS application region",
	"Third MFS media region",
	"Fourth MFS application region",
	"Fourth MFS media region",
	"Fifth MFS application region",
	"Fifth MFS media region",
	"Sixth MFS application region",
	"Sixth MFS media region",
};

/* Build the actual partition tables on the drive.  Called once per device. */
int
build_partition_table (struct backup_info *info, int devno)
{
	int loop;
	unsigned int curstart;

	unsigned char partitions[16] = {0, 1, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

/* If this is the first device, do re-ordering of the partitions to */
/* "balance" the partitions for better performance. */
	if (info->back_flags & RF_BALANCE)
	{
/* Walk through each partition.  This looks wrong because it starts at the */
/* beginning (The partition table) but accounts for it in curstart.  However */
/* it is fine because it only looks at MFS partitions (> 10) and only media */
/* (odd partition number) - furthermore, the number should come from the */
/* partition size instead of being hard-coded.  But this is just for */
/* purposes of estimating the center of the drive for partition balancing. */
		for (loop = 0, curstart = 64; loop < info->nnewparts && curstart < info->devs[devno].sectors / 2; loop++)
		{
/* Check if it is MFS media on device 0. */
			if (info->newparts[loop].devno == devno && info->newparts[loop].partno > 10 && (info->newparts[loop].partno & 1) == 1)
			{
/* Check if the partition is primarily on the beginning of the drive.  This */
/* is done by seeing if the distance between the middle of the drive and the */
/* end of the partition is less than the distance between the middle of the */
/* drive and the beginning of the partition.  Of course, if it entirely */
/* falls into the beginning, no worries. */
				struct backup_partition tmp;
				curstart += info->newparts[loop].sectors;
				if (curstart > info->devs[devno].sectors / 2 && curstart - info->devs[devno].sectors / 2 > info->devs[devno].sectors / 2 - (curstart - info->newparts[loop].sectors))
					break;
#if DEBUG
				fprintf (stderr, "Moving partition %d\n", info->newparts[loop].partno);
#endif
/* Move the partition to the beginning of the drive. */
				tmp = info->newparts[loop];
				memmove (&info->newparts[1], &info->newparts[0], (loop) * sizeof (tmp));
				info->newparts[0] = tmp;
			}
		}
	}

/* That was kind of lame.  It does not attempt to move partitions to */
/* best optimize the balancing.  It is just a quick dumb pass. */
/* Notice no balancing for drive 2.  It's app partitions are only hit */
/* one every 10 seconds or so, in theory.  Well, if the second app of a */
/* factory married dual drive is in there, it could be worse.  Will have */
/* to consider that. XXX */

/* Create the partitions in the order they are in the list.  This allocates */
/* the space sequentially. */
	for (loop = 0; loop < info->nnewparts; loop++)
	{
/* If it is this device, make it. */
		if (info->newparts[loop].devno == devno)
		{
/* Damn byte sex problems.  Fortunately PPC uses network byte order, so */
/* I can cheat and use the network functions.  Even the new MIPS TiVo is */
/* big endian. */
			unsigned char partno = info->newparts[loop].partno;
			unsigned char tmppartno = partno;

			while (partitions[tmppartno - 1] > partno)
				tmppartno--;

			if (partitions[tmppartno] < 255)
				memmove (&partitions[tmppartno + 1], &partitions[tmppartno], 16 - tmppartno);
			partitions[tmppartno] = partno;
			const char *pname = partition_strings[devno][partno][0];
			const char *ptype = partition_strings[devno][partno][1];
			if (partno == 14 && (info->back_flags & BF_PREMIERE)) 
			{	 /* special case for Gen07 SQLite partition */
				pname = "SQLite";
				ptype = "Ext2";
			}

			tivo_partition_add (info->devs[devno].devname, info->newparts[loop].sectors, tmppartno, pname, ptype );
		}
	}

/* Apply better names to the MFS partitions. */
	for (loop = 0; loop < info->nmfs && loop < 12; loop++)
	{
		if (info->mfsparts[loop].devno == devno)
			tivo_partition_rename (info->devs[devno].devname, info->mfsparts[loop].partno, mfsnames[loop]);
	}

	tivo_partition_table_write (info->devs[devno].devname);

/* Also let Linux re-read the partition table. */
/* XXX why bother.  May add this in after restore is done. */
/*	ioctl (info->devs[devno].fd, BLKRRPART, 0); */

	return 0;
}

int
restore_start (struct backup_info *info)
{
	int loop;

	if (info->back_flags & RF_INITIALIZED || info->state <= bsInfoEnd || !info->devs)
	{
		info->err_msg = "Internal error 6 restore not initialized";
		return -1;
	}

	for (loop = 0; loop < info->ndevs; loop++) {
		if (build_partition_table (info, loop) < 0)
			return -1;

		if (!info->devs[loop].files)
		{
			/** This needs to be allocated large enough for the maximum partition # used.
			 *  16 would be enough for all current tivos.
			 */
			info->devs[loop].files = calloc (sizeof (struct tivo_partition_file *), 32);
			if (!info->devs[loop].files)
			{
				info->err_msg = "Memory exhausted";
				return -1;
			}
		}
	}

	info->back_flags |= RF_INITIALIZED;
	return 0;
}

static const char swapspace[] = "SWAP-SPACE";
static const char swapspacev1[] = "SWAPSPACE2";
#define SWAP_PAGESZ 0x1000

int
restore_make_swap (struct backup_info *info)
{
	tpFILE *file;
	int size;
	unsigned int swaphdr[SWAP_PAGESZ / 4];
	int loop = 0;
	unsigned int loop2 = 0;

	file = tivo_partition_open_direct (info->devs[0].devname, 8, O_RDWR);

	if (!file)
	{
		info->err_msg = "Error opening swap partition";
		return -1;
	}

	size = tivo_partition_size (file);

	bzero (swaphdr, sizeof (swaphdr));

	if (info->back_flags & RF_SWAPV1)
	{
// Version 1 swap header
		struct {
			char bootbits[1024];
			unsigned int version;
			unsigned int last_page;
			unsigned int nr_badpages;
			unsigned int padding[125];
			unsigned int badpages[1];
		} *hdr = (void *)swaphdr;

		memcpy ((char *)swaphdr + sizeof (swaphdr) - strlen (swapspacev1), swapspacev1, strlen (swapspacev1));
		hdr->version = intswap32 (1);
		hdr->last_page = intswap32 (size / (SWAP_PAGESZ / 512) - 1);
		hdr->nr_badpages = 0;
	}
	else
	{
// Version 0 swap header
		memcpy ((char *)swaphdr + sizeof (swaphdr) - strlen (swapspace), swapspace, strlen (swapspace));

		while (loop < 0xff0 / 4 && size >= 32 * SWAP_PAGESZ / 512)
		{
			swaphdr[loop++] = 0xffffffff;
			size -= 32 * SWAP_PAGESZ / 512;
		}

		for (loop2 = 0x1; loop2 > 0 && size >= SWAP_PAGESZ / 512; size -= SWAP_PAGESZ / 512, loop2 <<= 1)
		{
			swaphdr[loop] |= intswap32 (loop2);
		}

		swaphdr[0] &= ~intswap32 (1);
	}

	loop = tivo_partition_write (file, swaphdr, 0, sizeof (swaphdr) / 512);
	tivo_partition_close (file);

	if (loop < 0)
	{
		info->err_msg = "Error initializing swap partition";
		return -1;
	}

	return 0;
}

int
restore_cleanup_parts(struct backup_info *info)
{
	unsigned int loop, loop2;
	char buf[32 * 1024];

	bzero (buf, sizeof (buf));

	for (loop = 2; loop < 10; loop++)
	{
		for (loop2 = 0; loop2 < info->nparts; loop2++)
		{
			if (info->parts[loop2].devno == 0 && info->parts[loop2].partno == loop)
				break;
		}

		if (loop2 >= info->nparts)
		{
			tpFILE *file = info->devs[0].files[loop];
			int tot;

			if (!file)
				file = tivo_partition_open_direct (info->devs[0].devname, loop, O_RDWR);
			if (!file)
			{
				info->err_msg = "Error cleaning up partitions";
				return -1;
			}

				tot = tivo_partition_size (file);

			if  (! (info->back_flags & RF_ZEROPART) && tot > (2048/512))
				tot = 2048 / 512;

			for (loop2 = 0; loop2 < tot; loop2 += sizeof (buf) / 512)
			{
				int towrite;

				towrite = loop2 + sizeof (buf) / 512 > tot? tot - loop2: sizeof (buf) / 512;
				tivo_partition_write (file, buf, loop2, towrite);
			}
		}
	}

	return 0;
}

int
restore_finish(struct backup_info *info)
{
	if (info->cursector != info->nsectors)
	{
		info->err_msg = "Premature end of backup data";
		return -1;
	}

	if (info->state < bsComplete)
	{
		info->err_msg = "Internal error: State machine in state %d, expecting complete";
		info->err_arg1 = (size_t)info->state;
		return -1;
	}

	if (info->state < bsMax)
	{
		enum backup_state_ret ret;

		ret = ((*info->state_machine)[info->state]) (info, 0, 0, 0);

		switch (ret)
		{
		case bsError:
			return -1;
			break;

		case bsNextState:
			break;

		default:
			info->err_msg = "Bad return from state machine: %d";
			info->err_arg1 = (size_t)ret;
		}
	}

	return 0;
}

/*****************************/
/* Display the restore error */
void
restore_perror (struct backup_info *info, char *str)
{
	int err = 0;

	if (info->err_msg)
	{
		fprintf (stderr, "%s: ", str);
		fprintf (stderr, info->err_msg, info->err_arg1, info->err_arg2, info->err_arg3);
		fprintf (stderr, ".\n");
		err = 1;
	}

	if (info->mfs && mfs_has_error (info->mfs))
	{
		mfs_perror (info->mfs, str);
		err = 2;
	}

	if (info->vols && mfsvol_has_error (info->vols))
	{
		mfsvol_perror (info->vols, str);
		err = 3;
	}

	if (err == 0)
	{
		fprintf (stderr, "%s: No error.\n", str);
	}
}

/*****************************************/
/* Return the restore error in a string. */
int
restore_strerror (struct backup_info *info, char *str)
{
	if (info->err_msg)
		sprintf (str, info->err_msg, info->err_arg1, info->err_arg2, info->err_arg3);
	else if (info->mfs && mfs_has_error (info->mfs))
		return (mfs_strerror (info->mfs, str));
	else if (info->vols && mfsvol_has_error (info->vols))
		return (mfsvol_strerror (info->vols, str));
	else
	{
		strcpy (str, "No error");
		return 0;
	}

	return 1;
}

/************************/
/* Restore has an error */
int
restore_has_error (struct backup_info *info)
{
	if (info->err_msg)
		return 1;

	if (info->mfs && mfs_has_error (info->mfs))
		return 1;

	if (info->vols && mfsvol_has_error (info->vols))
		return 1;

	return 0;
}

/********************/
/* Clear any errors */
void
restore_clearerror (struct backup_info *info)
{
	info->err_msg = 0;
	info->err_arg1 = 0;
	info->err_arg2 = 0;
	info->err_arg3 = 0;

	if (info->mfs)
		mfs_clearerror (info->mfs);

	if (info->vols)
		mfsvol_clearerror (info->vols);
}
