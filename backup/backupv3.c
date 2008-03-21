#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#if HAVE_MALLOC_H
#include <malloc.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_SYS_MALLOC_H
#include <sys/malloc.h>
#endif
#include <sys/types.h>
#ifdef HAVE_ASM_TYPES_H
#include <asm/types.h>
#endif
#include <fcntl.h>
#include <zlib.h>
#include <string.h>
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#include <ctype.h>

#include "mfs.h"
#include "macpart.h"
#include "backup.h"

/**************************************************************/
/* Add an inode to the list, allocating more space if needed. */
static int
backup_inode_list_add (unsigned **list, unsigned *allocated, unsigned *total, unsigned val)
{
/* No space, (re)allocate space. */
	if (*allocated <= *total)
	{
		*allocated += 32;
		*list = realloc (*list, *allocated * sizeof (val));
	}

/* Allocation error. */
	if (!*list)
		return -1;

/* Append the value to the end of the list. */
	(*list)[(*total)++] = val;

	return 0;
}

/*****************************************************************/
/* Scan the inode table and generate a list of inodes to backup. */
unsigned
backup_scan_inodes (struct backup_info *info)
{
	unsigned int loop, loop2, loop3;
	int ninodes = mfs_inode_count (info->mfs);
	uint64_t highest = 0;

	uint64_t appsectors = 0, mediasectors = 0;
#if DEBUG
	unsigned int mediainodes = 0, appinodes = 0;
#endif

	unsigned allocated = 0;

/* Add inodes. */
	for (loop = 0; loop < ninodes; loop++)
	{
		mfs_inode *inode = mfs_read_inode (info->mfs, loop);

		if (mfs_has_error (info->mfs))
		{
			if (info->inodes)
				free (info->inodes);
			info->inodes = 0;
			return ~0;
		}

/* Don't think this should ever happen. */
		if (!inode)
			continue;

/* If it a stream, treat it specially. */
		if (inode->type == tyStream && inode->refcount > 0)
		{
			unsigned int streamsize;

			if (info->back_flags & (BF_THRESHTOT | BF_STREAMTOT))
				streamsize = intswap32 (inode->blocksize) / 512 * intswap32 (inode->size);
			else
				streamsize = intswap32 (inode->blocksize) / 512 * intswap32 (inode->blockused);

/* Ignore streams with no allocated data. */
			if (streamsize == 0)
			{
				free (inode);
				continue;
			}
/* Ignore streams bigger than the threshhold. (Size) */
			if ((info->back_flags & BF_THRESHSIZE) && streamsize > info->thresh)
			{
				free (inode);
				continue;
			}
/* Ignore streams bigger than the threshhold. (fsID) */
			if (!(info->back_flags & BF_THRESHSIZE) && intswap32 (inode->fsid) > info->thresh)
			{
				free (inode);
				continue;
			}

/* If the total size is only for comparison, get the used size now. */
			if ((info->back_flags & (BF_THRESHTOT | BF_STREAMTOT)) == BF_THRESHTOT)
				streamsize = intswap32 (inode->blocksize) / 512 * intswap32 (inode->blockused);

/* Count the inode's sectors in the total. */
			mediasectors += streamsize;
#if DEBUG
			mediainodes++;
#endif

/* Add the inode to the list. */
			if (backup_inode_list_add (&info->inodes, &allocated, &info->ninodes, loop) < 0)
			{
				info->err_msg = "Memory exhausted (Inode scan %d)";
				info->err_arg1 = (void *)loop;
				free (inode);
				return ~0;
			}

#if DEBUG
			fprintf (stderr, "Inode %d (%d) added\n", intswap32 (inode->inode), intswap32 (inode->fsid));
#endif
		}
		else if (inode->refcount != 0 && inode->type != tyStream && intswap32 (inode->size) > 512 - offsetof (mfs_inode, datablocks) && inode->numblocks > 0)
		{
/* Count the space used by non-stream inodes */
			appsectors += ((intswap32 (inode->size) + 511) & ~511) >> 9;
#if DEBUG
			appinodes++;
#endif

		}

/* Either an application data inode or a stream inode being backed up. */
		for (loop2 = 0; loop2 < intswap32 (inode->numblocks); loop2++)
		{
			uint64_t thiscount;
			uint64_t thissector;

			if (mfs_is_64bit (info->mfs))
			{
				thiscount = intswap32 (inode->datablocks.d64[loop2].count);
				thissector = intswap64 (inode->datablocks.d64[loop2].sector);
			}
			else
			{
				thiscount = intswap32 (inode->datablocks.d32[loop2].count);
				thissector = intswap32 (inode->datablocks.d32[loop2].sector);
			}

			if (highest < thiscount + thissector)
			{
				highest = thiscount + thissector;
			}
		}

		free (inode);
	}

// Make sure all needed data is present.
	if (info->back_flags & BF_TRUNCATED)
	{
		uint64_t set_size = mfs_volume_set_size (info->mfs);
		if (highest > set_size)
		{
			info->err_msg = "Required data at %ld beyond end of the device (%ld)";
			info->err_arg1 = (void *)highest;
			info->err_arg2 = (void *)set_size;

			return ~0;
		}
	}

	// Record highest block to backup.
	if ((info->back_flags & BF_SHRINK) && highest > info->shrink_to)
		info->shrink_to = highest;

#if DEBUG
	fprintf (stderr, "Backing up %d media sectors (%d inodes), %d app sectors (%d inodes) and %d inode sectors\n", mediasectors, mediainodes, appsectors, appinodes, ninodes);
#endif

/* Count the space for the inodes themselves */
	info->nsectors += ninodes + appsectors + mediasectors;

	return info->ninodes;
}

/**************************************************************/
/* Count the sectors of various items not scanned during init */
int
backup_info_count_misc (struct backup_info *info)
{
	zone_header *zone = NULL;

// Boot sector
	info->nsectors++;
// Volume header
	info->nsectors++;
// Checksum
	info->nsectors++;
	if (mfs_is_64bit (info->mfs))
	{
// Transaction log
		info->nsectors += intswap32 (info->mfs->vol_hdr.v64.lognsectors);
// ??? region
		info->nsectors += intswap32 (info->mfs->vol_hdr.v64.unknsectors);
	}
	else
	{
// Transaction log
		info->nsectors += intswap32 (info->mfs->vol_hdr.v32.lognsectors);
// ??? region
		info->nsectors += intswap32 (info->mfs->vol_hdr.v32.unksectors);
	}

// Zone maps
	while ((zone = mfs_next_zone (info->mfs, zone)) != NULL)
	{
		uint64_t zonesector;
		unsigned int zonelength;

		if (mfs_is_64bit (info->mfs))
		{
			zonesector = intswap64 (zone->z64.sector);
			zonelength = intswap32 (zone->z64.length);
		}
		else
		{
			zonesector = intswap32 (zone->z32.sector);
			zonelength = intswap32 (zone->z32.length);
		}

		if (info->shrink_to && zonesector > info->shrink_to)
			break;

		info->nsectors += zonelength;
	}
}

/*************************************/
/* Initializes the backup structure. */
struct backup_info *
init_backup_v3 (char *device, char *device2, int flags)
{
 	struct backup_info *info;

	flags &= BF_FLAGS;

 	if (!device)
 		return 0;
 
 	info = calloc (sizeof (*info), 1);
 
 	if (!info)
 	{
 		return 0;
 	}

	info->crc = ~0;
	info->state_machine = &backup_v3;

	info->mfs = mfs_init (device, device2, O_RDONLY);
 
 	info->back_flags = flags;

	if (info->mfs && mfs_is_64bit (info->mfs))
	{
		info->back_flags |= BF_64;
	}

	info->thresh = 2000;

	if (!tivo_partition_swabbed (device))
		info->back_flags |= BF_NOBSWAP;

	info->hda = strdup (device);
	if (!info->hda)
	{
		info->err_msg = "Memory exhausted";
	}

	if (!mfs_has_error (info->mfs))
	{
		info->back_flags &= ~BF_TRUNCATED;
	}
 
	return info;
}

/***************************************************************************/
/* State handlers - return val -1 = error, 0 = more data needed, 1 = go to */
/* next state. */

/**************************/
/* Scan MFS for v3 backup */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_scan_mfs_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	if ((add_partitions_to_backup_info (info, info->hda)) != 0) {
		return bsError;
	}

	if (backup_scan_inodes (info) == ~0)
	{
		free (info->parts);
		return bsError;
	}

	if (add_mfs_partitions_to_backup_info (info) != 0) {
		free (info->parts);
		free (info->inodes);
		return bsError;
	}

	if (backup_info_count_misc (info) != 0)
	{
		free (info->parts);
		free (info->inodes);
		free (info->mfs);
		return bsError;
	}

	info->nsectors += (info->ninodes * sizeof (unsigned long) + info->nparts * sizeof (struct backup_partition) + info->nmfs * sizeof (struct backup_partition) + sizeof (struct backup_head_v3) + 511) / 512;

	return bsNextState;
}

/*******************/
/* Begin v3 backup */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
backup_state_begin_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	struct backup_head_v3 *head = data;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	head->magic = TB3_MAGIC;
	head->flags = info->back_flags;
	head->nsectors = info->nsectors;
	head->nparts = info->nparts;
	head->ninodes = info->ninodes;
	head->mfspairs = info->nmfs;

	info->shared_val1 = (sizeof (*head) + 7) & (512 - 8);
	*consumed = 0;

	return bsNextState;
}

/*********************************/
/* Add partition info to backup. */
/* state_val1 = index of last copied partition */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
backup_state_info_partitions (struct backup_info *info, void *data, unsigned size, unsigned *consumed);
/* Defined in backup.c */

/*****************************/
/* Add inode list to backup. */
/* state_val1 = index of last copied inode */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
backup_state_info_inodes_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	unsigned count = info->ninodes * sizeof (unsigned) - info->state_val1;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

/* Copy as much as possible */
	if (count + info->shared_val1 > size * 512)
	{
		count = size * 512 - info->shared_val1;
	}

	memcpy ((char *)data + info->shared_val1, (char *)info->inodes + info->state_val1, count);

	info->state_val1 += count;
	info->shared_val1 += count;
	*consumed = info->shared_val1 / 512;
	info->shared_val1 &= 511;

	if (info->state_val1 < info->ninodes * sizeof (unsigned))
		return bsMoreData;

	return bsNextState;
}

/*************************************/
/* Add MFS partition info to backup. */
/* state_val1 = index of last copied MFS partition */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
backup_state_info_mfs_partitions (struct backup_info *info, void *data, unsigned size, unsigned *consumed);
/* Defined in backup.c */

/********************************/
/* Finish off the backup header */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
backup_state_info_end (struct backup_info *info, void *data, unsigned size, unsigned *consumed);
/* Defined in backup.c */

/*************************/
/* Backup the boot block */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_boot_block (struct backup_info *info, void *data, unsigned size, unsigned *consumed);
/* Defined in backup.c */

/***************************************/
/* Backup the raw (non-MFS) partitions */
/* state_val1 = current partition index */
/* state_val2 = offset within current partition */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_partitions (struct backup_info *info, void *data, unsigned size, unsigned *consumed);
/* Defined in backup.c */

/****************************/
/* Backup the volume header */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_volume_header_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	memcpy (data, &info->mfs->vol_hdr, sizeof (info->mfs->vol_hdr));
	memset ((char *)data + sizeof (info->mfs->vol_hdr), 0, 512 - sizeof (info->mfs->vol_hdr));

	*consumed = 1;

	return bsNextState;
}

/******************************/
/* Backup the transaction log */
/* state_val1 = offset within transaction log */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_transaction_log_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	unsigned tocopy;
	uint64_t logstart;
	unsigned lognsectors;

	if (mfs_is_64bit (info->mfs))
	{
		logstart = intswap64 (info->mfs->vol_hdr.v64.logstart);
		lognsectors = intswap32 (info->mfs->vol_hdr.v64.lognsectors);
	}
	else
	{
		logstart = intswap32 (info->mfs->vol_hdr.v32.logstart);
		lognsectors = intswap32 (info->mfs->vol_hdr.v32.lognsectors);
	}

	tocopy = lognsectors - info->state_val1;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	if (tocopy > size)
	{
		tocopy = size;
	}

	if (mfs_read_data (info->mfs, data, logstart + info->state_val1, tocopy) < 0)
	{
		info->err_msg = "Error reading MFS transaction log";
		return bsError;
	}

	*consumed = tocopy;
	info->state_val1 += tocopy;

	if (info->state_val1 < lognsectors)
		return bsMoreData;

	return bsNextState;
}

/***********************************************************************/
/* Backup the unknown region referenced in the volume header after the */
/* transaction log */
/* state_val1 = offset within region */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_unk_region_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	unsigned tocopy;
	uint64_t unkstart;
	uint32_t unksectors;

	if (mfs_is_64bit (info->mfs))
	{
		unkstart = intswap64 (info->mfs->vol_hdr.v64.unkstart);
		unksectors = intswap32 (info->mfs->vol_hdr.v64.unknsectors);
	}
	else
	{
		unkstart = intswap32 (info->mfs->vol_hdr.v32.unkstart);
		unksectors = intswap32 (info->mfs->vol_hdr.v32.unksectors);
	}

	tocopy = unksectors - info->state_val1;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	if (tocopy > size)
	{
		tocopy = size;
	}

	if (mfs_read_data (info->mfs, data, unkstart + info->state_val1, tocopy) < 0)
	{
		info->err_msg = "Error reading MFS data";
		return bsError;
	}

	*consumed = tocopy;
	info->state_val1 += tocopy;

	if (info->state_val1 < unksectors)
		return bsMoreData;

	return bsNextState;
}

/********************/
/* Backup zone maps */
/* state_val1 = offset within zone map */
/* state_val2 = --unused-- */
/* state_ptr1 = current zone map */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_zone_maps_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	unsigned tocopy;
	zone_header *cur_zone = info->state_ptr1;
	unsigned int zonelength;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	if (info->state_val1 == 0)
	{
		uint64_t zonesector;
		cur_zone = mfs_next_zone (info->mfs, cur_zone);

		if (!cur_zone)
		{
			return bsNextState;
		}

		if (mfs_is_64bit (info->mfs))
		{
			zonesector = intswap64 (cur_zone->z64.sector);
		}
		else
		{
			zonesector = intswap32 (cur_zone->z32.sector);
		}

		if (info->shrink_to > 0 && info->shrink_to < zonesector)
		{
/* The restore will be able to figure out the next zone is beyond the end of */
/* the shrunken volume. */
			return bsNextState;
		}

		info->state_ptr1 = cur_zone;
	}

	if (mfs_is_64bit (info->mfs))
	{
		zonelength = intswap32 (cur_zone->z64.length);
	}
	else
	{
		zonelength = intswap32 (cur_zone->z32.length);
	}

	tocopy = zonelength - info->state_val1;

	if (tocopy > size)
	{
		tocopy = size;
	}

	memcpy (data, (char *)cur_zone + info->state_val1 * 512, tocopy * 512);

	*consumed = tocopy;
	info->state_val1 += tocopy;

	if (info->state_val1 >= zonelength)
	{
		info->state_val1 = 0;
	}

	return bsMoreData;
}

/*****************************/
/* Backup application inodes */
/* Write inode sector, followed by date for non tyStream inodes. */
/* state_val1 = current inode number */
/* state_val2 = offset within zone map */
/* state_ptr1 = current inode structure */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_app_inodes_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	unsigned tocopy = 0, copied;
	mfs_inode *inode;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	if (info->state_val2 == 0)
	{
		unsigned inode_size;

/* Fetch the next inode */
		inode = mfs_read_inode (info->mfs, info->state_val1);

		if (!inode)
		{
			return bsError;
		}

/* Hopefully this will never happen because I'm not 100% confident in the */
/* initialization values being "good" */
/* Trying to recover instead of aborting for the sake of drive failure */
/* recovery. */
		if (info->state_val1 != intswap32 (inode->inode))
		{
			fprintf (stderr, "Inode %d uninitialized\n", info->state_val1);
			inode->inode = intswap32 (info->state_val1);
			inode->refcount = 0;
			inode->numblocks = 0;
			inode->fsid = 0;
			inode->size = 0;
/* Don't bother updating the CRC, restore will do that */
		}

		inode_size = offsetof (mfs_inode, datablocks);
		if (mfs_is_64bit (info->mfs))
		{
			inode_size += intswap32 (inode->numblocks) * sizeof (inode->datablocks.d64[0]);
		}
		else
		{
			inode_size += intswap32 (inode->numblocks) * sizeof (inode->datablocks.d32[0]);
		}

/* Data in inode. */
		if (inode->type != tyStream && (inode->inode_flags & intswap32 (INODE_DATA)))
		{
			inode_size += intswap32 (inode->size);
			if (inode_size > 512)
				inode_size = 512;
		}

/* Zeros compress easier, so might as well eliminate any unneeded data. */
		memcpy (data, inode, inode_size);
		if (inode_size < 512)
			memset ((char *)data + inode_size, 0, 512 - inode_size);

		data = (char *)data + 512;
		--size;
		++*consumed;

/* Start at offset 1 */
		info->state_val2++;
		info->state_ptr1 = inode;
	}
	else
	{
		inode = info->state_ptr1;
	}

	if (inode->type != tyStream && inode->refcount > 0 && inode->numblocks > 0 && !(inode->inode_flags & intswap32 (INODE_DATA)))
		tocopy = intswap32 (inode->size) - (info->state_val2 - 1) * 512;

	copied = tocopy;

	if (copied > size * 512)
	{
		copied = size * 512;
	}

/* If there was no data to copy, or only enough room for the inode itself, */
/* tocopy could be 0. */
	if (copied > 0)
	{
		if (mfs_read_inode_data_part (info->mfs, inode, data, info->state_val2 - 1, (copied + 511) / 512) < 0)
		{
			info->err_msg = "Error reading inode %d";
			info->err_arg1 = (void *)info->state_val1;
			free (inode);
			return bsError;
		}

/* Once again, zeros compress well, so zero out any garbage data. */
		if ((copied & 511) > 0)
		{
			memset ((char *)data + copied, 0, 512 - (copied & 511));
		}
	}

	*consumed += (copied + 511) / 512;
	info->state_val2 += (copied + 511) / 512;

/* If the entire data was copied, go to the next inode */
	if (copied == tocopy)
	{
		info->state_val1++;
		info->state_val2 = 0;
		free (inode);
	}

	if (info->state_val1 < mfs_inode_count (info->mfs))
		return bsMoreData;

	return bsNextState;
}

/***************************/
/* Backup the media inodes */
/* state_val1 = currend inode (index) */
/* state_val2 = offset within current inode */
/* state_ptr1 = pointer to current inode */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_media_inodes_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	mfs_inode *inode;
	unsigned tocopy, copied;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	if (info->state_val2 == 0)
	{
/* Fetch the next inode */
		inode = mfs_read_inode (info->mfs, info->inodes [info->state_val1]);

		if (!inode)
		{
			return bsError;
		}

		info->state_ptr1 = inode;
	}
	else
	{
		inode = info->state_ptr1;
	}

/* Check if total size or used size is requested for backup */
	if (info->back_flags & BF_STREAMTOT)
		tocopy = intswap32 (inode->size);
	else
		tocopy = intswap32 (inode->blockused);

/* tocopy is currently in blocksize chunks, convert it to disk sectors */
	tocopy *= intswap32 (inode->blocksize) / 512;

	tocopy -= info->state_val2;

	copied = tocopy;

	if (copied > size)
	{
		copied = size;
	}

	if (mfs_read_inode_data_part (info->mfs, inode, data, info->state_val2, copied) < 0)
	{
		info->err_msg = "Error reading from tyStream id %d";
		info->err_arg1 = (void *)intswap32 (inode->fsid);
		free (inode);
		return bsError;
	}

	*consumed = copied;
	info->state_val2 += copied;

	if (copied == tocopy)
	{
		free (inode);
		info->state_val1++;
		info->state_val2 = 0;
	}

	if (info->state_val1 < info->ninodes)
		return bsMoreData;

	return bsNextState;
}

/*****************/
/* Finish backup */
/* For v3 backup, store CRC32 at end of the backup. */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_complete_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	memset (data, 0, 512);
	info->crc = compute_crc (data, 512 - sizeof (unsigned int), info->crc);
	*(unsigned int *)((char *)data + 512 - sizeof (unsigned int)) = ~info->crc;

	*consumed = 1;

#ifdef DEBUG
	if (info->nsectors != info->cursector + 1)
	{
		fprintf (stderr, "nsectors %d != cursector + 1 %d\n", info->nsectors, info->cursector);
	}
#endif

#if HAVE_SYNC
/* Make sure changes are committed to disk */
	sync ();
#endif

	return bsNextState;
}

backup_state_handler backup_v3 = {
	&backup_state_scan_mfs_v3,				// bsScanMFS
	&backup_state_begin_v3,					// bsBegin
	&backup_state_info_partitions,			// bsInfoPartition
	NULL,									// bsInfoBlocks
	&backup_state_info_inodes_v3,			// bsInfoInodes
	&backup_state_info_mfs_partitions,		// bsInfoMFSPartitions
	&backup_state_info_end,					// bsInfoEnd
	&backup_state_boot_block,				// bsBootBlock
	&backup_state_partitions,				// bsPartitions
	NULL,									// bsMFSInit
	NULL,									// bsBlocks
	&backup_state_volume_header_v3,			// bsVolumeHeader
	&backup_state_transaction_log_v3,		// bsTransactionLog
	&backup_state_unk_region_v3,			// bsUnkRegion
	&backup_state_zone_maps_v3,				// bsZoneMaps
	&backup_state_app_inodes_v3,			// bsAppInodes
	&backup_state_media_inodes_v3,			// bsMediaInodes
	&backup_state_complete_v3				// bsComplete
};
