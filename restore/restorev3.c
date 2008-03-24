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

int
restore_create_empty_inodes (struct backup_info *info, uint64_t start, uint64_t size)
{
	char buf[4096];
	mfs_inode *inode = (mfs_inode *)&buf;
	int loop;

	/* Initialize a blank inode */
	memset (&buf, 0, sizeof (buf));
	inode->pad = 0xaa;
	if (mfs_is_64bit (info->mfs))
		inode->sig = intswap32 (MFS64_INODE_SIG);
	else
		inode->sig = intswap32 (MFS32_INODE_SIG);
	MFS_update_crc (inode, 512, inode->checksum);

	/* Copy the blank inode to fill out the buffer */
	for (loop = 512; loop < sizeof (buf); loop += 512)
	{
		memcpy (buf + loop, buf, 512);
	}

	while (size > 0)
	{
		int tocopy = 8;
		if (size < 8)
		{
			tocopy = size;
		}

		if (mfs_write_data (info->mfs, buf, start, tocopy) <= 0)
			return -1;

		size -= tocopy;
		start += tocopy;
	}

	return 1;
}

/* Recreate the zone maps stored in the backup */
int
restore_generate_zone_maps (struct backup_info *info)
{
	int loop;
	int createdmedia = 0;
	int fsmem_ptr_offset = 0;

	unsigned int mediaminalloc = 0x800;

	uint64_t curappvolstart = 0;
	uint64_t curmediavolstart = mfs_volume_size (info->mfs, 0);
	uint64_t curapplastsector = curmediavolstart - 2;

	for (loop = 0; loop < info->nzones; loop++)
	{
		int maplength;
		uint64_t zonesize;
		uint64_t first;
		uint64_t mapsector;
		uint64_t mapsbackup;

		switch (info->zonemaps[loop].zone_type)
		{
		case ztInode:
			zonesize = info->zonemaps[loop].size;
			break;
		case ztMedia:
			/* This is used to bump up the app start if more than one media */
			/* zones are being created in a row */
			if (createdmedia)
			{
				/* Bump the app volume up to the next one. */
				curappvolstart += mfs_volume_size (info->mfs, curappvolstart);
				curappvolstart += mfs_volume_size (info->mfs, curappvolstart);
				info->shared_val1 = 0;
				curapplastsector = mfs_volume_size (info->mfs, curappvolstart);
				if (curapplastsector > 0)
					curapplastsector--;
			}
			zonesize = mfs_volume_size (info->mfs, curmediavolstart);
			mediaminalloc = info->zonemaps[loop].min_au;
			break;
		case ztApplication:
			zonesize = curapplastsector - info->shared_val1;
			/* Compensate for the space taken up by the zone map */
			zonesize -= ((mfs_new_zone_map_size (info->mfs, zonesize / info->zonemaps[loop].min_au) + 511) / 512) * 2;
			break;
		default:
			info->err_msg = "Unknown zone type %d";
			info->err_arg1 = (void *)info->zonemaps[loop].zone_type;
			return -1;
		}

		/* This should catch the case of allocating app or media when there is no space */
		if (zonesize < info->zonemaps[loop].min_au)
		{
			info->err_msg = "Error creating zone map %d";
			info->err_arg1 = (void *)(loop + 1);
			return -1;
		}

		maplength = (mfs_new_zone_map_size (info->mfs, zonesize / info->zonemaps[loop].min_au) + 511) / 512;

		zonesize -= zonesize % info->zonemaps[loop].min_au;

		/* Make sure there is space for the zone maps */
		if (curapplastsector < info->shared_val1 || curapplastsector - info->shared_val1 + 1 < maplength * 2)
		{
			info->err_msg = "No MFS application space for zone %d";
			info->err_arg1 = (void *)(loop + 1);
			return -1;
		}
		mapsector = info->shared_val1 + curappvolstart;
		info->shared_val1 += maplength;
		curapplastsector -= maplength;
		mapsbackup = curapplastsector + 1 + curappvolstart;

		switch (info->zonemaps[loop].zone_type)
		{
		case ztInode:
			first = info->shared_val1 + curappvolstart;
			info->shared_val1 += zonesize;
			if (restore_create_empty_inodes (info, first, zonesize) < 0)
				return -1;
			break;
		case ztMedia:
			first = curmediavolstart;
			/* Bump the media volume up to the next one. */
			/* If it is the last volume, it will just add 0 the second time */
			curmediavolstart += mfs_volume_size (info->mfs, curmediavolstart);
			curmediavolstart += mfs_volume_size (info->mfs, curmediavolstart);
			createdmedia = 1;
			break;
		case ztApplication:
			first = info->shared_val1 + curappvolstart;
			/* Bump the app volume up to the next one. */
			curappvolstart += mfs_volume_size (info->mfs, curappvolstart);
			curappvolstart += mfs_volume_size (info->mfs, curappvolstart);
			info->shared_val1 = 0;
			curapplastsector = mfs_volume_size (info->mfs, curappvolstart);
			if (curapplastsector > 0)
				curapplastsector--;
			createdmedia = 0;
			break;
		}

		if (mfs_new_zone_map (info->mfs, mapsector, mapsbackup, first, zonesize, info->zonemaps[loop].min_au, info->zonemaps[loop].zone_type, info->zonemaps[loop].fsmem_base + fsmem_ptr_offset))
		{
			return -1;
		}
		if (mfs_load_zone_maps (info->mfs) < 0)
		{
			return -1;
		}

		/* Track the difference between the expected size and the created */
		/* size to at least try to get the fsmem pointers right. */
		fsmem_ptr_offset += (maplength - info->zonemaps[loop].map_length) * 512;
	}

/* Fill out any remaining volumes */
	while (curappvolstart < curmediavolstart)
	{
		uint64_t zonesize = mfs_volume_size (info->mfs, curmediavolstart);
		int maplength = mfs_new_zone_map_size (info->mfs, zonesize / mediaminalloc);

		if (info->shared_val1 > curapplastsector || curapplastsector - info->shared_val1 + 1 < maplength * 2)
		{
			info->err_msg = "No MFS application space for new media zone";
			info->err_arg1 = (void *)(loop + 1);
			return -1;
		}
		if (mfs_new_zone_map (info->mfs, info->shared_val1 + curappvolstart, curappvolstart + curapplastsector - maplength + 1, curmediavolstart, zonesize, mediaminalloc, ztMedia, 0))
		{
			return -1;
		}
		if (mfs_load_zone_maps (info->mfs) < 0)
		{
			return -1;
		}

		curappvolstart = curmediavolstart + zonesize;
		info->shared_val1 = 0;
		curapplastsector = mfs_volume_size (info->mfs, curappvolstart);
		if (!curapplastsector)
		{
			break;
		}
		curmediavolstart = curappvolstart + curapplastsector;
		curapplastsector--;
	}

	return 1;
}

/*********************************************************************/
/* Split up a chunk of data into appropriate sized blocks for a zone */
void
restore_add_blocks_to_inode (struct backup_info *info, zone_header *zone, mfs_inode *inode, uint64_t sector, uint64_t size)
{
	uint64_t first;
	uint64_t minalloc;
	int numbitmaps;
	int numblocks = intswap32 (inode->numblocks);

	if (mfs_is_64bit (info->mfs))
	{
		first = intswap64 (zone->z64.first);
		minalloc = intswap32 (zone->z64.min);
		numbitmaps = intswap32 (zone->z64.num);
	}
	else
	{
		first = intswap32 (zone->z32.first);
		minalloc = intswap32 (zone->z32.min);
		numbitmaps = intswap32 (zone->z32.num);
	}

	/* Align the sector on the zone */
	sector -= (sector - first) % minalloc;
	/* Make sure the size is proper for this zone as well */
	size += minalloc - 1 - ((size + minalloc - 1) % minalloc);

	while (size > 0)
	{
		int bitno = (sector - first) / minalloc;
		int bitrun = (size + minalloc - 1) / minalloc;
		int nbits = bitno & ~(bitno - 1);

		uint64_t blocksize;

		if (!nbits)
			nbits = 1 << numbitmaps;
		while (nbits > bitrun)
		{
			nbits >>= 1;
		}

		blocksize = minalloc * nbits;

		if (mfs_is_64bit (info->mfs))
		{
			inode->datablocks.d64[numblocks].sector = intswap64 (sector);
			inode->datablocks.d64[numblocks].count = intswap32 (blocksize);
		}
		else
		{
			inode->datablocks.d32[numblocks].sector = intswap32 (sector);
			inode->datablocks.d32[numblocks].count = intswap32 (blocksize);
		}
		numblocks++;
		inode->numblocks = intswap32 (numblocks);

		sector += blocksize;
		size -= blocksize;
	}
}

/***************************************************************************/
/* State handlers - return val -1 = error, 0 = more data needed, 1 = go to */
/* next state. */

/***********************************/
/* Generic handler for header data */
enum backup_state_ret
restore_read_header (struct backup_info *info, void *data, unsigned size, unsigned *consumed, void *dest, unsigned total, unsigned datasize);
/* Defined in restore.c */

/*****************************/
/* Begin restore - v3 backup format */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
restore_state_begin_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	struct backup_head_v3 *head = data;

	if (size == 0)
	{
		info->err_msg = "Internal error: Restore buffer empty";
		return bsError;
	}

	switch (head->magic)
	{
	case TB3_MAGIC:
		break;
	case TB3_ENDIAN:
		info->back_flags |= RF_ENDIAN;
		break;
	default:
		info->err_msg = "Not v3 backup format";
		return bsError;
	}

/* Copy header fields into backup info */
	if (info->back_flags & RF_ENDIAN)
	{
		info->back_flags |= Endian16_Swap (head->flags);
		info->nparts = Endian16_Swap (head->nparts);
		info->nmfs = Endian16_Swap (head->mfspairs);
		info->nzones = Endian16_Swap (head->nzones);
		info->nsectors = Endian64_Swap (head->nsectors);
		info->appsectors = Endian64_Swap (head->appsectors);
		info->mediasectors = Endian64_Swap (head->mediasectors);
		info->appinodes = Endian32_Swap (head->appinodes);
		info->mediainodes = Endian32_Swap (head->mediainodes);
		info->ilogtype = Endian32_Swap (head->ilogtype);
		info->ninodes = Endian32_Swap (head->ninodes);
		info->shared_val1 = Endian32_Swap (head->size);
	}
	else
	{
		info->back_flags |= head->flags;
		info->nparts = head->nparts;
		info->nmfs = head->mfspairs;
		info->nzones = head->nzones;
		info->nsectors = head->nsectors;
		info->appsectors = head->appsectors;
		info->mediasectors = head->mediasectors;
		info->appinodes = head->appinodes;
		info->mediainodes = head->mediainodes;
		info->ilogtype = head->ilogtype;
		info->ninodes = head->ninodes;
		info->shared_val1 = head->size;
	}

/* Allocate storage for backup description */
	info->parts = calloc (sizeof (struct backup_partition), info->nparts);
	info->zonemaps = calloc (sizeof (struct zone_map_info), info->nzones);
	info->mfsparts = calloc (sizeof (struct backup_partition), info->nmfs);

	if (!info->parts || !info->zonemaps || !info->mfsparts)
	{
		if (info->parts)
			free (info->parts);
		info->parts = 0;
		info->nparts = 0;

		if (info->zonemaps)
			free (info->zonemaps);
		info->zonemaps = 0;
		info->nzones = 0;

		if (info->mfsparts)
			free (info->mfsparts);
		info->mfsparts = 0;
		info->nmfs = 0;

		info->err_msg = "Memory exhausted (Begin restore)";
		return bsError;
	}

	info->shared_val1 = (info->shared_val1 + 7) & (512 - 8);
	*consumed = 0;

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

/************************/
/* Read MFS volume list */
/* state_val1 = offset of last copied MFS partition */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = next offset to use in block */
enum backup_state_ret
restore_state_mfs_partition_info (struct backup_info *info, void *data, unsigned size, unsigned *consumed);
/* Defined in restore.c */

/*********************/
/* Restore zone maps */
/* state_val1 = --unused-- */
/* state_val2 = offset within zone map */
/* state_ptr1 = current zone map */
/* shared_val1 = --unused-- */
enum backup_state_ret
restore_state_zone_map_info_v3 (struct backup_info *info, void *data, unsigned size,unsigned *consumed)
{
	return restore_read_header (info, data, size, consumed, info->zonemaps, info->nzones, sizeof (struct zone_map_info));
}

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

/*****************************/
/* Restore the volume header */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = Lowest available sector in first volume */
enum backup_state_ret
restore_state_volume_header_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	unsigned first_partition_size = mfsvol_volume_size (info->vols, 0);
	int loop;
	int do64bit;
	union {
		volume_header hdr[2];
		char pad[512];
	} vol;
	volume_header *datahdr = (volume_header *)data;

	if (size == 0)
	{
		info->err_msg = "Internal error: Resture buffer empty";
		return bsError;
	}

	if (first_partition_size == 0)
	{
		info->err_msg = "Internal error: Bad partition map";
		return bsError;
	}

/* Track sector usage in the first volume */
	info->shared_val1 = 1;

	memset (&vol, 0, sizeof (vol));

/* First build a 64 bit volume header structure in native byte order */
/* Use hdr[1] to build the 64 bit header and [0] for the final header */
/* The values here are the only ones needed to be copied, the rest */
/* will be generated */
	if (info->back_flags & BF_64)
	{
		vol.hdr[1].v64.off0c = intswap32 (datahdr->v64.off0c);
		vol.hdr[1].v64.root_fsid = intswap32 (datahdr->v64.root_fsid);
		vol.hdr[1].v64.off14 = intswap32 (datahdr->v64.off14);
		vol.hdr[1].v64.off1c = intswap32 (datahdr->v64.off1c);
		vol.hdr[1].v64.off20 = intswap32 (datahdr->v64.off20);
		vol.hdr[1].v64.unknsectors = intswap32 (datahdr->v64.unknsectors);
		vol.hdr[1].v64.lognsectors = intswap32 (datahdr->v64.lognsectors);
		vol.hdr[1].v64.off100 = intswap32 (datahdr->v64.off100);
		vol.hdr[1].v64.off110 = intswap32 (datahdr->v64.off110);
	}
	else
	{
		vol.hdr[1].v64.off0c = intswap32 (datahdr->v32.off0c);
		vol.hdr[1].v64.root_fsid = intswap32 (datahdr->v32.root_fsid);
		vol.hdr[1].v64.off14 = intswap32 (datahdr->v32.off14);
		vol.hdr[1].v64.off1c = intswap32 (datahdr->v32.off1c);
		vol.hdr[1].v64.off20 = intswap32 (datahdr->v32.off20);
		vol.hdr[1].v64.unknsectors = intswap32 (datahdr->v32.unksectors);
		vol.hdr[1].v64.lognsectors = intswap32 (datahdr->v32.lognsectors);
		vol.hdr[1].v64.off100 = intswap32 (datahdr->v32.offa8);
		vol.hdr[1].v64.off110 = intswap32 (datahdr->v32.offe4);
	}

/* Create the partition list */
	for (loop = 0; loop < info->nmfs; loop++)
	{
		sprintf (vol.hdr[1].v64.partitionlist + strlen (vol.hdr[1].v64.partitionlist), "%s/dev/hd%c%d", loop > 0? " ": "", 'a' + info->mfsparts[loop].devno, info->mfsparts[loop].partno);
	}

	if (strlen (vol.hdr[1].v64.partitionlist) + 1 > sizeof (vol.hdr[1].v64.partitionlist))
	{
		info->err_msg = "Partition list too long";
		return -1;
	}

/* Start allocating space for necessities */
	vol.hdr[1].v64.logstart = info->shared_val1;
	info->shared_val1 += vol.hdr[1].v64.lognsectors;
	vol.hdr[1].v64.unkstart = info->shared_val1;
	info->shared_val1 += vol.hdr[1].v64.unknsectors;

/* Fill in some more values */
	vol.hdr[1].v64.total_sectors = mfsvol_volume_set_size (info->vols);
	vol.hdr[1].v64.firstpartsize = first_partition_size / 1024;

/* The rest of the header data will be initialized later, or is initialized to 0 */

	do64bit = info->back_flags & BF_64;
	if (info->back_flags & RF_REBUILDBITS)
	{
		do64bit = !do64bit;
	}

/* Now copy the header into the real thing and initialize the remaining values */
	if (do64bit)
	{
		vol.hdr[0].v64.magic = intswap32 (MFS64_MAGIC);
		vol.hdr[0].v64.off0c = intswap32 (vol.hdr[1].v64.off0c);
		vol.hdr[0].v64.root_fsid = intswap32 (vol.hdr[1].v64.root_fsid);
		vol.hdr[0].v64.off14 = intswap32 (vol.hdr[1].v64.off14);
		vol.hdr[0].v64.firstpartsize = intswap32 (vol.hdr[1].v64.firstpartsize);
		vol.hdr[0].v64.off1c = intswap32 (vol.hdr[1].v64.off1c);
		vol.hdr[0].v64.off20 = intswap32 (vol.hdr[1].v64.off20);
		strcpy (vol.hdr[0].v64.partitionlist, vol.hdr[1].v64.partitionlist);
		vol.hdr[0].v64.total_sectors = intswap64 (vol.hdr[1].v64.total_sectors);
		vol.hdr[0].v64.logstart = intswap64 (vol.hdr[1].v64.logstart);
		vol.hdr[0].v64.unkstart = intswap64 (vol.hdr[1].v64.unkstart);
		vol.hdr[0].v64.lognsectors = intswap32 (vol.hdr[1].v64.lognsectors);
		vol.hdr[0].v64.unknsectors = intswap32 (vol.hdr[1].v64.unknsectors);
		vol.hdr[0].v64.off100 = intswap32 (vol.hdr[1].v64.off100);
		vol.hdr[0].v64.off110 = intswap32 (vol.hdr[1].v64.off110);

		MFS_update_crc (&vol.hdr[0].v64, sizeof (vol.hdr[0].v64), vol.hdr[0].v64.checksum);
	}
	else
	{
		if (strlen (vol.hdr[1].v64.partitionlist) >= sizeof (vol.hdr[0].v32.partitionlist))
		{
			info->err_msg = "Partition list too long";
			return -1;
		}

		vol.hdr[0].v32.magic = intswap32 (MFS32_MAGIC);
		vol.hdr[0].v32.off0c = intswap32 (vol.hdr[1].v64.off0c);
		vol.hdr[0].v32.root_fsid = intswap32 (vol.hdr[1].v64.root_fsid);
		vol.hdr[0].v32.off14 = intswap32 (vol.hdr[1].v64.off14);
		vol.hdr[0].v32.firstpartsize = intswap32 (vol.hdr[1].v64.firstpartsize);
		vol.hdr[0].v32.off1c = intswap32 (vol.hdr[1].v64.off1c);
		vol.hdr[0].v32.off20 = intswap32 (vol.hdr[1].v64.off20);
		strcpy (vol.hdr[0].v32.partitionlist, vol.hdr[1].v64.partitionlist);
		vol.hdr[0].v32.total_sectors = intswap32 (vol.hdr[1].v64.total_sectors);
		vol.hdr[0].v32.logstart = intswap32 (vol.hdr[1].v64.logstart);
		vol.hdr[0].v32.unkstart = intswap32 (vol.hdr[1].v64.unkstart);
		vol.hdr[0].v32.lognsectors = intswap32 (vol.hdr[1].v64.lognsectors);
		vol.hdr[0].v32.unksectors = intswap32 (vol.hdr[1].v64.unknsectors);
		vol.hdr[0].v32.offa8 = intswap32 (vol.hdr[1].v64.off100);
		vol.hdr[0].v32.offe4 = intswap32 (vol.hdr[1].v64.off110);

		MFS_update_crc (&vol.hdr[0].v32, sizeof (vol.hdr[0].v32), vol.hdr[0].v32.checksum);
	}

	memset (&vol.hdr[1], 0, sizeof (vol.hdr[1]));

	if (mfsvol_write_data (info->vols, &vol, 0, 1) != 512)
	{
		info->err_msg = "%s writing MFS volume header";
		if (errno)
			info->err_arg1 = strerror (errno);
		else
			info->err_arg1 = "Unknown error";
		return bsError;
	}

	if (mfsvol_write_data (info->vols, &vol, first_partition_size - 1, 1) != 512)
	{
		info->err_msg = "%s writing MFS volume header";
		if (errno)
			info->err_arg1 = strerror (errno);
		else
			info->err_arg1 = "Unknown error";
		return bsError;
	}

	*consumed = 1;

/* Enough of MFS is initialized to go through mfs calls now */
	mfsvol_cleanup (info->vols);

	info->vols = 0;

/* Initialize MFS at this point */
	info->mfs = mfs_init (info->devs[0].devname, info->ndevs > 1? info->devs[1].devname: NULL, O_RDWR);

	if (!info->mfs ||
		do64bit && !mfs_is_64bit (info->mfs) ||
		!do64bit && mfs_is_64bit (info->mfs) ||
		mfs_is_64bit (info->mfs) && !MFS_check_crc (&info->mfs->vol_hdr.v64, sizeof (info->mfs->vol_hdr.v64), info->mfs->vol_hdr.v64.checksum) ||
		!mfs_is_64bit (info->mfs) && !MFS_check_crc (&info->mfs->vol_hdr.v32, sizeof (info->mfs->vol_hdr.v32), info->mfs->vol_hdr.v32.checksum))
	{
		if (!info->mfs || !mfs_has_error (info->mfs))
		{
			info->err_msg = "Error initializing MFS";
		}

		return bsError;
	}

/* While the mfs_init() returned a valid volume, it certainly had an error */
/* loading the zone maps */
	mfs_clearerror (info->mfs);

	return bsNextState;
}

/*******************************/
/* Restore the transaction log */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = Lowest available sector in first volume */
enum backup_state_ret
restore_state_transaction_log_v3 (struct backup_info *info, void *data, unsigned
size, unsigned *consumed)
{
	unsigned lognsectors;
	uint64_t logstart;
	unsigned logstamp;
	unsigned char buf[2048];
	log_hdr *hdr, *hdr2;
	int loop;

	if (mfs_is_64bit (info->mfs))
	{
		lognsectors = intswap32 (info->mfs->vol_hdr.v64.lognsectors);
		logstart = intswap64 (info->mfs->vol_hdr.v64.logstart);
		logstamp = intswap32 (info->mfs->vol_hdr.v64.logstamp);
	}
	else
	{
		lognsectors = intswap32 (info->mfs->vol_hdr.v32.lognsectors);
		logstart = intswap32 (info->mfs->vol_hdr.v32.logstart);
		logstamp = intswap32 (info->mfs->vol_hdr.v32.logstamp);
	}

	memset (buf, 0, sizeof (buf));

	/* Start by making the buffer look like a blank entry */
	hdr = (log_hdr *)buf;
	hdr->logstamp = 0xffffffff;
	MFS_update_crc (hdr, 512, hdr->crc);
	/* Copy the blank entry to the rest of the buffer */
	hdr2 = (log_hdr *)(buf + 512);
	hdr2->logstamp = 0xffffffff;
	hdr2->crc = hdr->crc;
	hdr2 = (log_hdr *)(buf + 1024);
	hdr2->logstamp = 0xffffffff;
	hdr2->crc = hdr->crc;
	hdr2 = (log_hdr *)(buf + 1536);
	hdr2->logstamp = 0xffffffff;
	hdr2->crc = hdr->crc;

	for (loop = 0; loop < lognsectors; loop += 4)
	{
		int tocopy = 4;

		if (lognsectors - loop < 4)
			tocopy = lognsectors - loop;

		if (loop / 4 == logstamp / 4)
		{
			hdr = (log_hdr *)(buf + 512 * (logstamp & 3));
			/* Create an entry with one 0 length log entry */
			hdr->logstamp = intswap32 (logstamp);
			hdr->size = 2;
			MFS_update_crc (hdr, 512, hdr->crc);
		}

		if (mfs_write_data (info->mfs, buf, logstart + loop, tocopy) < 0)
		{
			info->err_msg = "Error writing MFS data";
			return bsError;
		}

		if (loop / 4 == logstamp / 4)
		{
			hdr2 = (log_hdr *)(buf + 1536 - 512 * (logstamp & 3));
			/* Reset to the base entry */
			hdr->size = 0;
			hdr->logstamp = 0xffffffff;
			hdr->crc = hdr2->crc;
		}
	}

	*consumed = 0;

	return bsNextState;
}

/************************************************************************/
/* Restore the unknown region referenced in the volume header after the */
/* transaction log */
/* state_val1 = --unused-- */
/* state_val2 = --unised-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = Lowest available sector in first volume */
enum backup_state_ret
restore_state_unk_region_v3 (struct backup_info *info, void *data, unsigned
size, unsigned *consumed)
{
	unsigned unknsectors;
	uint64_t unkstart;
	unsigned unkstamp;
	unsigned char buf[2048];
	log_hdr *hdr, *hdr2;
	int loop;

	if (mfs_is_64bit (info->mfs))
	{
		unknsectors = intswap32 (info->mfs->vol_hdr.v64.unknsectors);
		unkstart = intswap64 (info->mfs->vol_hdr.v64.unkstart);
		unkstamp = intswap32 (info->mfs->vol_hdr.v64.unkstamp);
	}
	else
	{
		unknsectors = intswap32 (info->mfs->vol_hdr.v32.unksectors);
		unkstart = intswap32 (info->mfs->vol_hdr.v32.unkstart);
		unkstamp = intswap32 (info->mfs->vol_hdr.v32.unkstamp);
	}

	memset (buf, 0, sizeof (buf));

	/* Start by making the buffer look like a blank entry */
	hdr = (log_hdr *)buf;
	hdr->logstamp = 0xffffffff;
	MFS_update_crc (hdr, 512, hdr->crc);
	/* Copy the blank entry to the rest of the buffer */
	hdr2 = (log_hdr *)(buf + 512);
	hdr2->logstamp = 0xffffffff;
	hdr2->crc = hdr->crc;
	hdr2 = (log_hdr *)(buf + 1024);
	hdr2->logstamp = 0xffffffff;
	hdr2->crc = hdr->crc;
	hdr2 = (log_hdr *)(buf + 1536);
	hdr2->logstamp = 0xffffffff;
	hdr2->crc = hdr->crc;

	for (loop = 0; loop < unknsectors; loop += 4)
	{
		int tocopy = 4;

		if (unknsectors - loop < 4)
			tocopy = unknsectors - loop;

		if (loop / 4 == unkstamp / 4)
		{
			hdr = (log_hdr *)(buf + 512 * (unkstamp & 3));
			/* Create an entry with one 0 length log entry */
			hdr->logstamp = intswap32 (unkstamp);
			hdr->size = 2;
			MFS_update_crc (hdr, 512, hdr->crc);
		}

		if (mfs_write_data (info->mfs, buf, unkstart + loop, tocopy) < 0)
		{
			info->err_msg = "Error writing MFS data";
			return bsError;
		}

		if (loop / 4 == unkstamp / 4)
		{
			hdr2 = (log_hdr *)(buf + 1536 - 512 * (unkstamp & 3));
			/* Reset to the base entry */
			hdr->size = 0;
			hdr->logstamp = 0xffffffff;
			hdr->crc = hdr2->crc;
		}
	}

	*consumed = 0;

	return bsNextState;
}

/************************************************/
/* Write zone maps and reinitialize mfs library */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = Lowest available sector in current volume */
enum backup_state_ret
restore_state_mfs_reinit_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	if (restore_generate_zone_maps (info) < 0)
		return bsError;

	if (mfs_log_fssync (info->mfs) <= 0)
		return bsError;

	info->mfs->inode_log_type = info->ilogtype;

	return bsNextState;
}

/******************************/
/* Restore application inodes */
/* Write inode sector, followed by date for non tyStream inodes. */
/* state_val1 = current inode index */
/* state_val2 = offset within inode data */
/* state_ptr1 = current inode structure */
/* shared_val1 = size of current inode data */
enum backup_state_ret
restore_state_inodes_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	int needcommit = 0;
	int numsincecommit = 0;
	mfs_inode *inode;

	if (size == 0)
	{
		info->err_msg = "Internal error: Restore buffer empty";
		return bsError;
	}

	while (size > *consumed && info->state_val1 < info->ninodes)
	{
		uint64_t datasize;
		uint64_t allocsize;

		zone_header *bestzone, *zone;
		int zoneno, bestzoneno;
		unsigned int desired_zone_type;
		uint64_t bestzoneused;

/* Check for data to write on an existing inode */
		if (info->state_ptr1)
		{
			inode = (mfs_inode *)info->state_ptr1;
			uint64_t towrite = info->shared_val1 - info->state_val2;

			if (towrite > size - *consumed)
			{
				towrite = size - *consumed;
			}

			if (mfs_write_inode_data_part (info->mfs, inode, (unsigned char *)data + *consumed * 512, info->state_val2, towrite) <= 0)
			{
				free (info->state_ptr1);
				info->state_ptr1 = NULL;
				return bsError;
			}

			*consumed += towrite;
			info->state_val2 += towrite;

			if (info->state_val2 >= info->shared_val1)
			{
				free (info->state_ptr1);
				info->state_ptr1 = NULL;
				info->state_val1++;
				numsincecommit++;
			}

			continue;
		}

		if (numsincecommit > 128)
		{
			if (mfs_log_commit (info->mfs) <= 0)
			{
				return bsError;
			}
			numsincecommit = 0;
			needcommit = 0;
		}

		inode = (mfs_inode *)((unsigned char *)data + *consumed * 512);
		if (!(inode->inode_flags & intswap32 (INODE_DATA)))
		{
			if (inode->type == tyStream)
			{
				if (info->back_flags & BF_STREAMTOT)
					datasize = intswap32 (inode->size);
				else
					datasize = intswap32 (inode->blockused);
				datasize *= intswap32 (inode->blocksize);
				allocsize = intswap32 (inode->blocksize) * intswap32 (inode->size);
			}
			else
			{
				datasize = intswap32 (inode->size);
				allocsize = datasize;
			}
			datasize = (datasize + 511) / 512;
			allocsize = (allocsize + 511) / 512;
		}
		else
		{
			allocsize = 0;
			datasize = 0;
		}
		if (!datasize)
		{
			unsigned char tmpbuf[512];
			memcpy (tmpbuf, inode, 512);
			inode = (mfs_inode *)tmpbuf;
			if (mfs_log_inode_update (info->mfs, inode) <= 0)
			{
				return bsError;
			}
			numsincecommit++;
			info->state_val1++;

			++*consumed;

			continue;
		}

		inode = malloc (512);
		memcpy (inode, (unsigned char *)data + *consumed * 512, 512);
		info->state_ptr1 = inode;
		++*consumed;

		while (allocsize > 0)
		{
/* Find a zone to stick the data in */
			bestzone = NULL;
			zone = NULL;

			switch (inode->type)
			{
			case tyStream:
				desired_zone_type = ztMedia;
				break;
			default:
				desired_zone_type = ztApplication;
				break;
			}

			if (needcommit & (1 << desired_zone_type))
			{
				if (mfs_log_commit (info->mfs) <= 0)
				{
					return bsError;
				}
				numsincecommit = 0;
				needcommit = 0;
			}
			needcommit |= (1 << desired_zone_type);

			desired_zone_type = intswap32 (desired_zone_type);

			while ((zone = mfs_next_zone (info->mfs, zone)) != NULL)
			{
				uint64_t size;
				uint64_t free;

				if (mfs_is_64bit (info->mfs))
				{
					if (zone->z64.type != desired_zone_type)
						continue;
					size = intswap64 (zone->z64.size);
					free = intswap64 (zone->z64.free);
				}
				else
				{
					if (zone->z32.type != desired_zone_type)
						continue;
					size = intswap32 (zone->z32.size);
					free = intswap32 (zone->z32.free);
				}

/* Track which of this zone type it is */
				zoneno++;

/* Not enough space in this zone */
				if (free < allocsize)
				{
					if (!bestzone)
					{
						bestzone = zone;
						bestzoneno = zoneno;
						bestzoneused = ~0;
					}
					continue;
				}

/* Try and balance out usage across zones */
				if (!bestzone || size - free < bestzoneused)
				{
					bestzone = zone;
					bestzoneno = zoneno;
					bestzoneused = size - free;
				}
			}

			if (!bestzone)
			{
				info->err_msg = "Out of space to restore data";
				free (inode);
				return bsError;
			}
			else
			{
				uint64_t first;
				uint64_t size;
				uint64_t free;
				uint64_t datasector;
				uint64_t thiszoneamount = allocsize;
				uint32_t min_au;

				if (mfs_is_64bit (info->mfs))
				{
					first = intswap64 (bestzone->z64.first);
					size = intswap64 (bestzone->z64.size);
					free = intswap64 (bestzone->z64.free);
				}
				else
				{
					first = intswap32 (bestzone->z32.first);
					size = intswap32 (bestzone->z32.size);
					free = intswap32 (bestzone->z32.free);
				}

				if (allocsize > free)
				{
					thiszoneamount = free;
				}

				/* First zone move data towards the end (Middle of the disk) */
				/* Second zone move it towards the beginning (Also middle) */
				if (bestzoneno == 1)
				{
					datasector = first + free - thiszoneamount;
				}
				else
				{
					datasector = first + size - free;
				}

				restore_add_blocks_to_inode (info, bestzone, inode, datasector, thiszoneamount);
				allocsize -= thiszoneamount;
			}
		}

		if (mfs_log_inode_update (info->mfs, inode) < 0)
		{
			return bsError;
		}
		info->state_ptr1 = inode;
		info->shared_val1 = datasize;
		info->state_val2 = 0;
	}

	if (numsincecommit || needcommit)
	{
		if (mfs_log_commit (info->mfs) <= 0)
		{
			return bsError;
		}
	}

	if (info->state_val1 < info->ninodes)
		return bsMoreData;

	return bsNextState;
}

/***********************/
/* Finish restore (V3) */
/* state_val1 = --unused-- */
/* state_val2 = --unused-- */
/* state_ptr1 = --unused-- */
/* shared_val1 = --unused-- */
enum backup_state_ret
restore_state_complete_v3 (struct backup_info *info, void *data, unsigned size, unsigned *consumed)
{
	if (size == 0)
	{
		info->err_msg = "Internal error: Restore buffer empty";
		return bsError;
	}

	if (info->state != bsComplete)
	{
/* Go ahead and print it to stderr - this is really a development message */
		fprintf (stderr, "State machine missing states\n");
	}

	if (size > 1)
	{
		fprintf (stderr, "Extra blocks at end of restore???");
	}

	if (restore_cleanup_parts (info) < 0)
		return bsError;
	if (restore_make_swap (info) < 0)
		return bsError;
	if (restore_fudge_inodes (info) < 0)
		return bsError;
	if (restore_fudge_transactions (info) < 0)
		return bsError;

#if HAVE_SYNC
/* Make sure changes are committed to disk */
	sync ();
#endif

	if (compute_crc (data, 512, info->crc) != CRC32_RESIDUAL)
	{
		info->err_msg = "Backup CRC check failed: %08x != %08x";
		info->err_arg1 = (void *)compute_crc (data, 512, info->crc);
		info->err_arg2 = (void *)CRC32_RESIDUAL;
		return -1;
	}

	*consumed = 1;
	return bsNextState;
}

backup_state_handler restore_v3 = {
	NULL,									// bsScanMFS
	restore_state_begin_v3,					// bsBegin
	restore_state_partition_info,			// bsInfoPartition
	NULL,									// bsInfoBlocks
	restore_state_mfs_partition_info,		// bsInfoMFSPartitions
	restore_state_zone_map_info_v3,			// bsInfoZoneMaps
	restore_state_info_end,					// bsInfoEnd
	restore_state_boot_block,				// bsBootBlock
	restore_state_partitions,				// bsPartitions
	restore_state_mfs_init,					// bsMFSInit
	NULL,									// bsBlocks
	restore_state_volume_header_v3,			// bsVolumeHeader
	restore_state_transaction_log_v3,		// bsTransactionLog
	restore_state_unk_region_v3,			// bsUnkRegion
	restore_state_mfs_reinit_v3,			// bsMfsReinit
	restore_state_inodes_v3,				// bsInodes
	restore_state_complete_v3				// bsComplete
};
