#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
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

static int tivo_partition_open_direct_int (tpFILE *file, char *path, int partnum, int flags);

/****************************************************************************/
/* Opens a file normally.  If it fails with EFBIG open it with O_LARGEFILE. */
int
lfopen (char *device, int flags)
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
		*fd = lfopen (device, flags);
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
			parts[partitions].name = strdup (part->name);
			parts[partitions].type = strdup (part->type);
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
	size_t partoff;
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
/* The file exists, time to see what it is.  Use the 64 version just incase */
/* the filesystem has big file support, and it is in a really big file. */
#ifdef O_LARGEFILE
			struct stat64 st;

			if (fstat64 (newfile.fd, &st) == 0)
#else
			struct stat st;

			if (fstat (newfile.fd, &st) == 0)
#endif
			{
				if (S_ISBLK (st.st_mode))
				{
/* The file is really a block device.  Get it's size with BLKGETSIZE. */
					if (ioctl (newfile.fd, BLKGETSIZE, &newfile.extra.kernel.sectors) >= 0)
					{
						newfile.tptype = pDEVICE;
					}
				}
				else if (S_ISREG (st.st_mode))
				{
/* The file is a regular file.  It's size was returned by the fstat call. */
					newfile.tptype = pFILE;
					newfile.extra.kernel.sectors = st.st_size / 512;
				}
				else
				{
/* Not a block device or regular file.  Ummmm, right.  Can't really use */
/* character devices. */
					errno = ENOTBLK;
					close (newfile.fd);
					newfile.fd = -1;
				}
			}
			if (newfile.extra.kernel.sectors == 0)
			{
/* If it is too small, throw it back.  If the mode is accAUTO this will case */
/* it to try opening the entire device, instead. */
				close (newfile.fd);
				bzero (file, sizeof (newfile));
				newfile.fd = -1;
			}
		}
	}

/* If the type is still unknown and it is auto mode, or if the mode is direct */
/* only, try opening the main device. */
	if (newfile.tptype == pUNKNOWN && tivo_partition_accmode == accAUTO || tivo_partition_accmode == accDIRECT)
	{
/* Find the device name. */
		int tmp = 0;
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
/* and whole drives /dev/XX/cXbXtXuX.  Even though this naming is depricated, */
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
/* uses the past in file instead. */
static int
tivo_partition_open_direct_int (tpFILE *file, char *path, int partnum, int flags)
{
	struct tivo_partition_table *table;

/* Get the partition table for that dev.  This may have to read it. */
	table = tivo_read_partition_table (path, flags);

/* Make sure the table and partition are valid. */
	if (table && table->count >= partnum && partnum > 0)
	{

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
int tivo_partition_count (char *path)
{
	struct tivo_partition_table *table;

/* Get the partition table for that dev.  This may have to read it. */
	table = tivo_read_partition_table (path, O_RDONLY);

	if (table)
	{
		return table->count;
	}

	return 0;
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
	free (file);
}

/***********************************/
/* Return the size of a partition. */
unsigned int
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
unsigned int
tivo_partition_sizeof (char *device, int partnum)
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
tivo_partition_name (char *device, int partnum)
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
tivo_partition_type (char *device, int partnum)
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
unsigned int
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
tivo_partition_read_bootsector (char *device, void *buf)
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
