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
#include <sys/errno.h>
#include <sys/param.h>
#include <linux/fs.h>
#include <linux/unistd.h>

/* For htonl() */
#include <netinet/in.h>

#include "mfs.h"
#include "macpart.h"

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
	unsigned int sectors = mfshnd->zones[ztMedia].size;

	if (sectors > 72 * 1024 * 1024 * 2)
		sectors -= 12 * 1024 * 1024 * 2;
	else if (sectors > 14 * 1024 * 1024 * 2)
		sectors -= (sectors - 14 * 1024 * 1024 * 2) / 4;

	return sectors / SABLOCKSEC;
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
unsigned int
mfs_inode_to_sector (struct mfs_handle *mfshnd, unsigned int inode)
{
	struct zone_map *cur;

/* Don't bother if it's not a valid inode. */
	if (inode >= mfs_inode_count (mfshnd))
	{
		return 0;
	}

/* For ease of calculation, turn this into a sector offset into the inode */
/* maps. */
	inode *= 2;

/* Loop through each inode map, seeing if the current inode is within it. */
	for (cur = mfshnd->zones[ztInode].next; cur; cur = cur->next)
	{
		if (inode < htonl (cur->map->size))
		{
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

/************************************************************************/
/* Return how big a new zone map would need to be for a given number of */
/* allocation blocks. */
static int
mfs_new_zone_map_size (unsigned int blocks)
{
	int size = sizeof (zone_header) + 4;
	int order = 0;

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
static int
mfs_new_zone_map (struct mfs_handle *mfshnd, unsigned int sector, unsigned int backup, unsigned int first, unsigned int size, unsigned int minalloc, zone_type type)
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
	for (cur = mfshnd->loaded_zones; cur->next_loaded; cur = cur->next_loaded);

	if (!cur)
	{
		return -1;
	}

	last = cur->map;

/* To get the pointer into fsmem, start with the first pointer from the */
/* previous zone map.  Subtract the header and all the fsmem pointers from */
/* it, and thats the base for that map.  Now add in all the sectors from that */
/* map, plus 1 extra and 8 bytes. */
	fsmem_base = htonl (*(unsigned int *) (last + 1)) - (sizeof (*last) + htonl (last->num) * 4) + htonl (last->length) * 512 + 512 + 8;

	buf = malloc (zonesize);

	if (!buf)
	{
		return -1;
	}

/* Fill in everything with lots and lots of dead beef.  Hope theres no */
/* vegitarians or vegans in the crowd. */
	for (loop = 0; loop < zonesize; loop += 4)
	{
		*(int *) (buf + loop) = htonl (0xdeadbeef);
	}

/* Figure out the order of the blocks count. */
	while ((1 << order) < blocks)
	{
		order++;
	}

	order++;

	zone = (zone_header *) buf;

/* Fill in the header values. */
	zone->sector = htonl (sector);
	zone->sbackup = htonl (backup);
	zone->length = htonl (zonesize / 512);
	zone->next.sector = 0;
	zone->next.length = 0;
	zone->next.size = 0;
	zone->next.min = 0;
	zone->type = htonl (type);
	zone->logstamp = 0;
	zone->checksum = htonl (0xdeadf00d);
	zone->first = htonl (first);
	zone->last = htonl (first + size - 1);
	zone->size = htonl (size);
	zone->min = htonl (minalloc);
	zone->free = htonl (size);
	zone->zero = 0;
	zone->num = htonl (order);

/* Grab a pointer to the array where fsmem pointers will go. */
	fsmem_pointers = (unsigned int *) (zone + 1);
	curofs = (unsigned int *) (zone + 1) + order;

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
		fsmem_pointers[loop] = htonl (fsmem_base + (char *) curofs - (char *) zone);

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
		if (blocks & 1)
		{
			bitmap->last = htonl (blocks - 1);
			bitmap->freeblocks = htonl (1);
			curofs[4 + (blocks - 1) / 32] = htonl (1 << (31 - (blocks - 1) % 32));
		}
		else
		{
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
	mfsvol_write_data (mfshnd->vols, zone, htonl (zone->sector), htonl (zone->length));
	mfsvol_write_data (mfshnd->vols, zone, htonl (zone->sbackup), htonl (zone->length));
	mfsvol_write_data (mfshnd->vols, last, htonl (last->sector), htonl (last->length));
	mfsvol_write_data (mfshnd->vols, last, htonl (last->sbackup), htonl (last->length));

	return 0;
}

unsigned int
mfs_volume_pair_app_size (unsigned int blocks, unsigned int minalloc)
{
	if (minalloc == 0)
		minalloc = 0x800;

	return (2 + 2 * ((mfs_new_zone_map_size (blocks / minalloc) + 511) / 512) + MFS_PARTITION_ROUND - 1) & ~(MFS_PARTITION_ROUND - 1);
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
	if (strlen (mfshnd->vol_hdr.partitionlist) + strlen (app) + strlen (media) + 3 >= 128)
	{
		fprintf (stderr, "No space in volume list for new volumes.\n");
		return -1;
	}

/* Make sure block 0 is writable.  It wouldn't do to get all the way to */
/* the end and not be able to update the volume header. */
	if (!mfsvol_is_writable (mfshnd->vols, 0))
	{
		fprintf (stderr, "mfs_add_volume_pair: Readonly volume set.\n");
		return -1;
	}

/* Walk the list of zone maps to find the last loaded zone map. */
	for (cur = mfshnd->loaded_zones; cur && cur->next_loaded; cur = cur->next_loaded);

/* For cur to be null, it must have never been set. */
	if (!cur)
	{
		fprintf (stderr, "mfs_add_volume_pair: Zone maps not loaded?\n");
		return -1;
	}

/* Check that the last zone map is writable.  This is needed for adding the */
/* new pointer. */
	if (!mfsvol_is_writable (mfshnd->vols, htonl (cur->map->sector)))
	{
		fprintf (stderr, "mfs_add_volume_pair: Readonly volume set.\n");
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
	int appstart, mediastart;
	int appsize, mediasize, mapsize;
	char *tmp;
	unsigned char foo[512];

/* If no minalloc, make it default. */
	if (minalloc == 0)
	{
		minalloc = 0x800;
	}

/* Make sure the volumes being added don't overflow the 128 bytes. */
	if (strlen (mfshnd->vol_hdr.partitionlist) + strlen (app) + strlen (media) + 3 >= 128)
	{
		fprintf (stderr, "No space in volume list for new volumes.\n");
		return -1;
	}

/* Make sure block 0 is writable.  It wouldn't do to get all the way to */
/* the end and not be able to update the volume header. */
	if (!mfsvol_is_writable (mfshnd->vols, 0))
	{
		fprintf (stderr, "mfs_add_volume_pair: Readonly volume set.\n");
		return -1;
	}

/* Walk the list of zone maps to find the last loaded zone map. */
	for (cur = mfshnd->loaded_zones; cur && cur->next_loaded; cur = cur->next_loaded);

/* For cur to be null, it must have never been set. */
	if (!cur)
	{
		fprintf (stderr, "mfs_add_volume_pair: Zone maps not loaded?\n");
		return -1;
	}

/* Check that the last zone map is writable.  This is needed for adding the */
/* new pointer. */
	if (!mfsvol_is_writable (mfshnd->vols, htonl (cur->map->sector)))
	{
		fprintf (stderr, "mfs_add_volume_pair: Readonly volume set.\n");
		return -1;
	}

	tmp = mfsvol_device_translate (app);
	tpApp = tivo_partition_open (tmp, O_RDWR);
	if (!tpApp)
	{
		perror (tmp);
		return -1;
	}

	tmp = mfsvol_device_translate (media);
	tpMedia = tivo_partition_open (tmp, O_RDWR);
	if (!tpMedia)
	{
		perror (tmp);
		return -1;
	}

	tivo_partition_close (tpApp);
	tivo_partition_close (tpMedia);

	appstart = mfsvol_add_volume (mfshnd->vols, app, O_RDWR);
	mediastart = mfsvol_add_volume (mfshnd->vols, media, O_RDWR);

	if (appstart < 0 || mediastart < 0)
	{
		fprintf (stderr, "mfs_add_volume_pair: Error adding new volumes to set.\n");
		mfs_reinit (mfshnd, O_RDWR);
		return -1;
	}

	if (!mfsvol_is_writable (mfshnd->vols, appstart) || !mfsvol_is_writable (mfshnd->vols, mediastart))
	{
		fprintf (stderr, "mfs_add_volume_pair: Could not add new volumes writable.\n");
		mfs_reinit (mfshnd, O_RDWR);
		return -1;
	}

	appsize = mfsvol_volume_size (mfshnd->vols, appstart);
	mediasize = mfsvol_volume_size (mfshnd->vols, mediastart);
	mapsize = (mfs_new_zone_map_size (mediasize / minalloc) + 511) / 512;

	if (mapsize * 2 + 2 > appsize)
	{
		fprintf (stderr, "mfs_add_volume_pair: New app size too small!  (Need %d more bytes)\n", (mapsize * 2 + 2 - appsize) * 512);
		mfs_reinit (mfshnd, O_RDWR);
		return -1;
	}

	if (mfs_new_zone_map (mfshnd, appstart + 1, appstart + appsize - mapsize - 1, mediastart, mediasize, minalloc, ztMedia) < 0)
	{
		fprintf (stderr, "mfs_add_volume_pair: Failed initializing new zone map.\n");
		mfs_reinit (mfshnd, O_RDWR);
		return -1;
	}

	snprintf (foo, 128, "%s %s %s", mfshnd->vol_hdr.partitionlist, app, media);
	foo[127] = 0;
	strcpy (mfshnd->vol_hdr.partitionlist, foo);
	mfshnd->vol_hdr.total_sectors = htonl (mfsvol_volume_set_size (mfshnd->vols));
	MFS_update_crc (&mfshnd->vol_hdr, sizeof (mfshnd->vol_hdr), mfshnd->vol_hdr.checksum);

	memset (foo, 0, sizeof (foo));
	memcpy (foo, &mfshnd->vol_hdr, sizeof (mfshnd->vol_hdr));
	mfsvol_write_data (mfshnd->vols, foo, 0, 1);
	mfsvol_write_data (mfshnd->vols, foo, mfsvol_volume_size (mfshnd->vols, 0) - 1, 1);

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

			mfshnd->zones[loop].next = map->next;
			free (map->map);
			free (map);
		}
	}

	mfshnd->loaded_zones = NULL;
}

/*************************************************************/
/* Load a zone map from the drive and verify it's integrity. */
static zone_header *
mfs_load_zone_map (struct mfs_handle *mfshnd, zone_map_ptr * ptr)
{
	zone_header *hdr = calloc (htonl (ptr->length), 512);

	if (!hdr)
	{
		return NULL;
	}

/* Read the map. */
	mfsvol_read_data (mfshnd->vols, (unsigned char *) hdr, htonl (ptr->sector), htonl (ptr->length));

/* Verify the CRC matches. */
	if (!MFS_check_crc ((unsigned char *) hdr, htonl (ptr->length) * 512, hdr->checksum))
	{
		fprintf (stderr, "mfs_load_zone_map: Primary zone map corrupt, loading backup.\n");
/* If the CRC doesn't match, try the backup map. */
		mfsvol_read_data (mfshnd->vols, (unsigned char *) hdr, htonl (ptr->sbackup), htonl (ptr->length));
		if (!MFS_check_crc ((unsigned char *) hdr, htonl (ptr->length) * 512, hdr->checksum))
		{
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
int
mfs_load_zone_maps (struct mfs_handle *mfshnd)
{
	zone_map_ptr *ptr = &mfshnd->vol_hdr.zonemap;
	zone_header *cur;
	struct zone_map **loaded_head = &mfshnd->loaded_zones;
	struct zone_map **cur_heads[ztMax];
	int loop;

/* Start clean. */
	mfs_cleanup_zone_maps (mfshnd);
	memset (mfshnd->zones, 0, sizeof (mfshnd->zones));

	for (loop = 0; loop < ztMax; loop++)
	{
		cur_heads[loop] = &mfshnd->zones[loop].next;
	}

	loop = 0;

	while (ptr->sector && ptr->sbackup != htonl (0xdeadbeef))
	{
		struct zone_map *newmap;

/* Read the map, verify it's checksum. */
		cur = mfs_load_zone_map (mfshnd, ptr);

		if (!cur)
		{
			return -1;
		}

		if (htonl (cur->type) < 0 || htonl (cur->type) >= ztMax)
		{
			fprintf (stderr, "mfs_load_zone_maps: Bad map type %d.\n", htonl (cur->type));
			free (cur);
			return -1;
		}

		newmap = calloc (sizeof (*newmap), 1);
		if (!newmap)
		{
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
		mfshnd->zones[htonl (cur->type)].size += htonl (cur->size);
		mfshnd->zones[htonl (cur->type)].free += htonl (cur->free);
		loop++;

		ptr = &cur->next;
	}

	return loop;
}
