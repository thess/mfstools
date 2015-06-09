#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <inttypes.h>

#include "mfs.h"
#include "macpart.h"
#include "log.h"

#include "mfsck.h"

void
mfsck_usage (char *progname)
{
	fprintf (stderr, "%s %s\n", PACKAGE, VERSION);
	fprintf (stderr, "Usage: %s [options] Adrive [Bdrive]\n", progname);
	fprintf (stderr, "Options:\n");
	fprintf (stderr, " -h        Display this help message\n");
	fprintf (stderr, " -r        Revalidate TiVo partitions on Adrive [Bdrive]\n");
#if DEBUG
	fprintf (stderr, " -m [1-5]  Set volume header magic to OK, FS_CHK, LOG_CHK, DB_CHK, or CLEAN\n");
	fprintf (stderr, " -e [1-3]  Set vol_hdr.v64.off0c to 0x00000010, TiVo, or Dish\n");
#endif
}

int
scan_bit_range (zone_bitmap *map, int startbit, int endbit, int desiredval)
{
	unsigned int desiredbits = 0;
	int startint;
	int endint;
	unsigned int startbits;
	unsigned int endbits;

	if (desiredval)
		desiredbits = ~0;

	startint = startbit / 32;
	endint = endbit / 32;
	startbit = startbit & 31;
	endbit = endbit & 31;
	startbits = ~((1 << startbit) - 1);
	if (endbit == 31)
		endbits = ~0;
	else
		endbits = (1 << (endbit + 1)) - 1;

	/* Easy case, they are the same int, so check the range between */
	if (startint == endint)
	{
		return (map->bits[startint] & startbits & endbits) == (desiredbits & startbits & endbits);
	}

	/* Check the bits in the first int */
	if ((map->bits[startint] & startbits) != (desiredbits & startbits))
	{
		return 0;
	}

	/* Check all the ints inbetween */
	while (++startint < endint)
	{
		if (map->bits[startint] != desiredbits)
		{
			return 0;
		}
	}

	/* Check the bits in the last int */
	if ((map->bits[endint] & endbits) != (desiredbits & endbits))
	{
		return 0;
	}

	return 1;
}

void
set_bit_range (zone_bitmap *map, int startbit, int endbit)
{
	int startint;
	int endint;
	unsigned int startbits;
	unsigned int endbits;

	startint = startbit / 32;
	endint = endbit / 32;
	startbit = startbit & 31;
	endbit = endbit & 31;
	startbits = ~((1 << startbit) - 1);
	if (endbit == 31)
		endbits = ~0;
	else
		endbits = (1 << (endbit + 1)) - 1;
	

	/* Easy case, they are the same int, so set the range between */
	if (startint == endint)
	{
		map->bits[startint] |= startbits & endbits;
		return;
	}

	/* Set the bits in the first int */
	map->bits[startint] |= startbits;

	/* Set all the ints inbetween */
	while (++startint < endint)
	{
		map->bits[startint] = ~0;
	}

	/* Set the bits in the last int */
	map->bits[endint] |= endbits;
}

void
clear_bit_range (zone_bitmap *map, int startbit, int endbit)
{
	int startint;
	int endint;
	unsigned int startbits;
	unsigned int endbits;

	startint = startbit / 32;
	endint = endbit / 32;
	startbit = startbit & 31;
	endbit = endbit & 31;
	startbits = ~((1 << startbit) - 1);
	if (endbit == 31)
		endbits = ~0;
	else
		endbits = (1 << (endbit + 1)) - 1;

	/* Easy case, they are the same int, so set the range between */
	if (startint == endint)
	{
		map->bits[startint] &= ~(startbits & endbits);
		return;
	}

	/* Set the bits in the first int */
	map->bits[startint] &= ~startbits;

	/* Set all the ints inbetween */
	while (++startint < endint)
	{
		map->bits[startint] = 0;
	}

	/* Set the bits in the last int */
	map->bits[endint] &= ~endbits;
}

void
scan_zone_maps (struct mfs_handle *mfs, zone_bitmap **ckbitmaps)
{
	uint64_t nextsector, sector;
	uint64_t nextsbackup, sbackup;
	uint32_t nextlength, length;
	uint64_t nextsize, size;
	uint32_t nextblocksize, blocksize;
	uint64_t first, last;
	uint32_t free, foundfree;
	uint64_t vol_set_size = mfs_volume_set_size (mfs);
	unsigned *fsmem_ptrs;
	int numbitmaps;

	zone_header *curzone = NULL;
	int zoneno = -1;
	int loop;
	
	zone_bitmap **bitmaploop;

	*ckbitmaps = NULL;

	uint64_t totalfree = 0;
	int totalbits = 0;

	/* Find the first zone pointer */
	if (mfs_is_64bit (mfs))
	{
		volume_header_64 *vol_hdr = &mfs_volume_header (mfs)->v64;
		nextsector = intswap64 (vol_hdr->zonemap.sector);
		nextsbackup = intswap64 (vol_hdr->zonemap.sbackup);
		nextlength = intswap64 (vol_hdr->zonemap.length);
		nextsize = intswap64 (vol_hdr->zonemap.size);
		nextblocksize = intswap64 (vol_hdr->zonemap.min);
	}
	else
	{
		volume_header_32 *vol_hdr = &mfs_volume_header (mfs)->v32;
		nextsector = intswap32 (vol_hdr->zonemap.sector);
		nextsbackup = intswap32 (vol_hdr->zonemap.sbackup);
		nextlength = intswap32 (vol_hdr->zonemap.length);
		nextsize = intswap32 (vol_hdr->zonemap.size);
		nextblocksize = intswap32 (vol_hdr->zonemap.min);
	}

	while ((curzone = mfs_next_zone (mfs, curzone)) != NULL)
	{
		int type;

		zoneno++;
		if (mfs_is_64bit (mfs))
		{
			sector = intswap64 (curzone->z64.sector);
			sbackup = intswap64 (curzone->z64.sbackup);
			length = intswap32 (curzone->z64.length);
			size = intswap64 (curzone->z64.size);
			blocksize = intswap32 (curzone->z64.min);
			first = intswap64 (curzone->z64.first);
			last = intswap64 (curzone->z64.last);
			free = intswap64 (curzone->z64.free);
			numbitmaps = intswap32 (curzone->z64.num);
			type = intswap32 (curzone->z64.type);
			fsmem_ptrs = (void *)(&curzone->z64 + 1);
		}
		else
		{
			sector = intswap32 (curzone->z32.sector);
			sbackup = intswap32 (curzone->z32.sbackup);
			length = intswap32 (curzone->z32.length);
			size = intswap32 (curzone->z32.size);
			blocksize = intswap32 (curzone->z32.min);
			first = intswap32 (curzone->z32.first);
			last = intswap32 (curzone->z32.last);
			free = intswap32 (curzone->z32.free);
			numbitmaps = intswap32 (curzone->z32.num);
			type = intswap32 (curzone->z32.type);
			fsmem_ptrs = (void *)(&curzone->z32 + 1);
		}

		/* Check the current zone against the previous zone's pointer */
		if (sector != nextsector)
		{
			printf ("Zone %d sector (%" PRId64 ") mismatch to zone %d nextsector (%" PRId64 ")\n", zoneno, sector, zoneno-1, nextsector);
		}
		if (sbackup != nextsbackup)
		{
			printf ("Zone %d alternate sector (%" PRId64 ") mismatch to zone %d next alternate sector (%" PRId64 ")\n", zoneno, sbackup, zoneno-1, nextsbackup);
		}
		if (length != nextlength)
		{
			printf ("Zone %d length (%d) mismatch to zone %d next length (%d)\n", zoneno, length, zoneno-1, nextlength);
		}
		if (size != nextsize)
		{
			printf ("Zone %d size (%" PRId64 ") mismatch to zone %d next size (%" PRId64 ")\n", zoneno, size, zoneno-1, nextsize);
		}
		if (blocksize != nextblocksize)
		{
			printf ("Zone %d block size (%d) mismatch to zone %d next block size (%d)\n", zoneno, blocksize, zoneno-1, nextblocksize);
		}

		if (mfs_is_64bit (mfs))
		{
			nextsector = intswap64 (curzone->z64.next_sector);
			nextsbackup = intswap64 (curzone->z64.next_sbackup);
			nextlength = intswap32 (curzone->z64.next_length);
			nextsize = intswap64 (curzone->z64.next_size);
			nextblocksize = intswap32 (curzone->z64.next_min);
		}
		else
		{
			nextsector = intswap32 (curzone->z32.next.sector);
			nextsbackup = intswap32 (curzone->z32.next.sbackup);
			nextlength = intswap32 (curzone->z32.next.length);
			nextsize = intswap32 (curzone->z32.next.size);
			nextblocksize = intswap32 (curzone->z32.next.min);
		}

		/* Check a few values for sanity */
		if (first > last)
		{
			printf ("Zone %d start sector > end sector (%" PRId64 " > %" PRId64 ")\n", zoneno, first, last);
		}
		if (size != last - first + 1)
		{
			printf ("Zone %d size (%" PRId64 ") mismatches difference between start and end sectors (%" PRId64 "-%" PRId64 ")\n", zoneno, size, first, last);
		}
		if (last >= vol_set_size)
		{
			printf ("Zone %d end sector (%" PRId64 ") past end of MFS volume (%" PRId64 ")\n", zoneno, last, vol_set_size);
		}
		if (size % blocksize)
		{
			printf ("Zone %d size is not divisible by blocksize (%" PRId64 " / %d remainder = %d)\n", zoneno, size, blocksize, (int) (size % blocksize));
		}

		/* Make sure this zone doesn't overlap with any others */
		for (loop = 0, bitmaploop = ckbitmaps; *bitmaploop; bitmaploop = &(*bitmaploop)->next, loop++)
		{
			if (first <= (*bitmaploop)->last && last >= (*bitmaploop)->first)
			{
				printf ("Zone %d (%" PRId64 "-%" PRId64 ") overlaps with zone %d (%" PRId64 "-%" PRId64 ")\n", zoneno, first, last, loop, (*bitmaploop)->first, (*bitmaploop)->last);
			}
		}

		size /= blocksize;

		*bitmaploop = calloc (sizeof (zone_bitmap), 1);
		(*bitmaploop)->bits = calloc ((size + 32) / 32, 4);
		(*bitmaploop)->type = type;
		(*bitmaploop)->first = first;
		(*bitmaploop)->last = last;
		(*bitmaploop)->blocksize = blocksize;
		foundfree = 0;

		for (loop = 0; loop < numbitmaps; loop++, size /= 2, blocksize *= 2)
		{
			unsigned int nbits, nints, setbits, foundbits;
			unsigned int curint;

			bitmap_header *bitmaphdr = (bitmap_header *)((size_t)&fsmem_ptrs[numbitmaps] + intswap32 (fsmem_ptrs[loop]) - intswap32 (fsmem_ptrs[0]));

			unsigned int *ints = (unsigned int *)(bitmaphdr + 1);

			/* Check to make sure it's not pointing off into random memory */
			if ((size_t)ints >= (size_t)curzone + length * 512 ||
				(size_t)ints + intswap32 (bitmaphdr->nints) * 4 >= (size_t)curzone + length * 512)
			{
				printf ("Zone %d bitmap %d is beyond end of the zone map\n", zoneno, loop);
				continue;
			}

			nbits = intswap32 (bitmaphdr->nbits);
			nints = intswap32 (bitmaphdr->nints);
			setbits = intswap32 (bitmaphdr->freeblocks);
			foundbits = 0;

			/* Sanity check the values in the bitmap header */
			if ((nbits + 31) / 32 != nints)
			{
				printf ("Zone %d bitmap %d number of ints (%d) does not match number of bits (%d bits / %d ints)\n", zoneno, loop, nints, nbits, (nbits + 31) / 32);
			}

			if (nbits < size)
			{
				printf ("Zone %d bitmap %d has fewer bits (%d) than needed (%" PRId64 ")\n", zoneno, loop, nbits, size);
			}

			/* Scan for set bits on a coarse level */
			for (curint = 0; curint < nints; curint++)
			{
				unsigned int bits;
				unsigned int curbit;
				/* Just track the last bit in this int for reporting what can */
				/* be combined in the bitmap */
				unsigned int lastbit = -1;

				/* If no bits found here, skip to the next int */
				if (!ints[curint])
					continue;

				/* Scan this int on a fine level */
				bits = intswap32 (ints[curint]);
				for (curbit = 0; curbit < 32 && bits; curbit++)
				{
					if (bits & (1 << (31 - curbit)))
					{
						int bitno = curint * 32 + curbit;

						if ((curbit & 1) && lastbit == curbit - 1)
						{
							printf ("Zone %d bitmap %d bits %d-%d (%" PRId64 "-%" PRId64 ") has blocks that could be combined\n", zoneno, loop, bitno - 1, bitno, (bitno - 1) * blocksize + (*bitmaploop)->first, (bitno + 1) * blocksize - 1 + (*bitmaploop)->first);
						}
						lastbit = curbit;

						/* Clear it so the loop can break out early */
						bits &= ~(1 << (31 - curbit));

						/* Make sure it is within the bitmap */
						if (bitno >= size)
						{
							printf ("Zone %d bitmap %d has free space beyond the zone - bit %d (%" PRId64 "-%" PRId64 ")\n", zoneno, loop, bitno, bitno * blocksize + (*bitmaploop)->first, (bitno + 1) * blocksize - 1 + (*bitmaploop)->first);
							continue;
						}

						/* Make sure the bit wasn't already set */
						if (!scan_bit_range (*bitmaploop, bitno << loop, ((bitno + 1) << loop) - 1, 0))
						{
							printf ("Zone %d bitmap %d bit %d (%" PRId64 "-%" PRId64 ") overlaps with previous bitmap\n", zoneno, loop, bitno, bitno * blocksize + (*bitmaploop)->first, (bitno + 1) * blocksize - 1 + (*bitmaploop)->first);
						}

						/* Track this bitmap's bit */
						foundbits++;
						set_bit_range (*bitmaploop, bitno << loop, ((bitno + 1) << loop) - 1);
					}
				}
			}

			if (foundbits != setbits)
			{
				printf ("Zone %d bitmap %d bits marked available (%d) mismatch against bitmap header (%d)\n", zoneno, loop, foundbits, setbits);
			}

			foundfree += foundbits * blocksize;
			totalbits += foundbits;
		}

		if (free != foundfree)
		{
			printf ("Zone %d free space (%d) does not match header (%d)\n", zoneno, foundfree, free);
		}
		totalfree += foundfree;
	}

	printf ("Total: %" PRId64 " free sectors in %d chunks across %d zone maps\n", totalfree, totalbits, zoneno + 1);
}

void
set_fsid_range (zone_bitmap *bitmap, int startbit, int endbit, unsigned int fsid)
{
	if (!bitmap->fsids)
	{
		bitmap->fsids = calloc (4, (bitmap->last + 1 - bitmap->first) / bitmap->blocksize);
	}

	while (startbit <= endbit)
	{
		bitmap->fsids[startbit] = fsid;
		startbit++;
	}
}

void
scan_inode_overlap (zone_bitmap *bitmap, unsigned int curinode, mfs_inode *inode, int bitno, int bitcount)
{
	int rangestart = -1;
	int rangefsid = 0;

	for (; bitcount > 0; bitcount--, bitno++)
	{
		int newfsid = 0;
		int isclear = scan_bit_range (bitmap, bitno, bitno, 0);

		if (bitmap->fsids)
		{
			newfsid = bitmap->fsids[bitno];
		}

		if (((newfsid != rangefsid && (rangefsid != 0 || rangestart >= 0))) || (rangestart >= 0 && isclear))
		{
			if (rangefsid > 0)
			{
				printf ("Inode %d fsid %d data block %" PRId64 " size %d overlaps with fsid %d\n", curinode, intswap32 (inode->fsid), bitno * bitmap->blocksize + bitmap->first, (bitno - rangestart) * bitmap->blocksize, rangefsid);
			}
			else
			{
				printf ("Inode %d fsid %d data block %" PRId64 " size %d marked free in zone map\n", curinode, intswap32 (inode->fsid), bitno * bitmap->blocksize + bitmap->first, (bitno - rangestart) * bitmap->blocksize);
			}
			if (isclear)
			{
				rangestart = -1;
				rangefsid = 0;
			}
			else
			{
				rangestart = bitno;
				rangefsid = newfsid;
			}
		}
		else if (newfsid != rangefsid)
		{
			if (isclear)
			{
				rangestart = -1;
				rangefsid = -1;
			}
			else
			{
				rangestart = bitno;
				rangefsid = newfsid;
			}
		}
	}

	if (rangefsid > 0)
	{
		printf ("Inode %d fsid %d data block %" PRId64 " size %d overlaps with fsid %d\n", curinode, intswap32 (inode->fsid), bitno * bitmap->blocksize + bitmap->first, (bitno - rangestart) * bitmap->blocksize, rangefsid);
	}
	else
	{
		printf ("Inode %d fsid %d data block %" PRId64 " size %d marked free in zone map\n", curinode, intswap32 (inode->fsid), bitno * bitmap->blocksize + bitmap->first, (bitno - rangestart) * bitmap->blocksize);
	}
}

void
scan_inodes (struct mfs_handle *mfs, zone_bitmap *bitmaps)
{
	int curinode = 0;
	int maxinode = mfs_inode_count (mfs);
	int loop;

	int nchained = 0;
	int chainlength = 0;
	int maxchainlength = 0;
	int extrachained = 0;
	int needchained = 0;
	int allocinode = 0;

	int maxblocks = 0;

	mfs_inode *inode;

	// Bit 1 = chain needed, bit 2 = chain set
	unsigned char *chained_inodes = calloc (1, maxinode);

	if (mfs_is_64bit (mfs))
	{
		maxblocks = (512 - sizeof (*inode)) / sizeof (inode->datablocks.d64[0]);
	}
	else
	{
		maxblocks = (512 - sizeof (*inode)) / sizeof (inode->datablocks.d32[0]);
	}

	for (curinode = 0; curinode < maxinode; curinode++)
	{
		inode = mfs_read_inode (mfs, curinode);

		if (!inode)
		{
			if (mfs_has_error (mfs))
			{
				char msg[1024];
				mfs_strerror (mfs, msg);
				printf ("Error reading inode %d: %s\n", curinode, msg);
				mfs_clearerror (mfs);
			}
			else
			{
				printf ("Error reading inode %d: Unknown\n", curinode);
			}
			continue;
		}

		switch (intswap32 (inode->sig))
		{
		case MFS32_INODE_SIG:
			if (mfs_is_64bit (mfs))
			{
				printf ("Inode %d claims to be 32 bit in 64 bit volume\n", curinode);
			}
			break;
		case MFS64_INODE_SIG:
			if (!mfs_is_64bit (mfs))
			{
				printf ("Inode %d claims to be 64 bit in 32 bit volume\n", curinode);
			}
			break;
		default:
			printf ("Inode %d unknown signature %08x\n", curinode, intswap32 (inode->sig));
			break;
		}

		/* Mark if this inode is chained */
		if (inode->inode_flags & intswap32 (INODE_CHAINED))
		{
			chained_inodes[curinode] |= 2;
		}

		if (inode->fsid)
		{
			int curchainlength = 0;
			int expectedzonetype;

			allocinode++;

			/* Mark if this fsid needs any inodes before it chained */
			loop = intswap32 (inode->fsid) * MFS_FSID_HASH % maxinode;
			while (loop != curinode)
			{
				chained_inodes[loop] |= 1;
				curchainlength++;
				loop = (loop + 1) % maxinode;
			}

			/* Track statistics on chained inodes */
			if (curchainlength > 0)
			{
				nchained++;
				chainlength += curchainlength;
				if (curchainlength > maxchainlength)
					maxchainlength = curchainlength;
			}

			if (intswap32 (inode->inode) != curinode)
			{
				printf ("Inode %d fsid %d inode number mismatch with data %d\n", curinode, intswap32 (inode->fsid), intswap32 (inode->inode));
			}

			if (!inode->refcount)
			{
				printf ("Inode %d fsid %d has zero reference count\n", curinode, intswap32 (inode->fsid));
			}

			switch (inode->type)
			{
			case tyStream:
				if (inode->blocksize != inode->unk3)
				{
					printf ("Inode %d fsid %d stream total block blocksize %d mismatch used block blocksize %d\n", curinode, intswap32 (inode->fsid), intswap32 (inode->unk3), intswap32 (inode->blocksize));
				}
				if (intswap32 (inode->size) < intswap32 (inode->blockused))
				{
					printf ("Inode %d fsid %d stream total block count %d less than used block count %d\n", curinode, intswap32 (inode->fsid), intswap32 (inode->size), intswap32 (inode->blockused));
				}
				expectedzonetype = ztMedia;
				if (inode->zone != 1)
				{
					printf ("Inode %d fsid %d marked for data type %d (Expect 1)\n", curinode, intswap32 (inode->fsid), inode->zone);
				}
				break;
			default:
				printf ("Inode %d fsid %d unknown type %d\n", curinode, intswap32 (inode->fsid), inode->type);
				/* Intentionally fall through */
			case tyFile:
			case tyDir:
			case tyDb:
				if (inode->blocksize || inode->blockused || inode->unk3)
				{
					printf ("Inode %d fsid %d non-stream inode defines stream block sizes\n", curinode, intswap32 (inode->fsid));
				}
				expectedzonetype = ztApplication;
				if (inode->zone != 2)
				{
					printf ("Inode %d fsid %d marked for data type %d (Expect 2)\n", curinode, intswap32 (inode->fsid), inode->zone);
				}
				break;
			}

			if (inode->inode_flags & intswap32 (INODE_DATA) || inode->inode_flags & intswap32 (INODE_DATA2))
			{
				if (inode->numblocks)
				{
					printf ("Inode %d fsid %d has data in inode block and non-zero extent count %d\n", curinode, intswap32 (inode->fsid), intswap32 (inode->numblocks));
				}

				if (intswap32 (inode->size) + sizeof (*inode) > 512)
				{
					printf ("Inode %d fsid %d has data in inode block but size %d greather than max allowed %d\n", curinode, intswap32 (inode->fsid), intswap32 (inode->size), (int) (512 - sizeof (*inode)));
				}

				if (inode->type == tyStream)
				{
					printf ("Inode %d fsid %d has data in inode block with tyStream data type\n", curinode, intswap32 (inode->fsid));
				}
			}
			else if (intswap32 (inode->numblocks) > maxblocks)
			{
				printf ("Inode %d fsid %d has more extents (%d) than max (%d)\n", curinode, intswap32 (inode->fsid), intswap32 (inode->numblocks), maxblocks);
			}
			else
			{
				uint64_t totalsize=0;

				for (loop = 0; loop < intswap32 (inode->numblocks); loop++)
				{
					uint64_t sector;
					uint32_t count;
					int bitno;
					int bitcount;

					if (mfs_is_64bit (mfs))
					{
						sector = sectorswap64 (inode->datablocks.d64[loop].sector);
						count = intswap32 (inode->datablocks.d64[loop].count);
					}
					else
					{
						sector = intswap32 (inode->datablocks.d32[loop].sector);
						count = intswap32 (inode->datablocks.d32[loop].count);
					}

					totalsize += count;

					zone_bitmap *bitmapforblock = bitmaps;
					while (bitmapforblock->first > sector || bitmapforblock->last < sector)
						bitmapforblock = bitmapforblock->next;

					if (!bitmapforblock)
					{
						printf ("Inode %d fsid %d extent %d (Sector %" PRId64 " size %d) not within any zone\n", curinode, intswap32 (inode->fsid), loop, sector, count);
						continue;
					}

					if (expectedzonetype != bitmapforblock->type)
					{
						printf ("Inode %d fsid %d expected zone type %d but extent %d is in type %d\n", curinode, intswap32 (inode->fsid), expectedzonetype, loop, bitmapforblock->type);
					}

					if ((sector - bitmapforblock->first) % bitmapforblock->blocksize)
					{
						printf ("Inode %d fsid %d extent %d (Sector %" PRId64 " size %d) not aligned inside zone\n", curinode, intswap32 (inode->fsid), loop, sector, count);
						continue;
					}

					if (count % bitmapforblock->blocksize)
					{
						printf ("Inode %d fsid %d extent %d (Sector %" PRId64 " size %d) size not a multiple of zone block size\n", curinode, intswap32 (inode->fsid), loop, sector, count);
						continue;
					}

					/* Make sure the range isn't marked already */
					bitno = (sector - bitmapforblock->first) / bitmapforblock->blocksize;
					bitcount = count / bitmapforblock->blocksize;
					if (!scan_bit_range (bitmapforblock, bitno, bitno + bitcount - 1, 0))
					{
						scan_inode_overlap (bitmapforblock, curinode, inode, bitno, bitcount);
					}
					set_bit_range (bitmapforblock, bitno, bitno + bitcount - 1);
					set_fsid_range (bitmapforblock, bitno, bitno + bitcount - 1, intswap32 (inode->fsid));
				}

				if (inode->type == tyStream)
				{
					if (totalsize*512 < intswap32 (inode->size) * intswap32 (inode->unk3))
					{
						printf ("Inode %d fsid %d allocated size (%" PRId64 ") less than data size (%" PRId64 ")\n", curinode, intswap32 (inode->fsid), totalsize*512, (uint64_t)intswap32 (inode->size) * (uint64_t)intswap32 (inode->unk3));
					}
				}
				else
				{
					if (totalsize*512 < intswap32 (inode->size))
					{
						printf ("Inode %d fsid %d allocated size (%" PRId64 ") less than data%s size (%" PRId64 ")\n", curinode, intswap32 (inode->fsid), totalsize*512, intswap32 (inode->numblocks) ? "" : " (in inode)", (uint64_t)intswap32 (inode->size));
					}
				}
			}
		}
		else
		{
			if (inode->refcount)
			{
				printf ("Inode %d has %d references and no fsid\n", curinode, intswap32 (inode->refcount));
			}

			if (inode->numblocks)
			{
				printf ("Inode %d free but has datablocks allocated to it\n", curinode);
			}
		}

		free (inode);
	}

	for (curinode = 0; curinode < maxinode; curinode++)
	{
		/* Bit 1 = chain needed, bit 2 = chain set */
		switch (chained_inodes[curinode])
		{
			case 1:
				printf ("Inode %d requires chained flag, but not set\n", curinode);
				needchained++;
				break;
			case 2:
				extrachained++;
				break;
		}
	}

	printf ("%d/%d inodes used\n", allocinode, maxinode);
	if (nchained)
	{
		printf ("%d fsids in chained inodes, %d max inode chain length, %d average length\n", nchained, maxchainlength, (chainlength + nchained / 2) / nchained);
	}
	if (extrachained || needchained)
	{
		printf ("%d inodes unnecessarily chained, %d not chained need to be\n", extrachained, needchained);
	}

	free (chained_inodes);
}

void
scan_unclaimed_blocks (struct mfs_handle *mfs, zone_bitmap *bitmap)
{
	while (bitmap)
	{
		int nbits = (bitmap->last + 1 - bitmap->first) / bitmap->blocksize;
		int nints = (nbits + 31) / 32;
		int loop;

		int startunclaimed = -1;

		for (loop = 0; loop < nints; loop++)
		{
			if (bitmap->bits[loop] != ~0)
			{
				int loop2;
				for (loop2 = 0; loop2 < 32 && loop2 + loop * 32 < nbits; loop2++)
				{
					if (!(bitmap->bits[loop] & (1 << loop2)))
					{
						if (startunclaimed < 0)
						{
							startunclaimed = loop2 + loop * 32;
						}
					}
					else if (startunclaimed >= 0)
					{
						printf ("Block type %d at %" PRId64 " for %" PRId64 " sectors unclaimed by zone maps or inodes\n", bitmap->type, (uint64_t)startunclaimed * bitmap->blocksize + bitmap->first, (uint64_t)(loop2 + loop * 32 - startunclaimed) * bitmap->blocksize);
						startunclaimed = -1;
					}
				}
			}
			else if (startunclaimed >= 0)
			{
				printf ("Block type %d at %" PRId64 " for %" PRId64 " sectors unclaimed by zone maps or inodes\n", bitmap->type, (uint64_t)startunclaimed * bitmap->blocksize + bitmap->first, (uint64_t)(loop * 32 - startunclaimed) * bitmap->blocksize);
				startunclaimed = -1;
			}
		}

		if (startunclaimed >= 0)
		{
			printf ("Block type %d at %" PRId64 " for %" PRId64 " sectors unclaimed by zone maps or inodes\n", bitmap->type, (uint64_t)startunclaimed * bitmap->blocksize + bitmap->first, (uint64_t)(nbits - startunclaimed) * bitmap->blocksize);
			startunclaimed = -1;
		}

		bitmap = bitmap->next;
	}
}

int
mfsck_main (int argc, char **argv)
{
	int opt = 0;
	struct mfs_handle *mfs;
	zone_bitmap *usedblocks;
	char *tmp;
	int inconsistent = 0;
	int esata = 0;
	int doreval = 0;

	tivo_partition_direct ();

#if DEBUG
	while ((opt = getopt (argc, argv, "hm:e:r")) > 0)
#else
	while ((opt = getopt (argc, argv, "hr")) > 0)
#endif
	{
		switch (opt)
		{
#if DEBUG
		case 'm':
			inconsistent = strtoul (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -m.\n", argv[0]);
				return 1;
			}
			if (inconsistent < 1 || inconsistent > 5)
			{
				fprintf (stderr, "%s: The value for -m must be in the range of 1 to 5.\n", argv[0]);
				return 1;
			}
			break;
		case 'e':
			esata = strtoul (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -s.\n", argv[0]);
				return 1;
			}
			if (esata < 1 || esata > 3)
			{
				fprintf (stderr, "%s: The value for -s must be in the range of 1 to 3.\n", argv[0]);
				return 1;
			}
			break;
#endif
		case 'r':
			doreval = 1;
			break;
		default:
			mfsck_usage (argv[0]);
			return 1;
		}
	}

	if (optind == argc || argc > optind + 2)
	{
		mfsck_usage (argv[0]);
		return 4;
	}

	if (doreval)
	{
		fprintf (stderr, "Revalidating partion table on %s...  ", argv[optind]);
		if (revalidate_drive(argv[optind]) < 0)
			fprintf (stderr, "Failed!\n");
		else
			fprintf (stderr, "Success!\n");
		if (optind + 1 < argc)
		{
			fprintf (stderr, "Revalidating partion table on %s...  ", argv[optind + 1]);
			if (revalidate_drive(argv[optind + 1]) < 0)
				fprintf (stderr, "Failed!\n");
			else
				fprintf (stderr, "Success!\n");
		}
		return 0;
	}
	
	mfs = mfs_init (argv[optind], optind + 1 < argc? argv[optind + 1] : NULL, (O_RDONLY | MFS_ERROROK));

	if (!mfs)
	{
		fprintf (stderr, "Unable to open MFS volume.\n");
		return 1;
	}

	if (mfs_has_error (mfs))
	{
		mfs_perror (mfs, argv[0]);
		return 1;
	}
	
#if DEBUG
  // Advanced option to (re)set the volume header magic.  Left in because I still have v3 backups without shrink forced...
	if (inconsistent>0)
	{
		uint32_t magic = 0;
		switch (inconsistent)
		{
		case 2:
			// Filesystem is inconsistent - cannot mount!
			// Filesystem is inconsistent, will attempt repair! 
			// Green Screen mfscheck will occur
			printf ("\nSetting volume magic to  magic to MFS_MAGIC_FS_CHK(");
			magic=MFS_MAGIC_FS_CHK;
			break;
		case 3:
			// Filesystem is inconsistent - cannot mount!
			// Filesystem logs are bad - log roll-forward inhibited!
			// Green Screen mfscheck will occur
			// NOTE: May result in a magic=MFS_MAGIC_FS_CHK and 2nd Green Screen
			printf ("\nSetting volume magic to  magic to MFS_MAGIC_LOG_CHK(");
			magic=MFS_MAGIC_LOG_CHK;
			break;
		case 4:
			// Database is inconsistent - cannot mount! 
			// fsfix:  mounted MFS volume, starting consistency checks.
			// Green Screen mfscheck will occur
			printf ("\nSetting volume magic to  magic to MFS_MAGIC_DB_CHK(");
			magic=MFS_MAGIC_DB_CHK;
			break;
		case 5:
			// No consistency warnings or fsfix messages
			// Cleans up recordings with no tystream, but does not cause a Green Screen mfscheck
			// NOTE: Experimientally determined to no be valid for S1.
			printf ("\nSetting volume magic to  magic to MFS_MAGIC_CLEAN(");
			magic=MFS_MAGIC_CLEAN;
			break;
		default:
			printf ("\nSetting volume magic to  magic to MFS_MAGIC_OK(");
			magic=MFS_MAGIC_OK;
		}
		
		if (mfs_is_64bit (mfs))
			magic=magic | MFS_MAGIC_64BIT;

		printf("0x%08x)\n", magic);
		// Doesn't matter if the header is 32 or 64, as long as we write to the correct endianness version
		mfs_reinit (mfs, O_RDWR);
		if (mfsLSB)
			mfs->vol_hdr.v32.magicLSB = intswap32 (magic);
		else
			mfs->vol_hdr.v32.magicMSB = intswap32 (magic);
		mfs_write_volume_header (mfs);
		mfs_reinit (mfs, O_RDONLY);
		if (esata == 0)
			exit(0);
	}
	
	if (esata>0 && mfs->is_64)
	{
		mfs_reinit (mfs, O_RDWR);
		switch (esata)
		{
		case 1:
			fprintf (stderr, "Previous off0c = 0x%08x (0x%08x) new off0c = 0x%08x (0x%08x)\n", intswap32 (mfs->vol_hdr.v64.off0c), intswap32 (mfs->vol_hdr.v64.off14), 0x10, 0x40);
			mfs->vol_hdr.v64.off0c = intswap32 (0x00000010);
			mfs->vol_hdr.v64.off14 = intswap32 (0x00000040);
			break;
		case 2:
			fprintf (stderr, "Previous off0c = 0x%08x (0x%08x) new off0c = 0x%08x (0x%08x)\n", intswap32 (mfs->vol_hdr.v64.off0c), intswap32 (mfs->vol_hdr.v64.off14), 0x5469566F, 0x0);
			mfs->vol_hdr.v64.off0c = intswap32 (0x5469566F);
			mfs->vol_hdr.v64.off14 = intswap32 (0x0);
			break;
		case 3:
			fprintf (stderr, "Previous off0c = 0x%08x (0x%08x) new off0c = 0x%08x (0x%08x)\n", intswap32 (mfs->vol_hdr.v64.off0c), intswap32 (mfs->vol_hdr.v64.off14), 0x44697368, 0xffffffff);
			mfs->vol_hdr.v64.off0c = intswap32 (0x44697368);
			mfs->vol_hdr.v64.off14 = intswap32 (0xffffffff);
			break;
		default:
			printf ("\nInvalid setting, bailing...");
			exit(0);
		}
		mfs_write_volume_header (mfs);
		mfs_reinit (mfs, O_RDONLY);
		exit(0);
	}
#endif

	printf ("\nChecking zone maps...\n");
	scan_zone_maps (mfs, &usedblocks);

	while (usedblocks)
	{
		zone_bitmap *tmp = usedblocks;
		usedblocks = tmp->next;
		if (tmp->bits)
			free (tmp->bits);
		if (tmp->fsids)
			free (tmp->fsids);
		free (tmp);
	}

	printf ("Replaying transaction log...\n");
	mfs_enable_memwrite (mfs);
	mfs_log_fssync (mfs);

	printf ("Re-scanning zone maps...\n");
	scan_zone_maps (mfs, &usedblocks);

	printf ("Scanning inodes...\n");
	scan_inodes (mfs, usedblocks);

	printf ("Checking for unclaimed blocks...\n");
	scan_unclaimed_blocks (mfs, usedblocks);

	printf ("Done!\n");
	return 0;
}
