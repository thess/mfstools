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

/*************************************/
/* Read an inode data and return it. */
mfs_inode *
mfs_read_inode (struct mfs_handle *mfshnd, unsigned int inode)
{
	mfs_inode *in = calloc (512, 1);
	int sector;

	if (!in)
	{
		return NULL;
	}

/* Find the sector number for this inode. */
	sector = mfs_inode_to_sector (mfshnd, inode);
	if (sector == 0)
	{
		free (in);
		return NULL;
	}

	if (mfsvol_read_data (mfshnd->vols, (void *) in, sector, 1) != 512)
	{
		free (in);
		return NULL;
	}

/* If the CRC is good, don't bother reading the next inode. */
	if (MFS_check_crc (in, 512, in->checksum))
	{
		return in;
	}

/* CRC is bad, try reading the backup on the next sector. */
	fprintf (stderr, "mfs_read_inode: Inode %d corrupt, trying backup.\n", inode);

	if (mfsvol_read_data (mfshnd->vols, (void *) in, sector + 1, 1) != 512)
	{
		free (in);
		return NULL;
	}

	if (MFS_check_crc (in, 512, in->checksum))
	{
		return in;
	}

	fprintf (stderr, "mfs_read_inode: Inode %d backup corrupt, giving up.\n", inode);
	return NULL;
}

/*************************************/
/* Read an inode data and return it. */
int
mfs_write_inode (struct mfs_handle *mfshnd, mfs_inode *inode)
{
	char buf[1024];
	int sector;

/* Find the sector number for this inode. */
	sector = mfs_inode_to_sector (mfshnd, htonl (inode->inode));
	if (sector == 0)
	{
		return -1;
	}

	MFS_update_crc (inode, 512, inode->checksum);
	memcpy (buf, inode, 512);
	memcpy (buf + 512, inode, 512);

	if (mfsvol_write_data (mfshnd->vols, buf, sector, 2) != 1024)
	{
		return -1;
	}

	return 0;
}

/******************************************************************/
/* Read an inode data based on an fsid, scanning ahead as needed. */
mfs_inode *
mfs_read_inode_by_fsid (struct mfs_handle *mfshnd, unsigned int fsid)
{
	int inode = (fsid * MFS_FSID_HASH) & (mfs_inode_count (mfshnd) - 1);
	mfs_inode *cur = NULL;
	int inode_base = inode;

	do
	{
		if (cur)
		{
			free (cur);
		}

		cur = mfs_read_inode (mfshnd, inode);
/* Repeat until either the fsid matches, the CHAINED flag is unset, or */
/* every inode has been checked, which I hope I will not have to do. */
	}
	while (htonl (cur->fsid) != fsid && (htonl (cur->inode_flags) & INODE_CHAINED) && (inode = (inode + 1) & (mfs_inode_count (mfshnd) - 1)) != inode_base);

/* If cur is NULL or the fsid is correct and in use, then cur contains the */
/* right return. */
	if (!cur || (htonl (cur->fsid) == fsid && cur->refcount != 0))
	{
		return cur;
	}

/* This is not the inode you are looking for.  Move along. */
	free (cur);
	return NULL;
}

/*************************************/
/* Read a portion of an inodes data. */
int
mfs_read_inode_data_part (struct mfs_handle *mfshnd, mfs_inode * inode, unsigned char *data, unsigned int start, unsigned int count)
{
	int totread = 0;

/* Parameter sanity check. */
	if (!data || !count || !inode)
	{
		return 0;
	}

/* If it doesn't fit in the sector find out where it is. */
	if (inode->numblocks)
	{
		int loop;

/* Loop through each block in the inode. */
		for (loop = 0; count && loop < htonl (inode->numblocks); loop++)
		{
/* For sanity sake, make these variables. */
			unsigned int blkstart = htonl (inode->datablocks[loop].sector);
			unsigned int blkcount = htonl (inode->datablocks[loop].count);
			int result;

/* If the start offset has not been reached, skip to it. */
			if (start)
			{
				if (blkcount <= start)
				{
/* If the start offset is not within this block, decrement the start and keep */
/* going. */
					start -= blkcount;
					continue;
				}
				else
				{
/* The start offset is within this block.  Adjust the block parameters a */
/* little, since this is just local variables. */
					blkstart += start;
					blkcount -= start;
					start = 0;
				}
			}

/* If the entire data is within this block, make this block look like it */
/* is no bigger than the data. */
			if (blkcount > count)
			{
				blkcount = count;
			}

			result = mfsvol_read_data (mfshnd->vols, data, blkstart, blkcount);
			count -= blkcount;

/* Error - propogate it up. */
			if (result < 0)
			{
				return result;
			}

/* Add to the total. */
			totread += result;
			data += result;
/* If this is it, or if the amount read was truncated, return it. */
			if (result != blkcount * 512 || count == 0)
			{
				return totread;
			}
		}
	}
	else if (htonl (inode->size) < 512 - 0x3c && inode->type != tyStream)
	{
		if (start)
		{
			return 0;
		}
		memset (data, 0, 512);
		memcpy (data, (unsigned char *) inode + 0x3c, htonl (inode->size));
		return 512;
	}

/* They must have asked for more data than there was.  Return the total read. */
	return totread;
}

/******************************************************************************/
/* Read all the data from an inode, set size to how much was read.  This does */
/* not allow streams, since they are be so big. */
unsigned char *
mfs_read_inode_data (struct mfs_handle *mfshnd, mfs_inode * inode, int *size)
{
	unsigned char *data;
	int result;

/* If it doesn't make sense to read the data, don't do it.  Since streams are */
/* so large, it doesn't make sense to read the whole thing. */
	if (inode->type == tyStream || !inode || !size || !inode->size)
	{
		if (size)
		{
			*size = 0;
		}
		return NULL;
	}

	*size = htonl (inode->size);

	data = malloc ((*size + 511) & ~511);
	if (!data)
	{
		*size = 0;
		return NULL;
	}

/* This function is just a wrapper for read_inode_data_part, with the last */
/* parameter being implicitly the whole data. */
	result = mfs_read_inode_data_part (mfshnd, inode, data, 0, (*size + 511) / 512);

	if (result < 0)
	{
		*size = result;
		free (data);
		return NULL;
	}

	return data;
}
