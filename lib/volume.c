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

/* For htonl() */
#include <netinet/in.h>

#include "mfs.h"
#include "macpart.h"

/***********************************************************************/
/* Translate a device name from the TiVo view of the world to reality, */
/* allowing relocating of MFS volumes by setting MFS_... variables. */
/* For example, MFS_HDA=/dev/hdb would make /dev/hda* into /dev/hdb* */
/* Note that it goes most specific first, so MFS_HDA10 would match before */
/* MFS_HDA on /dev/hda10.  Also note that MFS_HDA1 would match /dev/hda10, */
/* so be careful.  In addition to relocating, if a relocated device starts */
/* with RO: the device or file will be opened O_RDONLY no matter what the */
/* requested mode was. */
char *
mfsvol_device_translate (char *dev)
{
	static char devname[1024];
	int dev_len = strcspn (dev, " ");

/* See if it is in /dev, to be relocated. */
	if (!strncmp (dev, "/dev/", 5))
	{
		char dev_sub_var[128];
		char *dev_sub = NULL;
		int loop;

		strcpy (dev_sub_var, "MFS_");

/* Copy in the entire rest of the name after /dev/, uppercasing it. */
		for (loop = 0; dev[5 + loop] && dev[5 + loop] != ' '; loop++)
		{
			dev_sub_var[loop + 4] = toupper (dev[loop + 5]);
		}
		dev_sub_var[loop + 4] = 0;

/* Slowly eat off one character until it is MFS_X checking for a variable */
/* each time, breaking when one is found. */
		while (loop > 1 && !(dev_sub = getenv (dev_sub_var)))
		{
			dev_sub_var[--loop + 4] = 0;
		}

/* If the variable is found, substitute it, tacking on the remainder of the */
/* device name passed in. */
		if (dev_sub)
		{
#if TARGET_OS_MAC
			sprintf (devname, "%ss%.*s", dev_sub, dev_len - (loop + 5), dev + loop + 5);
#else
			sprintf (devname, "%s%.*s", dev_sub, dev_len - (loop + 5), dev + loop + 5);
#endif

			return devname;
		}
	}

/* Otherwise, just copy out the name of this device.  This is always done */
/* instead of just returning the name since the list is space seperated. */
	sprintf (devname, "%.*s", dev_len, dev);
	return devname;
}

/***************************************************************************/
/* Add a volume to the internal list of open volumes.  Open it with flags. */
int
mfsvol_add_volume (struct volume_handle *hnd, char *path, int flags)
{
	struct volume_info *newvol;
	struct volume_info **loop;

	newvol = calloc (sizeof (*newvol), 1);

	if (!newvol)
	{
		fprintf (stderr, "Out of memory!");
		return -1;
	}

/* Translate the device name to it's real world equivelent. */
	path = mfsvol_device_translate (path);

/* If the user requested RO, let them have it.  This may break a writer */
/* program, but thats what it is intended to do.  Also if fake_write is set, */
/* set RO as well, for the actual file, just in case. */
	if (hnd->fake_write || !strncmp (path, "RO:", 3))
	{
		path += 3;
		flags = (flags & ~O_ACCMODE) | O_RDONLY;
	}

/* Open the file. */
	newvol->file = tivo_partition_open (path, flags);

	if (!newvol->file)
	{
		perror (path);
		return 0;
	}

/* If read-only was requested, make it so, unless fake_write was selected. */
	if ((flags & O_ACCMODE) == O_RDONLY && !hnd->fake_write)
	{
		newvol->vol_flags |= VOL_RDONLY;
	}

/* Find out the size of the device. */
	newvol->sectors = tivo_partition_size (newvol->file);
	if (newvol->sectors == 0)
	{
		perror (path);
	}

/* TiVo rounds off the size of the partition to even counts of 1024 sectors. */
	newvol->sectors &= ~(MFS_PARTITION_ROUND - 1);

/* If theres nothing there, assume the worst. */
	if (!newvol->sectors)
	{
		fprintf (stderr, "Error: Empty partition %s.\n", path);
		tivo_partition_close (newvol->file);
		free (newvol);
		return -1;
	}

/* Add it to the tail of the volume list. */
	for (loop = &hnd->volumes; *loop; loop = &(*loop)->next)
	{
		newvol->start = (*loop)->start + (*loop)->sectors;
	}

	*loop = newvol;

	return newvol->start;
}

/*******************************************************/
/* Return the volume info for the volume sector is in. */
struct volume_info *
mfsvol_get_volume (struct volume_handle *hnd, unsigned int sector)
{
	struct volume_info *vol;

/* Find the volume this sector is from in the table of open volumes. */
	for (vol = hnd->volumes; vol; vol = vol->next)
	{
		if (vol->start <= sector && vol->start + vol->sectors > sector)
		{
			break;
		}
	}

	return vol;
}

/*************************************************/
/* Return the size of volume starting at sector. */
unsigned int
mfsvol_volume_size (struct volume_handle *hnd, unsigned int sector)
{
	struct volume_info *vol;

/* Find the volume this sector is from in the table of open volumes. */
	for (vol = hnd->volumes; vol; vol = vol->next)
	{
		if (vol->start == sector)
		{
			break;
		}
	}

	if (vol)
	{
		return (vol->sectors);
	}

	return 0;
}

/**********************************************/
/* Return the size of all loaded volume sets. */
unsigned int
mfsvol_volume_set_size (struct volume_handle *hnd)
{
	struct volume_info *vol;
	int total = 0;

	for (vol = hnd->volumes; vol; vol = vol->next)
	{
		total += vol->sectors;
	}

	return total;
}

/****************************************************************************/
/* Verify that a sector is writable.  This should be done for all groups of */
/* sectors to be written, since individual volumes can be opened RDONLY. */
int
mfsvol_is_writable (struct volume_handle *hnd, unsigned int sector)
{
	struct volume_info *vol;

	vol = mfsvol_get_volume (hnd, sector);

	if (!vol || vol->vol_flags & VOL_RDONLY)
	{
		return hnd->fake_write;
	}

	return 1;
}

/***********************************************/
/* Free space used by the volumes linked list. */
void
mfsvol_cleanup (struct volume_handle *hnd)
{
	while (hnd->volumes)
	{
		struct volume_info *cur;

		cur = hnd->volumes;
		hnd->volumes = hnd->volumes->next;

		tivo_partition_close (cur->file);
		free (cur);
	}

	free (hnd);
}

/*****************************************************************************/
/* Read data from the MFS volume set.  It must be in whole sectors, and must */
/* not cross a volume boundry. */
int
mfsvol_read_data (struct volume_handle *hnd, void *buf, unsigned int sector, int count)
{
	struct volume_info *vol;

	vol = mfsvol_get_volume (hnd, sector);

/* If no volumes claim this sector, it's an IO error. */
	if (!vol)
	{
		errno = EIO;
		return -1;
	}

/* Make the sector number relative to this volume. */
	sector -= vol->start;

	if (sector + count > vol->sectors)
	{
#if DEBUG
		fprintf (stderr, "Attempt to read across volume boundry %d %d %d %d!", sector + vol->start, count, vol->start, vol->sectors);
#else
		fprintf (stderr, "Attempt to read across volume boundry!");
#endif
		errno = EIO;
		return -1;
	}

/* Read the data. */
	return tivo_partition_read (vol->file, buf, sector, count);
}

/****************************************************************************/
/* Doesn't really belong here, but useful for debugging with MFS_FAKE_WRITE */
/* set, this gets called instead of writing. */
static void
hexdump (unsigned char *buf, unsigned int sector)
{
	int ofs;

	for (ofs = 0; ofs < 512; ofs += 16)
	{
		unsigned char line[20];
		int myo;

		printf ("%05x:%03x ", sector, ofs);

		for (myo = 0; myo < 16; myo++)
		{
			printf ("%02x%c", buf[myo + ofs], myo < 15 && (myo & 3) == 3 ? '-' : ' ');
			line[myo] = (isprint (buf[myo + ofs]) ? buf[myo + ofs] : '.');
		}

		printf ("|");
		line[16] = ':';
		line[17] = '\n';
		line[18] = 0;
		printf ("%s", line);
	}
}

/****************************************************************************/
/* Write data to the MFS volume set.  It must be in whole sectors, and must */
/* not cross a volume boundry. */
int
mfsvol_write_data (struct volume_handle *hnd, void *buf, unsigned int sector, int count)
{
	struct volume_info *vol;

	vol = mfsvol_get_volume (hnd, sector);

/* If no volumes claim this sector, it's an IO error. */
	if (!vol)
	{
		errno = EIO;
		return -1;
	}

	if (hnd->fake_write)
	{
		int loop;
		for (loop = 0; loop < count; loop++)
		{
			hexdump ((unsigned char *) buf + loop * 512, sector + loop);
		}
		return count * 512;
	}

/* If the volume this sector is in was opened read-only, it's an error. */
/* Perhaps this should pretend to write, printing to stderr the attempt */
/* instead, useful for debug? */
	if (vol->vol_flags & VOL_RDONLY)
	{
		fprintf (stderr, "mfsvol_write_data: Attempt to write to read-only volume. \n");
		errno = EPERM;
		return -1;
	}

/* Make the sector number relative to this volume. */
	sector -= vol->start;
	if (sector + count > vol->sectors)
	{
		fprintf (stderr, "Attempt to write across volume boundry!");
		errno = EIO;
		return -1;
	}

/* Write the data. */
	return tivo_partition_write (vol->file, buf, sector, count);
}

/******************************************************************************/
/* Just a quick init.  All it really does is scan for the env MFS_FAKE_WRITE. */
struct volume_handle *
mfsvol_init ()
{
	char *fake = getenv ("MFS_FAKE_WRITE");
	struct volume_handle *hnd;

	hnd = calloc (sizeof (*hnd), 1);
	if (!hnd)
		return hnd;

	if (fake && *fake)
	{
		hnd->fake_write = 1;
	}

	return hnd;
}
