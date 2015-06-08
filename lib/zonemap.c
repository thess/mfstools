#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/param.h>
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#ifdef HAVE_LINUX_UNISTD_H
#include <linux/unistd.h>
#endif

#include "mfs.h"
#include "macpart.h"
#include "log.h"

zone_header *
mfs_next_zone (struct mfs_handle *mfshnd, zone_header *cur)
{
	struct zone_map *loop = mfshnd->loaded_zones;

	if (!cur && loop)
		return loop->map;

	while (loop && loop->map != cur)
		loop = loop->next_loaded;

	loop = loop->next_loaded;
	if (loop)
		return loop->map;

	return 0;
}

/**********************************/
/* Estimate size of MFS in hours. */
unsigned int
mfs_sa_hours_estimate (struct mfs_handle *mfshnd)
{
	uint64_t sectors = mfshnd->zones[ztMedia].size;

	if (sectors > 72 * 1024 * 1024 * 2)
		sectors -= 12 * 1024 * 1024 * 2;
	else if (sectors > 14 * 1024 * 1024 * 2)
		sectors -= (sectors - 14 * 1024 * 1024 * 2) / 4;

	return (unsigned int) (sectors / SABLOCKSEC);
}

/*****************************************************************************/
/* Return the count of inodes.  Each inode is 2 sectors, so the count is the */
/* size of the inode zone maps divided by 2. */
unsigned int
mfs_inode_count (struct mfs_handle *mfshnd)
{
	return mfshnd->zones[ztInode].size / 2;
}

/****************************************/
/* Find the sector number for an inode. */
uint64_t
mfs_inode_to_sector (struct mfs_handle *mfshnd, unsigned int inode)
{
	struct zone_map *cur;
	uint64_t sector = inode * 2;

/* Don't bother if it's not a valid inode. */
	if (inode >= mfs_inode_count (mfshnd))
	{
		return 0;
	}

	if (mfshnd->is_64)
	{
/* Loop through each inode map, seeing if the current inode is within it. */
		for (cur = mfshnd->zones[ztInode].next; cur; cur = cur->next)
		{
			if (sector < intswap64 (cur->map->z64.size))
			{
				return (sector + intswap64 (cur->map->z64.first));
			}

/* If not, subtract the size so the inode sector offset is now relative to */
/* the next inode zone. */
			sector -= intswap64 (cur->map->z64.size);
		}
	}
	else
	{
/* Loop through each inode map, seeing if the current inode is within it. */
		for (cur = mfshnd->zones[ztInode].next; cur; cur = cur->next)
		{
			if (sector < intswap32 (cur->map->z32.size))
			{
				return (sector + intswap32 (cur->map->z32.first));
			}

/* If not, subtract the size so the inode sector offset is now relative to */
/* the next inode zone. */
			sector -= intswap32 (cur->map->z32.size);
		}
	}

/* This should never happen. */
	mfshnd->err_msg = "Inode %d out of bounds";
	mfshnd->err_arg1 = inode;
	return 0;
}

static inline struct zone_map *
mfs_zone_for_block (struct mfs_handle *mfshnd, uint64_t sector, uint64_t size)
{
	struct zone_map *zone;

	if (mfshnd->is_64)
	{
	/* Find the zone to update based on the start sector */
		for (zone = mfshnd->loaded_zones; zone; zone = zone->next_loaded)
		{
			if (sector >= intswap64 (zone->map->z64.first) && sector <= intswap64 (zone->map->z64.last))
				break;
		}
	}
	else
	{
	/* Find the zone to update based on the start sector */
		for (zone = mfshnd->loaded_zones; zone; zone = zone->next_loaded)
		{
			if (sector >= intswap32 (zone->map->z32.first) && sector <= intswap32 (zone->map->z32.last))
				break;
		}
	}

	if (!zone)
	{
		mfshnd->err_msg = "Sector %u out of bounds for zone map";
		mfshnd->err_arg1 = (int64_t) sector;
		return NULL;
	}

	if ((mfshnd->is_64 && sector + size - 1 > intswap64 (zone->map->z64.last)) ||
	    (!mfshnd->is_64 && sector + size - 1 > intswap32 (zone->map->z32.last)))
	{
		mfshnd->err_msg = "Sector %u size %d crosses zone map boundry";
		mfshnd->err_arg1 = (int64_t) sector;
		mfshnd->err_arg2 = (int64_t) size;
		return NULL;
	}
	
	if ( (mfshnd->is_64 && (sector - intswap64 (zone->map->z64.first)) % size) ||
	     (!mfshnd->is_64 && (sector - intswap32 (zone->map->z32.first)) % size))
	{
		mfshnd->err_msg = "Sector %u size %d not aligned with zone map";
		mfshnd->err_arg1 = (int64_t) sector;
		mfshnd->err_arg2 = (int64_t) size;
		return NULL;
	}

	return zone;
}

/************************************************************************/
/* Return the state of a bit in a bitmap */
static int
mfs_zone_map_bit_state_get (bitmap_header *bitmap, unsigned int bit)
{
	unsigned int *mapints = (unsigned int *)(bitmap + 1);

	/* Find the int that contains this bit */
	mapints += bit / 32;
	
	/* Adjust the bit to be within this int */
	/* MSB is bit 0, LSB is bit 31, etc */
	bit = 31 & ~bit;
	
	/* Make it the actual bit */
	bit = intswap32 (1 << bit);
	
	/* return it as 1 or 0 */
	return (bit & *mapints) ? 1 : 0;
}

/************************************************************************/
/* Set the state of a bit in a bitmap */
static void
mfs_zone_map_bit_state_set (bitmap_header *bitmap, unsigned int bit)
{
	unsigned int *mapints = (unsigned int *)(bitmap + 1);

	/* Find the int that contains this bit */
	mapints += bit / 32;
	
	/* Adjust the bit to be within this int */
	/* MSB is bit 0, LSB is bit 31, etc */
	bit = 31 & ~bit;
	
	/* Make it the actual bit */
	bit = intswap32 (1 << bit);

	*mapints |= bit;
}

/************************************************************************/
/* Clear the state of a bit in a bitmap */
static void
mfs_zone_map_bit_state_clear (bitmap_header *bitmap, unsigned int bit)
{
	unsigned int *mapints = (unsigned int *)(bitmap + 1);

	/* Find the int that contains this bit */
	mapints += bit / 32;
	
	/* Adjust the bit to be within this int */
	/* MSB is bit 0, LSB is bit 31, etc */
	bit = 31 & ~bit;
	
	/* Make it the actual bit */
	bit = intswap32 (1 << bit);

	*mapints &= ~bit;
}

/************************************************************************/
/* Get the current state of a specifc block in the zone map */
/* This checks only for the explicit size, not that the block could be part */
/* of a larger free block, for example. */
int
mfs_zone_map_block_state (struct mfs_handle *mfshnd, uint64_t sector, uint64_t size)
{
	int order;
	unsigned int minalloc;
	unsigned int numbitmaps;
	uint64_t first;

	struct zone_map *zone = mfs_zone_for_block (mfshnd, sector, size);
	if (!zone)
		return -1;

	if (mfshnd->is_64)
	{
		minalloc = intswap32 (zone->map->z64.min);
		numbitmaps = intswap32 (zone->map->z64.num);
		first = intswap64 (zone->map->z64.first);
	}
	else
	{
		minalloc = intswap32 (zone->map->z32.min);
		numbitmaps = intswap32 (zone->map->z32.num);
		first = intswap32 (zone->map->z32.first);
	}

	/* Find which level of bitmaps this block is on */
	for (order = 0; order < numbitmaps; order++)
	{
		if ((minalloc << order) >= size)
			break;
	}

	/* One last set of sanity checks on the size */
	if (order >= numbitmaps)
	{
		/* Should be caught by above check that it crosses the zone map boundry */
		mfshnd->err_msg = "Sector %u size %d too large for zone map";
		mfshnd->err_arg1 = sector;
		mfshnd->err_arg2 = size;
		return -1;
	}

	if (((uint64_t)minalloc << order) != size)
	{
		mfshnd->err_msg = "Sector %u size %d not multiple of zone map allocation";
		mfshnd->err_arg1 = sector;
		mfshnd->err_arg2 = size;
		return -1;
	}

	/* Return the current state as 1 or 0 */
	return mfs_zone_map_bit_state_get (zone->bitmaps[order], ((sector - first) >> order) / minalloc)? 1: 0;
}

/************************************************************************/
/* Allocate or free a block out of the bitmap */
int
mfs_zone_map_update (struct mfs_handle *mfshnd, uint64_t sector, uint64_t size, unsigned int state, unsigned int logstamp)
{
	struct zone_map *zone;
	int order;
	int orderfree;
	unsigned int mapbit;

	unsigned int minalloc;
	unsigned int numbitmaps;

	zone = mfs_zone_for_block (mfshnd, sector, size);
	if (!zone)
		return 0;

	/* Check the logstamp to see if this has already been updated...  */
	/* Sure, there could be some integer wrap... */
	/* After a hundred or so years */
	if ( (mfshnd->is_64 && logstamp <= intswap32 (zone->map->z64.logstamp)) ||
	     (!mfshnd->is_64 && logstamp <= intswap32 (zone->map->z32.logstamp)))
		return 1;

	/* From this point on, it is assumed that the request makes sense */
	/* For example, no request to free a block that is partly free, or to */
	/* allocate a block that is partly allocated */
	/* Allocating a block that is fully allocated or freeing a block that */
	/* is fully free is fine, however. */

	if (mfshnd->is_64)
	{
		minalloc = intswap32 (zone->map->z64.min);
		numbitmaps = intswap32 (zone->map->z64.num);
	}
	else
	{
		minalloc = intswap32 (zone->map->z32.min);
		numbitmaps = intswap32 (zone->map->z32.num);
	}

	/* Find which level of bitmaps this block is on */
	for (order = 0; order < numbitmaps; order++)
	{
		if (((uint64_t)minalloc << order) >= size)
			break;
	}

	/* One last set of sanity checks on the size */
	if (order >= numbitmaps)
	{
		/* Should be caught by above check that it crosses the zone map boundry */
		mfshnd->err_msg = "Sector %u size %d too large for zone map";
		mfshnd->err_arg1 = sector;
		mfshnd->err_arg2 = size;
		return 0;
	}

	if (((uint64_t)minalloc << order) != size)
	{
		mfshnd->err_msg = "Sector %u size %d not multiple of zone map allocation";
		mfshnd->err_arg1 = sector;
		mfshnd->err_arg2 = size;
		return 0;
	}

	if (mfshnd->is_64)
	{
		mapbit = (sector - intswap64 (zone->map->z64.first)) / ((uint64_t)minalloc << order);
	}
	else
	{
		mapbit = (sector - intswap32 (zone->map->z32.first)) / ((uint64_t)minalloc << order);
	}

	/* Find the first free bit */
	for (orderfree = order; orderfree < numbitmaps; orderfree++)
	{
		if (mfs_zone_map_bit_state_get (zone->bitmaps[orderfree], mapbit >> (orderfree - order)))
			break;
	}

	/* Free bit not found */
	if (orderfree >= numbitmaps)
		orderfree = -1;

	if (state)
	{
		/* Free a block */
		if (orderfree >= 0)
		{
			/* Already free */
			return 1;
		}

		/* Set the bit to mark it free */
		if (mfshnd->is_64)
		{
			zone->map->z64.free = intswap64 (intswap64 (zone->map->z64.free) + size);
		}
		else
		{
			zone->map->z32.free = intswap32 (intswap32 (zone->map->z32.free) + size);
		}
		mfs_zone_map_bit_state_set (zone->bitmaps[order], mapbit);
		zone->bitmaps[order]->freeblocks = intswap32 (intswap32 (zone->bitmaps[order]->freeblocks) + 1);

		/* Coalesce neighboring free bits into larger blocks */
		while (order + 1 < numbitmaps &&
			mfs_zone_map_bit_state_get (zone->bitmaps[order], mapbit ^ 1))
		{
			/* Clear the bit and it's neighbor in the bitmap */
			mfs_zone_map_bit_state_clear (zone->bitmaps[order], mapbit);
			mfs_zone_map_bit_state_clear (zone->bitmaps[order], mapbit ^ 1);
			zone->bitmaps[order]->freeblocks = intswap32 (intswap32 (zone->bitmaps[order]->freeblocks) - 2);

			/* Move on to the next bitmap */
			order++;
			mapbit >>= 1;

			/* Set the single bit in the next bitmap that represents both bits cleared */
			mfs_zone_map_bit_state_set (zone->bitmaps[order], mapbit);
			zone->bitmaps[order]->freeblocks = intswap32 (intswap32 (zone->bitmaps[order]->freeblocks) + 1);
		}

		/* Mark it dirty */
		zone->dirty = 1;

		/* Done! */
		return 1;
	}
	else
	{
		/* Allocate a block */
		if (orderfree < 0)
		{
			/* Already allocated */
			return 1;
		}

		/* Set all the bit as free that are left over from borrowing from larger chunks */
		while (order < orderfree)
		{
			mfs_zone_map_bit_state_set (zone->bitmaps[order], mapbit ^ 1);
			zone->bitmaps[order]->freeblocks = intswap32 (intswap32 (zone->bitmaps[order]->freeblocks) + 1);

			/* Move on to the next bitmap */
			order++;
			mapbit >>= 1;
		}

		/* Clear the bit to mark it allocated */
		if (mfshnd->is_64)
		{
			zone->map->z64.free = intswap64 (intswap64 (zone->map->z64.free) - size);
		}
		else
		{
			zone->map->z32.free = intswap32 (intswap32 (zone->map->z32.free) - size);
		}
		mfs_zone_map_bit_state_clear (zone->bitmaps[order], mapbit);
		zone->bitmaps[order]->freeblocks = intswap32 (intswap32 (zone->bitmaps[order]->freeblocks) - 1);
		
		/* Set the last bit allocated - bit numbering is 1 based here (Or maybe it's next bit after last allocated) */
		/* Hypothesis: This is used as a base for the search for next free bit */
		zone->bitmaps[order]->last = intswap32 (mapbit + 1);

		/* Mark it dirty */
		zone->dirty = 1;

		/* Done! */
		return 1;
	}
}

/************************************************************************/
/* Clean up storage used by tracking changes */
static void
mfs_zone_map_clear_changes (struct mfs_handle *mfshnd, struct zone_map *zone)
{
	int numbitmaps;
	int loop;

	if (mfshnd->is_64)
	{
		numbitmaps = intswap32 (zone->map->z64.num);
	}
	else
	{
		numbitmaps = intswap32 (zone->map->z32.num);
	}

	for (loop = 0; loop < numbitmaps; loop++)
	{
		while (zone->changed_runs[loop])
		{
			struct zone_changed_run *cur = zone->changed_runs[loop];
			zone->changed_runs[loop] = cur->next;
			free (cur);
		}
		zone->changes[loop].allocated = 0;
		zone->changes[loop].freed = 0;
	}
}

void
mfs_zone_map_commit (struct mfs_handle *mfshnd, unsigned int logstamp)
{
	struct zone_map *zone;

	for (zone = mfshnd->loaded_zones; zone; zone = zone->next_loaded)
	{
		if (zone->dirty)
		{
			if (mfshnd->is_64)
			{
				zone->map->z64.logstamp = intswap32 (logstamp);
			}
			else
			{
				zone->map->z32.logstamp = intswap32 (logstamp);
			}

			mfs_zone_map_clear_changes (mfshnd, zone);
		}
	}
}

/************************************************************************/
/* Write changed zone maps back to disk */
int
mfs_zone_map_sync (struct mfs_handle *mfshnd, unsigned int logstamp)
{
	struct zone_map *zone;

	for (zone = mfshnd->loaded_zones; zone; zone = zone->next_loaded)
	{
		if (zone->dirty)
		{
			int towrite;
			uint64_t sector, sbackup;

			if (mfshnd->is_64)
			{
				zone->map->z64.logstamp = intswap32 (logstamp);
				towrite = intswap32 (zone->map->z64.length);
				sector = intswap64 (zone->map->z64.sector);
				sbackup = intswap64 (zone->map->z64.sbackup);
				MFS_update_crc (zone->map, towrite * 512, zone->map->z64.checksum);
			}
			else
			{
				zone->map->z32.logstamp = intswap32 (logstamp);
				towrite = intswap32 (zone->map->z32.length);
				sector = intswap32 (zone->map->z32.sector);
				sbackup = intswap32 (zone->map->z32.sbackup);
				MFS_update_crc (zone->map, towrite * 512, zone->map->z32.checksum);
			}

			if (mfsvol_write_data (mfshnd->vols, zone->map, sector, towrite) < 0)
			{
				return -1;
			}
			if (mfsvol_write_data (mfshnd->vols, zone->map, sbackup, towrite) < 0)
			{
				return -1;
			}

			mfs_zone_map_clear_changes (mfshnd, zone);
		}
	}
	return 1;
}

/************************************************************************/
/* Return how big a new zone map would need to be for a given number of */
/* allocation blocks. */
int
mfs_new_zone_map_size (struct mfs_handle *mfshnd, unsigned int blocks)
{
/* I don't remember what the original +4 was for, but the +24 is because */
/* apparently TiVo likes a little breathing room at the end, and throws */
/* a tantrum if it doesn't have it.  This happens when the zone map has */
/* 18 levels of bitmaps. */
	int size = 4 + 28;
	int order = 0;

	if (mfshnd->is_64)
	{
		size += sizeof (zone_header_64);
	}
	else
	{
		size += sizeof (zone_header_32);
	}

/* Figure out the first order of 2 that is needed to have at least 1 bit for */
/* every block. */
	while ((1 << order) < blocks)
	{
		order++;
	}

/* Increment it by one for loops and math. */
	order++;

/* Start by adding in the sizes for all the bitmap headers. */
	size += (sizeof (bitmap_header) + sizeof (bitmap_header *)) * (order);

/* Estimate the size of the bitmap table for each order of 2. */
	while (order--)
	{
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
int
mfs_new_zone_map (struct mfs_handle *mfshnd, uint64_t sector, uint64_t backup, uint64_t first, uint64_t size, unsigned int minalloc, zone_type type, unsigned int fsmem_base)
{
	uint64_t blocks = size / minalloc;
	int zonesize = (mfs_new_zone_map_size (mfshnd, blocks) + 511) & ~511;
	unsigned char *buf;
	zone_header *zone;
	zone_header *last;
	struct zone_map *cur;
	int loop;
	int order = 0;
	unsigned int *fsmem_pointers;
	unsigned int *curofs;

/* Truncate the size to the nearest allocation sized block. */
	size -= size % minalloc;

/* Find the last loaded zone. */
	for (cur = mfshnd->loaded_zones; cur && cur->next_loaded; cur = cur->next_loaded);

	if (cur)
	{
		last = cur->map;
	}
	else
	{
		last = NULL;
	}

/* To get the pointer into fsmem, start with the first pointer from the */
/* previous zone map.  Subtract the header and all the fsmem pointers from */
/* it, and thats the base for that map.  Now add in all the sectors from that */
/* map, plus 1 extra and 8 bytes. */
/* It looks like it's more complicated than that now...  But it also looks */
/* like the numbers don't matter and TiVo will correct them itself because */
/* this still works. */
	if (!fsmem_base)
	{
		if (!last)
		{
			mfshnd->err_msg = "Attempt to create first zone without fsmem base";
			return -1;
		}

		if (mfshnd->is_64)
		{
			fsmem_base = intswap32 (*(unsigned int *) (&last->z64 + 1)) - (sizeof (last->z64) + intswap32 (last->z64.num) * 4) + intswap32 (last->z64.length) * 512 + 512 + 8;
		}
		else
		{
			fsmem_base = intswap32 (*(unsigned int *) (&last->z32 + 1)) - (sizeof (last->z32) + intswap32 (last->z32.num) * 4) + intswap32 (last->z32.length) * 512 + 512 + 8;
		}
	}

	buf = malloc (zonesize);

	if (!buf)
	{
		return -1;
	}

/* Fill in everything with lots and lots of aaaaaaaa for a vegetarian MFS */
	memset (buf, 0xaa, zonesize);

/* Figure out the order of the blocks count. */
	while ((1 << order) < blocks)
	{
		order++;
	}

	order++;

	zone = (zone_header *) buf;

/* Fill in the header values. */
	if (mfshnd->is_64)
	{
		zone->z64.sector = intswap64 (sector);
		zone->z64.sbackup = intswap64 (backup);
		zone->z64.length = intswap32 (zonesize / 512);
		zone->z64.next_sector = 0;
		zone->z64.next_length = 0;
		zone->z64.next_size = 0;
		zone->z64.next_min = 0;
		zone->z64.type = intswap32 (type);
		zone->z64.logstamp = intswap32 (mfs_log_last_sync (mfshnd));
		zone->z64.checksum = intswap32 (0xdeadf00d);
		zone->z64.first = intswap64 (first);
		zone->z64.last = intswap64 (first + size - 1);
		zone->z64.size = intswap64 (size);
		zone->z64.min = intswap32 (minalloc);
		zone->z64.free = intswap64 (size);
		zone->z64.zero = 0;
		zone->z64.num = intswap32 (order);

/* Grab a pointer to the array where fsmem pointers will go. */
		fsmem_pointers = (unsigned int *) (&zone->z64 + 1);
		curofs = (unsigned int *) (&zone->z64 + 1) + order;
	}
	else
	{
		zone->z32.sector = intswap32 (sector);
		zone->z32.sbackup = intswap32 (backup);
		zone->z32.length = intswap32 (zonesize / 512);
		zone->z32.next.sector = 0;
		zone->z32.next.length = 0;
		zone->z32.next.size = 0;
		zone->z32.next.min = 0;
		zone->z32.type = intswap32 (type);
		zone->z32.logstamp = intswap32 (mfs_log_last_sync (mfshnd));
		zone->z32.checksum = intswap32 (0xdeadf00d);
		zone->z32.first = intswap32 (first);
		zone->z32.last = intswap32 (first + size - 1);
		zone->z32.size = intswap32 (size);
		zone->z32.min = intswap32 (minalloc);
		zone->z32.free = intswap32 (size);
		zone->z32.zero = 0;
		zone->z32.num = intswap32 (order);

/* Grab a pointer to the array where fsmem pointers will go. */
		fsmem_pointers = (unsigned int *) (&zone->z32 + 1);
		curofs = (unsigned int *) (&zone->z32 + 1) + order;
	}

/* Fill in the allocation bitmaps.  This is simpler than it sounds.  The */
/* bitmaps are regressing from the full 1 bit = min allocation block up to */
/* 1 bit = entire drive.  A bit means the block is free.  Free blocks are */
/* represented by the largest bit possible.  In a perfect power of 2, a */
/* completely free table is represented by 1 bit in the last table.  This */
/* may sound complex, but it's really easy to fill in an empty table. */
/* While filling in the size values for the headers for each bitmap, any */
/* time you have an odd number of active bits, set the last one, because */
/* it is not represented by any larger bits. */
	for (loop = 0; order-- > 0; loop++, blocks /= 2)
	{
		int nbits;
		int nints;
		bitmap_header *bitmap = (bitmap_header *) curofs;
		fsmem_pointers[loop] = intswap32 (fsmem_base + (char *) curofs - (char *) zone);

/* Set in the basic, constant header values.  The nbits is how many bits */
/* there are in the table, including extra inactive bits padding to the */
/* next power of 2.  The nints represents how many ints those bits take up. */
		nbits = 1 << order;
		bitmap->nbits = intswap32 (nbits);
		nints = (nbits + 31) / 32;
		bitmap->nints = intswap32 (nints);

/* Clear all the bits by default. */
		memset (curofs + sizeof (*bitmap) / 4, 0, nints * 4);

/* Set the rest of the header.  The */
/* reason to set the last bit is that this is the last table that block */
/* will be represented in, so it needs to be marked free here.  The next */
/* table's bit is too big it overflows into the inactive area, so is itself */
/* inactive. */
		if (blocks & 1)
		{
			bitmap->freeblocks = intswap32 (1);
			curofs[4 + (blocks - 1) / 32] = intswap32 (1 << (31 - (blocks - 1) % 32));
		}
		else
		{
			bitmap->freeblocks = 0;
		}
		bitmap->last = 0;

/* Step past this table. */
		curofs += sizeof (*bitmap) / 4 + (nbits + 57) / 32;
	}

	if (mfshnd->is_64)
	{
/* Copy the pointer into the current end of the zone list. */
		if (last)
		{
			last->z64.next_sector = zone->z64.sector;
			last->z64.next_sbackup = zone->z64.sbackup;
			last->z64.next_length = zone->z64.length;
			last->z64.next_size = zone->z64.size;
			last->z64.next_min = zone->z64.min;
/* Update the CRC in the new zone, as well as the previous tail, since it's */
/* next pointer was updated. */
			MFS_update_crc (last, intswap32 (last->z64.length) * 512, last->z64.checksum);
		}
		else
		{
			mfshnd->vol_hdr.v64.zonemap.sector = zone->z64.sector;
			mfshnd->vol_hdr.v64.zonemap.sbackup = zone->z64.sbackup;
			mfshnd->vol_hdr.v64.zonemap.length = intswap64 (intswap32 (zone->z64.length));
			mfshnd->vol_hdr.v64.zonemap.size = zone->z64.size;
			mfshnd->vol_hdr.v64.zonemap.min = intswap64 (intswap32 (zone->z64.min));
		}
		MFS_update_crc (zone, intswap32 (zone->z64.length) * 512, zone->z64.checksum);
	}
	else
	{
/* Copy the pointer into the current end of the zone list. */
		if (last)
		{
			last->z32.next.sector = zone->z32.sector;
			last->z32.next.sbackup = zone->z32.sbackup;
			last->z32.next.length = zone->z32.length;
			last->z32.next.size = zone->z32.size;
			last->z32.next.min = zone->z32.min;

/* Update the CRC in the new zone, as well as the previous tail, since it's */
/* next pointer was updated. */
			MFS_update_crc (last, intswap32 (last->z32.length) * 512, last->z32.checksum);
		}
		else
		{
			mfshnd->vol_hdr.v32.zonemap.sector = zone->z32.sector;
			mfshnd->vol_hdr.v32.zonemap.sbackup = zone->z32.sbackup;
			mfshnd->vol_hdr.v32.zonemap.length = zone->z32.length;
			mfshnd->vol_hdr.v32.zonemap.size = zone->z32.size;
			mfshnd->vol_hdr.v32.zonemap.min = zone->z32.min;
		}
		MFS_update_crc (zone, intswap32 (zone->z32.length) * 512, zone->z32.checksum);
	}

/* Write the changes, with the changes to live MFS last.  This should use */
/* the journaling facilities, but I don't know how. */
	mfsvol_write_data (mfshnd->vols, zone, sector, zonesize / 512);
	mfsvol_write_data (mfshnd->vols, zone, backup, zonesize / 512);
	if (last)
	{
		if (mfshnd->is_64)
		{
			mfsvol_write_data (mfshnd->vols, last, intswap64 (last->z64.sector), intswap32 (last->z64.length));
			mfsvol_write_data (mfshnd->vols, last, intswap64 (last->z64.sbackup), intswap32 (last->z64.length));
		}
		else
		{
			mfsvol_write_data (mfshnd->vols, last, intswap32 (last->z32.sector), intswap32 (last->z32.length));
			mfsvol_write_data (mfshnd->vols, last, intswap32 (last->z32.sbackup), intswap32 (last->z32.length));
		}
	}
	else
	{
		mfs_write_volume_header (mfshnd);
	}

	return 0;
}

uint64_t
mfs_volume_pair_app_size (struct mfs_handle *mfshnd, uint64_t blocks, unsigned int minalloc)
{
	if (minalloc == 0)
		minalloc = 0x800;

	// Make it twice as big as needed for some spare room
	return (2 + 4 * ((mfs_new_zone_map_size (mfshnd, blocks / minalloc) + 511) / 512) + MFS_PARTITION_ROUND - 1) & ~(MFS_PARTITION_ROUND - 1);
}

int
mfs_can_add_volume_pair (struct mfs_handle *mfshnd, char *app, char *media, unsigned int minalloc)
{
	struct zone_map *cur;

/* If no minalloc, make it default. */
	if (minalloc == 0)
	{
		minalloc = 0x800;
	}

/* Make sure the volumes being added don't overflow the 128 bytes. */
	if ((mfshnd->is_64 && strlen (mfshnd->vol_hdr.v64.partitionlist) + strlen (app) + strlen (media) + 3 >= sizeof (mfshnd->vol_hdr.v64.partitionlist)) ||
	    (!mfshnd->is_64 && strlen (mfshnd->vol_hdr.v32.partitionlist) + strlen (app) + strlen (media) + 3 >= sizeof (mfshnd->vol_hdr.v32.partitionlist)))
	{
		mfshnd->err_msg = "No space in volume list for new volumes";
		return -1;
	}

/* Make sure block 0 is writable.  It wouldn't do to get all the way to */
/* the end and not be able to update the volume header. */
	if (!mfsvol_is_writable (mfshnd->vols, 0))
	{
		mfshnd->err_msg = "Readonly volume set";
		return -1;
	}

/* Walk the list of zone maps to find the last loaded zone map. */
	for (cur = mfshnd->loaded_zones; cur && cur->next_loaded; cur = cur->next_loaded);

/* For cur to be null, it must have never been set. */
	if (!cur)
	{
		mfshnd->err_msg = "Zone maps not loaded";
		return -1;
	}

/* Check that the last zone map is writable.  This is needed for adding the */
/* new pointer. */
	if ((mfshnd->is_64 && !mfsvol_is_writable (mfshnd->vols, intswap64 (cur->map->z64.sector))) ||
	    (!mfshnd->is_64 && !mfsvol_is_writable (mfshnd->vols, intswap32 (cur->map->z32.sector))))
	{
		mfshnd->err_msg = "Readonly volume set";
		return -1;
	}

	return 0;
}

/***********************************************************************/
/* Add a new set of partitions to the MFS volume set.  In other words, */
/* mfsadd. */
int
mfs_add_volume_pair (struct mfs_handle *mfshnd, char *app, char *media, unsigned int minalloc)
{
	struct zone_map *cur;
	tpFILE *tpApp, *tpMedia;
	uint64_t appstart, mediastart;
	uint64_t appsize, mediasize, mapsize;
	char *tmp;
	char foo[512];

/* If no minalloc, make it default. */
	if (minalloc == 0)
	{
		minalloc = 0x800;
	}

/* Make sure the volumes being added don't overflow the 128 bytes. */
	if ((mfshnd->is_64 && strlen (mfshnd->vol_hdr.v64.partitionlist) + strlen (app) + strlen (media) + 3 >= sizeof (mfshnd->vol_hdr.v64.partitionlist)) ||
	    (!mfshnd->is_64 && strlen (mfshnd->vol_hdr.v32.partitionlist) + strlen (app) + strlen (media) + 3 >= sizeof (mfshnd->vol_hdr.v32.partitionlist)))
	{
		mfshnd->err_msg = "No space in volume list for new volumes";
		return -1;
	}

/* Make sure block 0 is writable.  It wouldn't do to get all the way to */
/* the end and not be able to update the volume header. */
	if (!mfsvol_is_writable (mfshnd->vols, 0))
	{
		mfshnd->err_msg = "Readonly volume set";
		return -1;
	}

/* Walk the list of zone maps to find the last loaded zone map. */
	for (cur = mfshnd->loaded_zones; cur && cur->next_loaded; cur = cur->next_loaded);

/* For cur to be null, it must have never been set. */
	if (!cur)
	{
		mfshnd->err_msg = "Zone maps not loaded";
		return -1;
	}

/* Check that the last zone map is writable.  This is needed for adding the */
/* new pointer. */
	if ((mfshnd->is_64 && !mfsvol_is_writable (mfshnd->vols, intswap64 (cur->map->z64.sector))) ||
	    (!mfshnd->is_64 && !mfsvol_is_writable (mfshnd->vols, intswap32 (cur->map->z32.sector))))
	{
		mfshnd->err_msg = "Readonly volume set";
		return -1;
	}

	tmp = mfsvol_device_translate (mfshnd->vols, app);
	if (!tmp)
		return -1;

	tpApp = tivo_partition_open (tmp, O_RDWR);
	if (!tpApp)
	{
		mfshnd->err_msg = "%s: %s";
		mfshnd->err_arg1 = (size_t) tmp;
		mfshnd->err_arg2 = (size_t) strerror (errno);
		return -1;
	}

	tmp = mfsvol_device_translate (mfshnd->vols, media);
	tpMedia = tivo_partition_open (tmp, O_RDWR);
	if (!tpMedia)
	{
		mfshnd->err_msg = "%s: %s";
		mfshnd->err_arg1 = (size_t) tmp;
		mfshnd->err_arg2 = (size_t) strerror (errno);
		return -1;
	}

	tivo_partition_close (tpApp);
	tivo_partition_close (tpMedia);

	appstart = mfsvol_add_volume (mfshnd->vols, app, O_RDWR);
	mediastart = mfsvol_add_volume (mfshnd->vols, media, O_RDWR);

	if (appstart < 0 || mediastart < 0)
	{
		mfshnd->err_msg = "Error adding new volumes to set";
		mfs_reinit (mfshnd, O_RDWR);
		return -1;
	}

	if (!mfsvol_is_writable (mfshnd->vols, appstart) || !mfsvol_is_writable (mfshnd->vols, mediastart))
	{
		mfshnd->err_msg = "Could not add new volumes writable";
		mfs_reinit (mfshnd, O_RDWR);
		return -1;
	}

	appsize = mfsvol_volume_size (mfshnd->vols, appstart);
	mediasize = mfsvol_volume_size (mfshnd->vols, mediastart);
	mapsize = (mfs_new_zone_map_size (mfshnd, mediasize / minalloc) + 511) / 512;

	if (mapsize * 2 + 2 > appsize)
	{
		mfshnd->err_msg = "New app size too small, need %d more bytes";
		mfshnd->err_arg1 = ((mapsize * 2 + 2 - appsize) * 512);
		mfs_reinit (mfshnd, O_RDWR);
		return -1;
	}

	if (mfs_new_zone_map (mfshnd, appstart + 1, appstart + appsize - mapsize - 1, mediastart, mediasize, minalloc, ztMedia, 0) < 0)
	{
		mfshnd->err_msg = "Failed initializing new zone map";
		mfs_reinit (mfshnd, O_RDWR);
		return -1;
	}

	if (mfshnd->is_64)
	{
		snprintf (foo, sizeof (mfshnd->vol_hdr.v64.partitionlist), "%s %s %s", mfshnd->vol_hdr.v64.partitionlist, app, media);
		foo[127] = 0;
		strcpy (mfshnd->vol_hdr.v64.partitionlist, foo);
		mfshnd->vol_hdr.v64.total_sectors = intswap64 (mfsvol_volume_set_size (mfshnd->vols));
	}
	else
	{
		snprintf (foo, sizeof (mfshnd->vol_hdr.v32.partitionlist), "%s %s %s", mfshnd->vol_hdr.v32.partitionlist, app, media);
		foo[127] = 0;
		strcpy (mfshnd->vol_hdr.v32.partitionlist, foo);
		mfshnd->vol_hdr.v32.total_sectors = intswap32 (mfsvol_volume_set_size (mfshnd->vols));
	}

	mfs_write_volume_header (mfshnd);
	mfs_cleanup_zone_maps(mfshnd);
	return mfs_load_zone_maps (mfshnd);
}

/******************************************/
/* Free the memory used by the zone maps. */
void
mfs_cleanup_zone_maps (struct mfs_handle *mfshnd)
{
	int loop;

	for (loop = 0; loop < ztMax; loop++)
	{
		while (mfshnd->zones[loop].next)
		{
			struct zone_map *map = mfshnd->zones[loop].next;

			mfs_zone_map_clear_changes (mfshnd, map);

			mfshnd->zones[loop].next = map->next;
			free (map->map);
			if (map->bitmaps)
				free (map->bitmaps);
			if (map->changed_runs)
				free (map->changed_runs);
			if (map->changes)
				free (map->changes);
			free (map);
		}
	}

	mfshnd->loaded_zones = NULL;
}

/*************************************************************/
/* Load a zone map from the drive and verify it's integrity. */
static zone_header *
mfs_load_zone_map (struct mfs_handle *mfshnd, uint64_t sector, uint64_t sbackup, uint32_t length)
{
	zone_header *hdr = calloc (length, 512);

	if (!hdr)
	{
		return NULL;
	}

/* Read the map. */
	mfsvol_read_data (mfshnd->vols, (unsigned char *) hdr, sector, length);

/* Verify the CRC matches. */
	if ((mfshnd->is_64 && !MFS_check_crc ((unsigned char *) hdr, length * 512, hdr->z64.checksum)) ||
	    (!mfshnd->is_64 && !MFS_check_crc ((unsigned char *) hdr, length * 512, hdr->z32.checksum)))
	{

/* If the CRC doesn't match, try the backup map. */
		mfsvol_read_data (mfshnd->vols, (unsigned char *) hdr, sbackup, length);

		if ((mfshnd->is_64 && !MFS_check_crc ((unsigned char *) hdr, length * 512, hdr->z64.checksum)) ||
		    (!mfshnd->is_64 && !MFS_check_crc ((unsigned char *) hdr, length * 512, hdr->z32.checksum)))
		{
			mfshnd->err_msg = "Zone map checksum error";
			free (hdr);
			return NULL;
		}
	}

	return hdr;
}

/***************************/
/* Load the zone map list. */
int
mfs_load_zone_maps (struct mfs_handle *mfshnd)
{
	uint64_t ptrsector;
	uint64_t ptrsbackup;
	uint32_t ptrlength;
	zone_header *cur;
	struct zone_map **loaded_head = &mfshnd->loaded_zones;
	struct zone_map **cur_heads[ztMax];
	int loop;
	
	if (mfshnd->is_64)
	{
		ptrsector = intswap64 (mfshnd->vol_hdr.v64.zonemap.sector);
		ptrsbackup = intswap64 (mfshnd->vol_hdr.v64.zonemap.sbackup);
		ptrlength = intswap64 (mfshnd->vol_hdr.v64.zonemap.length);
	}
	else
	{
		ptrsector = intswap32 (mfshnd->vol_hdr.v32.zonemap.sector);
		ptrsbackup = intswap32 (mfshnd->vol_hdr.v32.zonemap.sbackup);
		ptrlength = intswap32 (mfshnd->vol_hdr.v32.zonemap.length);
	}

/* Start clean. */
	mfs_cleanup_zone_maps (mfshnd);
	memset (mfshnd->zones, 0, sizeof (mfshnd->zones));

	for (loop = 0; loop < ztMax; loop++)
	{
		cur_heads[loop] = &mfshnd->zones[loop].next;
	}

	loop = 0;

	while (ptrsector && ptrsbackup != 0xdeadbeef && ptrlength)
	{
		struct zone_map *newmap;
		uint32_t *bitmap_ptrs;
		int loop2;
		int type;
		int numbitmaps;

/* Read the map, verify it's checksum. */
		cur = mfs_load_zone_map (mfshnd, ptrsector, ptrsbackup, ptrlength);

		if (!cur)
		{
			return -1;
		}

		if (mfshnd->is_64)
		{
			type = intswap32 (cur->z64.type);
			numbitmaps = intswap32 (cur->z64.num);
		}
		else
		{
			type = intswap32 (cur->z32.type);
			numbitmaps = intswap32 (cur->z32.num);
		}

		if (type < 0 || type >= ztMax)
		{
			mfshnd->err_msg = "Bad map type %d";
			mfshnd->err_arg1 = type;
			free (cur);
			return -1;
		}

		newmap = calloc (sizeof (*newmap), 1);
		if (!newmap)
		{
			mfshnd->err_msg = "Out of memory";
			free (cur);
			return -1;
		}
		
		if (numbitmaps)
		{
			newmap->bitmaps = calloc (sizeof (*newmap->bitmaps), numbitmaps);
			if (!newmap->bitmaps)
			{
				mfshnd->err_msg = "Out of memory";
				free (newmap);
				free (cur);
				return -1;
			}
		}
		else
		{
			newmap->bitmaps = NULL;
		}

/* Link it into the proper map type pool. */
		newmap->map = cur;
		*cur_heads[type] = newmap;
		cur_heads[type] = &newmap->next;

/* Get pointers to the bitmaps for easy access */
		if (numbitmaps != 0)
		{
			if (mfshnd->is_64)
			{
				bitmap_ptrs = (uint32_t *)(&cur->z64 + 1);
			}
			else
			{
				bitmap_ptrs = (uint32_t *)(&cur->z32 + 1);
			}
			newmap->bitmaps[0] = (bitmap_header *)&bitmap_ptrs[numbitmaps];
			for (loop2 = 1; loop2 < numbitmaps; loop2++)
			{
				newmap->bitmaps[loop2] = (bitmap_header *)((size_t)newmap->bitmaps[0] + (intswap32 (bitmap_ptrs[loop2]) - intswap32 (bitmap_ptrs[0])));
			}

/* Allocate head pointers for changes for each level of the map */
			newmap->changed_runs = calloc (sizeof (*newmap->changed_runs), numbitmaps);
			newmap->changes = calloc (sizeof (*newmap->changes), numbitmaps);
		}

/* Also link it into the loaded order. */
		*loaded_head = newmap;
		loaded_head = &newmap->next_loaded;
/* And add it to the totals. */
		if (mfshnd->is_64)
		{
			mfshnd->zones[type].size += intswap64 (cur->z64.size);
			mfshnd->zones[type].free += intswap64 (cur->z64.free);
			ptrsector = intswap64 (cur->z64.next_sector);
			ptrsbackup = intswap64 (cur->z64.next_sbackup);
			ptrlength = intswap32 (cur->z64.next_length);
		}
		else
		{
			mfshnd->zones[type].size += intswap32 (cur->z32.size);
			mfshnd->zones[type].free += intswap32 (cur->z32.free);
			ptrsector = intswap32 (cur->z32.next.sector);
			ptrsbackup = intswap32 (cur->z32.next.sbackup);
			ptrlength = intswap32 (cur->z32.next.length);
		}
		loop++;
	}

	return loop;
}

/*************************************************************/
/* Find a free run of a certain size within a specific szone */
static int
mfs_zone_find_run (struct mfs_handle *mfshnd, struct zone_map *zone, int order)
{
	int curorder;
	int numbitmaps;
	int freebit = -1;
	struct zone_changed_run **changed_runs;

	if (mfshnd->is_64)
	{
		numbitmaps = intswap32 (zone->map->z64.num);
	}
	else
	{
		numbitmaps = intswap32 (zone->map->z32.num);
	}

	for (curorder = order; curorder < numbitmaps; curorder++)
	{
		int numfree = intswap32 (zone->bitmaps[curorder]->freeblocks) + zone->changes[curorder].freed - zone->changes[curorder].allocated;
		if (numfree > 0)
			break;
	}

	if (curorder >= numbitmaps)
		return -1;

	/* Find the free bit in the bitmap */
	for (changed_runs = &zone->changed_runs[curorder]; *changed_runs; changed_runs = &(*changed_runs)->next)
	{
		if ((*changed_runs)->newstate)
		{
			struct zone_changed_run *tmp = *changed_runs;
			*changed_runs = tmp->next;

			freebit = tmp->bitno;
			free (tmp);
			break;
		}
	}

	/* Didn't find something in the list, find it in the bitmap */
	if (freebit < 0)
	{
		int nints = intswap32 (zone->bitmaps[curorder]->nints);
		int startint = (intswap32 (zone->bitmaps[curorder]->last) / 32) % nints;
		int loop;
		unsigned *bits = (unsigned *)(zone->bitmaps[curorder] + 1);

		for (loop = 0; loop < nints; loop++)
		{
			unsigned curint = bits[(loop + startint) % nints];
			if (curint)
			{
				curint = intswap32 (curint);
				int loop2;
				int thisbit = -1;
				for (loop2 = 0; curint && loop2 < 32; loop2++)
				{
					if (curint & (1 << (31 - loop2)))
					{
						thisbit = ((loop + startint) % nints) * 32 + loop2;
						/* Make sure the bit wasn't already allocated */
						struct zone_changed_run *crloop;;
						for (crloop = zone->changed_runs[curorder]; crloop; crloop = crloop->next)
						{
							if (crloop->bitno == thisbit)
							{
								break;
							}
						}

						if (!crloop)
							break;

						thisbit = -1;
					}
				}

				if (thisbit >= 0)
				{
					freebit = thisbit;
				}
			}
		}

		if (freebit < 0)
		{
			/* Something is wrong */
			return -1;
		}

		/* Add the allocation to the list */
		/* Due to the loop earlier, this points to the tail of the list */
		*changed_runs = calloc (sizeof (*changed_runs), 1);
		(*changed_runs)->bitno = freebit;
	}

	zone->changes[curorder].allocated++;
	while (curorder > order)
	{
		struct zone_changed_run *newchange;

		freebit <<= 1;
		curorder--;

		/* Create a notation that there is now a free block for the */
		/* "other half" of the allocation */
		newchange = calloc (sizeof (*newchange), 1);
		newchange->next = zone->changed_runs[curorder];
		zone->changed_runs[curorder] = newchange;

		newchange->newstate = 1;
		newchange->bitno = freebit + 1;
		zone->changes[curorder].freed++;
	}

	return freebit;
}

/*******************************/
/* Perform a greedy allocation */
/* This is not an ideal solution, but the ideal solution is not NP complete */
/* (See knapsack problem) */
/* This allocates blocks for the file starting at the largest and going */
/* down to the smallest.  This works great for a fresh volume, but it */
/* can break down when there has been some churn, leaving lots of */
/* small runs unallocated until late. */
/* This can be a problem if the free space is so fragmented that it needs */
/* more runs than can be allocated to describe a file */
int
mfs_alloc_greedy (struct mfs_handle *mfshnd, mfs_inode *inode, uint64_t highest)
{
	zone_type alloctype = ztApplication;
	uint64_t size = intswap32 (inode->size);
	uint64_t *runsizes;
	int *freeblocks;
	int *curorders;
	int *nbitmaps;
	struct zone_map **zones;
	int nzones;
	struct zone_map *zone;
	int maxruns, currun = 0;
	uint64_t lastrunsize = 0xffffffff;

	if (mfshnd->is_64)
	{
		maxruns = (512 - offsetof (mfs_inode, datablocks)) / sizeof (inode->datablocks.d64[0]);
	}
	else
	{
		maxruns = (512 - offsetof (mfs_inode, datablocks)) / sizeof (inode->datablocks.d32[0]);
	}

	if (inode->type == tyStream)
	{
		alloctype = ztMedia;
		size *= intswap32 (inode->blocksize);
	}

	/* Convert bytes to blocks */
	size = (size + 511) / 512;

	inode->numblocks = 0;

	/* Make it really high if it wasn't specified */
	if (!highest)
		highest = ~INT64_C(0);

	/* Count the number of loaded maps */
	nzones = 0;
	for (zone = mfshnd->zones[alloctype].next; zone; zone = zone->next)
	{
		if (mfshnd->is_64)
		{
			if (intswap64 (zone->map->z64.last) < highest)
			{
				nzones++;
			}
		}
		else
		{
			if (intswap32 (zone->map->z32.last) < highest)
			{
				nzones++;
			}
		}
	}

	/* Quick sanity check */
	if (!nzones)
	{
		return 0;
	}

	/* Allocate temp arrays (Off the stack) */
	freeblocks = alloca (nzones * sizeof (*freeblocks));
	curorders = alloca (nzones * sizeof (*curorders));
	runsizes = alloca (nzones * sizeof (*runsizes));
	zones = alloca (nzones * sizeof (*zones));
	nbitmaps = alloca (nzones * sizeof (*nbitmaps));

	/* Fill in the data for the zones */
	nzones = 0;
	for (zone = mfshnd->zones[alloctype].next; zone; zone = zone->next)
	{
		if (mfshnd->is_64)
		{
			if (intswap64 (zone->map->z64.last) < highest)
			{
				int curorder = intswap32 (zone->map->z64.num) - 1;
				zones[nzones] = zone;
				runsizes[nzones] = intswap32 (zone->map->z64.min);
				curorders[nzones] = curorder;
				freeblocks[nzones] = intswap32 (zone->bitmaps[curorder]->freeblocks) + zone->changes[curorder].freed - zone->changes[curorder].allocated;
				nbitmaps[nzones] = curorder + 1;
				nzones++;
			}
		}
		else
		{
			if (intswap32 (zone->map->z32.last) < highest)
			{
				int curorder = intswap32 (zone->map->z32.num) - 1;
				zones[nzones] = zone;
				runsizes[nzones] = intswap32 (zone->map->z32.min);
				curorders[nzones] = curorder;
				freeblocks[nzones] = intswap32 (zone->bitmaps[curorder]->freeblocks) + zone->changes[curorder].freed - zone->changes[curorder].allocated;
				nbitmaps[nzones] = curorder + 1;
				nzones++;
			}
		}
	}

	/* Keep going until the entire size is taken up */
	while (size > 0)
	{
		int loop;
		uint64_t largestfit = 0;
		int largestfitno = 0;
		/* To use as a tie-breaker */
		int largestfitborrow = 0;
		/* To use as a second tiebreaker */
		unsigned largestfitfree = 0;

		if (currun >= maxruns)
		{
			/* Out of space within the requested limits */
			return 0;
		}

		/* Find the first zone with available space */
		for (loop = 0; loop < nzones; loop++)
		{
			unsigned unitsleft = (size + runsizes[loop] - 1) / runsizes[loop];
			int nborrow = 0;
			int loop2;
			int runstoalloc;

			/* Don't waste space unless it's the last level */
			/* Also make sure it never goes bigger */
			while (curorders[loop] > 0 &&
				((runsizes[loop] << curorders[loop]) > lastrunsize ||
				!(unitsleft >> curorders[loop]) ||
				!freeblocks[loop]))
			{
				curorders[loop]--;
				freeblocks[loop] <<= 1;
				freeblocks[loop] += intswap32 (zones[loop]->bitmaps[curorders[loop]]->freeblocks) + zones[loop]->changes[curorders[loop]].freed - zones[loop]->changes[curorders[loop]].allocated;
			}

			/* If the current largest is already bigger, keep going */
			/* As an exception, if the largest is bigger than the size */
			/* remaining, keep this block in consideration */
			if (largestfit > runsizes[loop] << curorders[loop]
				&& largestfit < size)
			{
				continue;
			}

			/* If this is the last level of this zone map, switch from */
			/* biggest fit to smallest */
			if (!curorders[loop])
			{
				if (largestfit && size < runsizes[loop] && runsizes[loop] > largestfit)
					continue;
			}

			runstoalloc = unitsleft >> curorders[loop];
			if (runstoalloc > freeblocks[loop])
				runstoalloc = freeblocks[loop];
			/* ??? Shouldn't ever happen */
			if (!runstoalloc)
				continue;

			/* Count how many blocks would need to be borrowed */
			for (loop2 = curorders[loop]; runstoalloc && loop2 < nbitmaps[loop]; loop2++)
			{
				unsigned thisfree = intswap32 (zones[loop]->bitmaps[loop2]->freeblocks) + zones[loop]->changes[loop2].freed - zones[loop]->changes[loop2].allocated;
				thisfree = thisfree << (loop2 - curorders[loop]);
				if (thisfree > runstoalloc)
					thisfree = runstoalloc;
				/* Theoretically the algorithm will never borrow more */
				/* than one from anything but the current level */
				nborrow += thisfree * (loop2 - curorders[loop]);
				runstoalloc -= thisfree;
			}

			/* If they are equal, go to the tiebreaker */
			if (largestfit == runsizes[loop] << curorders[loop])
			{
				/* First tiebreaker, whoever is borrowing the least */
				if (nborrow > largestfitborrow)
					continue;
				/* Second tiebreaker, whoever has more free space */
				if (freeblocks[loop] < largestfitfree)
					continue;
			}

			largestfit = runsizes[loop] << curorders[loop];
			largestfitfree = freeblocks[loop];
			largestfitborrow = nborrow;
			largestfitno = loop;
		}

		if (!largestfit)
		{
			/* Out of space within the requested limits */
			return 0;
		}

		int bitno = mfs_zone_find_run (mfshnd, zones[largestfitno], curorders[largestfitno]);
		if (bitno < 0)
		{
			/* Shouldn't happen, but just in case */
			return 0;
		}

		if (mfshnd->is_64)
		{
			uint64_t sector = intswap64 (zones[largestfitno]->map->z64.first);
			sector += (bitno * runsizes[largestfitno]) << curorders[largestfitno];
			inode->datablocks.d64[currun].sector = intswap64 (sector);
			inode->datablocks.d64[currun].count = intswap32 (largestfit);
#if DEBUG
			fprintf (stderr, "mfs_alloc_greedy: Allocated %d block of %d at %lld for %d\n", alloctype, (unsigned)largestfit, sector, intswap32 (inode->fsid));
#endif
		}
		else
		{
			unsigned sector = intswap32 (zones[largestfitno]->map->z32.first);
			sector += (bitno * runsizes[largestfitno]) << curorders[largestfitno];
			inode->datablocks.d32[currun].sector = intswap32 (sector);
			inode->datablocks.d32[currun].count = intswap32 (largestfit);
#if DEBUG
			fprintf (stderr, "mfs_alloc_greedy: Allocated %d block of %d at %d for %d\n", alloctype, (unsigned)largestfit, sector, intswap32 (inode->fsid));
#endif
		}

		currun++;
		freeblocks[largestfitno]--;
		if (size > largestfit)
			size -= largestfit;
		else
			size = 0;
		lastrunsize = largestfit;
	}

	inode->numblocks = intswap32 (currun);
	return currun;
}
