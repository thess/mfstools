#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

/* For htonl() */
#include <netinet/in.h>

#include "mfs.h"

char *progname;

fs_entry *
find_file_by_name (unsigned char *dir, unsigned int dirlen, unsigned char *name)
{
	int offset = 4;

	while (offset < dirlen)
	{
		fs_entry *cur = (fs_entry *) (dir + offset);

		if (!strcmp (cur->name, name))
		{
			return cur;
		}

		offset += cur->entry_length;
	}

	return 0;
}

void
print_file_details (fs_entry * file)
{
	unsigned char *type = "ty???";
	unsigned char date[17] = "xx/xx/xx xx:xx";
	mfs_inode *nfo;
	time_t modtime;

	nfo = mfs_read_inode_by_fsid (htonl (file->fsid));

	if (nfo)
	{
		modtime = htonl (nfo->lastmodified);
		strftime (date, 16, "%D %R", localtime (&modtime));
	}

	switch (file->type)
	{
	case tyFile:
		type = "tyFile";
		break;
	case tyStream:
		type = "tyStream";
		break;
	case tyDir:
		type = "tyDir";
		break;
	case tyDb:
		type = "tyDb";
		break;
	default:
		type = "ty???";
		break;
	}

	if (file->type == tyStream)
	{
		printf ("    %-26s%-8s%8d%16s%7d x %d(%d)\n", file->name, type, htonl (file->fsid), date, nfo ? htonl (nfo->blocksize) : -1, nfo ? htonl (nfo->size) : -1, nfo ? htonl (nfo->blockused) : -1);
	}
	else
	{
		printf ("    %-26s%-8s%8d%16s%7d\n", file->name, type, htonl (file->fsid), date, nfo ? htonl (nfo->size) : -1);
	}
	if (nfo)
		free (nfo);
}

void
list_file (unsigned char *name)
{
	mfs_inode *cur_nfo = 0;
	unsigned char *cur_dir = 0;
	unsigned int cur_dir_length = 0;
	fs_entry *cur_file = 0;
	int trailingslash = 0;
	if (!name)
	{
		name = "/";
	}

	cur_nfo = mfs_read_inode_by_fsid (1);
	if (!cur_nfo)
	{
		fprintf (stderr, "Couldn't read root directory!\n");
		exit (1);
	}

	while (*name)
	{
		char *next;

		trailingslash = 0;
		while (*name == '/')
			name++, trailingslash++;
		next = strchr (name, '/');
		if (next)
			*next = 0;

		if (!*name)
		{
			break;
		}

		if (cur_dir)
			free (cur_dir);
		cur_dir = mfs_read_inode_data (cur_nfo, &cur_dir_length);
		if (!cur_dir)
		{
			fprintf (stderr, "No such file or directory!\n");
			exit (1);
		}

		cur_file = find_file_by_name (cur_dir, cur_dir_length, name);
		if (!cur_file)
		{
			fprintf (stderr, "No such file or directory!\n");
			exit (1);
		}

		free (cur_nfo);
		cur_nfo = mfs_read_inode_by_fsid (htonl (cur_file->fsid));
		if (!cur_nfo)
		{
			fprintf (stderr, "No such file or directory!\n");
			exit (1);
		}

		if (next)
		{
			*next = '/';
			trailingslash = 1;
			name = next + 1;
		}
		else
		{
			trailingslash = 0;
			break;
		}
	}

	printf ("    Name                      Type        FsId      Date  Time   Size\n");
	printf ("    ----                      ----        ----      ----  ----   ----\n");

	if (!cur_file || (cur_file->type == tyDir && trailingslash))
	{
		int offset = 4;
		free (cur_nfo);
		if (cur_file)
		{
			cur_nfo = mfs_read_inode_by_fsid (htonl (cur_file->fsid));
		}
		else
		{
			cur_nfo = mfs_read_inode_by_fsid (1);
		}
		if (!cur_nfo)
		{
			fprintf (stderr, "No such file or directory!\n");
			exit (1);
		}
		if (cur_dir)
			free (cur_dir);
		cur_dir = mfs_read_inode_data (cur_nfo, &cur_dir_length);
		if (!cur_dir)
		{
			fprintf (stderr, "No such file or directory!\n");
			exit (1);
		}
		for (offset = 4; offset < cur_dir_length; offset += cur_file->entry_length)
		{
			cur_file = (fs_entry *) (cur_dir + offset);
			print_file_details (cur_file);
		}
	}
	else
	{
		print_file_details (cur_file);
	}
}

int
mls_main (int argc, char **argv)
{
	if (mfs_init (O_RDONLY) < 0)
	{
		fprintf (stderr, "mfs_init: Failed.  Bailing.\n");
		return 1;
	}

	list_file (argv[1]);

	return 0;
}
