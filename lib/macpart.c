#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#if HAVE_SYS_DISK_H
#include <sys/disk.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <sys/param.h>
#ifdef HAVE_LINUX_FS_H
#include <linux/fs.h>
#endif
#ifdef HAVE_LINUX_UNISTD_H
#include <linux/unistd.h>
#endif

#include <linux/blkpg.h>
#include <sys/ioctl.h> 

#if !defined(BLKGETSIZE64)
# define BLKGETSIZE64 _IOR(0x12,114,size_t) /* return device size in bytes (uint64_t * arg) */
#endif

#include "mfs.h"
#include "macpart.h"


/* Some static variables..  Really this should be a class and these */
/* private members. */
static struct tivo_partition_table *partition_tables = NULL;
static enum
{ accAUTO, accDIRECT, accKERNEL }
tivo_partition_accmode = accAUTO;

static int tivo_partition_open_direct_int (tpFILE *file, char *path, int partnum, int flags);
void data_swab (void *data, int size);

/* Get the size of a file or device.  It also returns weather it is a file */
/* or device being dealth with. */
static int
file_or_dev_size (int fd, uint64_t *size)
{
#ifdef BLKGETSIZE64
	uint64_t sz64;
#endif
#ifdef BLKGETSIZE
	uint32_t sz32;
#endif
#ifdef O_LARGEFILE
	struct stat64 st;
#else
	struct stat st;
#endif

#ifdef DKIOCGETBLOCKCOUNT      /* For Mac OS X */
	{
		uint32_t	blocksize;
		uint64_t	size64;
		
		if (ioctl(fd, DKIOCGETBLOCKSIZE, &blocksize) == 0 &&
			ioctl(fd, DKIOCGETBLOCKCOUNT, &size64) == 0)
		{
			if ((sizeof(*size) < sizeof(unsigned long long))
			   && ((size64 * (blocksize / 512)) > 0xFFFFFFFF))
			       return EFBIG;

			*size = size64 * (blocksize / 512);
			return 1;
		}
	}
#endif

#ifdef BLKGETSIZE64
	if (ioctl (fd, BLKGETSIZE64, &sz64 ) == 0)
	{
		*size =  sz64 / 512;
		return 1;
	}
#endif

#ifdef BLKGETSIZE
	if (ioctl (fd, BLKGETSIZE, &sz32 ) == 0)
	{
		*size =  sz32;
		return 1;
	}
#endif

#ifdef O_LARGEFILE
	if (fstat64 (fd, &st) == 0)
#else
	if (fstat (fd, &st) == 0)
#endif
	{
		//TODO: Should this be 
		//*size = st.st_blocks / 512;
		*size = st.st_blocks;
		return 0;
	}
	return -1;
}

/****************************************************************************/
/* Opens a file normally.  If it fails with EFBIG open it with O_LARGEFILE. */
int
lfopen (const char *device, int flags)
{
	int fd = open (device, flags);

#ifdef O_LARGEFILE
	if (fd < 0 && errno == EFBIG)
	{
		errno = 0;
		fd = open (device, flags | O_LARGEFILE);
	}
#endif

	return fd;
}

/**************************************************/
/* Read the TiVo partition table off of a device. */
struct tivo_partition_table *
tivo_read_partition_table (const char *device, int flags)
{
	struct tivo_partition_table *table;

/* See if this device has been opened yet. */
	for (table = partition_tables; table; table = table->next)
	{
	     if (table->device != NULL) {		
		if (!strcmp (device, table->device))
		{
			break;
		}
	}
	}

/* If not, open it and read the partition table. */
	if (!table)
	{

		int *fd;
		unsigned char buf[512];
		uint64_t cursec;
		uint64_t maxsec = 1;
		int partitions = 0;
		struct tivo_partition parts[256];

		table = calloc (sizeof (*table), 1);

		if (!table)
		{
			fprintf (stderr, "Out of memory!\n");
			return 0;
		}

		table->ro_fd = -1;
		table->rw_fd = -1;

/* Figure out if we are supposed to open it RO or RW, and use the right fd */
/* variable. */
		fd = (flags & O_ACCMODE) == O_RDONLY ? &table->ro_fd : &table->rw_fd;
		*fd = lfopen (device, flags);
		if (*fd < 0)
		{
			free (table);
			return 0;
		}

/* Get the size and if it is a file or device. */
		switch (file_or_dev_size (*fd, &table->devsize))
		{
		case 0:
			table->vol_flags |= VOL_FILE;
		case 1:
			break;
		default:
			close (*fd);
			free (table);
			return 0;
		}

/* Read the boot block. */
		lseek (*fd, 0, SEEK_SET);
		if (read (*fd, buf, 512) != 512)
		{
			close (*fd);
			free (table);
			return 0;
		}

/* Find out what the magic is in the bootblock. */
		switch (intswap16 (*(unsigned short *) buf))
		{
		case TIVO_BOOT_MAGIC:
/* It is the right magic.  Do nothing. */
			break;
		case TIVO_BOOT_AMIGC:
/* It is the right magic, but it is byte-swapped (swab) or the wrong endianness. */
			data_swab(buf, 512);
			if (strstr((char *)buf+4, "/hda"))
			{
				// Series 1 
			table->vol_flags |= VOL_SWAB;
			}
			else
			{
				struct mac_partition *part;

				// You would think if the boot magic is swapped then so is the mac magic, but we had better not assume that...
				// Should be safe to assume the MFS is little endian.
				mfsLSB=1;
#if DEBUG
				fprintf (stderr, "MFS detected as LSB (Roamio or newer?).  Switching MFS to little endian.\n");
#endif
				// Load the first parttion, so we can inspect it's endianness
				lseek (*fd, 512, SEEK_SET);
				if (read (*fd, buf, 512) != 512)
				{
					close (*fd);
					free (table);
					return 0;
				}
				part = (struct mac_partition *) buf;
				
				if (partintswap16 (part->signature) == MAC_PARTITION_AMIGC || partintswap16 (part->signature) == TIVO_BIGPARTITION_AMIGC)
				{
					partLSB = 1;
				}
				else if (partintswap16 (part->signature) == MAC_PARTITION_MAGIC || partintswap16 (part->signature) == TIVO_BIGPARTITION_MAGIC)
				{
#if DEBUG
					fprintf (stderr, "MAC_PARTITION_MAGIC is not the same endianness as TIVO_BOOT_MAGIC.\n");
#endif
					partLSB = 0;
				}
				else
				{					
					return 0;
				}
			}
			break;
		default:
/* Wrong magic.  Bail. */
			close (*fd);
			free (table);
			return 0;
		}

/* At this point, the size of the partition table is not known.  However, it */
/* is the first sector.  So the maxsec is bootstrapped to 1, and is updated */
/* after the first sector. */
		for (cursec = 1; cursec <= maxsec && partitions < 256; cursec++)
		{
			struct mac_partition *part;
			struct tivo_bigpartition *bigpart;
			lseek (*fd, 512 * cursec, SEEK_SET);
			if (read (*fd, buf, 512) != 512)
			{
				close (*fd);
				free (table);
				return 0;
			}
			if (table->vol_flags & VOL_SWAB)
			{
				data_swab (buf, 512);
			}
			part = (struct mac_partition *) buf;

/* If this is the first, update the max. */
			if (cursec == 1)
			{
				maxsec = partintswap32 (part->map_count);
			}

/* If it doesn't have the magic, it's not hip.  No more partitions. */
			if (partintswap16 (part->signature) == MAC_PARTITION_MAGIC)
			{

/* Add it to the list. */
				parts[partitions].start = partintswap32 (part->start_block);
				parts[partitions].sectors = partintswap32 (part->block_count);
			parts[partitions].name = strdup (part->name);
			parts[partitions].type = strdup (part->type);
			parts[partitions].refs = 1;
#if DEBUG
				fprintf (stderr, "Part %s(%s) signature == MAC_PARTITION_MAGIC %u %u.\n", parts[partitions].name, parts[partitions].type, parts[partitions].start, parts[partitions].sectors);
#endif
			partitions++;
		}
			else if (partintswap16 (part->signature) == TIVO_BIGPARTITION_MAGIC)
			{
				bigpart = (struct tivo_bigpartition *) buf;

/* Add it to the list. */
				parts[partitions].start = partintswap64 (bigpart->start_block);
				parts[partitions].sectors = partintswap64 (bigpart->block_count);
				parts[partitions].name = strdup (bigpart->name);
				parts[partitions].type = strdup (bigpart->type);
				parts[partitions].refs = 1;
#if DEBUG
				fprintf (stderr, "Part %s(%s) signature == TIVO_BIGPARTITION_MAGIC %llu %llu.\n", parts[partitions].name, parts[partitions].type, parts[partitions].start, parts[partitions].sectors);
#endif
				partitions++;

			} else 
				break;

		}

/* No partitions.  None.  Nada. */
		if (partitions == 0)
		{
			close (*fd);
			free (table);
			return 0;
		}

/* If it doesn't make sense, it doesn't make sense. */
		if (partitions > parts[0].sectors)
		{
			close (*fd);
			free (table);
			return 0;
		}

/* Copy the partitions into place. */
		table->partitions = calloc (parts[0].sectors, sizeof (struct tivo_partition));
		if (!table->partitions)
		{
			close (*fd);
			free (table);
			return 0;
		}
		memcpy (table->partitions, parts, partitions * sizeof (struct tivo_partition));
		table->count = partitions;
		table->allocated = parts[0].sectors;
		table->device = strdup (device);
		table->next = partition_tables;
		partition_tables = table;
	}
	else
	{
/* Okay, the table already exists.  Make sure the device is opened for the */
/* proper access mode already, as well. */

		if ((flags & O_ACCMODE) == O_RDONLY)
		{
			if (table->ro_fd < 0)
			{
				table->ro_fd = lfopen (device, flags);
			}
		}
		else
		{
			if (table->rw_fd < 0)
			{
				table->rw_fd = lfopen (device, flags);
			}
		}
	}

	return table;
}

/***************************************************************************/
/* Preforms the equivelent of the BLKRRPART ioctl.  Really this just frees */
/* the structure, forcing a device re-read next time. */
int
tivo_partition_rrpart (const char *device)
{
	struct tivo_partition_table **table;

/* See if this device has been opened yet. */
	for (table = &partition_tables; *table; table = &(*table)->next)
	{
		if (!strcmp (device, (*table)->device))
		{
			break;
		}
	}

	if (*table)
	{
		int loop;
		struct tivo_partition_table *tofree = *table;

		if (tofree->refs != 0)
		{
			errno = EBUSY;
			return -1;
		}

		for (loop = 0; loop < tofree->count; loop++)
		{
			if (tofree->partitions[loop].refs > 1)
			{
				errno = EBUSY;
				return -1;
			}
		}

		*table = tofree->next;

		for (loop = 0; loop < tofree->count; loop++)
		{
			if (tofree->partitions[loop].name)
				free (tofree->partitions[loop].name);
			if (tofree->partitions[loop].type)
				free (tofree->partitions[loop].type);
		}
		free (tofree->partitions);
		if (tofree->ro_fd >= 0)
			close (tofree->ro_fd);
		if (tofree->rw_fd >= 0)
			close (tofree->rw_fd);
		if (tofree->device)
			free (tofree->device);
		bzero (tofree, sizeof (tofree));
		free (tofree);
		return 0;
	}

	return 0;
}

int
tivo_partition_validate (struct tivo_partition_table *table)
{
	uint64_t loop;
	char partsused[256];

	if (!table)
	{
		return -1;
	}

/* Only need to validate once. */
	if (table->vol_flags & VOL_VALID)
		return 0;

	bzero (&partsused, sizeof (partsused));

	loop = 1;

	if (table->count >= 256)
	{
		return -1;
	}

/* Loop through each sector on the drive. */
	while (loop < table->devsize)
	{
		int partno = 0, loop2;
/* Find the nonzero partition that is closest to this sector. */
		for (loop2 = 0; loop2 < table->count; loop2++)
			if (table->partitions[loop2].start >= loop && (partno < 1 || table->partitions[loop2].start < table->partitions[partno - 1].start) && (table->partitions[loop2].sectors != 0))
				partno = loop2 + 1;

/* If there is no partition and it is beyond the first sector, there are no */
/* more partitions. */
		if (!partno)
			break;

		partno--;

/* Mark this partition as used. */
		partsused[partno] = 1;

/* If there was a gap, create a partition to fill it. */
		if (table->partitions[loop2].start > loop && table->partitions[loop2].start - loop > 3)
		{
/* If the table is too big for the first partition or for a max of 256, */
/* which is very generous since TiVo only allows 15 max, report an error. */
			if (table->count >= table->allocated || table->count >= 256)
				return -1;

/* Create the new partition. */
			table->partitions[table->count].sectors = table->partitions[loop2].start - loop;
			table->partitions[table->count].start = loop;
			table->partitions[table->count].refs = 1;
			table->partitions[table->count].name = strdup ("Extra");
			table->partitions[table->count].type = strdup ("Apple_Free");
			table->partitions[table->count].table = table;
			partsused[table->count] = 1;
			table->count++;
		}

/* Mathematically speaking, this prevents integer wrap.  The proof of this */
/* is if x (Sector number) + y (Partition size) > z (Integer size) then */
/* x + y - y > z - y or x > z - y.  To put this in other terms, if the */
/* sector number is greater than the max int - the partition size, there */
/* will be wrap.  In this case, 0 is taking the place of maxint, since it */
/* will wrap. */
		if (loop > (uint64_t)(0 - table->partitions[partno].sectors))
		{
			return -1;
		}

		loop += table->partitions[partno].sectors;
	}

/* If the partition table describes too many partitions, it is an error. */
	if (loop > table->devsize)
		return -1;

/* If the partition table describes too few partitions, create a filler. */
	if (loop < table->devsize && table->devsize - loop > 3)
	{
/* If the table is too big for the first partition or for a max of 256, */
/* which is very generous since TiVo only allows 15 max, report an error. */
		if (table->count >= table->allocated || table->count >= 256)
			return -1;

/* Create the new partition. */
			table->partitions[table->count].sectors = table->devsize - loop;
		table->partitions[table->count].start = loop;
		table->partitions[table->count].refs = 1;
		table->partitions[table->count].name = strdup ("Extra");
		table->partitions[table->count].type = strdup ("Apple_Free");
		table->partitions[table->count].table = table;
		partsused[table->count] = 1;
		table->count++;
	}

	for (loop = 0; loop < table->count; loop++)
	{
/* Check that all the partitions were used in this walk.*/
/* However ignore zero byte partitions since they are unused.*/
		if (!partsused[loop] && (table->partitions[loop].sectors != 0))
			return -1;

/* Check that the partitions have valid names and types, needed for write */
/* back to the drive. */
		if (!table->partitions[loop].name || !table->partitions[loop].type)
			return -1;
	}

/* All should be A-Ok now. */
	table->vol_flags |= VOL_VALID;
	return 0;
}

/* Find out how much free space is left. */
uint64_t
tivo_partition_total_free (const char *device)
{
	struct tivo_partition_table *table;
	int loop;
	uint64_t total = 0;

/* Make sure it has a partition table. */
	table = tivo_read_partition_table (device, O_RDONLY);
	if (!table)
	{
		return 0;
	}
/* Make sure the partition table makes sense. */
	if (tivo_partition_validate (table) < 0)
	{
		return 0;
	}

/* Check there is room for a partition. */
	if (table->count + 1 >= table->allocated)
	{
		return 0;
	}

/* Find any Apple_Free partitions. */
	for (loop = 0; loop < table->count; loop++)
	{
		if (!strcmp (table->partitions[loop].type, "Apple_Free"))
			total += table->partitions[loop].sectors;
	}

	return total;
}

// Find out total used space 
uint64_t
tivo_partition_total_used (const char *device)
{
	struct tivo_partition_table *table;
	int loop;
	uint64_t total = 0;

/* Make sure it has a partition table. */
	table = tivo_read_partition_table (device, O_RDONLY);
	if (!table)
		return 0;
/* Make sure the partition table makes sense. */
	if (tivo_partition_validate (table) < 0)
		return 0;

/* Check there is room for a partition. */
	if (table->count + 1 >= table->allocated)
	{
		return 0;
	}

/* Find any Apple_Free partitions. */
	for (loop = 0; loop < table->count; loop++)
	{
		if (strcmp (table->partitions[loop].type, "Apple_Free"))
			total += table->partitions[loop].sectors;
	}

	return total;
}


/* Find the largest bit of free space on a drive. */
uint64_t
tivo_partition_largest_free (const char *device)
{
	struct tivo_partition_table *table;
	uint64_t first = 0;
	uint64_t last = 0;
	int startpart = 0;
	int loop;
	uint64_t largest = 0;

/* Make sure it has a partition table. */
	table = tivo_read_partition_table (device, O_RDONLY);
	if (!table)
		return 0;
/* Make sure the partition table makes sense. */
	if (tivo_partition_validate (table) < 0)
		return 0;

/* Check there is room for a partition. */
	if (table->count + 1 >= table->allocated)
	{
		return 0;
	}

/* Loop until the start is the last partition. */
	while (startpart < table->count)
	{
		int nextpart = 0;

/* Find the free space that is closest to the start.  This will either */
/* find free space that can be appended to the current space, or it will */
/* pick the next free space chunk. */
		for (loop = startpart; loop < table->count; loop++)
		{
			if (!strcmp (table->partitions[loop].type, "Apple_Free") &&
			   table->partitions[loop].start >= last &&
			   (!nextpart || table->partitions[loop].start < table->partitions[nextpart].start))
			{
				nextpart = loop;
			}
		}

/* If there was no further partition found, and the loop is still going, */
/* there is no uncounted free space. */
		if (!nextpart)
			break;

/* If the partition found starts at the previous last, keep counting the */
/* space, otherwise start over. */
		if (table->partitions[nextpart].start == last)
		{
			last += table->partitions[nextpart].sectors;
		}
		else
		{
			first = table->partitions[nextpart].start;
			last = first + table->partitions[nextpart].sectors;
			if (last - first > largest)
				largest = last - first;
		}
	}
	return largest;
}

int
tivo_partition_rename (const char *device, int partition, const char *name)
{
	char *old;
	struct tivo_partition_table *table;

	table = tivo_read_partition_table (device, O_RDONLY);
	if (!table)
		return -1;

	partition--;

	if (table->count < partition)
		return -1;

	old = table->partitions[partition].name;
	if (name && strlen (name) <= strlen (table->partitions[partition].name))
	{
		strcpy (table->partitions[partition].name, name);
		return 0;
	}

	table->partitions[partition].name = strdup (name);
	if (!table->partitions[partition].name)
	{
		table->partitions[partition].name = old;
		return -1;
	}

	free (old);

	return 0;
}

/**
 *  Move a partition to another slot.  Only the last partition in the map can be moved.
 *  Information about the old partition is clobbered, so this is a dangerous operation.
 */
int
tivo_partition_remap (const char *device, int old, int new)
{
	struct tivo_partition_table *table;

	table = tivo_read_partition_table (device, O_RDONLY);
	if (!table)
		return -1;

	if (old != table->count-1)
		return -1;

	table->partitions[new-1] = table->partitions[old-1];
	table->partitions[old-1] = table->partitions[old];
	table->count--;
	table->vol_flags |= VOL_DIRTY;

	return 0;
}

/* Add a partition to the specified device.  Make the partition size */
/* sectors, add it before a specific partition (Or 0 for at the end) */
/* and assign it's name and type. */
int
tivo_partition_add (const char *device, uint64_t size, int before, const char *name, const char *type)
{
	struct tivo_partition_table *table;
	uint64_t first = 0;
	uint64_t last = 0;
	int startpart = 0;
	int loop;

/* If a before is specified, convert it to array index notation */
	if (before)
		before--;

/* Partitions must have a size....at one time but since Bolts have zero byte partition we need to remove that requirement. */
/* Hopefully this does not screw up something else. */
//	if (!size)
//	{
//		return -1;
//	}

/* Make sure it has a partition table. */
	table = tivo_read_partition_table (device, O_RDONLY);
	if (!table)
	{
		return -1;
	}
/* Make sure the partition table makes sense. */
	if (tivo_partition_validate (table) < 0)
	{
		return -1;
	}

/* Make sure no partitions that are currently being used are affected. */
	for (startpart = table->count - 1; startpart > 0; startpart--)
	{
		if (table->partitions[startpart].refs > 1)
			break;
	}
	startpart++;

/* Check there is room for a partition. */
	if (table->count + 1 >= table->allocated)
	{
		return -1;
	}

/* Also make sure if it is before a partition, no later partitions are */
/* in use. */
	if (before > 0 && before < startpart)
	{
		return -1;
	}

/* Loop until the current sector range is large enough. */
	while (last - first < size)
	{
		int nextpart = 0;

/* Find the free space that is closest to the start.  This will either */
/* find free space that can be appended to the current space, or it will */
/* pick the next free space chunk. */
		for (loop = startpart; loop < table->count; loop++)
		{
			if (!strcmp (table->partitions[loop].type, "Apple_Free") &&
			   table->partitions[loop].start >= last &&
			   (!nextpart || table->partitions[loop].start < table->partitions[nextpart].start))
			{
				nextpart = loop;
			}
		}

/* If there was no further partition found, and the loop is still going, */
/* there is not enough free space. */
		if (!nextpart)
		{
			return -1;
		}

/* If the partition found starts at the previous last, keep counting the */
/* space, otherwise start over. */
		if (table->partitions[nextpart].start == last)
		{
			last += table->partitions[nextpart].sectors;
		}
		else
		{
			first = (table->partitions[nextpart].start + 7 ) & ~0x07;  /* round to 4K boundary for modern disks */
			last = first + table->partitions[nextpart].sectors;
		}
	}

/* However, if our partition is of a zero byte size, the loop would not run so we have to have a special. */
/* case where size is zero and we can still get the sector for the start of the partition. */
	if (!size)
	{
		int nextpart = 0;

/* Find the free space that is closest to the start.  This will either */
/* find free space that can be appended to the current space, or it will */
/* pick the next free space chunk. */
		for (loop = startpart; loop < table->count; loop++)
		{
			if (!strcmp (table->partitions[loop].type, "Apple_Free") &&
			   table->partitions[loop].start >= last &&
			   (!nextpart || table->partitions[loop].start < table->partitions[nextpart].start))
			{
				nextpart = loop;
			}
		}

/* If there was no further partition found, and the loop is still going, */
/* there is not enough free space. */
		if (!nextpart)
		{
			return -1;
		}

/* If the partition found starts at the previous last, keep counting the */
/* space, otherwise start over. */
		if (table->partitions[nextpart].start == last)
		{
			last += table->partitions[nextpart].sectors;
		}
		else
		{
			first = (table->partitions[nextpart].start + 7 ) & ~0x07;  /* round to 4K boundary for modern disks */
			last = first + table->partitions[nextpart].sectors;
		}
	}


	if (last - first > size)
	{
		last = first + size;
	}

/* Delete all partitions that fall within the now used range. */
	for (loop = startpart; loop < table->count; loop++)
	{
		uint64_t tfirst, tlast;
		tfirst = table->partitions[loop].start;
		tlast = tfirst + table->partitions[loop].sectors;
		if (tfirst < last  && first < tlast)
		{
			if (tlast > last)
			{
/* It is really only using part of this partition; truncate it. */
				table->partitions[loop].sectors -= last - table->partitions[loop].start;
				table->partitions[loop].start = last;
			}
			else
			{
				memmove (&table->partitions[loop], &table->partitions[loop + 1], (table->count - loop - 1) * sizeof (*table->partitions));
				table->count--;
				bzero (&table->partitions[table->count], sizeof (*table->partitions));
			}
		}
	}

/* Finally, create the new partition */
	if (before > table->count || before == 0)
	{
		before = table->count;
		while (before > 1 && !strcmp (table->partitions[before - 1].type, "Apple_Free"))
		{
			before--;
		}
	}

	if (before < table->count)
	{
		memmove (&table->partitions[before + 1], &table->partitions[before], (table->count - before) * sizeof (*table->partitions));
	}

	table->count++;
	table->partitions[before].start = first;
	table->partitions[before].sectors = last - first;
	table->partitions[before].name = strdup (name);
	table->partitions[before].type = strdup (type);
	table->partitions[before].refs = 1;
	if (!table->partitions[before].name || !table->partitions[before].type)
	{
		table->vol_flags &= ~VOL_VALID;
		return -1;
	}
	table->vol_flags |= VOL_DIRTY;

	return before + 1;
}


/***********************************************/
/* Initialize the partition table for a drive. */
int
tivo_partition_table_init (const char *device, int swab)
{
	struct tivo_partition_table *table;

	if (tivo_partition_rrpart (device) != 0)
	{
		return -1;
	}

	table = calloc (sizeof (*table), 1);
	if (!table)
	{
		fprintf (stderr, "Out of memory!\n");
		return -1;
	}

	table->rw_fd = lfopen (device, O_RDWR);
	if (table->rw_fd < 0)
	{
		free (table);
		return -1;
	}

	switch (file_or_dev_size (table->rw_fd, &table->devsize))
	{
	case 0:
		table->vol_flags |= VOL_FILE;
	case 1:
		break;
	default:
		close (table->rw_fd);
		free (table);
		return -1;
	}

	if (swab)
		table->vol_flags |= VOL_SWAB;

	table->partitions = calloc (63, sizeof (struct tivo_partition));
	if (!table->partitions)
	{
		close (table->rw_fd);
		free (table);
		return -1;
	}

	table->partitions[0].start = 1;
	table->partitions[0].sectors = 63;
	table->partitions[0].name = strdup ("Apple");
	table->partitions[0].type = strdup ("Apple_partition_map");
	table->partitions[0].refs = 1;
	table->count = 1;
	table->allocated = 63;
	table->device = strdup (device);
	table->next = partition_tables;
	table->vol_flags |= VOL_NONINIT;
	partition_tables = table;

	return 0;
}

/* Writes a partition table back to disk */
int
tivo_partition_table_write (const char *device)
{
	char buf[512];
	tpFILE file;
	struct tivo_partition_table *table = tivo_read_partition_table (device, O_RDWR);
	struct mac_partition *mp = (struct mac_partition *)buf;
	struct tivo_bigpartition *tbp = (struct tivo_bigpartition *)buf;
	int loop;

	if (!table)
		return -1;

	if (!(table->vol_flags & VOL_DIRTY))
	{
		return 0;
	}

	file.tptype = table->vol_flags & VOL_FILE? pDIRECTFILE: pDIRECT;
	file.fd = table->rw_fd;
	file.extra.direct.pt = table;
	bzero (buf, sizeof (buf));

	if (table->vol_flags & VOL_NONINIT)
	{
		struct tivo_partition part;

		table->vol_flags &= ~VOL_NONINIT;

		part.sectors = 1;
		part.start = 0;
		part.table = table;
		file.extra.direct.part = &part;
		*(unsigned short *)buf = intswap16 (TIVO_BOOT_MAGIC);
		if (tivo_partition_write (&file, buf, 0, 1) != 512)
			return -1;
	}

	file.extra.direct.part = &table->partitions[0];

	for (loop = 0; loop <= table->count && loop < table->allocated; loop++)
	{
		bzero (buf, sizeof (buf));
		if (loop < table->count)
		{
			char *p, *name;
			// Use the old format if the partition doesn't start above and isn't bigger that the old UINT_MAX limit
			// TODO: This appears to be safe comapared to pdisk64, but keep a watch on it, as it be be
			//       necessary to use the new format if any part goes above UINT_MAX.
			//       pdisk64 has been replaced by tdisk, with no source available to confirm..
			//if (table->partitions[loop].start < UINT_MAX &&
			//		(table->partitions[loop].start + table->partitions[loop].sectors) < UINT_MAX )
			if (table->partitions[loop].start <= UINT_MAX &&
					table->partitions[loop].sectors <= UINT_MAX )
			{
				mp->signature = partintswap16 (MAC_PARTITION_MAGIC);
				mp->map_count = partintswap32 (table->count);
				mp->start_block = partintswap32 (table->partitions[loop].start);
				mp->block_count = partintswap32 (table->partitions[loop].sectors);
/* One smaller so the result is null terminated, due to the bzero(). */
			strncpy (mp->name, table->partitions[loop].name, sizeof (mp->name) - 1);
			strncpy (mp->type, table->partitions[loop].type, sizeof (mp->type) - 1);
			mp->data_count = mp->block_count;
				mp->status = partintswap32 (0x33);
				// Set special tivo AV status bit for media partitions.
				name = strdup(mp->name);
				for(p=name; *p != 0; p++)
					*p = tolower(*p);
				if(strstr(name,"media") != 0)
					mp->status |= partintswap32 (0x100);
				free(name);
			} else 	{
				tbp->signature = partintswap16 (TIVO_BIGPARTITION_MAGIC);
				tbp->map_count = partintswap32 (table->count);
				tbp->start_block = partintswap64 (table->partitions[loop].start);
				tbp->block_count = partintswap64 (table->partitions[loop].sectors);
/* One smaller so the result is null terminated, due to the bzero(). */
				strncpy (tbp->name, table->partitions[loop].name, sizeof (tbp->name) - 1);
				strncpy (tbp->type, table->partitions[loop].type, sizeof (tbp->type) - 1);
				tbp->data_count = tbp->block_count;
				tbp->status = partintswap32 (0x33);
				// Set special tivo AV status bit for media partitions.
				name = strdup(tbp->name);
				for(p=name; *p != 0; p++)
					*p = tolower(*p);
				if(strstr(name,"media") != 0)
					tbp->status |= partintswap32 (0x100);
				free(name);
			}

		}
		if (tivo_partition_write (&file, buf, loop, 1) != 512)
			return -1;
	}
	table->vol_flags &= ~VOL_DIRTY;
	return 0;
}

/********************************************************************/
/* Open a partition that is not byte-swapped or the kernel does not */
/* recognize.  This also handles regular files or partition devices. */
/* The variable tivo_partition_accmode defines the preferred way to */
/* open partitions.  In accKERNEL, it assumes the kernel knows what */
/* to do and opens it directly.  In accDIRECT it opens the device */
/* directly and tries to figure out the partition table itself.  In */
/* accAUTO, it will first open it as a file, then if it gets an error, */
/* or if the partition is empty, it will try opening the entire device. */

tpFILE *
tivo_partition_open (char *path, int flags)
{
	char devpath[MAXPATHLEN];
	int partnum;
	tpFILE newfile;
	tpFILE *file = &newfile;

	bzero (file, sizeof (newfile));

	newfile.fd = -1;

/* If the preferred access mode is kernel, or if it is auto, try to open it. */
	if (tivo_partition_accmode == accAUTO || tivo_partition_accmode == accKERNEL)
	{
		newfile.fd = lfopen (path, flags);
		if (newfile.fd >= 0)
		{
/* The file exists, time to see what it is. */
			switch (file_or_dev_size (newfile.fd, &newfile.extra.kernel.sectors))
			{
			case 0:
				newfile.tptype = pFILE;
				break;
			case 1:
				newfile.tptype = pDEVICE;
				break;
			default:
				errno = ENOTBLK;
				close (newfile.fd);
				newfile.fd = -1;
			}

			if (newfile.extra.kernel.sectors == 0)
			{
/* If it is too small, throw it back.  If the mode is accAUTO this will cause */
/* it to try opening the entire device, instead. */
				close (newfile.fd);
				bzero (file, sizeof (newfile));
				newfile.fd = -1;
			}
		}
	}

/* If the type is still unknown and it is auto mode, or if the mode is direct */
/* only, try opening the main device. */
	if ((newfile.tptype == pUNKNOWN && tivo_partition_accmode == accAUTO) || tivo_partition_accmode == accDIRECT)
	{
/* Find the device name. */

#if TARGET_OS_MAC
		int disk;
		char extra;
		
		if (sscanf (path, "/dev/disk%ds%d%c", &disk, &partnum, &extra) == 2 && partnum > 0)
		{
			sprintf (devpath, "/dev/disk%d", disk);

/* Read the partition.  Don't care about the return value, cause the type */
/* set will be enough to know if it succeeded. */
			tivo_partition_open_direct_int (&newfile, devpath, partnum, flags);
		}
#else
		int tmp = 0;
		size_t partoff;

		do
		{
			partoff = tmp;
			tmp += strspn (path + tmp, "0123456789");
			tmp += strcspn (path + tmp, "0123456789");
		}
		while (path[tmp] != 0);

/* Check to make sure it has a partition number. */
		if (partoff < MAXPATHLEN && path[partoff])
		{
			strncpy (devpath, path, partoff);
			devpath[partoff] = '\0';

			partnum = strtoul (path + partoff, &path, 10);

			if (!strncmp (devpath, "/dev/", 5))
			{
/* Devfs doesn't just append the partition number, it calls the partitions */
/* part1, part2, part3, etc.  If devfs is in use, and the device being */
/* referred to is not a compatibility name, this needs to be changed to */
/* disc to refer to the entire drive. Only do this if the device name starts */
/* with dev. */
				if (!strcmp (devpath + partoff - 5, "/part"))
				{
					strcpy (devpath + partoff - 5, "/disc");
				}
/* Likewise, the old devfs naming scheme called partitions /dev/XX/cXbXtXuXpX */
/* and whole drives /dev/XX/cXbXtXuX.  Even though this naming is deprecated, */
/* it is still an option in devfsd, which creates a compatibility namespace. */
				else if (devpath[partoff - 1] == 'p' && isdigit (devpath[partoff - 2]))
				{
					devpath[partoff - 1] = '\0';
				}
			}

			if (!(path && *path) && partnum > 0)
			{
/* Read the partition.  Don't care about the return value, cause the type */
/* set will be enough to know if it succeeded. */
				tivo_partition_open_direct_int (&newfile, devpath, partnum, flags);
			}
		}
#endif
	}

/* If the type is still unknown, it is an error, the file was unable to be */
/* opened, or the default access mode is invalid. */
	if (newfile.tptype == pUNKNOWN)
	{
		if (newfile.fd >= 0)
		{
			close (newfile.fd);
		}
		return 0;
	}

/* Allocate the actual file structure now that it is certain it will be used. */
	file = malloc (sizeof (*file));
	if (file)
	{
		memcpy (file, &newfile, sizeof (*file));
	}
	else
	{
		close (newfile.fd);
		errno = ENOMEM;
	}

	return file;
}

/********************************************************************/
/* Internal routine to open a partition directly by device name and */
/* partition number.  Only difference is it does not allocate a file, but */
/* uses the passed in file instead. */
static int
tivo_partition_open_direct_int (tpFILE *file, char *path, int partnum, int flags)
{
	struct tivo_partition_table *table;

/* Get the partition table for that dev.  This may have to read it. */
	table = tivo_read_partition_table (path, flags);

/* Make sure the table and partition are valid. */
	if (table && table->count >= partnum && partnum > 0)
	{
/* Not initialized yet. */
		if (table->partitions[partnum - 1].refs < 1)
			return 0;
		if (table->vol_flags & VOL_NONINIT)
			return 0;

/* Get the proper fd. */
		if ((flags & O_ACCMODE) == O_RDONLY)
		{
			file->fd = table->ro_fd;
		}
		else
		{
			file->fd = table->rw_fd;
		}

		if (table->vol_flags & VOL_FILE)
		{
			file->tptype = pDIRECTFILE;
		}
		else
		{
			file->tptype = pDIRECT;
		}

		file->extra.direct.pt = table;
		file->extra.direct.part = &table->partitions[partnum - 1];

		table->refs++;
		table->partitions[partnum - 1].refs++;

		return 1;
	}

    return 0;
}

/******************************************************************/
/* Open a partition directly by device name and partition number. */
tpFILE *
tivo_partition_open_direct (char *path, int partnum, int flags)
{
	tpFILE newfile;
	tpFILE *file = NULL;

	bzero (&newfile, sizeof (newfile));

	if (tivo_partition_open_direct_int (&newfile, path, partnum, flags))
	{
		file = malloc (sizeof (newfile));

		if (file)
		{
			memcpy (file, &newfile, sizeof (newfile));
		}
	} 

	return file;
}

/**************************************************************************/
/* Return the count for the number of partitions on a given device.  This */
/* count is the number of the last partition, inclusive. */
int tivo_partition_count (const char *device)
{
	struct tivo_partition_table *table;

/* Get the partition table for that dev.  This may have to read it. */
	table = tivo_read_partition_table (device, O_RDONLY);

	if (table)
	{
		return table->count;
	}

	return 0;
}

/***********************************************************/
/* Return if a device is being byte-swapped by the kernel. */
int tivo_partition_devswabbed (const char *device)
{
	int fd, tmp;
	char tempfile[MAXPATHLEN];
	char *tmp2;
	char buf[4097];	// 4K + 1
	int retval = 0;

	tmp2 = strrchr (device, '/');

	if (!tmp2)
		return 0;

	sprintf (tempfile, "/proc/ide/%s/settings", tmp2 + 1);

	fd = open (tempfile, O_RDONLY);
	if (fd < 0)
		return 0;

	buf[0] = 0;
	int nread = 0;
	// Read 4K
	while( nread < 4096)
	{
		int n = read (fd, buf, 4096 - nread);
		if (n < 0) 
		{
			perror("read errpr");
			break;
		}
		nread += n;
	}
	// Null terminate buffer with extra byte
	buf[4096] = 0;

	close (fd);

	tmp2 = strstr (buf, "bswap");

	if (tmp2)
	{
		tmp = strcspn (tmp2, "01");

		if (tmp)
			retval = tmp2[tmp] - '0';
	}

	return retval;
}

/***************************************************************************/
/* Returns weather the data physically on the drive is byte swapped.  This */
/* is an xor of the kernel byte swapping and the access being byte swapped. */
/* If both the kernel AND the partition read are byte swapping, that means */
/* the data itself is not byte-swapped.  Similar if neither is.  But that */
/* should go without saying. */
int tivo_partition_swabbed (const char *device)
{
	int result = 0;
	struct tivo_partition_table *table;

/* Get the partition table for that dev.  This may have to read it. */
	table = tivo_read_partition_table (device, O_RDONLY);

	if (table && table->vol_flags & VOL_SWAB)
		result ^= 1;

	if (tivo_partition_devswabbed (device))
		result ^= 1;

	return result;
}

/**************************************************************************/
/* Clean up the tpFILE structure, closing the file if needed, and freeing */
/* the memory used. */
void
tivo_partition_close (tpFILE * file)
{
/* Only close the file if it is owned by this tpFILE pointer.  If it is a */
/* shared file leave it for the unwritten cleanup code. */
	if (file->fd >= 0 && file->tptype != pDIRECT && file->tptype != pDIRECTFILE)
	{
		close (file->fd);
		file->fd = -1;
	}
	else
	{
		file->extra.direct.pt->refs--;
		file->extra.direct.part->refs--;
	}
	free (file);
}

/***********************************/
/* Return the size of a partition. */
uint64_t
tivo_partition_size (tpFILE * file)
{
	switch (file->tptype)
	{
	case pFILE:
	case pDEVICE:
/* If this tpFILE structure owns the FD, it knows the size itself. */
		return file->extra.kernel.sectors;
	case pDIRECT:
	case pDIRECTFILE:
/* If the FD is owned by a partition table, go there for the size. */
		return file->extra.direct.part->sectors;
	default:
		return 0;
	}
}

/**********************************************/
/* Returns the size of a partition, directly. */
uint64_t
tivo_partition_sizeof (const char *device, int partnum)
{
	struct tivo_partition_table *table;
	table = tivo_read_partition_table (device, O_RDONLY);

	if (partnum < 1 || partnum > table->count)
	{
		return 0;
	}

	return table->partitions[partnum - 1].sectors;
}

/************************************************/
/* Returns the name of the partition, directly. */
char *
tivo_partition_name (const char *device, int partnum)
{
	struct tivo_partition_table *table;
	table = tivo_read_partition_table (device, O_RDONLY);

	if (partnum < 1 || partnum > table->count)
	{
		return 0;
	}

	return table->partitions[partnum - 1].name;
}

/************************************************/
/* Returns the type of the partition, directly. */
char *
tivo_partition_type (const char *device, int partnum)
{
	struct tivo_partition_table *table;
	table = tivo_read_partition_table (device, O_RDONLY);

	if (partnum < 1 || partnum > table->count)
	{
		return 0;
	}

	return table->partitions[partnum - 1].type;
}

/******************************************************/
/* Return the offset into the file of this partition. */
uint64_t
tivo_partition_offset (tpFILE * file)
{
	switch (file->tptype)
	{
	case pDIRECT:
	case pDIRECTFILE:
/* If a partition table owns the file, it knows the start offset. */
		return file->extra.direct.part->start;
	default:
/* If we own the file, the offset is always 0. */
		return 0;
	}
}

/***************************************************************************/
/* Return the device name that this partition resides on.  This is not the */
/* partition itself, but the whole device. */
const char *
tivo_partition_device_name (tpFILE * file)
{
	switch (file->tptype)
	{
	case pDIRECT:
	case pDIRECTFILE:
/* If a partition table ownes the file, it knows the device name. */
		return (char *) file->extra.direct.pt->device;
	default:
/* If we own the file, the device name is unknown. */
		return NULL;
	}
}

/*****************************/
/* Read the first 512 bytes. */
int
tivo_partition_read_bootsector (const char *device, void *buf)
{
	struct tivo_partition_table *table;
	tpFILE file;
	struct tivo_partition part;

	table = tivo_read_partition_table (device, O_RDONLY);

	if (!table)
	{
		return -1;
	}

	part.sectors = 1;
	part.start = 0;
	part.table = table;
	file.tptype = table->vol_flags & VOL_FILE? pDIRECTFILE: pDIRECT;
	file.fd = table->ro_fd;
	file.extra.direct.pt = table;
	file.extra.direct.part = &part;

	return (tivo_partition_read (&file, buf, 0, 1));
}

/*****************************/
/* Write the first 512 bytes. */
int
tivo_partition_write_bootsector (const char *device, void *buf)
{
	struct tivo_partition_table *table;
	tpFILE file;
	struct tivo_partition part;

	table = tivo_read_partition_table (device, O_RDWR);

	if (!table)
	{
		return -1;
	}

	part.sectors = 1;
	part.start = 0;
	part.table = table;
	file.tptype = table->vol_flags & VOL_FILE? pDIRECTFILE: pDIRECT;
	file.fd = table->rw_fd;
	file.extra.direct.pt = table;
	file.extra.direct.part = &part;

	return (tivo_partition_write (&file, buf, 0, 1));
}

/*******************************************************************/
/* A convenience function to set the access mode to direct access. */
void
tivo_partition_direct ()
{
	tivo_partition_accmode = accDIRECT;
}

/**************************************************************************/
/* A convenience function to set the access mode to file (kernel) access. */
void
tivo_partition_file ()
{
	tivo_partition_accmode = accKERNEL;
}

/**************************************************************************/
/* A convenience function to set the access mode to automatically detect. */
void
tivo_partition_auto ()
{
	tivo_partition_accmode = accAUTO;
}

//Revalidate partition
int
revalidate_drive (const char *device) {
       
        struct tivo_partition_table *table;
        struct blkpg_partition blkpgpt;
        struct blkpg_ioctl_arg blkpgarg;
        int loop = 0;
        int fd,ret;

        fd = open(device, O_RDONLY);
        if (fd < 0)
           return -1;

        //wipe the partition map from mem
        for (loop = 1; loop <= 16; loop ++) {
            blkpgpt.start = 0;
            blkpgpt.length = 0;
            blkpgpt.pno = loop;
            blkpgpt.devname[0] = '\0';
            blkpgpt.volname[0] = '\0';           
            blkpgarg.op = BLKPG_DEL_PARTITION;
            blkpgarg.flags = 0;
            blkpgarg.datalen = sizeof(blkpgpt);
            blkpgarg.data = &blkpgpt;
            ret = ioctl(fd, BLKPG, &blkpgarg); 
        }
        
  	table = tivo_read_partition_table (device, O_RDONLY);
        if (!table)
           return -1; 
        
        for (loop = 1; loop <= 16; loop ++) {
            blkpgpt.start = 512 * (long long)table->partitions[loop-1].start;    
            blkpgpt.length = 512 * (long long)table->partitions[loop-1].sectors;
            blkpgpt.pno = loop;
	    blkpgpt.devname[0] = '\0';
            blkpgpt.volname[0] = '\0';
            blkpgarg.op = BLKPG_ADD_PARTITION;
            blkpgarg.flags = 0;
            blkpgarg.datalen = sizeof(blkpgpt);
            blkpgarg.data = &blkpgpt;
	    ioctl(fd, BLKPG, &blkpgarg);  
        }
        if (table)
           free(table);        
        close (fd);
	
	//USB flash drive fix ???
	//sleep(3);
	//fd = open(device, O_RDONLY);
	//close (fd);
	
        return 1;
}

