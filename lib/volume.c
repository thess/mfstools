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

/* Some static variables..  Really this should be a class and these */
/* private members. */
static struct volume_info *volumes = NULL;
volume_header vol_hdr;

/***********************************************************************/
/* Translate a device name from the TiVo view of the world to reality, */
/* allowing relocating of MFS volumes by setting MFS_... variables. */
/* For example, MFS_HDA=/dev/hdb would make /dev/hda* into /dev/hdb* */
/* Note that it goes most specific first, so MFS_HDA10 would match before */
/* MFS_HDA on /dev/hda10.  Also note that MFS_HDA1 would match /dev/hda10, */
/* so be careful.  In addition to relocating, if a relocated device starts */
/* with RO: the device or file will be opened O_RDONLY no matter what the */
/* requested mode was. */
char *mfs_device_translate(char *dev)
{
	static char devname[1024];
	int dev_len = strcspn (dev, " ");

/* See if it is in /dev, to be relocated. */
	if (!strncmp (dev, "/dev/", 5)) {
		char dev_sub_var[128];
		char *dev_sub;
		int loop;

		strcpy (dev_sub_var, "MFS_");

/* Copy in the entire rest of the name after /dev/, uppercasing it. */
		for (loop = 0; dev[5 + loop] && dev[5 + loop] != ' '; loop++) {
			dev_sub_var[loop + 4] = toupper (dev[loop + 5]);
		}
		dev_sub_var[loop + 4] = 0;

/* Slowly eat off one character until it is MFS_X checking for a variable */
/* each time, breaking when one is found. */
		while (loop > 1 && !(dev_sub = getenv (dev_sub_var))) {
			dev_sub_var[--loop + 4] = 0;
		}

/* If the variable is found, substitute it, tacking on the remainder of the */
/* device name passed in. */
		if (dev_sub) {
			sprintf (devname, "%s%.*s", dev_sub, dev_len - (loop + 5), dev + loop + 5);

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
int mfs_add_volume (char *path, int flags)
{
	struct volume_info *newvol;
	struct volume_info **loop;

	newvol = calloc (sizeof (*newvol), 1);

	if (!newvol) {
		fprintf (stderr, "Out of memory!");
		return -1;
	}

/* Translate the device name to it's real world equivelent. */
	path = mfs_device_translate (path);

/* If the user requested RO, let them have it.  This may break a writer */
/* program, but thats what it is intended to do. */
	if (!strncmp (path, "RO:", 3)) {
		path += 3;
		flags = (flags & ~O_ACCMODE) | O_RDONLY;
	}

	newvol->fd = open (path, flags);

/* If the open failed, perhaps the kernel doesn't understand the TiVo */
/* partition format.  Try a raw device open. */
	if (newvol->fd < 0) {
		tivo_partition_open (path, flags, newvol);
	}

	if (newvol->fd < 0) {
		perror (path);
		return 0;
	}

/* If read-only was requested, make it so. */
	if ((flags & O_ACCMODE) == O_RDONLY) {
		newvol->vol_flags |= VOL_RDONLY;
	}

/* Find out the size of the device.  If the sectors is already set, assume */
/* it is correct, and the flags are correct as well. */
	if (newvol->sectors == 0 && 
	    ioctl (newvol->fd, BLKGETSIZE, &newvol->sectors) < 0) {
		int tmperr = errno;
		loff_t tmp;

/* If BLKGETSIZE fails, the file may not be a block device.  Try llseek */
/* instead. */
		if (_llseek (newvol->fd, 0, 0, &tmp, SEEK_END) < 0) {
/* If llseek fails as well, it is something without random access.  Bail. */
			perror ("llseek");
			errno = tmperr;
			perror ("BLKGETSIZE");
			close (newvol->fd);
			free (newvol);
			return -1;
		}

/* Since llseek was used, the size is in bytes.  Divide it by sector size. */
/* The extra is truncated, but thats okay, since the size is truncated again */
/* below.  Also since llseek was used, mark it as a file so readsector is not */
/* used later. */
		newvol->sectors = tmp / 512;
		newvol->vol_flags |= VOL_FILE;
	}

	if (!newvol->sectors) {
		close (newvol->fd);
		newvol->fd = -1;
		tivo_partition_open (path, flags, newvol);
	}

/* TiVo rounds off the size of the partition to even counts of 1024 sectors. */
	newvol->sectors &= ~(MFS_PARTITION_ROUND - 1);

/* If theres nothing there, assume the worst. */
	if (!newvol->sectors) {
		fprintf (stderr, "Error: Empty partition %s.\n", path);
		close (newvol->fd);
		free (newvol);
		return -1;
	}

/* Add it to the tail of the volume list. */
	for (loop = &volumes; *loop; loop = &(*loop)->next) {
		newvol->start = (*loop)->start + (*loop)->sectors;
	}

	*loop = newvol;

	return newvol->start;
}

/*******************************************************/
/* Return the volume info for the volume sector is in. */
struct volume_info *mfs_get_volume (unsigned int sector)
{
	struct volume_info *vol;

/* Find the volume this sector is from in the table of open volumes. */
	for (vol = volumes; vol; vol = vol->next) {
		if (vol->start <= sector && vol->start + vol->sectors > sector) {
			break;
		}
	}

	return vol;
}

/*************************************************/
/* Return the size of volume starting at sector. */
int mfs_volume_size (unsigned int sector)
{
	struct volume_info *vol;

/* Find the volume this sector is from in the table of open volumes. */
	for (vol = volumes; vol; vol = vol->next) {
		if (vol->start == sector) {
			break;
		}
	}

	if (vol) {
		return (vol->sectors);
	}

	return 0;
}

/**********************************************/
/* Return the size of all loaded volume sets. */
int mfs_volume_set_size ()
{
	struct volume_info *vol;
	int total = 0;

	for (vol = volumes; vol; vol = vol->next) {
		total += vol->sectors;
	}

	return total;
}

/***********************************************/
/* Free space used by the volumes linked list. */
void mfs_cleanup_volumes ()
{
	while (volumes) {
		struct volume_info *cur;

		cur = volumes;
		volumes = volumes->next;

		close (cur->fd);
		free (cur);
	}
}

/**************************************/
/* Load and verify the volume header. */
int mfs_load_volume_header (int flags)
{
	unsigned char buf[512];
	unsigned char *volume_names;
	unsigned int total_sectors = 0;
	struct volume_info *vol;

/* Read in the volume header. */
	if (mfs_read_data (buf, 0, 1) != 512) {
		perror ("mfs_load_volume_header: mfs_read_data");
		return -1;
	}

/* Copy it into the static space.  This is needed since mfs_read_data must */
/* read even sectors. */
	memcpy ((void *)&vol_hdr, buf, sizeof (vol_hdr));

/* Verify the checksum. */
	if (!MFS_check_crc (&vol_hdr, sizeof (vol_hdr), vol_hdr.checksum)) {
		fprintf (stderr, "Primary volume header corrupt, trying backup.\n");
/* If the checksum doesn't match, try the backup. */
		if (mfs_read_data (buf, volumes->sectors - 1, 1) != 512) {
			perror ("mfs_load_volume_header: mfs_read_data");
			return -1;
		}

		memcpy ((void *)&vol_hdr, buf, sizeof (vol_hdr));

		if (!MFS_check_crc (&vol_hdr, sizeof (vol_hdr), vol_hdr.checksum)) {
/* Backup checksum doesn't match either.  It's the end of the world! */
			fprintf (stderr, "Secondary volume header corrupt, giving up.\n");
			fprintf (stderr, "mfs_load_volume_header: Bad checksum.\n");
			return -1;
		}
	}

/* Load the partition list from MFS. */
	volume_names = vol_hdr.partitionlist;

/* Skip the first volume since it's already loaded. */
	if (*volume_names) {
		volume_names += strcspn (volume_names, " \t\r\n");
		volume_names += strspn (volume_names, " \t\r\n");
	}

/* If theres more volumes, add each one in turn.  When mfs_add_volume calls */
/* mfs_device_translate, it will take care of seperating out one device. */
	while (*volume_names) {
		if (mfs_add_volume (volume_names, flags) < 0) {
			return -1;
		}

/* Skip the device just loaded. */
		volume_names += strcspn (volume_names, " \t\r\n");
		volume_names += strspn (volume_names, " \t\r\n");
	}

/* Count the total number of sectors in the volume set. */
	for (vol = volumes; vol; vol = vol->next) {
		total_sectors += vol->sectors;
	}

/* If the sectors mismatch, report it.. But continue anyway. */
	if (total_sectors != htonl (vol_hdr.total_sectors)) {
		fprintf (stderr, "mfs_load_volume_header: Total sectors(%u) mismatch with volume header (%d)\n", total_sectors, htonl (vol_hdr.total_sectors));
		fprintf (stderr, "mfs_load_volume_header: Loading anyway.\n");
	}

	return total_sectors;
}
