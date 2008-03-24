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

/***********************************************************/
/* Queries the mfs code for the list of partitions in use. */
int
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
/* partitions 1 (Partition table) and one of 2/3/4 or 5/6/7 */
/* (One of the bootstrap/kernel/root sets) and 9 (/var) - 8 is skipped */
/* because it can easily be re-created.  It is, after all, just swap space. */
/* Nothing else is supported. */
int
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
	uint64_t volume_size = mfs_volume_set_size (info->mfs);
	zone_header *zone;

#if DEBUG
	fprintf (stderr, "Volume set size %lld\n", volume_size);
#endif

	for (zone = mfs_next_zone (info->mfs, NULL); zone; zone = mfs_next_zone (info->mfs, zone))
	{
		unsigned int zonetype;
		uint64_t zonefirst;

		if (mfs_is_64bit (info->mfs))
		{
			zonetype = intswap32 (zone->z64.type);
			zonefirst = intswap64 (zone->z64.first);
		}
		else
		{
			zonetype = intswap32 (zone->z32.type);
			zonefirst = intswap32 (zone->z32.first);
		}

		// Media zones will be accounted for later.
		if (zonetype == ztMedia)
			continue;

#if DEBUG
		fprintf (stderr, "Zone type %d at %lld\n", zonetype, zonefirst);
#endif

		if (zonefirst >= volume_size)
		{
			info->err_msg = "%s zone outside available volume";
			switch (zonetype)
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
	unsigned consumed = 0;

	/* Turn on memwrite mode to commit the transaction log */
	mfs_enable_memwrite (info->mfs);

	/* Commit the transaction log */
	mfs_log_fssync (info->mfs);

	/* Call the first state.  If this returns anything but bsNextState with */
	/* no consumed data, it's an error. */
	enum backup_state_ret ret = ((*info->state_machine)[info->state]) (info, NULL, 0, &consumed);

	if (ret != bsNextState || consumed != 0)
	{
		if (ret != bsError)
		{
			info->err_msg = "Internal error scanning MFS volume: %d %d";
			info->err_arg1 = (void *)ret;
			info->err_arg2 = (void *)consumed;
		}
		return -1;
	}

	while (info->state < bsMax && (*info->state_machine)[++info->state] == NULL) ;
/* Initialize per-state values */
	info->state_val1 = 0;
	info->state_val2 = 0;
	info->state_ptr1 = NULL;

	return 0;
}

/***************************************************************************/
/* State handlers - return val -1 = error, 0 = more data needed, 1 = go to */
/* next state. */

/***********************************/
/* Generic handler for header data */
enum backup_state_ret
backup_write_header (struct backup_info *info, void *data, unsigned size, unsigned *consumed, void *src, unsigned total, unsigned datasize)
{
	unsigned count = total * datasize - info->state_val1;

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

	memcpy ((char *)data + info->shared_val1, (char *)src + info->state_val1, count);

	info->state_val1 += count;
	info->shared_val1 += count;
	*consumed = info->shared_val1 / 512;
	info->shared_val1 &= 511;

	if (info->state_val1 < total * datasize)
		return bsMoreData;

	return bsNextState;
}

/*********************************/
/* Add partition info to backup. */
/* state_val1 = offset of last copied partition */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
backup_state_info_partitions (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	return backup_write_header (info, data, size, consumed, info->parts, info->nparts, sizeof (struct backup_partition));
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
	return backup_write_header (info, data, size, consumed, info->mfsparts, info->nmfs, sizeof (struct backup_partition));
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
