#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include "macpart.h"
#include "mfs.h"

static struct mfs_handle *mfs;
int user = -1;
int clip = -1;
int max = 0x7fffffff;

void
supersize_usage (char *progname)
{
	fprintf (stderr, "%s %s\n", PACKAGE, VERSION);
	fprintf (stderr, "Usage: %s [Adrive [Bdrive]] [options values]\n", progname);
	fprintf (stderr, "Options:\n");
	fprintf (stderr, " -h        Display this help message\n");
	fprintf (stderr, " -m        MaxDiskSize in KB (Default is 2147483647)\n");
	fprintf (stderr, " -u        User SizeInKb in KB (Default is -1)\n");
	fprintf (stderr, " -c        TivoClips SizeInKb in KB (Default is -1)\n");
}

/******************************************************************/
/* Read an inode data based on an fsid, scanning ahead as needed. */
/* Same as mfs_read_inode_by_fsid, but we don't look at refcount.  This allows us to find a inode that WinMFS set to refcount == 0 */
mfs_inode *
mfs_read_inode_by_fsid_ignore_refcount (struct mfs_handle *mfshnd, uint32_t fsid)
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
/* right return.  */
	if (!cur || (intswap32 (cur->fsid) == fsid))
	{
		return cur;
	}

/* This is not the inode you are looking for.  Move along. */
	free (cur);
	return NULL;
}

static void unlock_callback(int fsid, mfs_subobj_header *obj, mfs_attr_header *attr, void *data)
{
	int i;
	char *p = data;
	mfs_obj_attr *objattr;
	static uint16_t lasttype;
	static int lastid;

	if (!attr) {
		lasttype=intswap16 (obj->obj_type);
		return;
	}

	switch ((intswap16 (attr->attreltype) >> 8)>>6) {
	case TYPE_STRING:
	case TYPE_OBJECT:
		break;
	case TYPE_INT:
	case TYPE_FILE:
		if (lasttype==111 && (intswap16 (attr->attreltype) & 0xff)==16) {
			lastid = intswap32 (*(int *)p);
		}
		//MaxDiskSize
		if (lasttype==112 && (intswap16 (attr->attreltype) & 0xff)==20) {
			int oldsize = intswap32 (*(int *)p);
			if (oldsize != max) {
				*(int *)p = intswap32 (max);
			}
			fprintf(stderr, "Supersize: MaxDiskSize old value %d new value %d\n", oldsize, max);
		}
		//User
		if (lasttype==111  && (intswap16 (attr->attreltype) & 0xff)==17 && lastid==10) {
			int oldsize = intswap32 (*(int *)p);
			if (oldsize != user) {
				*(int *)p = intswap32 (user);
			}
				fprintf(stderr, "Supersize: User SizeInKb old value %d new value %d\n", oldsize, user);
		}
		//TivoClips
		if (lasttype==111  && (intswap16 (attr->attreltype) & 0xff)==17 && lastid==11) {
			int oldsize = intswap32 (*(int *)p);
			if (oldsize != clip) {
				*(int *)p = intswap32 (clip);
			}
				fprintf(stderr, "Supersize: TivoClips SizeInKb old value %d new value %d\n", oldsize, clip);
		}
			break;
	}
}

int
supersize()
{
	uint32_t fsid = mfs_resolve(mfs, "/Config/DiskConfigurations/Active");
	mfs_inode *inode;
	void *buf;
	uint32_t size;

	if (fsid == 0) {
		fprintf(stderr, "Supersize: Unable to locate /Config/DiskConfigurations/Active.  No changes made.\n");
		return 0;
	}
	
	inode = mfs_read_inode_by_fsid (mfs, fsid);
	
	if (!inode) {
		//We'll make an effort to undo a WinMFS supersize...
		inode = mfs_read_inode_by_fsid_ignore_refcount (mfs, fsid);
		if (!inode) {
			fprintf(stderr, "Supersize: Unable to locate /Config/DiskConfigurations/Active.  No changes made.\n");
			return 0;
		}
		// Let's reset the refcount to 3
		fprintf(stderr, "Supersize: Resetting refcount to 3.\n");
		inode->refcount = intswap32 (3);
		mfs_reinit (mfs, O_RDWR);
		mfs_write_inode(mfs, inode);
		mfs_reinit (mfs, O_RDONLY);
	}

	if (inode->type != tyDb) {
		return 0;
	}
	if (intswap32 (inode->unk3) == 0x20000) 
		size = intswap32 (inode->size) * intswap32 (inode->unk3);
	else
		size = intswap32 (inode->size);

	buf = mfs_read_inode_data(mfs, inode, &size);
	parse_object(fsid, buf, unlock_callback);
	
	fprintf(stderr, "Supersize: Recording the old values will allow you to reverse this process.\n");
	mfs_reinit (mfs, O_RDWR);
	mfs_write_inode_data_part (mfs, inode, (unsigned char *) buf, 0, size);
	mfs_reinit (mfs, O_RDONLY);

	free(buf);
}

int
supersize_main (int argc, char **argv)
{
	int opt;
	char *tmp;

	tivo_partition_direct ();

	while ((opt = getopt (argc, argv, "hm:c:u:")) > 0)
	{
		switch (opt)
		{
		case 'm':
			max = strtol (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -m.\n", argv[0]);
				return 1;
			}
			break;
		case 'u':
			user = strtol (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -u.\n", argv[0]);
				return 1;
			}
			break;
		case 'c':
			clip = strtol (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -c.\n", argv[0]);
				return 1;
			}
			break;
		default:
			supersize_usage (argv[0]);
			return 1;
		}
	}

	if (optind == argc || argc > optind + 2)
	{
		supersize_usage (argv[0]);
		return 4;
	}

	mfs = mfs_init (argv[optind], optind + 1 < argc? argv[optind + 1] : NULL, (O_RDONLY | MFS_ERROROK));

	if (!mfs)
	{
		fprintf (stderr, "mfs_init: Failed.  Bailing.\n");
		return 1;
	}

	if (mfs_has_error (mfs))
	{
		mfs_perror (mfs, argv[0]);
		return 1;
	}

	supersize();
	return 0;
}
