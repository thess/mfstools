#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
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

/***************************************************************************/
/* State handlers - return val -1 = error, 0 = more data needed, 1 = go to */
/* next state. */

/***********************************/
/* Generic handler for header data */
enum backup_state_ret
restore_read_header (struct backup_info *info, void *data, unsigned size, unsigned *consumed, void *dest, unsigned total, unsigned datasize);
/* Defined in restore.c */

/*******************************************/
/* Begin restore - determine v1 or v3 here */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block (Always 0 for v1) */
enum backup_state_ret
restore_state_begin_v1 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	struct backup_head *head = data;

	if (size == 0)
	{
		info->err_msg = "Internal error: Restore buffer empty";
		return bsError;
	}

	switch (head->magic)
	{
	case TB_MAGIC:
		break;
	case TB_ENDIAN:
		info->back_flags |= RF_ENDIAN;
		break;
	case TB3_MAGIC:
	case TB3_ENDIAN:
		info->state_machine = &restore_v3;
/* Return and let it call back into v3 handler */
		return bsMoreData;
		break;
	default:
		info->err_msg = "Unknown backup format";
		return bsError;
	}

/* Copy header fields into backup info */
	if (info->back_flags & RF_ENDIAN)
	{
		info->back_flags |= Endian32_Swap (head->flags);
		info->nsectors = Endian32_Swap (head->nsectors);
		info->nparts = Endian32_Swap (head->nparts);
		info->nblocks = Endian32_Swap (head->nblocks);
		info->nmfs = Endian32_Swap (head->mfspairs);
	}
	else
	{
		info->back_flags |= head->flags;
		info->nsectors = head->nsectors;
		info->nparts = head->nparts;
		info->nblocks = head->nblocks;
		info->nmfs = head->mfspairs;
	}

/* Allocate storage for backup description */
	info->parts = calloc (sizeof (struct backup_partition), info->nparts);
	info->blocks = calloc (sizeof (struct backup_block), info->nblocks);
	info->mfsparts = calloc (sizeof (struct backup_partition), info->nmfs);

	if (!info->parts || !info->blocks || !info->mfsparts)
	{
		if (info->parts)
			free (info->parts);
		info->parts = 0;
		info->nparts = 0;

		if (info->blocks)
			free (info->blocks);
		info->blocks = 0;
		info->nblocks = 0;

		if (info->mfsparts)
			free (info->mfsparts);
		info->mfsparts = 0;
		info->nmfs = 0;

		info->err_msg = "Memory exhausted (Begin restore)";
		return bsError;
	}

/* v1 backup has no other data in first block */
	*consumed = 1;
	info->shared_val1 = 0;

	return bsNextState;
}

/***********************************/
/* Read partition info from backup */
/* state_val1 = offset of last copied partition */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
restore_state_partition_info (struct backup_info *info, void *data, unsigned size, unsigned *consumed);
/* Defined in restore.c */

/**********************************/
/* Read block list from v1 backup */
/* state_val1 = offset of last copied block */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
restore_state_block_info_v1 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	return restore_read_header (info, data, size, consumed, info->blocks, info->nblocks, sizeof (struct backup_block));
}

/************************/
/* Read MFS volume list */
/* state_val1 = offset of last copied MFS partition */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
restore_state_mfs_partition_info (struct backup_info *info, void *data, unsigned size, unsigned *consumed);
/* Defined in restore.c */

/********************************/
/* Finish off the backup header */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
restore_state_info_end (struct backup_info *info, void *data, unsigned size, unsigned *consumed);
/* Defined in restore.c */

/**************************/
/* Restore the boot block */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
restore_state_boot_block (struct backup_info *info, void *data, unsigned size, unsigned *consumed);
/* Defined in restore.c */

/****************************************/
/* Restore the raw (non-MFS) partitions */
/* state_val1 = current partition index */
/* state_val2 = offset within current partition */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
restore_state_partitions (struct backup_info *info, void *data, unsigned size, unsigned *consumed);
/* Defined in restore.c */

/*********************************/
/* Initialize the MFS Volume set */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
restore_state_mfs_init (struct backup_info *info, void *data, unsigned size, unsigned *consumed);
/* Defined in restore.c */

/********************************************************/
/* Restore the blocks of data from MFS (V1 backup only) */
/* state_val1 = current block index */
/* state_val2 = offset within current block */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
restore_state_blocks_v1 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	int tocopy = info->blocks[info->state_val1].sectors - info->state_val2;

	if (size == 0)
	{
		info->err_msg = "Internal error: Restore buffer empty";
		return bsError;
	}

/* Restore as much as possible. */
	if (tocopy > size)
	{
		tocopy = size;
	}

	if (mfsvol_write_data (info->vols, data, info->blocks[info->state_val1].firstsector + info->state_val2, tocopy) < 0)
	{
		info->err_msg = "%s restoring MFS data";
		if (errno)
			info->err_arg1 = strerror (errno);
		else
			info->err_arg1 = "Unknown error";

		return bsError;
	}

	*consumed = tocopy;
	info->state_val2 += tocopy;

	if (info->state_val2 >= info->blocks[info->state_val1].sectors)
	{
		info->state_val1++;
		info->state_val2 = 0;
	}

	if (info->state_val1 < info->nblocks)
		return bsMoreData;

	return bsNextState;
}

/***********************/
/* Finish restore (V1) */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
restore_state_complete_v1 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	if (size > 0)
	{
		fprintf (stderr, "Extra blocks at end of restore???");
	}

	if (info->state != bsComplete)
	{
/* Go ahead and print it to stderr - this is really a development message */
		fprintf (stderr, "State machine missing states\n");
	}

	if (restore_cleanup_parts (info) < 0)
		return bsError;
	if (restore_make_swap (info) < 0)
		return bsError;
	if (restore_fixup_vol_list (info) < 0)
		return bsError;
	if (restore_fixup_zone_maps (info) < 0)
		return bsError;

	mfsvol_cleanup (info->vols);
	info->vols = 0;
	info->mfs = mfs_init (info->devs[0].devname, info->ndevs > 1? info->devs[1].devname: NULL, O_RDWR);
	if (!info->mfs || mfs_has_error (info->mfs))
		return bsError;
	if (restore_fudge_inodes (info) < 0)
		return bsError;
	if (restore_fudge_transactions (info) < 0)
		return bsError;

#if HAVE_SYNC
/* Make sure changes are committed to disk */
	sync ();
#endif

	return bsNextState;
}

backup_state_handler restore_v1 = {
	NULL,									// bsScanMFS
	restore_state_begin_v1,					// bsBegin
	restore_state_partition_info,			// bsInfoPartition
	restore_state_block_info_v1,			// bsInfoBlocks
	restore_state_mfs_partition_info,		// bsInfoMFSPartitions
	NULL,									// bsInfoZoneMaps
	restore_state_info_end,					// bsInfoEnd
	restore_state_boot_block,				// bsBootBlock
	restore_state_partitions,				// bsPartitions
	restore_state_mfs_init,					// bsMFSInit
	restore_state_blocks_v1,				// bsBlocks
	NULL,									// bsVolumeHeader
	NULL,									// bsTransactionLog
	NULL,									// bsUnkRegion
	NULL,									// bsMfsReinit
	NULL,									// bsInodes
	restore_state_complete_v1				// bsComplete
};

int
restore_fudge_inodes (struct backup_info *info)
{
	unsigned int loop, count;
	uint64_t total;

	if (!(info->back_flags & BF_SHRINK))
		return 0;

	count = mfs_inode_count (info->mfs);
	total = mfs_volume_set_size (info->mfs);

	for (loop = 0; loop < count; loop++)
	{
		mfs_inode *inode = mfs_read_inode (info->mfs, loop);

		if (inode)
		{
			if (inode->type == tyStream)
			{
				int loop2;
				int changed = 0;

				if (mfs_is_64bit (info->mfs))
				{
					for (loop2 = 0; loop2 < intswap32 (inode->numblocks); loop2++)
					{
						if (intswap64 (inode->datablocks.d64[loop2].sector) >= total)
						{
							inode->blockused = 0;
							changed = 1;
							inode->numblocks = intswap32 (intswap32 (inode->numblocks) - 1);
							if (loop2 < intswap32 (inode->numblocks))
								memmove (&inode->datablocks.d64[loop2], &inode->datablocks.d64[loop2 + 1], sizeof (*inode->datablocks.d64) * (intswap32 (inode->numblocks) - loop2));
							loop2--;
						}
					}
				}
				else
				{
					for (loop2 = 0; loop2 < intswap32 (inode->numblocks); loop2++)
					{
						if (intswap32 (inode->datablocks.d32[loop2].sector) >= total)
						{
							inode->blockused = 0;
							changed = 1;
							inode->numblocks = intswap32 (intswap32 (inode->numblocks) - 1);
							if (loop2 < intswap32 (inode->numblocks))
								memmove (&inode->datablocks.d32[loop2], &inode->datablocks.d32[loop2 + 1], sizeof (*inode->datablocks.d32) * (intswap32 (inode->numblocks) - loop2));
							loop2--;
						}
					}
				}

				if (changed)
					if (mfs_write_inode (info->mfs, inode) < 0)
					{
						info->err_msg = "Error fixing up inodes";
						return -1;
					}

			}
			free (inode);
		}
	}

	return 0;
}

int
restore_fudge_log (int is_64bit, char *trans, uint64_t volsize)
{
	log_entry_all *cur = (log_entry_all *)trans;
	if (intswap16 (cur->log.length) < sizeof (log_entry) - 2)
	{
		return 0;
	}

	switch (intswap32 (cur->log.transtype))
	{
	case ltMapUpdate:
		if (intswap16 (cur->log.length) < sizeof (log_map_update_32) - 2)
		{
			return 0;
		}

		if (intswap32 (cur->zonemap_32.sector) >= volsize)
		{
			return intswap16 (cur->log.length) + 2;
		}
		break;
	case ltMapUpdate64:
		if (intswap16 (cur->log.length) < sizeof (log_map_update_64) - 2)
		{
			return 0;
		}

		if (intswap64 (cur->zonemap_64.sector) >= volsize)
		{
			return intswap16 (cur->log.length) + 2;
		}
		break;
	case ltInodeUpdate:
	case ltInodeUpdate2:
		if (intswap16 (cur->log.length) < sizeof (log_inode_update) - 2)
		{
			return 0;
		}

		if (cur->inode.type != tyStream)
		{
			return 0;
		}

		if (intswap16 (cur->log.length) >= intswap32 (cur->inode.datasize) + sizeof (log_inode_update) - 2)
		{
			int loc, spot;
			unsigned int shrunk = 0;
			unsigned int bsize, dsize, dused, curblks;

			bsize = intswap32 (cur->inode.blocksize) / 512;
			dsize = intswap32 (cur->inode.size) * bsize;
			dused = intswap32 (cur->inode.blockused) * bsize;
			curblks = 0;

			if (is_64bit)
			{
				for (loc = 0, spot = 0; loc < intswap32 (cur->inode.datasize) / sizeof (cur->inode.datablocks.d64[0]); loc++)
				{
					if (intswap64 (cur->inode.datablocks.d64[loc].sector) < volsize)
					{
						if (shrunk)
						{
							cur->inode.datablocks.d64[spot] = cur->inode.datablocks.d64[loc];
						}
						spot++;
					} else {
						unsigned int count = intswap32 (cur->inode.datablocks.d64[loc].count);
						shrunk += sizeof (cur->inode.datablocks.d64[0]);
						if (dused > curblks)
						{
							if (dused > curblks + count)
							{
								dused -= count;
							}
							else
							{
								dused = curblks;
							}
						}
						dsize -= count;
					}

					curblks = intswap32 (cur->inode.datablocks.d64[loc].count);
				}
			}
			else
			{
				for (loc = 0, spot = 0; loc < intswap32 (cur->inode.datasize) / sizeof (cur->inode.datablocks.d32[0]); loc++)
				{
					if (intswap32 (cur->inode.datablocks.d32[loc].sector) < volsize)
					{
						if (shrunk)
						{
							cur->inode.datablocks.d32[spot] = cur->inode.datablocks.d32[loc];
						}
						spot++;
					} else {
						unsigned int count = intswap32 (cur->inode.datablocks.d32[loc].count);
						shrunk += sizeof (cur->inode.datablocks.d32[0]);
						if (dused > curblks)
						{
							if (dused > curblks + count)
							{
								dused -= count;
							}
							else
							{
								dused = curblks;
							}
						}
						dsize -= count;
					}

					curblks = intswap32 (cur->inode.datablocks.d32[loc].count);
				}
			}

			if (shrunk)
			{
				cur->log.length = intswap16 (intswap16 (cur->log.length) - shrunk);
				cur->inode.size = intswap32 (dsize / bsize);
				cur->inode.blockused = intswap32 (dused / bsize);
				cur->inode.datasize = intswap32 (intswap32 (cur->inode.datasize) - shrunk);
			}
			return shrunk;
		}
		break;
	}

	return 0;
}

int
restore_fudge_transactions (struct backup_info *info)
{
	unsigned int curlog;
	uint64_t volsize;

	int result;

	char bufs[2][512];
	log_hdr *hdrs[2] = {(log_hdr *)bufs[0], (log_hdr *)bufs[1]};
	int bufno = 1;

	int is_64bit = mfs_is_64bit (info->mfs);

	if (!(info->back_flags & BF_SHRINK))
		return 0;

	volsize = mfs_volume_set_size (info->mfs);

	curlog = mfs_log_last_sync (info->mfs);

	hdrs[0]->logstamp = hdrs[1]->logstamp = intswap32 (curlog - 2);

	while ((result = mfs_log_read (info->mfs, bufs[bufno], curlog)) > 0)
	{
		unsigned int size = intswap32 (hdrs[bufno]->size);
		unsigned int start;
		log_entry *cur;

		if (result != 512)
		{
			curlog++;
			continue;
		}

		if (size > 0x1f0)
		{
			fprintf (stderr, "MFS transaction logstamp %ud has invalid size, skipping\n", curlog);
			curlog++;
			continue;
		}

		if (intswap32 (hdrs[bufno]->first) > 0 &&
		  intswap32 (hdrs[bufno ^ 1]->logstamp) == curlog - 1)
		{
			unsigned int size2 = intswap32 (hdrs[bufno ^ 1]->size);

			start = intswap32 (hdrs[bufno ^ 1]->first);
			cur = (log_entry *)(bufs[bufno ^ 1] + start + 16);

			while (intswap16 (cur->length) + 2 < size2 - start) {
				start += intswap16 (cur->length) + 2;
				cur = (log_entry *)(bufs[bufno ^ 1] + start + 16);
			}

			if (size2 - start + intswap32 (hdrs[bufno]->first) == intswap16 (cur->length) + 2)
			{
				char tmp[1024];
				int shrunk;

				memcpy (tmp, cur, size2 - start);
				memcpy (tmp + size2 - start, bufs[bufno] + 16, intswap32 (hdrs[bufno]->first));

				shrunk = restore_fudge_log (is_64bit, tmp, volsize);
				if (shrunk)
				{
					unsigned short newsize = intswap16 (cur->length) - shrunk + 2;
					if (newsize > size2 - start)
					{
						memcpy (cur, tmp, size2 - start);
						memcpy (bufs[bufno] + 16, tmp + size2 - start, newsize - (size2 - start));
					}
					else
					{
						size2 = start + newsize;
						hdrs[bufno ^ 1]->size = intswap32 (size2);
						if (newsize > 0)
							memcpy (cur, tmp, newsize);
						bzero ((char *)cur + newsize, 0x1f0 - size2);
						if (mfs_log_write (info->mfs, bufs[bufno ^ 1]) != 512)
						{
							perror ("mfs_log_write");
							return -1;
						}
						shrunk = intswap32 (hdrs[bufno]->first);
					}
					memmove (bufs[bufno] + intswap32 (hdrs[bufno]->first) - shrunk + 16, bufs[bufno] + intswap32 (hdrs[bufno]->first) + 16, size - intswap32 (hdrs[bufno]->first));
					hdrs[bufno]->first = intswap32 (intswap32 (hdrs[bufno]->first) - shrunk);
					size -= shrunk;
				}
			}
		}

		start = intswap32 (hdrs[bufno]->first);
		cur = (log_entry *)(bufs[bufno] + start + 16);
		while (intswap16 (cur->length) + 2 + start <= size)
		{
			int oldsize = intswap16 (cur->length) + 2;
			int shrunk;

			shrunk = restore_fudge_log (is_64bit, (char *)cur, volsize);

			if (shrunk)
			{
				memmove ((char *)cur + oldsize - shrunk, (char *)cur + oldsize, size - start - oldsize);
				size -= shrunk;
				oldsize -= shrunk;
			}

			start += oldsize;
			cur = (log_entry *)(bufs[bufno] + start + 16);
		}

		if (intswap32 (hdrs[bufno]->size) != size)
		{
			bzero (bufs[bufno] + size + 16, intswap32 (hdrs[bufno]->size) - size);
			hdrs[bufno]->size = intswap32 (size);
			if (mfs_log_write (info->mfs, bufs[bufno]) != 512)
			{
				perror ("mfs_log_write");
				return -1;
			}
		}

		curlog++;
		bufno ^= 1;
	}

	return 0;
}

int
restore_fixup_vol_list (struct backup_info *info)
{
	int loop;
	union {
		volume_header hdr;
		char pad[512];
	} vol;

	if (mfsvol_read_data (info->vols, (void *)&vol, 0, 1) != 512)
	{
		info->err_msg = "Error fixing volume list";
		return -1;
	}

	if (info->back_flags & BF_64)
	{
		bzero (vol.hdr.v64.partitionlist, sizeof (vol.hdr.v64.partitionlist));

		for (loop = 0; loop < info->nmfs; loop++)
		{
			sprintf (vol.hdr.v64.partitionlist + strlen (vol.hdr.v64.partitionlist), "%s/dev/hd%c%d", loop > 0? " ": "", 'a' + info->mfsparts[loop].devno, info->mfsparts[loop].partno);
		}

		if (strlen (vol.hdr.v64.partitionlist) + 1 > sizeof (vol.hdr.v64.partitionlist))
		{
			info->err_msg = "Partition list too long";
			return -1;
		}

		vol.hdr.v64.total_sectors = intswap64 (mfsvol_volume_set_size (info->vols));

		MFS_update_crc (&vol.hdr.v64, sizeof (vol.hdr.v64), vol.hdr.v64.checksum);
	}
	else
	{
		bzero (vol.hdr.v32.partitionlist, sizeof (vol.hdr.v32.partitionlist));

		for (loop = 0; loop < info->nmfs; loop++)
		{
			sprintf (vol.hdr.v32.partitionlist + strlen (vol.hdr.v32.partitionlist), "%s/dev/hd%c%d", loop > 0? " ": "", 'a' + info->mfsparts[loop].devno, info->mfsparts[loop].partno);
		}

		if (strlen (vol.hdr.v32.partitionlist) + 1 > sizeof (vol.hdr.v32.partitionlist))
		{
			info->err_msg = "Partition list too long";
			return -1;
		}

		vol.hdr.v32.total_sectors = intswap32 (mfsvol_volume_set_size (info->vols));

		MFS_update_crc (&vol.hdr.v32, sizeof (vol.hdr.v32), vol.hdr.v32.checksum);
	}

	if (mfsvol_write_data (info->vols, (void *)&vol, 0, 1) != 512 || mfsvol_write_data (info->vols, (void *)&vol, mfsvol_volume_size (info->vols, 0) - 1, 1) != 512)
	{
		info->err_msg = "Error writing changes to volume header";
		return -1;
	}

	return 0;
}

int
restore_fixup_zone_maps(struct backup_info *info)
{
	uint64_t tot;
	union {
		volume_header hdr;
		char pad[512];
	} vol;
	zone_header *cur;

	uint64_t cursector, sbackup, nextsector;

	unsigned length;
	int didtruncate = 0;

	if (!(info->back_flags & BF_SHRINK))
		return 0;

	tot = mfsvol_volume_set_size (info->vols);

	if (mfsvol_read_data (info->vols, (void *)&vol, 0, 1) < 0)
	{
		info->err_msg = "Error truncating MFS volume";
		return -1;
	}

	cur = malloc (512);
	if (!cur)
	{
		info->err_msg = "Memory exhausted";
		return -1;
	}

	if (info->back_flags & BF_64)
	{
		cursector = intswap64 (vol.hdr.v64.zonemap.sector);
	}
	else
	{
		cursector = intswap32 (vol.hdr.v32.zonemap.sector);
	}

	if (mfsvol_read_data (info->vols, (void *)cur, cursector, 1) != 512)
	{
		free (cur);
		info->err_msg = "Error truncating MFS volume";
		return -1;
	}

	if (info->back_flags & BF_64)
	{
		nextsector = intswap64 (cur->z64.next_sector);
		length = intswap32 (cur->z64.length);
	}
	else
	{
		nextsector = intswap32 (cur->z32.next.sector);
		length = intswap32 (cur->z32.length);
	}

	while (nextsector && nextsector < tot)
	{
		cursector = nextsector;

		if (mfsvol_read_data (info->vols, (void *)cur, cursector, 1) != 512)
		{
			info->err_msg = "Error truncating MFS volume";
			free (cur);
			return -1;
		}

		if (info->back_flags & BF_64)
		{
			nextsector = intswap64 (cur->z64.next_sector);
			length = intswap32 (cur->z64.length);
		}
		else
		{
			nextsector = intswap32 (cur->z32.next.sector);
			length = intswap32 (cur->z32.length);
		}
	}

	cur = realloc (cur, length * 512);
	if (!cur)
	{
		info->err_msg = "Memory exhausted";
		return -1;
	}

	if (mfsvol_read_data (info->vols, (void *)cur, cursector, length) != length * 512)
	{
		info->err_msg = "Error truncating MFS volume";
		free (cur);
		return -1;
	}

	if (info->back_flags & BF_64)
	{
		if (cur->z64.next_sector)
		{
			cur->z64.next_sector = 0;
			cur->z64.next_length = 0;
			cur->z64.next_size = 0;
			cur->z64.next_min = 0;

			sbackup = intswap64 (cur->z64.sbackup);

			MFS_update_crc (cur, length * 512, cur->z64.checksum);
			didtruncate = 1;
		}
	}
	else
	{
		if (cur->z32.next.sector)
		{
			cur->z32.next.sector = 0;
			cur->z32.next.length = 0;
			cur->z32.next.size = 0;
			cur->z32.next.min = 0;

			sbackup = intswap32 (cur->z32.sbackup);

			MFS_update_crc (cur, length * 512, cur->z32.checksum);
			didtruncate = 1;
		}
	}

	if (didtruncate)
	{
		if (mfsvol_write_data (info->vols, (void *)cur, cursector, length) != length * 512 || mfsvol_write_data (info->vols, (void *)cur, sbackup, length) != length * 512)
		{
			info->err_msg = "Error truncating MFS volume";
			free (cur);
			return -1;
		}
	}

	free (cur);
	return 0;
}
