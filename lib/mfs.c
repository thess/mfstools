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

/***********************************************************/
/* Initialize MFS, load the volume set, and all zone maps. */
/* TODO: If opened read-write, also replay journal and make sure real event */
/* switcher is not running. */
int
mfs_init (int flags)
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

/* Load the first volume by hand. */
	if (mfs_add_volume (cur_volume, flags) < 0)
	{
		mfs_cleanup_volumes ();
		return -1;
	}

/* Take care of loading the rest. */
	if (mfs_load_volume_header (flags) <= 0)
	{
		mfs_cleanup_volumes ();
		return -1;
	}

/* Load the zone maps. */
	if (mfs_load_zone_maps () < 0)
	{
		mfs_cleanup_zone_maps ();
		mfs_cleanup_volumes ();
		return -1;
	}

/* Init the read/write. */
	if (mfs_readwrite_init () < 0)
	{
		mfs_cleanup_zone_maps ();
		mfs_cleanup_volumes ();
		return -1;
	}

	return 0;
}

/************************************************/
/* Free all used memory and close opened files. */
void
mfs_cleanup ()
{
	mfs_cleanup_zone_maps ();
	mfs_cleanup_volumes ();
}

/********************************/
/* Do a cleanup and init fresh. */
int
mfs_reinit (int flags)
{
	mfs_cleanup ();
	return mfs_init (flags);
}
