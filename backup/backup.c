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
/* For htonl() */
#include <netinet/in.h>

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
	unsigned int highest = 0;

	unsigned int appsectors = 0, mediasectors = 0;
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
				streamsize = htonl (inode->blocksize) / 512 * htonl (inode->size);
			else
				streamsize = htonl (inode->blocksize) / 512 * htonl (inode->blockused);

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
			if (!(info->back_flags & BF_THRESHSIZE) && htonl (inode->fsid) > info->thresh)
			{
				free (inode);
				continue;
			}

/* If the total size is only for comparison, get the used size now. */
			if ((info->back_flags & (BF_THRESHTOT | BF_STREAMTOT)) == BF_THRESHTOT)
				streamsize = htonl (inode->blocksize) / 512 * htonl (inode->blockused);

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
			fprintf (stderr, "Inode %d (%d) added\n", htonl (inode->inode), htonl (inode->fsid));
#endif
		}
		else if (inode->refcount != 0 && inode->type != tyStream && htonl (inode->size) > 512 - offsetof (mfs_inode, datablocks) && inode->numblocks > 0)
		{
/* Count the space used by non-stream inodes */
			appsectors += ((htonl (inode->size) + 511) & ~511) >> 9;
#if DEBUG
			appinodes++;
#endif

		}

/* Either an application data inode or a stream inode being backed up. */
		for (loop2 = 0; loop2 < htonl (inode->numblocks); loop2++)
		{
			unsigned int thiscount = htonl (inode->datablocks[loop2].count);
			unsigned int thissector = htonl (inode->datablocks[loop2].sector);

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
		unsigned set_size = mfs_volume_set_size (info->mfs);
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

/***********************************************************/
/* Queries the mfs code for the list of partitions in use. */
static int
add_mfs_partitions_to_backup_info (struct backup_info *info)
{
	char *mfs_partitions;
	int loop;
	unsigned int cursector = 0;

	mfs_partitions = mfs_partition_list (info->mfs);

	if (info->nmfs == 0)
	{
/* First count the number of partitions. */
		loop = 0;
		while (mfs_partitions[loop])
		{
			info->nmfs++;
			while (mfs_partitions[loop] && !isspace (mfs_partitions[loop]))
			{
				loop++;
			}
			while (mfs_partitions[loop] && isspace (mfs_partitions[loop]))
			{
				loop++;
			}
		}
	}

	info->mfsparts = calloc (sizeof (struct backup_partition), info->nmfs);

	if (!info->mfsparts)
	{
		info->nmfs = 0;
		info->err_msg = "Memory exhausted";
		return -1;
	}

/* This loop looks almost the same as the last one...  Except this time, */
/* it actually fills out the structures. */
	for (loop = 0; loop < info->nmfs; loop++)
	{
		if (strncmp (mfs_partitions, "/dev/hd", 7))
		{
			free (info->mfsparts);
			info->mfsparts = 0;
			info->nmfs = 0;
			info->err_msg = "Bad partition name (%.*s) in partition list";
			info->err_arg1 = (void *)strcspn (mfs_partitions, " ");
			info->err_arg2 = mfs_partitions;
			return -1;
		}

/* Since the TiVo only supports 2 IDE devices, assume hda=dev 0, hdb=dev 1. */
		switch (mfs_partitions[7])
		{
		case 'a':
			info->mfsparts[loop].devno = 0;
			break;
		case 'b':
			info->mfsparts[loop].devno = 1;
			break;
		default:
			info->err_msg = "Bad partition name (%.*s) in partition list";
			info->err_arg1 = (void *)strcspn (mfs_partitions, " ");
			info->err_arg2 = mfs_partitions;
			free (info->mfsparts);
			info->mfsparts = 0;
			info->nmfs = 0;
			return -1;
		}

/* Find the partition number from the device name. */
		info->mfsparts[loop].partno = strtoul (mfs_partitions + 8, &mfs_partitions, 10);

/* If there are other non-space characters after the number, thats a problem. */
		if (*mfs_partitions && !isspace (*mfs_partitions))
		{
			info->err_msg = "Bad partition name (%.*s) in partition list";
			info->err_arg1 = (void *)strcspn (mfs_partitions, " ");
			info->err_arg2 = mfs_partitions;
			free (info->mfsparts);
			info->mfsparts = 0;
			info->nmfs = 0;
			return -1;
		}

/* Get the size of this partition from MFS.  This may vary slightly from the */
/* real partition size.  But thats okay, this means a little space can be */
/* saved during the restore, if the partition table is re-created from */
/* scratch. */
		info->mfsparts[loop].sectors = mfs_volume_size (info->mfs, cursector);
		if (info->mfsparts[loop].sectors == 0)
		{
			info->err_msg = "Empty MFS partition %.*s";
			info->err_arg1 = (void *)strcspn (mfs_partitions, " ");
			info->err_arg2 = mfs_partitions;
			free (info->mfsparts);
			info->mfsparts = 0;
			info->nmfs = 0;
			return -1;
		}

/* Add this partition into the current running total. */
		cursector += info->mfsparts[loop].sectors;

/* Find the beginning of the next partition. */
		while (*mfs_partitions && isspace (*mfs_partitions))
		{
			mfs_partitions++;
		}
	}

	return 0;
}

/**********************************************************************/
/* Add the regular partitions to the backup info.  This only backs up */
/* partitions 1 (Partition table) and one of 2/3/4 or 5/6/7 (One of the */
/* bootstrap/kernel/root sets) and 9 (/var) - 8 is skipped because it can */
/* easily be re-created.  It is, after all, just swap space.  Nothing else */
/* is supported. */
static int
add_partitions_to_backup_info (struct backup_info *info, char *device)
{
	int loop;
	char bootsector[512];
	int rootdev;
	char *tmpc;

/* Four.  Always Four.  Or three. */
	if (info->back_flags & BF_BACKUPVAR)
	{
		info->nparts = 4;
	}
	else
	{
		info->nparts = 3;
	}
/* One.  No more, no less. */
	info->ndevs = 1;

	info->devs = calloc (sizeof (struct device_info), info->ndevs);
	if (!info->devs)
	{
		info->ndevs = 0;
		info->nparts = 0;
		info->err_msg = "Memory exhausted";
		return -1;
	}

	info->parts = calloc (sizeof (struct backup_partition), info->nparts);
	if (!info->parts)
	{
		info->nparts = 0;
		info->ndevs = 0;
		free (info->devs);
		info->err_msg = "Memory exhausted";
		return -1;
	}

	info->devs[0].devname = device;
	info->devs[0].nparts = tivo_partition_count (device);

/* 9 is the minimum number of devices needed. */
	if (info->devs[0].nparts < 9)
	{
		free (info->devs);
		free (info->parts);
		info->ndevs = 0;
		info->nparts = 0;
		info->err_msg = "Not enough partitions on source drive";
		return -1;
	}

	info->devs[0].files = calloc (sizeof (tpFILE *), info->devs[0].nparts);

	if (info->devs[0].files == NULL)
	{
		free (info->devs);
		free (info->parts);
		info->ndevs = 0;
		info->nparts = 0;
		info->err_msg = "Memory exhausted";
		return -1;
	}

	if (tivo_partition_read_bootsector (device, &bootsector) <= 0)
	{
		free (info->devs[0].files);
		free (info->devs);
		free (info->parts);
		info->ndevs = 0;
		info->nparts = 0;
		info->err_msg = "Error reading boot sector of source drive";
		return -1;
	}

	if (bootsector[2] != 3 && bootsector[2] != 6)
	{
		free (info->devs[0].files);
		free (info->devs);
		free (info->parts);
		info->ndevs = 0;
		info->nparts = 0;
		info->err_msg = "Can not determine primary boot partition from boot sector";
		return -1;
	}

	rootdev = bootsector[2] + 1;

/* Scan boot sector for root device.  2.5 seems to need this. */
	tmpc = &bootsector[4];
	while (tmpc && *tmpc && strncmp (tmpc, "root=/dev/hda", 13))
	{
		tmpc = strchr (tmpc, ' ');
		if (tmpc)
			tmpc++;
	}

	if (*tmpc)
	{
		if (((tmpc[13] == '4' || tmpc[13] == '7') && tmpc[14] == 0) || isspace (tmpc[14]))
		{
			rootdev = tmpc[13] - '0';
#if DEBUG
			fprintf (stderr, "Using root partition %d from boot sector.\n", rootdev);
#endif
		}
	}

	info->parts[0].partno = bootsector[2] - 1;
	info->parts[0].devno = 0;
	info->parts[1].partno = bootsector[2];
	info->parts[1].devno = 0;
	info->parts[2].partno = rootdev;
	info->parts[2].devno = 0;
	if (info->nparts > 3)
	{
		info->parts[3].partno = 9;
		info->parts[3].devno = 0;
	}

	for (loop = 0; loop < info->nparts; loop++)
	{
		tpFILE *file;

		file = tivo_partition_open_direct (device, info->parts[loop].partno, O_RDONLY);

		if (!file) {
			while (loop-- > 0)
			{
				tivo_partition_close (info->devs[0].files[(int)info->parts[loop].partno]);
			}
			free (info->devs[0].files);
			free (info->devs);
			free (info->parts);
			info->ndevs = 0;
			info->nparts = 0;
			info->err_msg = "Error opening partition %s%d";
			info->err_arg1 = device;
			info->err_arg2 = (void *)(unsigned)info->parts[loop].partno;
			return -1;
		}

		info->devs[0].files[(int)info->parts[loop].partno] = file;
		info->parts[loop].sectors = tivo_partition_size (file);
		info->nsectors += info->parts[loop].sectors;
	}

	return 0;
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
// Transaction log
	info->nsectors += htonl (info->mfs->vol_hdr.lognsectors);
// ??? region
	info->nsectors += htonl (info->mfs->vol_hdr.unksectors);
// Zone maps
	while ((zone = mfs_next_zone (info->mfs, zone)) != NULL)
	{
		if (info->shrink_to && htonl (zone->sector) > info->shrink_to)
			break;

		info->nsectors += htonl (zone->length);
	}
}

/*************************************/
/* Initializes the backup structure. */
struct backup_info *
init_backup (char *device, char *device2, int flags)
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

void
backup_set_thresh (struct backup_info *info, unsigned int thresh)
{
	info->thresh = thresh;
}

/*************************************************************/
/* Check that the non stream zone maps are within the volume */
int
backup_verify_zone_maps (struct backup_info *info)
{
	unsigned volume_size = mfs_volume_set_size (info->mfs);
	zone_header *zone;

#if DEBUG
	fprintf (stderr, "Volume set size %ld\n", volume_size);
#endif

	for (zone = mfs_next_zone (info->mfs, NULL); zone; zone = mfs_next_zone (info->mfs, zone))
	{
		// Media zones will be accounted for later.
		if (zone->type == ztMedia)
			continue;

#if DEBUG
		fprintf (stderr, "Zone type %d at %ld\n", zone->type, zone->first);
#endif

		if (htonl (zone->first) >= volume_size)
		{
			info->err_msg = "%s zone outside available volume";
			switch (zone->type)
			{
			case ztInode:
				info->err_arg1 = "Inode";
				break;
			case ztApplication:
				info->err_arg1 = "Application";
				break;
			default:
				info->err_arg1 = "Unknown";
				break;
			}

			return -1;
		}
	}

// All loaded non-media zones within volume.
	return 0;
}

/*********************************************/
/* Attempt to recover from a failed mfs_init */
void
backup_check_truncated_volume (struct backup_info *info)
{
	if (!(info->back_flags & BF_TRUNCATED))
	{
		info->err_msg = "Backup cannot proceed on failed init";
		return;
	}

	// Shrinking a truncated volume implied.
	info->back_flags |= BF_SHRINK;;

	// Clear any errors.
	backup_clearerror (info);

	mfs_load_zone_maps (info->mfs);

	// More likely more errors generated.  Clear them too.
	backup_clearerror (info);

	// Make sure all loaded app zone maps are within volume;
	backup_verify_zone_maps (info);

	// The rest of the checks occur later.
	//   Check that inodes referencing application data fall below volume end.
}

/*********************/
/* Start the backup. */
int
backup_start (struct backup_info *info)
{
	struct blocklist *blocks;

	if ((add_partitions_to_backup_info (info, info->hda)) != 0) {
		return -1;
	}

	if (backup_scan_inodes (info) == ~0)
	{
		free (info->parts);
		return -1;
	}

	if (add_mfs_partitions_to_backup_info (info) != 0) {
		free (info->parts);
		free (info->inodes);
		return -1;
	}

	if (backup_info_count_misc (info) != 0)
	{
		free (info->parts);
		free (info->inodes);
		free (info->mfs);
		return -1;
	}

	info->nsectors += (info->ninodes * sizeof (unsigned long) + info->nparts * sizeof (struct backup_partition) + info->nmfs * sizeof (struct backup_partition) + sizeof (struct backup_head_v3) + 511) / 512;

	return 0;
}

/***************************************************************************/
/* State handlers - return val -1 = error, 0 = more data needed, 1 = go to */
/* next state. */

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
backup_state_info_partitions (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	unsigned count = info->nparts - info->state_val1;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

/* Copy as much as possible */
	if (count * sizeof (struct backup_partition) + info->shared_val1 > size * 512)
	{
		count = (size * 512 - info->shared_val1) / sizeof (struct backup_partition);
	}

	memcpy ((char *)data + info->shared_val1, (char *)&info->parts[info->state_val1], count * sizeof (struct backup_partition));

	info->state_val1 += count;
	info->shared_val1 += count * sizeof (struct backup_partition) + 7;
	*consumed = info->shared_val1 / 512;
	info->shared_val1 &= 512 - 8;

	if (info->state_val1 < info->nparts)
		return bsMoreData;

	return bsNextState;
}

/*****************************/
/* Add inode list to backup. */
/* state_val1 = index of last copied inode */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
backup_state_info_inodes_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	unsigned count = info->ninodes - info->state_val1;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

/* Copy as much as possible */
	if (count * sizeof (unsigned) + info->shared_val1 > size * 512)
	{
		count = (size * 512 - info->shared_val1) / sizeof (unsigned);
	}

	memcpy ((char *)data + info->shared_val1, (char *)&info->inodes[info->state_val1], count * sizeof (unsigned));

	info->state_val1 += count;
	info->shared_val1 += count * sizeof (unsigned) + 7;
	*consumed = info->shared_val1 / 512;
	info->shared_val1 &= 512 - 8;

	if (info->state_val1 < info->ninodes)
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
backup_state_info_mfs_partitions (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	unsigned count = info->nmfs - info->state_val1;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

/* Copy as much as possible */
	if (count * sizeof (struct backup_partition) + info->shared_val1 > size * 512)
	{
		count = (size * 512 - info->shared_val1) / sizeof (struct backup_partition);
	}

	memcpy ((char *)data + info->shared_val1, (char *)&info->mfsparts[info->state_val1], count * sizeof (struct backup_partition));

	info->state_val1 += count;
	info->shared_val1 += count * sizeof (struct backup_partition) + 7;
	*consumed = info->shared_val1 / 512;
	info->shared_val1 &= 512 - 8;

	if (info->state_val1 < info->nmfs)
		return bsMoreData;

	return bsNextState;
}

/********************************/
/* Finish off the backup header */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
backup_state_info_end (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	if (info->shared_val1 > 0)
	{
		memset ((char *)data + info->shared_val1, 0, 512 - info->shared_val1);

		*consumed = 1;
		info->shared_val1 = 0;
	}

	return bsNextState;
}

/*************************/
/* Backup the boot block */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_boot_block (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	if (tivo_partition_read_bootsector (info->devs[0].devname, data) < 0)
	{
		info->err_msg = "Error reading boot block";
		return bsError;
	}

	*consumed = 1;

	return bsNextState;
}

/***************************************/
/* Backup the raw (non-MFS) partitions */
/* state_val1 = current partition index */
/* state_val2 = offset within current partition */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_partitions (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	tpFILE *file;
	int tocopy = info->parts[info->state_val1].sectors - info->state_val2;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
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
		file = tivo_partition_open_direct (info->devs[(int)info->parts[info->state_val1].devno].devname, info->parts[info->state_val1].partno, O_RDONLY);
/* The sick part, is most of this line is an lvalue. */
		info->devs[(int)info->parts[info->state_val1].devno].files[(int)info->parts[info->state_val1].partno] = file;

		if (!file)
		{
			info->err_msg = "Internal error opening partition %s%d";
			info->err_arg1 = info->devs[(int)info->parts[info->state_val1].devno].devname;
			info->err_arg2 = (void *)(unsigned)info->parts[info->state_val1].partno;
			return bsError;
		}
	}

	if (tivo_partition_read (file, data, info->state_val2, tocopy) < 0)
	{
		info->err_msg = "%s backing up partitions";
		if (errno)
			info->err_arg1 = strerror (errno);
		else
			info->err_arg1 = "Unknown error";
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
	unsigned tocopy = htonl (info->mfs->vol_hdr.lognsectors) - info->state_val1;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	if (tocopy > size)
	{
		tocopy = size;
	}

	if (mfs_read_data (info->mfs, data, htonl (info->mfs->vol_hdr.logstart) + info->state_val1, tocopy) < 0)
	{
		info->err_msg = "Error reading MFS transaction log";
		return bsError;
	}

	*consumed = tocopy;
	info->state_val1 += tocopy;

	if (info->state_val1 < htonl (info->mfs->vol_hdr.lognsectors))
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
	unsigned tocopy = htonl (info->mfs->vol_hdr.unksectors) - info->state_val1;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	if (tocopy > size)
	{
		tocopy = size;
	}

	if (mfs_read_data (info->mfs, data, htonl (info->mfs->vol_hdr.unkstart) + info->state_val1, tocopy) < 0)
	{
		info->err_msg = "Error reading MFS data";
		return bsError;
	}

	*consumed = tocopy;
	info->state_val1 += tocopy;

	if (info->state_val1 < htonl (info->mfs->vol_hdr.unksectors))
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

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	if (info->state_val1 == 0)
	{
		cur_zone = mfs_next_zone (info->mfs, cur_zone);

		if (!cur_zone)
		{
			return bsNextState;
		}

		if (info->shrink_to > 0 && info->shrink_to < htonl (cur_zone->sector))
		{
/* The restore will be able to figure out the next zone is beyond the end of */
/* the shrunken volume. */
			return bsNextState;
		}

		info->state_ptr1 = cur_zone;
	}

	tocopy = htonl (cur_zone->length) - info->state_val1;

	if (tocopy > size)
	{
		tocopy = size;
	}

	memcpy (data, (char *)cur_zone + info->state_val1 * 512, tocopy * 512);

	*consumed = tocopy;
	info->state_val1 += tocopy;

	if (info->state_val1 >= htonl (cur_zone->length))
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
		if (info->state_val1 != htonl (inode->inode))
		{
			fprintf (stderr, "Inode %d uninitialized\n", info->state_val1);
			inode->inode = htonl (info->state_val1);
			inode->refcount = 0;
			inode->numblocks = 0;
			inode->fsid = 0;
			inode->size = 0;
/* Don't bother updating the CRC, restore will do that */
		}

		inode_size = offsetof (mfs_inode, datablocks);
		inode_size += htonl (inode->numblocks) * sizeof (inode->datablocks[0]);

/* Data in inode. */
		if (inode->type != tyStream && (inode->inode_flags & htonl (INODE_DATA)))
		{
			inode_size += htonl (inode->size);
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

	if (inode->type != tyStream && inode->refcount > 0 && inode->numblocks > 0 && !(inode->inode_flags & htonl (INODE_DATA)))
		tocopy = htonl (inode->size) - (info->state_val2 - 1) * 512;

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
		tocopy = htonl (inode->size);
	else
		tocopy = htonl (inode->blockused);

/* tocopy is currently in blocksize chunks, convert it to disk sectors */
	tocopy *= htonl (inode->blocksize) / 512;

	tocopy -= info->state_val2;

	copied = tocopy;

	if (copied > size)
	{
		copied = size;
	}

	if (mfs_read_inode_data_part (info->mfs, inode, data, info->state_val2, copied) < 0)
	{
		info->err_msg = "Error reading from tyStream id %d";
		info->err_arg1 = (void *)htonl (inode->fsid);
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

/*****************************************************************************/
/* Return the next sectors in the backup.  This is where all the data in the */
/* backup originates.  If it's backed up, it came from here.  This only */
/* reads the data from the info structure.  Compression is handled */
/* elsewhere. */
static unsigned int
backup_next_sectors (struct backup_info *info, char *buf, int sectors)
{
	enum backup_state_ret ret;
	unsigned consumed;

	unsigned backup_blocks = 0;

	while (sectors > 0 && info->state < bsMax && info->state >= bsBegin)
	{
		consumed = 0;

		ret = ((*info->state_machine)[info->state]) (info, buf, sectors, &consumed);

		if (consumed > sectors)
		{
			info->err_msg = "Internal error: State %d consumed too much buffer";
			info->err_arg1 = (void *)info->state;
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
			info->err_arg1 = (void *)info->state;
			info->err_arg2 = (void *)ret;
			return -1;
		}

/* Deal with consumed buffer */
		if (consumed > 0)
		{
			info->crc = compute_crc (buf, consumed * 512, info->crc);
			info->cursector += consumed;
			backup_blocks += consumed;
			sectors -= consumed;
			buf += consumed * 512;
		}
	}

	return backup_blocks;
}

/*************************************************************************/
/* Pass the data to the front-end program.  This handles compression and */
/* all that fun stuff. */
unsigned int
backup_read (struct backup_info *info, char *buf, unsigned int size)
{
	unsigned int retval = 0;

	if (size < 512)
	{
		info->err_msg = "Internal error 2 - Backup buffer too small";
		return -1;
	}

	if (info->back_flags & BF_COMPRESSED)
	{
		if (info->cursector == 0)
		{
			retval = backup_next_sectors (info, buf, 1);
			if (retval != 1)
			{
				info->err_msg = "Error starting backup";
				return -1;
			}

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
			info->comp->avail_out = 0;
			if (deflateInit (info->comp, BF_COMPLVL (info->back_flags)) != Z_OK)
			{
				free (info->comp_buf);
				free (info->comp);
				info->err_msg = "Compression init error";
				return -1;
			}

			buf += 512;
			retval = 512;
			size -= 512;
		}

		if (!info->comp)
		{
			return retval;
		}

		info->comp->avail_out = size;
		info->comp->next_out = buf;
		while (info->comp && info->comp->avail_out > 0)
		{
			if (info->comp->avail_in)
			{
				if (deflate (info->comp, Z_NO_FLUSH) != Z_OK)
				{
					info->err_msg = "Compression error";
					return -1;
				}
			}
			else if (info->comp_buf)
			{
				int nread = backup_next_sectors (info, info->comp_buf, 2048);
				if (nread < 0)
				{
					return -1;
				}
				if (nread == 0)
				{
					free (info->comp_buf);
					info->comp_buf = 0;
					continue;
				}

				info->comp->avail_in = 512 * nread;
				info->comp->next_in = info->comp_buf;
			}
			else
			{
				int zres = deflate (info->comp, Z_FINISH);

				if (zres == Z_STREAM_END)
				{
					retval += size - info->comp->avail_out;
					zres = deflateEnd (info->comp);
					free (info->comp);
					info->comp = 0;
				}

				if (zres != Z_OK)
				{
					break;
				}
			}
		}
		if (info->comp)
		{
			retval += size - info->comp->avail_out;
		}
	}
	else
	{
		return backup_next_sectors (info, buf, size / 512) * 512;
	}

	return retval;
}

int
backup_finish(struct backup_info *info)
{
	if (info->cursector != info->nsectors)
	{
		info->err_msg = "Backup ended prematurely";
		return -1;
	}

	return 0;
}

/****************************/
/* Display the backup error */
void
backup_perror (struct backup_info *info, char *str)
{
	int err = 0;

	if (info->err_msg)
	{
		fprintf (stderr, "%s: ", str);
		fprintf (stderr, info->err_msg, info->err_arg1, info->err_arg2, info->err_arg3);
		fprintf (stderr, ".\n");
		err = 1;
	}

	if (info->mfs->err_msg || info->mfs->vols->err_msg)
	{
		mfs_perror (info->mfs, str);
		err = 2;
	}

	if (err == 0)
	{
		fprintf (stderr, "%s: No error.\n", str);
	}
}

/***********************/
/* Backup has an error */
int
backup_has_error (struct backup_info *info)
{
	if (info->err_msg)
		return 1;

	if (info->mfs)
		return mfs_has_error (info->mfs);

	return 0;
}

/*************************************/
/* Return the MFS error in a string. */
int
backup_strerror (struct backup_info *info, char *str)
{
	if (info->err_msg)
		sprintf (str, info->err_msg, info->err_arg1, info->err_arg2, info->err_arg3);
	else if (info->mfs)
		return (mfs_strerror (info->mfs, str));
	else
	{
		sprintf (str, "No error");
		return 0;
	}

	return 1;
}

/********************/
/* Clear any errors */
void
backup_clearerror (struct backup_info *info)
{
	info->err_msg = 0;
	info->err_arg1 = 0;
	info->err_arg2 = 0;
	info->err_arg3 = 0;

	if (info->mfs)
		mfs_clearerror (info->mfs);
}
