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

char *progname;

static struct mfs_handle *mfs;

static int long_list;

void
mls_usage ()
{
	fprintf (stderr, "%s %s\n", PACKAGE, VERSION);
	fprintf (stderr, "Usage: %s [Adrive [Bdrive]] [options] </path|fsid>", progname);
	fprintf (stderr, "Options:\n");
	fprintf (stderr, " -h        Display this help message\n");
	fprintf (stderr, " -l        long list (with size)\n");
	fprintf (stderr, " -R        recurse\n");
}

static void dir_list(int fsid, int recurse)
{
	mfs_dirent *dir;
	uint32_t count, i;
	dir = mfs_dir(mfs, fsid, &count);
	if (fsid == 0)
		{
		fprintf (stderr, "No such file or directory!\n");
		exit (1);
		}
	if (!dir)
	{
		fprintf (stderr, "No such directory!\n");
		exit (1);
	}

	if (long_list) {
		printf("     FsId Type         Date  Time      Size Name\n");
		printf("     ---- ----         ----  ----      ---- ----\n");
	} else {
		printf("      FsId   Type     Name\n");
		printf("      ----   ----     ----\n");
}
	for (i=0;i<count;i++) {
	char date[17] = "xx/xx/xx xx:xx";
	time_t modtime;
		if (long_list) {
			mfs_inode *inode;
			uint64_t size = 0;
			inode = mfs_read_inode_by_fsid (mfs, dir[i].fsid); 
			if (inode)
	{
				modtime = intswap32 (inode->lastmodified);
		strftime (date, 16, "%D %R", localtime (&modtime));
				if (intswap32 (inode->unk3) == 0x20000) 
					size = intswap32 (inode->size) * intswap32 (inode->unk3);
	else
					size = intswap32 (inode->size);
	}
			printf("%9d %-8s %14s%10ld %s\n", 
						dir[i].fsid, 
						mfs_type_string(dir[i].type),
						date,
						size,
						dir[i].name);
			if (inode)
				free (inode);
		} else {
			printf("   %7d   %-8s %s\n", 
						dir[i].fsid, 
						mfs_type_string(dir[i].type),
						dir[i].name);
}
	}

	if (recurse) {
		for (i=0;i<count;i++) {
			if (dir[i].type == tyDir) {
				printf("\n%s[%d]:\n", 
							dir[i].name, dir[i].fsid);
				dir_list(dir[i].fsid, 1);
	}
		}
		}

	if (dir) mfs_dir_free(dir);
		}


int
mls_main (int argc, char **argv)
		{
	int opt = 0;
	int fsid;
	int recurse=0;
	char *arg = argv[1];
	char *hda = NULL, *hdb = NULL;

	progname = argv[0];

	tivo_partition_direct ();
	
	while ((opt = getopt (argc, argv, "hRl")) > 0)
		{
		switch (opt)
		{
		case 'R':
			recurse=1;
			break;
		case 'l':
			long_list=1;
			break;
		default:
			mls_usage ();
			return 1;
	}
}

	argc -= optind-1;
	argv += optind-1;

	if (argc > 2)
	{
		hda = argv[1];
		arg = argv[2];

		if (argc > 3)
		{
			hdb = argv[2];
			arg = argv[3];

			if (argc > 4)
			{
				mls_usage ();
				return 1;
			}
		}
	}
	else
	{
		hda = getenv ("MFS_HDA");
		hdb = getenv ("MFS_HDB");
		if (!hda || !*hda)
		{
			hda = "/dev/hda";
			hdb = "/dev/hdb";
		}
	}

	mfs = mfs_init (hda, hdb, (O_RDONLY | MFS_ERROROK));
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

	fsid = mfs_resolve(mfs, arg);
	dir_list(fsid, recurse);

	return 0;
}
