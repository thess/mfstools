#include <stdio.h>
#include <fcntl.h>
#include "mfs.h"

void
mfsinfo_usage (char *progname)
{
#if TARGET_OS_MAC
	fprintf (stderr, "Usage:\n%s /dev/diskX [/dev/diskY]\n", progname);
#else
	fprintf (stderr, "Usage:\n%s /dev/hdX [/dev/hdY]\n", progname);
#endif
}

int
partition_info (struct mfs_handle *mfs, char *drives[])
{
	int count = 0;
	int loop;
	unsigned int offset;
	char *names[12];
	int namelens[12];
	char *list = mfs_partition_list (mfs);

	while (*list && count < 12)
	{
		offset = 0;

		while (*list && isspace (*list))
			list++;

		if (!*list)
			break;

		while (list[offset] && !isspace (list[offset]))
			offset++;

		names[count] = list;
		namelens[count] = offset;
		count++;
		list += offset;
	}

	fprintf (stderr, "The MFS volume set contains %d partitions\n", count);
	offset = 0;
	for (loop = 0; loop < count; loop++)
	{
		int d;
		int p;
		unsigned int size;

		p = namelens[loop] - 1;

		while (isdigit (names[loop][p]) && p > 0)
			p--;

		p++;

		if (names[loop][p])
		{
			d = names[loop][p - 1] - 'a';
			p = atoi (names[loop] + p);

			if (d == 0 || d == 1)
#if TARGET_OS_MAC
				fprintf (stderr, "  %ss%d\n", drives[d], p);
#else
				fprintf (stderr, "  %s%d\n", drives[d], p);
#endif
			else
				fprintf (stderr, "  %.*s\n", namelens[loop], names[loop]);
		}
		else
			fprintf (stderr, "  %.*s\n", namelens[loop], names[loop]);

		size = mfs_volume_size (mfs, offset);
		fprintf (stderr, "    MFS Partition Size: %uMiB\n", size / (1024 * 2));
		offset += size;
	}

	fprintf (stderr, "Total MFS volume size: %uMiB\n", offset / (1024 * 2));

	return count;
}

int
mfsinfo_main (int argc, char **argv)
{
	int ndrives = 0;
	char *drives[3];
	struct mfs_handle *mfs;
	int nparts = 0;

	while (ndrives + 1 < argc && ndrives < 3)
	{
		drives[ndrives] = argv[ndrives + 1];
		ndrives++;
	}

	if (ndrives == 0 || ndrives > 2)
	{
		mfsinfo_usage (argv[0]);
		return 1;
	}

	mfs = mfs_init (drives[0], drives[1], O_RDONLY);
	if (!mfs)
	{
		fprintf (stderr, "Could not open MFS volume set.\n");
		return 1;
	}

	fprintf (stderr, "MFS volume set for %s%s%s\n", drives[0], ndrives > 1? " and ": "", ndrives > 1? drives[1]: "");

	nparts = partition_info (mfs, drives);

	fprintf (stderr, "Estimated hours in a standalone TiVo: %d\n", mfs_sa_hours_estimate (mfs));
	fprintf (stderr, "This MFS volume may be expanded %d more time%s\n", (12 - nparts) / 2, nparts == 10? "": "s");

	return 0;
}
