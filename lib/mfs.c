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

#include "mfs.h"

/*************************************/
/* Write the volume header back out. */
int
mfs_write_volume_header (struct mfs_handle *mfshnd)
{
	unsigned char buf[512];
	memset (buf, 0, sizeof (buf));

	MFS_update_crc (&mfshnd->vol_hdr, sizeof (mfshnd->vol_hdr), mfshnd->vol_hdr.checksum);
	memcpy (buf, &mfshnd->vol_hdr, sizeof (mfshnd->vol_hdr));

	if (mfsvol_write_data (mfshnd->vols, buf, 0, 1) != 512)
	{
		mfshnd->err_msg = "%s writing volume header";
		mfshnd->err_arg1 = strerror (errno);
		return -1;
	}
	if (mfsvol_write_data (mfshnd->vols, buf, mfsvol_volume_size (mfshnd->vols, 0) - 1, 1) != 512)
	{
		mfshnd->err_msg = "%s writing volume header";
		mfshnd->err_arg1 = strerror (errno);
		return -1;
	}
}

/**************************************/
/* Load and verify the volume header. */
int
mfs_load_volume_header (struct mfs_handle *mfshnd, int flags)
{
	unsigned char buf[512];
	unsigned char *volume_names;
	unsigned int total_sectors = 0;

/* Read in the volume header. */
	if (mfsvol_read_data (mfshnd->vols, buf, 0, 1) != 512)
	{
		mfshnd->err_msg = "%s reading volume header";
		mfshnd->err_arg1 = strerror (errno);
		return -1;
	}

/* Copy it into the static space.  This is needed since mfsvol_read_data must */
/* read even sectors. */
	memcpy ((void *) &mfshnd->vol_hdr, buf, sizeof (mfshnd->vol_hdr));

/* Verify the checksum. */
	if (!MFS_check_crc (&mfshnd->vol_hdr, sizeof (mfshnd->vol_hdr), mfshnd->vol_hdr.checksum))
	{
/* If the checksum doesn't match, try the backup. */
		if (mfsvol_read_data (mfshnd->vols, buf, mfsvol_volume_size (mfshnd->vols, 0) - 1, 1) != 512)
		{
			mfshnd->err_msg = "%s reading volume header";
			mfshnd->err_arg1 = strerror (errno);
			return -1;
		}

		memcpy ((void *) &mfshnd->vol_hdr, buf, sizeof (mfshnd->vol_hdr));

		if (!MFS_check_crc (&mfshnd->vol_hdr, sizeof (mfshnd->vol_hdr), mfshnd->vol_hdr.checksum))
		{
/* Backup checksum doesn't match either.  It's the end of the world! */
			mfshnd->err_msg = "Volume header corrupt";
			return -1;
		}
	}

	/* Increment the boot cycle number */
	mfshnd->bootcycle = intswap32 (mfshnd->vol_hdr.bootcycles) + 1;
	/* Fake out seconds, all that's important is that it moves forward */
	mfshnd->bootsecs = 1;

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
	if (total_sectors != intswap32 (mfshnd->vol_hdr.total_sectors))
	{
		mfshnd->err_msg = "Volume size (%u) mismatch with reported size (%u)";
		mfshnd->err_arg1 = (void *)total_sectors;
		mfshnd->err_arg2 = (void *)intswap32 (mfshnd->vol_hdr.total_sectors);
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
mfs_init_internal (struct mfs_handle *mfshnd, char *hda, char *hdb, int flags)
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

	mfshnd->vols = mfsvol_init (hda, hdb);
	if (!mfshnd->vols)
	{
		free (mfshnd);
		return -1;
	}

/* Load the first volume by hand. */
	if (mfsvol_add_volume (mfshnd->vols, cur_volume, flags) < 0)
	{
		return -1;
	}

/* Take care of loading the rest. */
	if (mfs_load_volume_header (mfshnd, flags) <= 0)
	{
		return -1;
	}

/* Load the zone maps. */
	if (mfs_load_zone_maps (mfshnd) < 0)
	{
		return -1;
	}

	return 0;
}

/********************************/
/* Wrapper for first init case. */
/* The caller is responsible for checking for error cases. */
struct mfs_handle *
mfs_init (char *hda, char *hdb, int flags)
{
	struct mfs_handle *mfshnd = malloc (sizeof (*mfshnd));
	if (!mfshnd)
		return 0;

	mfs_init_internal (mfshnd, hda, hdb, flags);

	return mfshnd;
}

/*************************/
/* Display the MFS error */
void
mfs_perror (struct mfs_handle *mfshnd, char *str)
{
	int err = 0;

	if (mfshnd->err_msg)
	{
		fprintf (stderr, "%s: ", str);
		fprintf (stderr, mfshnd->err_msg, mfshnd->err_arg1, mfshnd->err_arg2, mfshnd->err_arg3);
		fprintf (stderr, ".\n");
		err = 1;
	}

	if (mfshnd->vols->err_msg)
	{
		mfsvol_perror (mfshnd->vols, str);
		err = 2;
	}

	if (err == 0)
	{
		fprintf (stderr, "%s: No error.\n", str);
	}
}

/*************************************/
/* Return the MFS error in a string. */
int
mfs_strerror (struct mfs_handle *mfshnd, char *str)
{
	if (mfshnd->err_msg)
		sprintf (str, mfshnd->err_msg, mfshnd->err_arg1, mfshnd->err_arg2, mfshnd->err_arg3);
	else return (mfsvol_strerror (mfshnd->vols, str));

	return 1;
}

/*******************************/
/* Check if there is an error. */
int
mfs_has_error (struct mfs_handle *mfshnd)
{
	if (mfshnd->err_msg)
		return 1;

	return mfsvol_has_error (mfshnd->vols);
}

/********************/
/* Clear any errors */
void
mfs_clearerror (struct mfs_handle *mfshnd)
{
	mfshnd->err_msg = 0;
	mfshnd->err_arg1 = 0;
	mfshnd->err_arg2 = 0;
	mfshnd->err_arg3 = 0;

	if (mfshnd->vols)
		mfsvol_clearerror (mfshnd->vols);
}

/************************************************/
/* Free all used memory and close opened files. */
void
mfs_cleanup (struct mfs_handle *mfshnd)
{
	mfs_cleanup_zone_maps (mfshnd);
	if (mfshnd->vols)
		mfsvol_cleanup (mfshnd->vols);
	free (mfshnd);
}

/********************************/
/* Do a cleanup and init fresh. */
/* The caller is responsible for checking for error cases. */
int
mfs_reinit (struct mfs_handle *mfshnd, int flags)
{
	int ret = 0;
	struct volume_handle *vols = mfshnd->vols;

	mfs_cleanup_zone_maps (mfshnd);

	mfs_init_internal (mfshnd, vols->hda, vols->hdb, flags);

	mfsvol_cleanup (vols);

	return 0;
}
