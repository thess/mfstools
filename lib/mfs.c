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

/**************************************/
/* Load and verify the volume header. */
int
mfs_load_volume_header (struct mfs_handle *mfshnd, int flags)
{
	unsigned char buf[512];
	unsigned char *volume_names;
	unsigned int total_sectors = 0;
	struct volume_info *vol;

/* Read in the volume header. */
	if (mfsvol_read_data (mfshnd->vols, buf, 0, 1) != 512)
	{
		perror ("mfs_load_volume_header: mfsvol_read_data");
		return -1;
	}

/* Copy it into the static space.  This is needed since mfsvol_read_data must */
/* read even sectors. */
	memcpy ((void *) &mfshnd->vol_hdr, buf, sizeof (mfshnd->vol_hdr));

/* Verify the checksum. */
	if (!MFS_check_crc (&mfshnd->vol_hdr, sizeof (mfshnd->vol_hdr), mfshnd->vol_hdr.checksum))
	{
		fprintf (stderr, "Primary volume header corrupt, trying backup.\n");
/* If the checksum doesn't match, try the backup. */
		if (mfsvol_read_data (mfshnd->vols, buf, mfsvol_volume_size (mfshnd->vols, 0) - 1, 1) != 512)
		{
			perror ("mfs_load_volume_header: mfsvol_read_data");
			return -1;
		}

		memcpy ((void *) &mfshnd->vol_hdr, buf, sizeof (mfshnd->vol_hdr));

		if (!MFS_check_crc (&mfshnd->vol_hdr, sizeof (mfshnd->vol_hdr), mfshnd->vol_hdr.checksum))
		{
/* Backup checksum doesn't match either.  It's the end of the world! */
			fprintf (stderr, "Secondary volume header corrupt, giving up.\n");
			fprintf (stderr, "mfs_load_volume_header: Bad checksum.\n");
			return -1;
		}
	}

/* Load the partition list from MFS. */
	volume_names = mfshnd->vol_hdr.partitionlist;

/* Skip the first volume since it's already loaded. */
	if (*volume_names)
	{
		volume_names += strcspn (volume_names, " \t\r\n");
		volume_names += strspn (volume_names, " \t\r\n");
	}

/* If theres more volumes, add each one in turn.  When mfsvol_add_volume calls */
/* mfsvol_device_translate, it will take care of seperating out one device. */
	while (*volume_names)
	{
		if (mfsvol_add_volume (mfshnd->vols, volume_names, flags) < 0)
		{
			return -1;
		}

/* Skip the device just loaded. */
		volume_names += strcspn (volume_names, " \t\r\n");
		volume_names += strspn (volume_names, " \t\r\n");
	}

/* Count the total number of sectors in the volume set. */
	total_sectors = mfsvol_volume_set_size (mfshnd->vols);

/* If the sectors mismatch, report it.. But continue anyway. */
	if (total_sectors != htonl (mfshnd->vol_hdr.total_sectors))
	{
		fprintf (stderr, "mfs_load_volume_header: Total sectors(%u) mismatch with volume header (%d)\n", total_sectors, htonl (mfshnd->vol_hdr.total_sectors));
		fprintf (stderr, "mfs_load_volume_header: Loading anyway.\n");
	}

	return total_sectors;
}

/***********************************************************************/
/* Return the list of partitions from the volume header.  That is all. */
char *
mfs_partition_list (struct mfs_handle *mfshnd)
{
	return mfshnd->vol_hdr.partitionlist;
}

/***********************************************************/
/* Initialize MFS, load the volume set, and all zone maps. */
/* TODO: If opened read-write, also replay journal and make sure real event */
/* switcher is not running. */
static int
mfs_init_internal (struct mfs_handle *mfshnd, int flags)
{
/* Bootstrap the first volume from MFS_DEVICE. */
	char *cur_volume = getenv ("MFS_DEVICE");

/* Only allow O_RDONLY or O_RDWR. */
	if ((flags & O_ACCMODE) == O_RDONLY)
	{
		flags = O_RDONLY;
	}
	else
	{
		flags = O_RDWR;
	}

/* If no volume is passed, assume hda10. */
	if (!cur_volume)
	{
		cur_volume = "/dev/hda10";
	}

	bzero (mfshnd, sizeof (*mfshnd));

	mfshnd->vols = mfsvol_init ();
	if (!mfshnd->vols)
	{
		free (mfshnd);
		return -1;
	}

/* Load the first volume by hand. */
	if (mfsvol_add_volume (mfshnd->vols, cur_volume, flags) < 0)
	{
		mfsvol_cleanup (mfshnd->vols);
		free (mfshnd);
		return -1;
	}

/* Take care of loading the rest. */
	if (mfs_load_volume_header (mfshnd, flags) <= 0)
	{
		mfsvol_cleanup (mfshnd->vols);
		free (mfshnd);
		return -1;
	}

/* Load the zone maps. */
	if (mfs_load_zone_maps (mfshnd) < 0)
	{
		mfs_cleanup_zone_maps (mfshnd);
		mfsvol_cleanup (mfshnd->vols);
		free (mfshnd);
		return -1;
	}

	return 0;
}

/********************************/
/* Wrapper for first init case. */
struct mfs_handle *
mfs_init (int flags)
{
	struct mfs_handle *mfshnd = malloc (sizeof (*mfshnd));
	if (!mfshnd)
		return 0;

	if (mfs_init_internal (mfshnd, flags))
	{
		free (mfshnd);
		return 0;
	}

	return mfshnd;
}

/************************************************/
/* Free all used memory and close opened files. */
void
mfs_cleanup (struct mfs_handle *mfshnd)
{
	mfs_cleanup_zone_maps (mfshnd);
	mfsvol_cleanup (mfshnd->vols);
	free (mfshnd);
}

/********************************/
/* Do a cleanup and init fresh. */
int
mfs_reinit (struct mfs_handle *mfshnd, int flags)
{
	mfs_cleanup_zone_maps (mfshnd);
	mfsvol_cleanup (mfshnd->vols);
	if (mfs_init_internal (mfshnd, flags))
	{
		bzero (mfshnd, sizeof (mfshnd));
		return -1;
	}
	return 0;
}
