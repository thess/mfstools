#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_ASM_TYPES_H
#include <asm/types.h>
#endif
#include <sys/param.h>

#include "mfs.h"
#include "backup.h"
#include "macpart.h"

#define BUFSIZE 512 * 256

void
restore_usage (char *progname)
{
	fprintf (stderr, "Usage: %s [options] Adrive [Bdrive]\n", progname);
	fprintf (stderr, "Options:\n");
	fprintf (stderr, " -h        Display this help message\n");
	fprintf (stderr, " -i file   Input from file, - for stdin\n");
	fprintf (stderr, " -p        Optimize partition layout\n");
	fprintf (stderr, " -x        Expand the backup to fill the drive(s)\n");
	fprintf (stderr, " -r scale  Expand the backup with block size scale\n");
	fprintf (stderr, " -q        Do not display progress\n");
	fprintf (stderr, " -qq       Do not display anything but error messages\n");
	fprintf (stderr, " -v size   Recreate /var as size megabytes (Only if not in backup)\n");
	fprintf (stderr, " -s size   Recreate swap as size megabytes\n");
	fprintf (stderr, " -l        Leave at least 2 partitions free\n");
	fprintf (stderr, " -b        Force no byte swapping on restore\n");
	fprintf (stderr, " -B        Force byte swapping on restore\n");
	fprintf (stderr, " -z        Zero out partitions not backed up\n");
	fprintf (stderr, " -M 32/64  Write MFS structures as 32 or 64 bit\n");
}

static unsigned int
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
expand_drive (struct mfs_handle *mfshnd, char *tivodev, char *realdev, unsigned int blocksize)
{
	unsigned int maxfree = tivo_partition_largest_free (realdev);
	unsigned int totalfree = tivo_partition_total_free (realdev);
	unsigned int used = maxfree & ~(blocksize - 1);
	unsigned int required = mfs_volume_pair_app_size (used, blocksize);
	unsigned int part1, part2;
	char app[MAXPATHLEN];
	char media[MAXPATHLEN];

	unsigned int newsize, oldsize;

	oldsize = mfs_sa_hours_estimate (mfshnd);

	if (totalfree - maxfree < required && maxfree - used < required)
	{
		used = (maxfree - required) & ~(blocksize - 1);
		required = mfs_volume_pair_app_size (used, blocksize);
	}

	if (totalfree - maxfree >= required && maxfree - used < required)
	{
		part2 = tivo_partition_add (realdev, used, 0, "New MFS Media", "MFS");
		part1 = tivo_partition_add (realdev, required, part2, "New MFS Application", "MFS");

		part2++;
	}
	else
	{
		part1 = tivo_partition_add (realdev, required, 0, "New MFS Application", "MFS");
		part2 = tivo_partition_add (realdev, used, 0, "New MFS Media", "MFS");
	}

	if (part1 < 2 || part2 < 2 || part1 > 16 || part2 > 16)
		return -1;

	sprintf (app, "%s%d", tivodev, part1);
	sprintf (media, "%s%d", tivodev, part2);

#if TARGET_OS_MAC
	fprintf (stderr, "Adding pair %ss%d-%ss%d\n", realdev, part1, realdev, part2);
#else
	fprintf (stderr, "Adding pair %s%d-%s%d\n", realdev, part1, realdev, part2);
#endif

	if (mfs_can_add_volume_pair (mfshnd, app, media, blocksize) < 0)
		return -1;

	if (tivo_partition_table_write (realdev) < 0)
		return -1;

	if (mfs_add_volume_pair (mfshnd, app, media, blocksize) < 0)
		return -1;

	newsize = mfs_sa_hours_estimate (mfshnd);
	fprintf (stderr, "New estimated standalone size: %d hours (%d more)\n", newsize, newsize - oldsize);

	return 0;
}

int
restore_main (int argc, char **argv)
{
	char *drive, *drive2, *tmp;
	struct backup_info *info;
	int opt;
	unsigned int varsize = 0, swapsize = 0, flags = 0;
	char *filename = 0;
	int quiet = 0;
	int bswap = 0;
	int expand = 0;
	int expandscale = 2;
	int restorebits = 0;

	tivo_partition_direct ();

	while ((opt = getopt (argc, argv, "hi:v:s:zqbBpxlr:M:")) > 0)
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
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -s.\n", argv[0]);
				return 1;
			}
			break;
		case 'z':
			flags |= RF_ZEROPART;
			break;
		case 'b':
			if (bswap != 0)
			{
				fprintf (stderr, "%s: Only one byte swapping option (-b/-B) allowed.\n", argv[0]);
				return 1;
			}
			bswap = -1;
			break;
		case 'B':
			if (bswap != 0)
			{
				fprintf (stderr, "%s: Only one byte swapping option (-b/-B) allowed.\n", argv[0]);
				return 1;
			}
			bswap = 1;
			break;
		case 'p':
			flags |= RF_BALANCE;
			break;
		case 'l':
			flags |= RF_NOFILL;
			break;
		case 'x':
			expand = 1;
			break;
		case 'r':
			expandscale = strtoul (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -r.\n", argv[0]);
				return 1;
			}
			if (expandscale < 0 || expandscale > 4)
			{
				fprintf (stderr, "%s: Scale value for -r must be in the range 0 to 4.\n", argv[0]);
				return 1;
			}
			break;
		case 'M':
			restorebits = strtoul (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -S.\n", argv[0]);
				return 1;
			}
			if (restorebits != 32 && restorebits != 64)
			{
				fprintf (stderr, "%s: Value for -S must be 32 or 64\n", argv[0]);
				return 1;
			}
			break;
		default:
			restore_usage (argv[0]);
			return 1;
		}
	}

	if (!filename)
	{
		restore_usage (argv[0]);
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
		restore_usage (argv[0]);
		return 1;
	}

	if (swapsize > 128)
	{
		flags |= RF_SWAPV1;
	}

	if (expand > 0)
		flags |= RF_NOFILL;

	info = init_restore (flags);
	if (restore_has_error (info))
	{
		restore_perror (info, "Restore");
		return 1;
	}

	if (info)
	{
		unsigned starttime;
		int fd, nread, nwrit;
		char buf[BUFSIZE];
		unsigned int cursec = 0, curcount;

		if (varsize)
			restore_set_varsize (info, varsize);
		if (swapsize)
			restore_set_swapsize (info, swapsize * 1024 * 2);
		if (bswap)
			restore_set_bswap (info, bswap);
		if (restorebits)
			restore_set_mfs_type (info, restorebits);

		if (filename[0] == '-' && filename[1] == '\0')
			fd = 0;
		else
		{
#if O_LARGEFILE
			fd = open (filename, O_RDONLY | O_LARGEFILE);
#else
			fd = open (filename, O_RDONLY);
#endif
		}

		if (fd < 0)
		{
			perror (filename);
			return 1;
		}

		nread = read (fd, buf, BUFSIZE);
		if (nread <= 0)
		{
			fprintf (stderr, "Restore failed: %s: %s\n", filename, strerror(errno));
			return 1;
		}

		nwrit = restore_write (info, buf, nread);
		if (nwrit < 0)
		{
			if (restore_has_error (info))
				restore_perror (info, "Restore");
			else
				fprintf (stderr, "Restore failed.\n");
			return 1;
		}

		if (swapsize > 128 && !(info->back_flags & BF_NOBSWAP))
			fprintf (stderr, "    ***WARNING***\nUsing version 1 swap signature to get >128MiB swap size, but the backup looks\nlike a series 1.  Stock SERIES 1 TiVo kernels do not support the version 1\nswap signature.  If you are using a stock SERIES 1 TiVo kernel, 128MiB is the\nlargest usable swap size.\n");
		if (restorebits == 64 && !(info->back_flags & BF_64))
			fprintf (stderr, "    ***WARNING***\nConverting MFS structure to 64 bit if very experimental, and will only work on\nSeries 3 based TiVo platforms or later, such as the TiVo HD.\n");

		if (info->back_flags & BF_TRUNCATED)
		{
			fprintf (stderr, "    ***WARNING***\nRestoring from a backup of an incomplete volume.  While the backup is whole,\nit is possible there was some required data missing.  Verify the restore.\n");
		}

		if (restore_trydev (info, drive, drive2) < 0)
		{
			if (restore_has_error (info))
				restore_perror (info, "Restore");
			else
				fprintf (stderr, "Restore failed.\n");
			return 1;
		}

		if (restore_start (info) < 0)
		{
			if (restore_has_error (info))
				restore_perror (info, "Restore");
			else
				fprintf (stderr, "Restore failed.\n");
			return 1;
		}

		if (restore_write (info, buf + nwrit, nread - nwrit) != nread - nwrit)
		{
			if (restore_has_error (info))
				restore_perror (info, "Restore");
			else
				fprintf (stderr, "Restore failed.\n");
			return 1;
		}

		starttime = time (NULL);

		fprintf (stderr, "Starting restore\nUncompressed backup size: %d megabytes\n", info->nsectors / 2048);
		while ((curcount = read (fd, buf, BUFSIZE)) > 0)
		{
			unsigned int prcnt, compr;
			if (restore_write (info, buf, curcount) != curcount)
			{
				if (quiet < 1)
					fprintf (stderr, "\n");
				if (restore_has_error (info))
					restore_perror (info, "Restore");
				else
					fprintf (stderr, "Restore failed.\n");
				return 1;
			}
			cursec += curcount / 512;
			prcnt = get_percent (info->cursector, info->nsectors);
			compr = get_percent (info->cursector - cursec, info->cursector);
			if (quiet < 1)
			{
				unsigned timedelta = time(NULL) - starttime;

				if (info->back_flags & BF_COMPRESSED)
					fprintf (stderr, "     \rRestoring %d of %d mb (%d.%02d%%) (%d.%02d%% comp)", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100, compr / 100, compr % 100);
				else
					fprintf (stderr, "     \rRestoring %d of %d mb (%d.%02d%%)", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100);

				if (prcnt > 100 && timedelta > 15)
				{
					unsigned ETA = timedelta * (10000 - prcnt) / prcnt;
					fprintf (stderr, " %d mb/sec (ETA %d:%02d:%02d)", info->cursector / timedelta / 2048, ETA / 3600, ETA / 60 % 60, ETA % 60);
				}
			}
		}

		if (quiet < 1)
			fprintf (stderr, "\n");

		if (restore_has_error (info))
		{
			restore_perror (info, "Restore");
			return 1;
		}
	}
	else
	{
		fprintf (stderr, "Restore failed.");
		return 1;
	}

	if (quiet < 2)
		fprintf (stderr, "Cleaning up restore.  Please wait a moment.\n");

	if (restore_finish (info) < 0)
	{
		if (restore_has_error (info))
			restore_perror (info, "Restore");
		else
			fprintf (stderr, "Restore failed.\n");
		return 1;
	}

	if (quiet < 2)
		fprintf (stderr, "Restore done!\n");

	if (expand > 0)
	{
		int blocksize = 0x800;
		struct mfs_handle *mfshnd;

		expand = 0;

		mfshnd = mfs_init (drive, drive2, O_RDWR);
		if (!mfshnd)
		{
			fprintf (stderr, "Drive expansion failed.\n");
			return 1;
		}

		if (mfs_has_error (mfshnd))
		{
			mfs_perror (mfshnd, "Drive expansion");
			return 1;
		}

		while (expandscale-- > 0)
			blocksize *= 2;

		if (tivo_partition_largest_free (drive) > 1024 * 1024 * 2)
		{
			if (expand_drive (mfshnd, "/dev/hda", drive, blocksize) < 0)
			{
				if (restore_has_error (info))
					restore_perror (info, "Expand drive A");
				else
					fprintf (stderr, "Drive A expansion failed.\n");
				return 1;
			}
			expand++;
		}

		if (drive2 && tivo_partition_largest_free (drive2) > 1024 * 1024 * 2)
		{
			if (expand_drive (mfshnd, "/dev/hdb", drive2, blocksize) < 0)
			{
				if (restore_has_error (info))
					restore_perror (info, "Expand drive B");
				else
					fprintf (stderr, "Drive B expansion failed.\n");
				return 1;
			}
			expand++;
		}

		if (!expand)
		{
			fprintf (stderr, "Not enough extra space to expand on A drive%s.\n", drive2? " or B drive": "");
		}
	}

	return 0;
}
