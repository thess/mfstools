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
	if (mfsvol_read_data (mfshnd->vols, (void *) in, sector + 1, 1) != 512)
	{
		free (in);
		return NULL;
	}

	if (MFS_check_crc (in, 512, in->checksum))
	{
		return in;
	}

	mfshnd->err_msg = "Inode %d corrupt";
	mfshnd->err_arg1 = (void *)inode;

	return NULL;
}

/*******************/
/* Write an inode. */
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

	memcpy (buf, inode, 512);
/* Do it after to avoid writing to source */
	MFS_update_crc (buf, 512, ((mfs_inode *)buf)->checksum);
	memcpy (buf + 512, buf, 512);

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
	while (cur && htonl (cur->fsid) != fsid && (htonl (cur->inode_flags) & INODE_CHAINED) && (inode = (inode + 1) % (mfs_inode_count (mfshnd))) != inode_base);

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

/******************************************************************/
/* Given a fsid, find an inode for it if one doesn't already exist. */
mfs_inode *
mfs_find_inode_for_fsid (struct mfs_handle *mfshnd, unsigned int fsid)
{
	int inode = (fsid * MFS_FSID_HASH) & (mfs_inode_count (mfshnd) - 1);
	mfs_inode *cur = NULL;
	int inode_base = inode;
	mfs_inode *first = NULL;

	do
	{
		if (cur && cur != first)
		{
			free (cur);
		}

		cur = mfs_read_inode (mfshnd, inode);
		if (cur && !first && !cur->fsid && !cur->refcount)
		{
			first = cur;
		}
/* Repeat until either the fsid matches, the CHAINED flag is unset, or */
/* every inode has been checked, which I hope I will not have to do. */
	}
	while (cur && htonl (cur->fsid) != fsid && (htonl (cur->inode_flags) & INODE_CHAINED) && (inode = (inode + 1) % (mfs_inode_count (mfshnd))) != inode_base);

/* If nothing was read, something is wrong */
	if (!cur)
	{
		if (first)
		{
			free (first);
		}
		return NULL;
	}

/* If the fsid was found, return the inode */
	if (cur && (htonl (cur->fsid) == fsid))
	{
		if (first)
		{
			free (first);
		}
		return cur;
	}

/* If the fsid wasn't located, but an empty inode was, return that. */
	if (first)
	{
		if (cur)
		{
			free (cur);
		}
		return first;
	}

/* Keep looking */
	do
	{
		if (cur)
		{
/* Mark this inode chained */
			if (!(cur->inode_flags & htonl (INODE_CHAINED)))
			{
				cur->inode_flags |= htonl (INODE_CHAINED);
				if (mfs_write_inode (mfshnd, cur) < 0)
				{
					free (cur);
					return NULL;
				}
			}
			free (cur);
		}

		cur = mfs_read_inode (mfshnd, inode);
		
/* Repeat until a free inode is found, or */
/* every inode has been checked, which I hope I will not have to do. */
	}
	while (cur && (cur->fsid || cur->refcount) && (inode = (inode + 1) % (mfs_inode_count (mfshnd))) != inode_base);

	if (!cur)
		return NULL;

	if (cur->fsid || cur->refcount)
	{
		free (cur);
		return NULL;
	}

	cur->inode = inode;
	return cur;
}

/**************************************/
/* Write a portion of an inodes data. */
int
mfs_write_inode_data_part (struct mfs_handle *mfshnd, mfs_inode * inode, unsigned char *data, unsigned int start, unsigned int count)
{
	int totwrit = 0;

/* Parameter sanity check. */
	if (!data || !count || !inode)
	{
		return 0;
	}

/* If it all fits in the inode block... */
	if (inode->inode_flags & htonl (INODE_DATA))
	{
		int result;

		if (start)
		{
			return 0;
		}

		memcpy ((unsigned char *)inode + 0x3c, data, 512 - 0x3c);
		result = mfs_write_inode (mfshnd, inode);

		return result < 0? result: 512;
	}
	else if (inode->numblocks)
/* If it doesn't fit in the sector find out where it is. */
	{
		int loop;

/* Loop through each block in the inode. */
		for (loop = 0; count && loop < htonl (inode->numblocks); loop++)
		{
/* For sanity sake (Mine, not the code's), make these variables. */
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

			result = mfsvol_write_data (mfshnd->vols, data, blkstart, blkcount);
			count -= blkcount;

/* Error - propogate it up. */
			if (result < 0)
			{
				return result;
			}

/* Add to the total. */
			totwrit += result;
			data += result;
/* If this is it, or if the amount written was truncated, return it. */
			if (result != blkcount * 512 || count == 0)
			{
				return totwrit;
			}
		}
	}

/* They must have asked for more data than there was.  Return the total written. */
	return totwrit;
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

/* All the data fits in the inode */
	if (inode->inode_flags & htonl (INODE_DATA))
	{
		int size = htonl (inode->size);

		if (start)
		{
			return 0;
		}

/* Corrupted inode, but fake it at least */
		if (size > 512 - 0x3c)
			size = 512 - 0xc3;

		memset (data + size, 0, 512 - size);
		memcpy (data, (unsigned char *) inode + 0x3c, size);
		return 512;
	}
/* If it doesn't fit in the sector find out where it is. */
	else if (inode->numblocks)
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
