#include <unistd.h>
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
backup_main (int argc, char **argv)
{
	struct backup_info *info;
	int loop;
	char *drive = argc < 2? "/dev/hda": argv[1];
	char *drive2 = argc < 3? 0: argv[2];

	fprintf (stderr, "Backing up %s and %s\n", drive, drive2);
	info = init_backup (drive, drive2, 48 * 1024 * 2, BF_COMPRESSED | BF_SHRINK | BF_BACKUPVAR);
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

	fprintf (stderr, "\n");
	if (backup_finish (info) < 0)
	{
		fprintf (stderr, "Backup failed!\n");
		exit (1);
	}

	exit (0);
}

int
restore_main (int argc, char **argv)
{
	char *drive = argc < 2? "/dev/hda": argv[1];
	char *drive2 = argc < 3? 0: argv[2];
	struct backup_info *info;
	int loop;

	info = init_restore ();
	if (info)
	{
		int secleft = 0;
		char buf[BUFSIZE];
		unsigned int cursec = 0, curcount;
		int nread;
		int nwrit;

		nread = read (0, buf, BUFSIZE);
		if (nread <= 0)
			return 1;

		nwrit = restore_write (info, buf, nread);
		if (nwrit < 0)
			return 1;

		if (restore_trydev (info, drive, drive2) < 0)
			return 1;

		if (restore_start (info) < 0)
			return 1;

		if (restore_write (info, buf + nwrit, nread - nwrit) != nread - nwrit)
		{
			return 1;
		}

		fprintf (stderr, "Restore of %d megs\n", info->nsectors / 2048);
		while ((curcount = read (0, buf, BUFSIZE)) > 0)
		{
			unsigned int prcnt, compr;
			restore_write (info, buf, curcount);
			cursec += curcount / 512;
			prcnt = get_percent (info->cursector, info->nsectors);
			compr = get_percent (info->cursector - cursec, info->cursector);
			fprintf (stderr, "Restoring %d of %d megs (%d.%02d%%) (%d.%02d%% compression)    \r", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100, compr / 100, compr % 100);
		}

	}

	fprintf (stderr, "\n");
	if (restore_finish (info) < 0)
	{
		fprintf (stderr, "Restore failed!\n");
		exit (1);
	}

	exit (0);
}

int
main (int argc, char **argv)
{
	int c;

	tivo_partition_direct ();

	switch (argv[1][0])
	{
	case 'b':
	case 'B':
		return backup_main (argc - 1, argv + 1);
	case 'r':
	case 'R':
		return restore_main (argc - 1, argv + 1);
	}
}
