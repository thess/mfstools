#include <stdio.h>
#include <fcntl.h>
#include "mfs.h"
#include "backup.h"

#define BUFSIZE 512 * 2048
//#define BUFSIZE 512

unsigned int
get_percent (unsigned int current, unsigned int max)
{
	unsigned int prcnt;
	if (max <= 0x7fffffff / 10000)
	{
		prcnt = current * 10000 / max;
	}
	else if (max <= 0x7fffffff / 100)
	{
		prcnt = current * 100 / (max / 100);
	}
	else
	{
		prcnt = current / (max / 10000);
	}

	return prcnt;
}

int
main (int argc, char **argv)
{
	struct backup_info *info;
	int loop;

	if (mfs_init (O_RDONLY) < 0)
	{
		fprintf (stderr, "mfsinit: Failed!  Bailing.\n");
		exit (1);
	}

	info = init_backup ("/dev/hda", NULL, 48 * 1024 * 2, BF_COMPRESSED);
	//info = init_backup (-1);

	if (info)
	{
		int secleft = 0;
		char buf[BUFSIZE];
		unsigned int cursec = 0, curcount;

		fprintf (stderr, "Backup of %d megs\n", info->nsectors / 2048);
		while ((curcount = backup_read (info, buf, BUFSIZE)) > 0)
		{
			unsigned int prcnt, compr;
			write (1, buf, curcount);
			cursec += curcount / 512;
			prcnt = get_percent (info->cursector, info->nsectors);
			compr = get_percent (info->cursector - cursec, info->cursector);
			fprintf (stderr, "Backing up %d of %d megs (%d.%02d%%) (%d.%02d%% compression)    \r", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100, compr / 100, compr % 100);
		}

	}

	exit (0);
}
