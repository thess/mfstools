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

/*********************************************/
/* Read an inode into a pre-allocated buffer */
int
mfs_read_inode_to_buf (struct mfs_handle *mfshnd, unsigned int inode, mfs_inode *inode_buf)
{
	uint64_t sector;

	if (!inode_buf)
	{
		return -1;
	}

/* Find the sector number for this inode. */
	sector = mfs_inode_to_sector (mfshnd, inode);
	if (sector == 0)
	{
		return -1;
	}

	if (mfsvol_read_data (mfshnd->vols, (void *) inode_buf, sector, 1) != 512)
	{
		return -1;
	}

/* If the CRC is good, don't bother reading the next inode. */
	if (MFS_check_crc (inode_buf, 512, inode_buf->checksum))
	{
		return 1;
	}

/* CRC is bad, try reading the backup on the next sector. */
	if (mfsvol_read_data (mfshnd->vols, (void *) inode_buf, sector + 1, 1) != 512)
	{
		return -1;
	}

	if (MFS_check_crc (inode_buf, 512, inode_buf->checksum))
	{
		return 1;
	}

	mfshnd->err_msg = "Inode %d corrupt";
	mfshnd->err_arg1 = inode;

	return -1;
}

/*************************************/
/* Read an inode data and return it. */
mfs_inode *
mfs_read_inode (struct mfs_handle *mfshnd, unsigned int inode)
{
	mfs_inode *in = calloc (512, 1);

	if (mfs_read_inode_to_buf (mfshnd, inode, in) <= 0)
	{
		free (in);
		return NULL;
	}

	return in;
}

/*******************/
/* Write an inode. */
int
mfs_write_inode (struct mfs_handle *mfshnd, mfs_inode *inode)
{
	char buf[1024];
	uint64_t sector;

/* Find the sector number for this inode. */
	sector = mfs_inode_to_sector (mfshnd, intswap32 (inode->inode));
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
mfs_read_inode_by_fsid (struct mfs_handle *mfshnd, uint32_t fsid)
{
	unsigned int inode = (fsid * MFS_FSID_HASH) & (mfs_inode_count (mfshnd) - 1);
	mfs_inode *cur = NULL;
	unsigned int inode_base = inode;

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
	while (cur && intswap32 (cur->fsid) != fsid && (intswap32 (cur->inode_flags) & INODE_CHAINED) && (inode = (inode + 1) % (mfs_inode_count (mfshnd))) != inode_base);

/* If cur is NULL or the fsid is correct and in use, then cur contains the */
/* right return. */
	if (!cur || (intswap32 (cur->fsid) == fsid && cur->refcount != 0))
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
	unsigned int inode = (fsid * MFS_FSID_HASH) & (mfs_inode_count (mfshnd) - 1);
	mfs_inode *cur = NULL;
	unsigned int inode_base = inode;
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
	while (cur && intswap32 (cur->fsid) != fsid && (intswap32 (cur->inode_flags) & INODE_CHAINED) && (inode = (inode + 1) % (mfs_inode_count (mfshnd))) != inode_base);

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
	if (cur && (intswap32 (cur->fsid) == fsid))
	{
		if (first && first != cur)
		{
			free (first);
		}
		return cur;
	}

/* If the fsid wasn't located, but an empty inode was, return that. */
	if (first)
	{
		if (cur && cur != first)
		{
			free (cur);
		}
/* Make sure the inode number is set */
		first->inode = intswap32 (inode);
		return first;
	}

/* Keep looking */
	do
	{
		if (cur)
		{
/* Mark this inode chained */
			if (!(cur->inode_flags & intswap32 (INODE_CHAINED)))
			{
				cur->inode_flags |= intswap32 (INODE_CHAINED);
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

	cur->inode = intswap32 (inode);
	return cur;
}

/**************************************/
/* Write a portion of an inodes data. */
int
mfs_write_inode_data_part (struct mfs_handle *mfshnd, mfs_inode * inode, unsigned char *data, uint32_t start, unsigned int count)
{
	int totwrit = 0;

/* Parameter sanity check. */
	if (!data || !count || !inode)
	{
		return 0;
	}

/* If it all fits in the inode block... */
	if (inode->inode_flags & intswap32 (INODE_DATA) || inode->inode_flags & intswap32 (INODE_DATA2))
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
		for (loop = 0; count && loop < intswap32 (inode->numblocks); loop++)
		{
/* For sanity sake (Mine, not the code's), make these variables. */
			uint64_t blkstart;
			uint32_t blkcount;
			int result;

			if (mfshnd->is_64)
			{
				blkstart = sectorswap64 (inode->datablocks.d64[loop].sector);
				blkcount = intswap32 (inode->datablocks.d64[loop].count);
			}
			else
			{
				blkstart = intswap32 (inode->datablocks.d32[loop].sector);
				blkcount = intswap32 (inode->datablocks.d32[loop].count);
			}

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
mfs_read_inode_data_part (struct mfs_handle *mfshnd, mfs_inode * inode, unsigned char *data, uint64_t start, unsigned int count)
{
	int totread = 0;

/* Parameter sanity check. */
	if (!data || !count || !inode)
	{
		return 0;
	}

/* All the data fits in the inode */
	if (inode->inode_flags & intswap32 (INODE_DATA) || inode->inode_flags & intswap32 (INODE_DATA2))
	{
		uint32_t size = intswap32 (inode->size);
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
		for (loop = 0; count && loop < intswap32 (inode->numblocks); loop++)
		{
/* For sanity sake, make these variables. */
			uint64_t blkstart;
			uint64_t blkcount;
			int result;

			if (mfshnd->is_64)
			{
				blkstart = sectorswap64 (inode->datablocks.d64[loop].sector);
				blkcount = intswap32 (inode->datablocks.d64[loop].count);
			}
			else
			{
				blkstart = intswap32 (inode->datablocks.d32[loop].sector);
				blkcount = intswap32 (inode->datablocks.d32[loop].count);
			}

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
			count -= (uint32_t) blkcount;

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

	*size = (int) intswap32 (inode->size);

	data = malloc ((unsigned)(*size + 511) & ~511);
	if (!data)
	{
		*size = 0;
		return NULL;
	}

/* This function is just a wrapper for read_inode_data_part, with the last */
/* parameter being implicitly the whole data. */
	result = (int) mfs_read_inode_data_part (mfshnd, inode, data, 0, (*size + 511) / 512);

	if (result < 0)
	{
		*size = result;
		free (data);
		return NULL;
	}

	return data;
}

/* Borrowed from mfs-utils */
/* return a string identifier for a tivo file type */
char *
mfs_type_string(fsid_type type)
{
	switch (type) {
	case tyFile: return "tyFile";
	case tyStream: return "tyStream";
	case tyDir: return "tyDir";
	case tyDb: return "tyDb";
	default: 
		return "ty???";
	}
}

/* free a dir from mfs_dir */
void
mfs_dir_free(mfs_dirent *dir)
{
	int i;
	for (i=0; dir[i].name; i++) {
		free(dir[i].name);
		dir[i].name = NULL;
	}
	free(dir);
}

/* list a mfs directory - make sure you free with mfs_dir_free() */
mfs_dirent *
mfs_dir(struct mfs_handle *mfshnd, int fsid, uint32_t *count)
{
	uint32_t *buf, *p;
	int n=0, i;
	int dsize, dflags;
	mfs_dirent *ret;
	uint16_t *u16buf;
	mfs_inode *inode = 0;
	int size = 0;

	*count = 0;

	inode = mfs_read_inode_by_fsid (mfshnd, fsid);
	if (inode) 
		buf = (uint32_t *) mfs_read_inode_data (mfshnd, inode, &size);
	if (size < 4) return NULL;

	if (inode->type != tyDir) {
		mfshnd->err_msg = "fsid %d is not a tyDir";
		mfshnd->err_arg1=(int) fsid;
		mfs_perror (mfshnd, "mfs_dir");
		return NULL;
	}

	u16buf = (uint16_t *) buf;
	dsize = intswap16 (u16buf[0]);
	dflags = intswap16 (u16buf[1]);

	p = buf + 1;
	while ((int)(p-buf) < dsize/4) {
		uint8_t *s = ((unsigned char *)p)+4;
		p += s[0]/4;
		n++;
	}
	ret = malloc((n+1)*sizeof(*ret));
	p = buf + 1;
	for (i=0;i<n;i++) {
		uint8_t *s = ((unsigned char *)p)+4;
		ret[i].name = strdup((char *)s+2);
		ret[i].type = s[1];
		ret[i].fsid = intswap32 (p[0]);
		p += s[0]/4;
	}	
	ret[n].name = NULL;
	free(buf);
	*count = n;

	/* handle meta-directories. These are just directories which are
	   lists of other directories. All we need to do is recursively read
	   the other directories and piece together the top level directory */
	if (dflags == 0x200) {
		mfs_dirent *meta_dir = NULL;
		int meta_size=0;

		*count = 0;

		for (i=0;i<n;i++) {
			mfs_dirent *d2;
			unsigned int n2;
			if (ret[i].type != tyDir) {
				mfshnd->err_msg = "ERROR: non dir %d/%s in meta-dir %d!";
				mfshnd->err_arg1=(uint32_t) ret[i].type;
				mfshnd->err_arg2=(size_t) ret[i].name;
				mfshnd->err_arg3=(int) fsid;
				mfs_perror (mfshnd, "mfs_dir");
				continue;
			}
			d2 = mfs_dir(mfshnd, ret[i].fsid, &n2);
			if (!d2 || n2 == 0) continue;
			meta_dir = realloc(meta_dir, sizeof(ret[0])*(meta_size + n2 + 1));
			memcpy(meta_dir+meta_size, d2, n2*sizeof(ret[0]));
			meta_size += n2;
			free(d2);
		}
		mfs_dir_free(ret);
		if (meta_dir) meta_dir[meta_size].name = NULL;
		*count = meta_size;
		return meta_dir;
	}

	return ret;
}

/* resolve a path to a fsid */
uint32_t
mfs_resolve(struct mfs_handle *mfshnd, const char *pathin)
{
	char *path, *tok, *r=NULL;
	uint32_t fsid;
	mfs_dirent *dir = NULL;

	if (pathin[0] != '/') {
		return atoi(pathin);
	}

	fsid = 1;
	path = strdup(pathin);
	for (tok=strtok_r(path,"/", &r); tok; tok=strtok_r(NULL,"/", &r)) {
		uint32_t count;
		int i;
		dir = mfs_dir(mfshnd, fsid, &count);
		if (!dir) {
			mfshnd->err_msg = "resolve failed for fsid=%d";
			mfshnd->err_arg1=(int) fsid;
			mfs_perror (mfshnd, "mfs_resolve");
			return 0;
		}
		for (i=0;i<count;i++) {
			if (strcmp(tok, dir[i].name) == 0) break;
		}
		if (i == count) {
			fsid = 0;
			goto done;
		}
		fsid = dir[i].fsid;
		if (dir[i].type != tyDir) {
			if (strtok_r(NULL, "/", &r)) {
				mfshnd->err_msg = "not a directory %s";
				mfshnd->err_arg1=(size_t) tok;
				mfs_perror (mfshnd, "mfs_resolve");
				fsid = 0;
				goto done;
			}
			goto done;
		}
		mfs_dir_free(dir);
		dir = NULL;
	}

 done:
	if (dir) mfs_dir_free(dir);
	if (path) free(path);
	return fsid;
}

static int
parse_attr(char *p, int obj_type, int fsid, mfs_subobj_header *obj, object_fn fn)
{
	mfs_attr_header *attr;
	int ret;

	attr = (mfs_attr_header *)p;

	p += sizeof(*attr);

	fn(fsid, obj, attr, p);

	ret = (intswap16 (attr->len)+3)&~3;
	return ret;
}

static void
parse_subobj(void *p, uint16_t type, int len, int fsid, mfs_subobj_header *obj, object_fn fn)
{
	int ofs=0;
	while (ofs < len) {
		ofs += parse_attr(p+ofs, type, fsid, obj, fn);
	}
}

/* this is the low-level interface to parsing an object. It will call fn() on
   all elements in all subobjects */
void
parse_object(int fsid, void *buf, object_fn fn)
{
	char *p;
	uint32_t ofs;
	mfs_obj_header *obj = buf;
	int i=0;

	p = buf;
	ofs = sizeof(*obj); 

	/* now the subobjects */
	while (ofs < intswap32 (obj->size)) {
		mfs_subobj_header *subobj = buf+ofs;
		fn(fsid, subobj, NULL, NULL);
		parse_subobj(buf+ofs+sizeof(*subobj), 
			     intswap16 (subobj->obj_type),
			     intswap16 (subobj->len)-sizeof(*subobj), fsid, subobj, fn);
		ofs += intswap16 (subobj->len);
		i++;
	}
}

