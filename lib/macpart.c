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

#define TIVO_BOOT_MAGIC		0x1492
#define TIVO_BOOT_AMIGC		0x9214
#define MAC_PARTITION_MAGIC	0x504d

/* Format of mac partition table. */
struct mac_partition {
	__u16	signature;
	__u16	res1;
	__u32	map_count;
	__u32	start_block;
	__u32	block_count;
	char	name[32];
	char	type[32];
	__u32	data_start;
	__u32	data_count;
	__u32	status;
};

/* Some static variables..  Really this should be a class and these */
/* private members. */
static struct tivo_partition_table *partition_tables = NULL;

/**************************************************/
/* Read the TiVo partition table off of a device. */
struct tivo_partition_table *tivo_read_partition_table (char *device, int flags)
{
	struct tivo_partition_table *table;

/* See if this device has been opened yet. */
	for (table = partition_tables; table; table = table->next) {
		if (!strcmp (device, table->device)) {
			break;
		}
	}

/* If not, open it and read the partition table. */
	if (!table) {
		int *fd;
		unsigned char buf[512];
		int cursec;
		int maxsec = 1;
		int partitions = 0;
		struct tivo_partition parts[256];

		table = calloc (sizeof (*table), 1);

		if (!table) {
			fprintf (stderr, "Out of memory!\n");
			return 0;
		}

/* Figure out if we are supposed to open it RO or RW, and use the right fd */
/* variable. */
		fd = (flags & O_ACCMODE) == O_RDONLY? &table->ro_fd: &table->rw_fd;
		*fd = open (device, flags);
		if (*fd < 0) {
			perror (device);
			free (table);
			return 0;
		}

/* Don't actually care about the size, just if it's a file or device. */
	        if (ioctl (*fd, BLKGETSIZE, &cursec) < 0) {
			table->vol_flags |= VOL_FILE;
		}

/* Read the boot block. */
		lseek (*fd, 0, SEEK_SET);
		if (read (*fd, buf, 512) != 512) {
			perror (device);
			close (*fd);
			free (table);
			return 0;
		}

/* Find out what the magic is in the bootblock. */
		switch (htons (*(unsigned short *)buf)) {
		case TIVO_BOOT_MAGIC:
/* It is the right magic.  Do nothing. */
			break;
		case TIVO_BOOT_AMIGC:
/* It is the right magic, but it is byte-swapped.  Enable byte-swapping. */
			table->vol_flags |= VOL_SWAB;
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
		for (cursec = 1; cursec <= maxsec && partitions < 256; cursec++) {
			struct mac_partition *part;
			lseek (*fd, 512 * cursec, SEEK_SET);
			if (read (*fd, buf, 512) != 512) {
				perror (device);
				close (*fd);
				free (table);
				return 0;
			}
			if (table->vol_flags & VOL_SWAB) {
				data_swab (buf, 512);
			}
			part = (struct mac_partition *)buf;

/* If it doesn't have the magic, it's not hip.  No more partitions. */
			if (htons (part->signature) != MAC_PARTITION_MAGIC) {
				break;
			}

/* If this is the first, update the max. */
			if (cursec == 1) {
				maxsec = htonl (part->map_count);
			}

/* Add it to the list. */
			parts[partitions].start = htonl (part->start_block);
			parts[partitions].sectors = htonl (part->block_count);
			partitions++;
		}

/* No partitions.  None.  Nada. */
		if (partitions == 0) {
			close (*fd);
			free (table);
			return 0;
		}

/* Copy the partitions into place. */
		table->partitions = calloc (partitions, sizeof (struct tivo_partition));
		if (!table->partitions) {
			close (*fd);
			free (table);
			return 0;
		}
		memcpy (table->partitions, parts, partitions * sizeof (struct tivo_partition));
		table->count = partitions;
		table->device = strdup (device);
		table->next = partition_tables;
		partition_tables = table;
	} else {
/* Okay, the table already exists.  Make sure the device is opened for the */
/* proper access mode already, as well. */
		if ((flags & O_ACCMODE) == O_RDONLY) {
			if (table->ro_fd < 0) {
				table->ro_fd = open (device, flags);
			}
		} else {
			if (table->rw_fd < 0) {
				table->rw_fd = open (device, flags);
			}
		}
	}

	return table;
}

/********************************************************************/
/* Open a partition that is not byte-swapped or the kernel does not */
/* recognize. */
int tivo_partition_open (char *path, int flags, struct volume_info *vol)
{
	char devpath [MAXPATHLEN];
	size_t partoff;
	int partnum;
	struct tivo_partition_table *table;

/* Find the device name. */
	partoff = strcspn (path, "0123456789");

/* Check to make sure it is in /dev, and has a partition number. */
	if (strncmp (path, "/dev/", 5) || partoff >= MAXPATHLEN ||
	    !path[partoff]) {
		return -1;
	}

	strncpy (devpath, path, partoff);
	devpath[partoff] = '\0';

	partnum = strtoul (path + partoff, &path, 10);

	if (path && *path || partnum < 1) {
		return -1;
	}

/* Get the partition table for that dev.  This may have to read it. */
	table = tivo_read_partition_table (devpath, flags);

/* Make sure the table and partition are valid. */
	if (!table || table->count < partnum) {
		return -1;
	}

/* Get the proper fd. */
	if ((flags & O_ACCMODE) == O_RDONLY) {
		vol->fd = table->ro_fd;
	} else {
		vol->fd = table->rw_fd;
	}

/* Copy the information to the vol header. */
	vol->vol_flags |= table->vol_flags;
	vol->sectors = table->partitions[partnum - 1].sectors;
	vol->offset = table->partitions[partnum - 1].start;

	return vol->fd;
}
