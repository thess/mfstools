/* For stat64 */
#define _LARGEFILE64_SOURCE

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

/* #include "mfs.h" */
#include "macpart.h"

/* Some static variables..  Really this should be a class and these */
/* private members. */
static struct tivo_partition_table *partition_tables = NULL;
static enum
{ accAUTO, accDIRECT, accKERNEL }
tivo_partition_accmode = accAUTO;

/**************************************************/
/* Read the TiVo partition table off of a device. */
struct tivo_partition_table *
tivo_read_partition_table (char *device, int flags)
{
	struct tivo_partition_table *table;

/* See if this device has been opened yet. */
	for (table = partition_tables; table; table = table->next)
	{
		if (!strcmp (device, table->device))
		{
			break;
		}
	}

/* If not, open it and read the partition table. */
	if (!table)
	{
		int *fd;
		unsigned char buf[512];
		int cursec;
		int maxsec = 1;
		int partitions = 0;
		struct tivo_partition parts[256];

		table = calloc (sizeof (*table), 1);

		if (!table)
		{
			fprintf (stderr, "Out of memory!\n");
			return 0;
		}

/* Figure out if we are supposed to open it RO or RW, and use the right fd */
/* variable. */
		fd = (flags & O_ACCMODE) == O_RDONLY ? &table->ro_fd : &table->rw_fd;
		*fd = open (device, flags);
		if (*fd < 0)
		{
			perror (device);
			free (table);
			return 0;
		}

/* Don't actually care about the size, just if it's a file or device. */
		if (ioctl (*fd, BLKGETSIZE, &cursec) < 0)
		{
			table->vol_flags |= VOL_FILE;
		}

/* Read the boot block. */
		lseek (*fd, 0, SEEK_SET);
		if (read (*fd, buf, 512) != 512)
		{
			perror (device);
			close (*fd);
			free (table);
			return 0;
		}

/* Find out what the magic is in the bootblock. */
		switch (htons (*(unsigned short *) buf))
		{
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
		for (cursec = 1; cursec <= maxsec && partitions < 256; cursec++)
		{
			struct mac_partition *part;
			lseek (*fd, 512 * cursec, SEEK_SET);
			if (read (*fd, buf, 512) != 512)
			{
				perror (device);
				close (*fd);
				free (table);
				return 0;
			}
			if (table->vol_flags & VOL_SWAB)
			{
				data_swab (buf, 512);
			}
			part = (struct mac_partition *) buf;

/* If it doesn't have the magic, it's not hip.  No more partitions. */
			if (htons (part->signature) != MAC_PARTITION_MAGIC)
			{
				break;
			}

/* If this is the first, update the max. */
			if (cursec == 1)
			{
				maxsec = htonl (part->map_count);
			}

/* Add it to the list. */
			parts[partitions].start = htonl (part->start_block);
			parts[partitions].sectors = htonl (part->block_count);
			partitions++;
		}

/* No partitions.  None.  Nada. */
		if (partitions == 0)
		{
			close (*fd);
			free (table);
			return 0;
		}

/* Copy the partitions into place. */
		table->partitions = calloc (partitions, sizeof (struct tivo_partition));
		if (!table->partitions)
		{
			close (*fd);
			free (table);
			return 0;
		}
		memcpy (table->partitions, parts, partitions * sizeof (struct tivo_partition));
		table->count = partitions;
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
				table->ro_fd = open (device, flags);
			}
		}
		else
		{
			if (table->rw_fd < 0)
			{
				table->rw_fd = open (device, flags);
			}
		}
	}

	return table;
}

/********************************************************************/
/* Open a partition that is not byte-swapped or the kernel does not */
/* recognize. */
tpFILE *
tivo_partition_open (char *path, int flags)
{
	char devpath[MAXPATHLEN];
	size_t partoff;
	int partnum;
	struct tivo_partition_table *table;
	tpFILE newfile;
	tpFILE *file = &newfile;

	bzero (file, sizeof (newfile));

	newfile.fd = -1;

	if (tivo_partition_accmode == accAUTO || tivo_partition_accmode == accKERNEL)
	{
		newfile.fd = open (path, flags);
		if (newfile.fd >= 0)
		{
			struct stat64 st;

			if (fstat64 (newfile.fd, &st) == 0)
			{
				if (S_ISBLK (st.st_mode))
				{
					if (ioctl (newfile.fd, BLKGETSIZE, &newfile.extra.kernel.sectors) >= 0)
					{
						newfile.tptype = pDEVICE;
					}
				}
				else if (S_ISREG (st.st_mode))
				{
					newfile.tptype = pFILE;
					newfile.extra.kernel.sectors = st.st_size / 512;
				}
				else
				{
					errno = ENOTBLK;
					close (newfile.fd);
					newfile.fd = -1;
				}
			}
		}
	}

	if (newfile.tptype == pUNKNOWN && tivo_partition_accmode == accAUTO || tivo_partition_accmode == accDIRECT)
	{
/* Find the device name. */
		partoff = strcspn (path, "0123456789");

/* Check to make sure it is in /dev, and has a partition number. */
		if (!strncmp (path, "/dev/", 5) && partoff >= MAXPATHLEN && !path[partoff])
		{
			strncpy (devpath, path, partoff);
			devpath[partoff] = '\0';

			partnum = strtoul (path + partoff, &path, 10);

			if (!(path && *path) && partnum > 0)
			{

/* Get the partition table for that dev.  This may have to read it. */
				table = tivo_read_partition_table (devpath, flags);

/* Make sure the table and partition are valid. */
				if (table && table->count >= partnum)
				{

/* Get the proper fd. */
					if ((flags & O_ACCMODE) == O_RDONLY)
					{
						newfile.fd = table->ro_fd;
					}
					else
					{
						newfile.fd = table->rw_fd;
					}

					if (table->vol_flags & VOL_FILE)
					{
						newfile.tptype = pDIRECTFILE;
					}
					else
					{
						newfile.tptype = pDIRECT;
					}

					newfile.extra.direct.pt = table;
					newfile.extra.direct.part = &table->partitions[partnum - 1];
				}
			}
		}
	}

	if (newfile.tptype == pUNKNOWN)
	{
		if (newfile.fd >= 0)
		{
			close (newfile.fd);
		}
		return 0;
	}

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

void
tivo_partition_close (tpFILE * file)
{
	if (file->fd >= 0)
	{
		close (file->fd);
		file->fd = -1;
	}
	free (file);
}

unsigned int
tivo_partition_size (tpFILE * file)
{
	switch (file->tptype)
	{
	case pFILE:
	case pDEVICE:
		return file->extra.kernel.sectors;
	case pDIRECT:
	case pDIRECTFILE:
		return file->extra.direct.part->sectors;
	default:
		return 0;
	}
}

unsigned int
tivo_partition_offset (tpFILE * file)
{
	switch (file->tptype)
	{
	case pDIRECT:
	case pDIRECTFILE:
		return file->extra.direct.part->start;
	default:
		return 0;
	}
}

const char *
tivo_partition_device_name (tpFILE * file)
{
	switch (file->tptype)
	{
	case pDIRECT:
	case pDIRECTFILE:
		return (char *) file->extra.direct.pt->device;
	default:
		return NULL;
	}
}

void
tivo_partition_direct ()
{
	tivo_partition_accmode = accDIRECT;
}

void
tivo_partition_file ()
{
	tivo_partition_accmode = accKERNEL;
}

void
tivo_partition_auto ()
{
	tivo_partition_accmode = accAUTO;
}
