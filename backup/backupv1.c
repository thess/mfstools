#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <inttypes.h>

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

struct blocklist
{
	int backup;
	unsigned int sector;
	struct blocklist *next;
};

/***************************************************************************/
/* Allocate a block from the given block pool.  If the pool is dry, calloc */
/* it.  Either way, the returned block shall be zeroed out. */
static struct blocklist *
alloc_block (struct blocklist **pool)
{
	struct blocklist *newblock = *pool;

	if (newblock)
	{
		*pool = newblock->next;
		newblock->sector = 0;
		newblock->backup = 0;
		newblock->next = 0;
	}
	else
	{
		newblock = calloc (sizeof (*newblock), 1);
	}

	return newblock;
}

/*******************************/
/* Return a block to the pool. */
static void
free_block (struct blocklist **pool, struct blocklist *block)
{
	block->next = *pool;
	*pool = block;
}

/***********************************************************************/
/* Free an entire list of blocks.  This can be used to cleanup a pool. */
static void
free_block_list (struct blocklist **blocks)
{
	while (*blocks)
	{
		struct blocklist *tmp = *blocks;
		*blocks = tmp->next;
		free (tmp);
	}
}

/*********************************/
/* Free an array of block lists. */
static void
free_block_list_array (struct blocklist **blocks)
{
	while (*blocks)
	{
		free_block_list (blocks);
		blocks++;
	}
}

/***********************************************************************/
/* Concatenates an array of block lists into a single list.  This list */
/* should be read or free only.  Any write to it will likely confuse the */
/* add function. */
static struct blocklist *
block_list_array_concat (struct blocklist **blocks)
{
	struct blocklist *res = NULL;
	struct blocklist **last = &res;

	while (*blocks)
	{
		*last = *blocks;

		while (*last)
		{
			last = &(*last)->next;
		}
		*blocks = 0;
		blocks++;
	}

	return res;
}

/************************************************/
/* Add a block to the list of blocks to backup. */
static int
backup_add_block (struct blocklist **blocks, uint64_t *partstart, struct blocklist **pool, uint64_t sector, uint64_t count)
{
	struct blocklist **loop;
	struct blocklist *prev = 0;

	while (partstart[1] <= sector)
	{
		partstart++;
		blocks++;
	}

/* A little debug here and there never hurt anything. */
#if DEBUG
	fprintf (stderr, "Adding block %" PRId64 " of %" PRId64 " from volume at %" PRId64 "\n", sector, count, partstart[0]);
#endif

/* Find where in the list this block fits.  This will return with &loop */
/* pointing to the first block with a sector number greater than the new */
/* block. */
	for (loop = blocks; *loop && (*loop)->sector < sector; loop = &((*loop)->next))
	{
		prev = *loop;
	}

	if (!*loop)
	{
/* There are no blocks prior to this one, and it doesn't butt up against */
/* the end of the list. */
		struct blocklist *newblock;
		newblock = alloc_block (pool);

		if (!newblock)
		{
			return -1;
		}

/* And one more.  Since this is the end of the list, this tail block is */
/* to indicate the size of this block. */

		newblock->next = alloc_block (pool);

		if (!newblock->next)
		{
			free (newblock);
			return -1;
		}

		newblock->backup = 1;
		newblock->sector = sector;

		newblock->next->next = *loop;
		newblock->next->sector = sector + count;

/* Insert the block at the position found. */
		*loop = newblock;
	}
	else if ((*loop)->backup)
	{
/* This block fits in a space currently set not to backup. */
		if (sector + count >= (*loop)->sector)
		{
/* In fact, it butts right up against the next backup block.  Merely extend */
/* that block to include up to the new sector. */
			(*loop)->sector = sector;
		}
		else
		{
/* This block is in the middle of a block set to not backup.  That means this */
/* block will have to be split into 3 parts, 2 of them new. */
			struct blocklist *newblock;
			newblock = alloc_block (pool);

			if (!newblock)
			{
				return -1;
			}

/* Allocate the second new block, that will take care of the unbacked up */
/* space between this backed up space and the next. */
			newblock->next = alloc_block (pool);

			if (!newblock->next)
			{
				free (newblock);
				return -1;
			}

			newblock->backup = 1;
			newblock->sector = sector;

			newblock->next->next = *loop;
			newblock->next->sector = sector + count;

/* Insert it into the current location. */
			*loop = newblock;
		}
	}
	else
	{
/* The next block the new one is not after is not set to be backed up. */
/* The only way this could happen is either because no blocks exist yet, */
/* or the new block is at the beginning of an unbacked up section, or no */
/* blocks before it are backed up. */
		if (prev && (*loop)->sector < sector + count)
		{
/* This is not the first block, and it extends the previous backed up block. */
			(*loop)->sector = sector + count;
		}
		else if (!prev)
		{
/* This is the first block in the list.  Split the initial non-backup block. */
			struct blocklist *newblock;
			newblock = alloc_block (pool);

			if (!newblock)
			{
				return -1;
			}

			newblock->next = alloc_block (pool);

			if (!newblock->next)
			{
				free (newblock);
				return -1;
			}

			newblock->backup = 1;
			newblock->sector = sector;

			newblock->next->sector = sector + count;
			newblock->next->next = (*loop)->next;;

/* Insert this block at the beginning of the list.  Also trick out the loop */
/* variable, since it is expected to point to the newly adjusted block to be */
/* backed up. */
			*loop = newblock;
			loop = &newblock->next;
		}
		else
		{
/* These ifs don't cover all cases...  But no other cases should occur.  If */
/* they do, it is an error. */
			return -1;
		}
	}

/* At this point it is possible for a block to overlap the next block. */
	while ((*loop)->next && ((*loop)->next->sector < (*loop)->sector || (*loop)->next->backup == (*loop)->backup))
	{
/* If there is overlap, or if the next block has the same backup/don't backup */
/* status, just drop the next block. */
		struct blocklist *oldblock = (*loop)->next;
		(*loop)->next = oldblock->next;
		free_block (pool, oldblock);
	}

	return 0;
}

/***********************************************************/
/* Create the main data structure for holding backup info. */
static int
backup_add_blocks_to_backup_info (struct backup_info *info, struct blocklist *blocks)
{
	struct blocklist *loop;
	int count = 0;

	if (!info)
	{
		return -1;
	}

/* Count how many blocks there are. */
	for (loop = blocks; loop; loop = loop->next)
	{
		if (loop->backup)
		{
			info->nblocks++;
		}
	}

/* Allocate space for the backup blocks in the portable format. */
	info->blocks = calloc (sizeof (struct backup_block), info->nblocks);

	if (!info->blocks)
	{
		info->err_msg = "Memory exhausted";
		return -1;
	}

/* Copy the temporary list to the final block list. */
	for (loop = blocks; loop; loop = loop->next)
	{
		if (loop->backup)
		{
			info->blocks[count].firstsector = loop->sector;
			info->blocks[count].sectors = loop->next->sector - loop->sector;
			info->nsectors += info->blocks[count].sectors;
			count++;
		}
	}
	return 0;
}

/*****************************************************************/
/* Scan the inode table and generate a list of blocks to backup. */
static struct blocklist *
backup_scan_inode_blocks (struct backup_info *info)
{
	uint64_t loop, loop2, loop3;
	int ninodes = mfs_inode_count (info->mfs);
	struct blocklist *blocks[32];
	uint64_t partstart[32];
	struct blocklist *pool = NULL;
	uint64_t highest = 0;

	bzero (blocks, sizeof (blocks));

	for (loop = 0, loop3 = 0; (loop2 = mfs_volume_size (info->mfs, loop)); loop += loop2, loop3++)
	{
		partstart[loop3] = loop;
		blocks[loop3] = calloc (sizeof (**blocks), 1);
		if (!blocks[loop3])
		{
			while (loop3 > 0)
			{
				loop3--;
				free (blocks[loop3]);
				info->err_msg = "Memory exhausted";
				return 0;
			}
		}
	}
	partstart[loop3] = ~0;
	blocks[loop3] = 0;

/* Add inodes. */
	for (loop = 0; loop < ninodes; loop++)
	{
		mfs_inode *inode = mfs_read_inode (info->mfs, loop);

		if (inode)
		{
/* If it a stream, treat it specially. */
			if (inode->type == tyStream)
			{
				unsigned int streamsize;

				if (info->back_flags & (BF_THRESHTOT | BF_STREAMTOT))
					streamsize = intswap32 (inode->blocksize) / 512 * intswap32 (inode->size);
				else
					streamsize = intswap32 (inode->blocksize) / 512 * intswap32 (inode->blockused);

/* Only backup streams that are smaller than the threshhold. */
				if (streamsize > 0 && (((info->back_flags & BF_THRESHSIZE) && streamsize < info->thresh) || (!(info->back_flags & BF_THRESHSIZE) && intswap32 (inode->fsid) <= info->thresh)))
				{
/* Add all blocks. */

					if ((info->back_flags & (BF_THRESHTOT | BF_STREAMTOT)) == BF_THRESHTOT)
						streamsize = intswap32 (inode->blocksize) / 512 * intswap32 (inode->blockused);

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

						if (thiscount > streamsize)
							thiscount = streamsize;

#if DEBUG
						fprintf (stderr, "Inode %d: ", intswap32 (inode->fsid));
#endif

						if (backup_add_block (blocks, partstart, &pool, thissector, thiscount) != 0)
						{
							free_block_list_array (blocks);
							free_block_list (&pool);
							free (inode);
							info->err_msg = "Memory exhausted";
							return 0;
						}
						streamsize -= thiscount;

						if (streamsize == 0)
							break;

						if (highest < thiscount + thissector)
							highest = thissector + thiscount;
					}

				}
			}
			else
			{
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
			}
			free (inode);
		}
	}

// Make sure all needed data is present.
	if (info->back_flags & BF_TRUNCATED)
	{
		uint64_t set_size = mfs_volume_set_size (info->mfs);
		if (highest > set_size)
		{
			info->err_msg = "Required data at %ld beyond end of the device (%ld)";
			info->err_arg1 = (int64_t)(size_t)highest;
			info->err_arg2 = (int64_t)(size_t)set_size;

			free_block_list_array (blocks);
			free_block_list (&pool);

			return 0;
		}
	}

	if (info->back_flags & BF_SHRINK)
	{
		zone_header *hdr = 0;

		while ((hdr = mfs_next_zone (info->mfs, hdr)) != 0)
		{
			if (mfs_is_64bit (info->mfs))
			{
#if DEBUG
				fprintf (stderr, "Checking zone at %lld of type %d for region %lld-%lld\n", intswap64 (hdr->z64.sector), intswap32 (hdr->z64.type), intswap64 (hdr->z64.first), intswap64 (hdr->z64.last));
#endif
				if (intswap32 (hdr->z64.type) != ztMedia)
				{
					if (intswap64 (hdr->z64.sector) + intswap32 (hdr->z64.length) > highest)
						highest = intswap64 (hdr->z64.sector) + intswap32 (hdr->z64.length);
					if (intswap64 (hdr->z64.last) > highest)
						highest = intswap64 (hdr->z64.last);
				}
			}
			else
			{
#if DEBUG
				fprintf (stderr, "Checking zone at %" PRIu32 " of type %d for region %" PRId32 "-%" PRId32 "\n", intswap32 (hdr->z32.sector), intswap32 (hdr->z32.type), intswap32 (hdr->z32.first), intswap32 (hdr->z32.last));
#endif
				if (intswap32 (hdr->z32.type) != ztMedia)
				{
					if (intswap32 (hdr->z32.sector) + intswap32 (hdr->z32.length) > highest)
						highest = intswap32 (hdr->z32.sector) + intswap32 (hdr->z32.length);
					if (intswap32 (hdr->z32.last) > highest)
						highest = intswap32 (hdr->z32.last);
				}
			}
		}
	}

/* Put in the whole volumes. */
	for (loop = 0, loop3 = 1; (loop2 = mfs_volume_size (info->mfs, loop)); loop += loop2, loop3 ^= 1)
	{
		if (loop3)
		{
			if ((info->back_flags & BF_SHRINK) && loop >= highest)
			{
#if DEBUG
				fprintf (stderr, "Truncating MFS at %" PRId64 "\n", loop);
#endif
				break;
			}
			if (backup_add_block (blocks, partstart, &pool, loop, loop2) != 0)
			{
				free_block_list_array (blocks);
				free_block_list (&pool);
				info->err_msg = "Memory exhausted";
				return 0;
			}
		}
		if (info->back_flags & BF_SHRINK)
			info->nmfs++;
	}

/* Free the data. */
	free_block_list (&pool);

	return block_list_array_concat (blocks);
}

/*************************************/
/* Initializes the backup structure. */
struct backup_info *
init_backup_v1 (char *device, char *device2, int flags)
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

	info->format = bfV1;

	info->crc = ~0;
	info->state_machine = &backup_v1;

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

/***********************************/
/* Generic handler for header data */
enum backup_state_ret
backup_write_header (struct backup_info *info, void *data, unsigned size, unsigned *consumed, void *src, unsigned total, unsigned datasize);
/* Defined in backup.c */

/**************************/
/* Scan MFS for v1 backup */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_scan_mfs_v1 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	struct blocklist *blocks;

	if ((add_partitions_to_backup_info (info, info->hda)) != 0)
	{
		return bsError;
	}

	blocks = backup_scan_inode_blocks (info);
	if (!blocks)
	{
		free (info->parts);
		return bsError;
	}

	if (backup_add_blocks_to_backup_info (info, blocks) != 0)
	{
		free_block_list (&blocks);
		free (info->parts);
		return bsError;
	}
	free_block_list (&blocks);

	if (add_mfs_partitions_to_backup_info (info) != 0) {
		free (info->parts);
		free (info->blocks);
		return bsError;
	}

	/* One for the boot block, one for the backup header, and however many */
	/* for the backup description */
	info->nsectors += (info->nblocks * sizeof (struct backup_block) + info->nparts * sizeof (struct backup_partition) + info->nmfs * sizeof (struct backup_partition) + 511) / 512 + 2;

	return bsNextState;
}

/*******************/
/* Begin v3 backup */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block (Always 0 for v1 backup begin) */
enum backup_state_ret
backup_state_begin_v1 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	struct backup_head *head = data;

	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	head->magic = TB_MAGIC;
	head->flags = info->back_flags;
	head->nsectors = info->nsectors;
	head->nparts = info->nparts;
	head->nblocks = info->nblocks;
	head->mfspairs = info->nmfs;

	info->shared_val1 = 0;
	*consumed = 1;

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
/* Add block list to backup. */
/* state_val1 = offset of last copied block */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
backup_state_info_blocks_v1 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	return backup_write_header (info, data, size, consumed, info->blocks, info->nblocks, sizeof (struct backup_block));
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

/********************/
/* Backup zone maps */
/* state_val1 = current block */
/* state_val2 = current sector within block */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
backup_state_blocks_v1 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	if (size == 0)
	{
		info->err_msg = "Internal error: Backup buffer full";
		return bsError;
	}

	while (info->state_val1 < info->nblocks)
	{
		while (info->state_val2 < info->blocks[info->state_val1].sectors)
		{
			int tocopy = info->blocks[info->state_val1].sectors - info->state_val2;
			int nread;

			if (tocopy > size - *consumed)
				tocopy = size - *consumed;

			/* If count is 0 it's because size is 0, so request more data */
			/* It is impossible for count to be initialized to 0 due to the */
			/* loop condition. */
			if (tocopy == 0)
			{
				return bsMoreData;
			}

			nread = mfs_read_data (info->mfs, (char *)data + *consumed * 512, info->blocks[info->state_val1].firstsector + info->state_val2, tocopy);
			if (nread < 512)
			{
				return bsError;
			}

			*consumed += nread / 512;
			info->state_val2 += nread / 512;
		}

		/* Breaking out of the loop without a return means the block is */
		/* done, so move on to the start of the next block */
		info->state_val1++;
		info->state_val2 = 0;
	}

	/* Breaking out of the loop without a return means all the blocks are */
	/* done, so report the done status */
	return bsNextState;
}

backup_state_handler backup_v1 = {
	backup_state_scan_mfs_v1,				// bsScanMFS
	backup_state_begin_v1,					// bsBegin
	backup_state_info_partitions,			// bsInfoPartition
	backup_state_info_blocks_v1,			// bsInfoBlocks
	backup_state_info_mfs_partitions,		// bsInfoMFSPartitions
	NULL,									// bsInfoZoneMaps
	NULL,									// bsInfoExtra
	backup_state_info_end,					// bsInfoEnd
	backup_state_boot_block,				// bsBootBlock
	backup_state_partitions,				// bsPartitions
	NULL,									// bsMFSInit
	backup_state_blocks_v1,					// bsBlocks
	NULL,									// bsVolumeHeader
	NULL,									// bsTransactionLog
	NULL,									// bsUnkRegion
	NULL,									// bsMfsReinit
	NULL,									// bsInodes
	NULL									// bsComplete
};
