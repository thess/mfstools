#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "mfs.h"
#include "backup.h"

#define BUFSIZE 512 * 2048
//#define BUFSIZE 512

#define backup_usage()
#define mfsadd_usage()
#define restore_usage()

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
	int loop, thresh = 0;
	unsigned int flags = 0;
	char threshopt = '\0';
	char *drive, *drive2;
	char *filename;
	char *tmp;
	int quiet = 0;
	int compressed = 0;

	while ((loop = getopt (argc, argv, "o:123456789vsf:l:tTaq")) > 0)
	{
		switch (loop)
		{
		case 'o':
			filename = optarg;
			break;
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			flags |= BF_SETCOMP (loop - '0');
			compressed = 0;
			break;
		case 'v':
			flags |= BF_BACKUPVAR;
			break;
		case 's':
			flags |= BF_SHRINK;
			break;
		case 'f':
			if (threshopt)
			{
				fprintf (stderr, "%s: -f and -%c cannot be used together\n", argv[0], threshopt);
				return 1;
			}
			threshopt = loop;
			thresh = strtoul (optarg, &tmp, 10);
			if (*tmp)
			{
				fprintf (stderr, "%s: Non integer argument to -f\n", argv[0]);
				return 1;
			}
			break;
		case 'l':
			if (threshopt)
			{
				fprintf (stderr, "%s: -l and -%c cannot be used together\n", argv[0], threshopt);
				return 1;
			}
			threshopt = loop;
			thresh = strtoul (optarg, &tmp, 10);
			thresh *= 1024 * 2;
			flags |= BF_THRESHSIZE;
			if (*tmp)
			{
				fprintf (stderr, "%s: Non integer argument to -l\n", argv[0]);
				return 1;
			}
			break;
		case 't':
			flags |= BF_THRESHTOT;
			break;
		case 'T':
			flags += BF_STREAMTOT;
			break;
		case 'a':
			if (threshopt)
			{
				fprintf (stderr, "%s: -a and -%c cannot be used together\n", argv[0], threshopt);
				return 1;
			}
			threshopt = loop;
			thresh = ~0;
			break;
		case 'q':
			quiet++;
			break;
		default:
			backup_usage ();
		}
	}

	if (!filename)
	{
		fprintf (stderr, "%s: No filename given.\n", argv[0]);
		return 1;
	}

	drive = 0;
	drive2 = 0;
	if (optind < argc)
		drive = argv[optind++];
	if (optind < argc)
		drive2 = argv[optind++];
	if (optind < argc || !drive)
	{
		fprintf (stderr, "%s: No devices to backup!\n", argv[0]);
		return 1;
	}

	info = init_backup (drive, drive2, flags);

	if (info)
	{
		int secleft = 0;
		char buf[BUFSIZE];
		unsigned int cursec = 0, curcount;
		int fd;

		if (filename[0] == '-' && filename[1] == '\0')
			fd = 1;
		else
			fd = open (filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);

		if (fd < 0)
		{
			perror (filename);
			return 1;
		}

		if (threshopt)
			backup_set_thresh (info, thresh);

		if (quiet < 2)
			fprintf (stderr, "Scanning source drive.  Please wait a moment.\n");

		if (backup_start (info) < 0)
		{
			if (last_err (info))
				fprintf (stderr, "Backup failed: %s\n", last_err (info));
			else
				fprintf (stderr, "Backup failed.\n");
			return 1;
		}

		if (quiet < 2)
			fprintf (stderr, "Uncompressed backup size: %d megabytes\n", info->nsectors / 2048);
		while ((curcount = backup_read (info, buf, BUFSIZE)) > 0)
		{
			unsigned int prcnt, compr;
			if (write (fd, buf, curcount) != curcount)
			{
				printf ("Backup failed: %s: %s\n", filename, sys_errlist[errno]);
				return 1;
			}
			cursec += curcount / 512;
			prcnt = get_percent (info->cursector, info->nsectors);
			compr = get_percent (info->cursector - cursec, info->cursector);
			if (quiet < 1)
			{
				if (compressed)
					fprintf (stderr, "Backing up %d of %d megabytes (%d.%02d%%) (%d.%02d%% compression)    \r", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100, compr / 100, compr % 100);
				else
					fprintf (stderr, "Backing up %d of %d megabytes (%d.%02d%%)     \r", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100);
			}
		}

		if (curcount < 0)
		{
			if (last_err (info))
				fprintf (stderr, "Backup failed: %s\n", last_err (info));
			else
				fprintf (stderr, "Backup failed.");
			return 1;
		}
	}
	else
	{
		fprintf (stderr, "%s: Backup failed.\n", argv[0]);
		return 1;
	}

	if (quiet < 1)
		fprintf (stderr, "\n");
	if (backup_finish (info) < 0)
	{
		if (last_err (info))
			fprintf (stderr, "Backup failed: %s\n", last_err (info));
		else
			fprintf (stderr, "Backup failed!\n");
		return 1;
	}

	if (quiet < 2)
		fprintf (stderr, "Backup done!\n");

	return 0;
}

int
restore_main (int argc, char **argv)
{
	char *drive, *drive2, *tmp;
	struct backup_info *info;
	int loop;
	int opt;
	unsigned int varsize = 0, swapsize = 0, flags = 0;
	char *filename = 0;
	int quiet = 0;

	while ((opt = getopt (argc, argv, "i:v:s:zq")) > 0)
	{
		switch (opt)
		{
		case 'q':
			quiet++;
			break;
		case 'i':
			filename = optarg;
			break;
		case 'v':
			varsize = strtoul (optarg, &tmp, 10);
			varsize *= 1024 * 2;
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -v.\n", argv[0]);
				return 1;
			}
			break;
		case 's':
			swapsize = strtoul (optarg, &tmp, 10);
			swapsize *= 1024 * 2;
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -s.\n", argv[0]);
				return 1;
			}
			break;
		case 'z':
			flags |= RF_ZEROPART;
			break;
		default:
			restore_usage ();
			return 1;
		}
	}

	if (!filename)
	{
		fprintf (stderr, "%s: Backup file name expected.\n", argv[0]);
		return 1;
	}

	drive = 0;
	drive2 = 0;

	if (optind < argc)
		drive = argv[optind++];
	if (optind < argc)
		drive2 = argv[optind++];

	if (optind < argc || !drive)
	{
		fprintf (stderr, "%s: Device name expected.\n", argv[0]);
		return 1;
	}

	info = init_restore (flags);
	if (info)
	{
		int fd, nread, nwrit, secleft = 0;
		char buf[BUFSIZE];
		unsigned int cursec = 0, curcount;

		if (varsize)
			restore_set_varsize (info, varsize);
		if (swapsize)
			restore_set_swapsize (info, swapsize);

		if (filename[0] == '-' && filename[1] == '\0')
			fd = 0;
		else
			fd = open (filename, O_RDONLY);

		if (fd < 0)
		{
			perror (filename);
			return 1;
		}

		nread = read (0, buf, BUFSIZE);
		if (nread <= 0)
		{
			fprintf (stderr, "Restore failed: %s: %s\n", filename, sys_errlist[errno]);
			return 1;
		}

		nwrit = restore_write (info, buf, nread);
		if (nwrit < 0)
		{
			if (last_err (info))
				fprintf (stderr, "Restore failed: %s\n", last_err (info));
			else
				fprintf (stderr, "Restore failed.\n", last_err (info));
			return 1;
		}

		if (restore_trydev (info, drive, drive2) < 0)
		{
			if (last_err (info))
				fprintf (stderr, "Restore failed: %s\n", last_err (info));
			else
				fprintf (stderr, "Restore failed.\n", last_err (info));
			return 1;
		}

		if (restore_start (info) < 0)
		{
			if (last_err (info))
				fprintf (stderr, "Restore failed: %s\n", last_err (info));
			else
				fprintf (stderr, "Restore failed.\n", last_err (info));
			return 1;
		}

		if (restore_write (info, buf + nwrit, nread - nwrit) != nread - nwrit)
		{
			return 1;
			if (last_err (info))
				fprintf (stderr, "Restore failed: %s\n", last_err (info));
			else
				fprintf (stderr, "Restore failed.\n", last_err (info));
		}

		fprintf (stderr, "Starting restore\nUncompressed backup size: %d megabytes\n", info->nsectors / 2048);
		while ((curcount = read (0, buf, BUFSIZE)) > 0)
		{
			unsigned int prcnt, compr;
			if (restore_write (info, buf, curcount) != curcount)
			{
				if (last_err (info))
					fprintf (stderr, "Restore failed: %s\n", last_err (info));
				else
					fprintf (stderr, "Restore failed.\n");
			}
			cursec += curcount / 512;
			prcnt = get_percent (info->cursector, info->nsectors);
			compr = get_percent (info->cursector - cursec, info->cursector);
			if (quiet < 1)
			{
				if (info->back_flags & BF_COMPRESSED)
					fprintf (stderr, "Restoring %d of %d megabytes (%d.%02d%%) (%d.%02d%% compression)    \r", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100, compr / 100, compr % 100);
				else
					fprintf (stderr, "Restoring %d of %d megabytes (%d.%02d%%)    \r", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100);
			}
		}

		if (curcount < 0)
		{
			fprintf (stderr, "Restore failed: %s: %s\n", filename, sys_errlist[errno]);
			return 1;
		}
	}
	else
	{
		fprintf (stderr, "Restore failed.");
		return 1;
	}

	if (quiet < 1)
		fprintf (stderr, "\n");

	if (quiet < 2)
		fprintf (stderr, "Cleaning up restore.  Please wait a moment.\n");

	if (restore_finish (info) < 0)
	{
		if (last_err (info))
			fprintf (stderr, "Restore failed: %s\n", last_err (info));
		else
			fprintf (stderr, "Restore failed.\n");
		return 1;
	}

	if (quiet < 2)
		fprintf (stderr, "Restore done!\n");

	return 0;
}

int
mfsadd_main (int argc, char **argv)
{
	unsigned int minalloc = 0x800;
	int opt;
	int extendall = 0;
	char *xdevs[2];
	int npairs = 0;
	char *pairs[32];
	char pairnums[32];
	int loop, loop2;
	char *drives[2] = {0, 0};

	while (opt == getopt (argc, argv, "x:p:"))
	{
		switch (opt)
		{
		case 'x':
			if (extendall < 2)
			{
				xdevs[extendall] = optarg;
				extendall++;
				break;
			}

			fprintf (stderr, "%s: Can only extend MFS to 2 devices.\n", argv[0]);
			return 1;
		case 'p':
			if (npairs < 31)
			{
				pairs[npairs] = optarg;
				npairs++;
				optind++;
				if (optind < argc)
				{
					pairs[npairs] = argv[optind];
					npairs++;
					break;
				}

				fprintf (stderr, "%s: -p option requires 2 partitions.\n", argv[0]);
				return 1;
			}

			fprintf (stderr, "%s: Too many pairs being added.\n", argv[0]);
			return 1;
		default:
			mfsadd_usage ();
			return 1;
		}
	}

	if (optind < argc)
	{
		drives[0] = argv[optind];
		optind++;
	}

	if (optind < argc)
	{
		drives[1] = argv[optind];
		optind++;
	}

	if (optind < argc || !drives[0])
	{
		mfsadd_usage ();
		return 1;
	}

	for (loop = 0; loop < extendall; loop++)
	{
		if (!strcmp (xdevs[loop], drives[0]))
			xdevs[loop] = drives[0];
		else if (!drives[1] || !strcmp (xdevs[loop], drives[1]))
			xdevs[loop] = drives[1];
		else
		{
			fprintf (stderr, "%s: Arguments to -x must be one of %s or %s.\n", argv[0], drives[0], drives[1]);
			return 1;
		}
	}

	if (extendall == 2 && xdevs[0] == xdevs[1])
	{
		fprintf (stderr, "%s: -x argument only makes sense once for each device.\n", argv[0]);
		return 1;
	}

	for (loop = 0; loop < npairs; loop++)
	{
		int tmp;
		char *str;

		if (!strncmp (pairs[loop], drives[0], strlen (drives[0])))
			pairnums[loop] = 0x00;
		else if (!drives[1] || !strncmp (pairs[loop], drives[1], strlen (drives[1])))
			pairnums[loop] = 0x40;
		else
		{
			fprintf (stderr, "%s: -p arguments must be partitions of either %s or %s\n", argv[0], drives[0], drives[1]);
			return 1;
		}

		if (!drives[pairnums[loop] >> 6])
		{
			char *num;
			char *new;

/* Known memory leak.  Deal. */
			new = strdup (drives[pairnums[loop] >> 6]);

			if (!new)
			{
				fprintf (stderr, "%s: Memory allocation error.\n", argv[0]);
				return 1;
			}

			for (num = pairs[loop] + strlen (pairs[loop]) - 1; num > pairs[loop] && isdigit (*num); num--)
				;
			num++;
			*num = 0;

			drives[pairnums[loop] >> 6] = new;
			drives[pairnums[loop] >> 6][num - pairs[loop] + 1] = 0;
		}

		tmp = strtoul (pairs[loop], &str, 10);
		if (str && *str)
		{
			fprintf (stderr, "%s: -p argument %s is not a partition of %s\n", argv[0], pairs[loop], drives[pairnums[loop] >> 6]);
			return 1;
		}

		if (tmp < 2 || tmp > 16)
		{
			fprintf (stderr, "%s: TiVo only supports partiton numbers 2 through 16\n");
			return 1;
		}

		pairnums[loop] |= tmp;

		//for (loop2 = 0; loop2 < 
	}

	

	return 0;
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
	case 'm':
	case 'M':
		return mfsadd_main (argc - 1, argv + 1);
	}
}
