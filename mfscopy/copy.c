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
copy_usage (char *progname)
{
	fprintf (stderr, "Usage: %s [options] SourceA[:SourceB] DestA[:DestB]\n", progname);
	fprintf (stderr, "General options:\n");
	fprintf (stderr, " -h        Display this help message\n");
	fprintf (stderr, " -q        Do not display progress\n");
	fprintf (stderr, " -qq       Do not display anything but error messages\n");
	fprintf (stderr, "Source options:\n");
	fprintf (stderr, " -f max    Copy only fsids below max\n");
	fprintf (stderr, " -L max    Copy only streams less than max megabytes\n");
	fprintf (stderr, " -t        Use total length of stream in calculations\n");
	fprintf (stderr, " -T        Copy total length of stream instead of used length\n");
	fprintf (stderr, " -a        Copy all streams\n");
	fprintf (stderr, "Target options:\n");
	fprintf (stderr, " -s        Shrink MFS whily copying\n");
	fprintf (stderr, " -p        Optimize partition layout\n");
	fprintf (stderr, " -x        Expand the volume to fill the drive(s)\n");
	fprintf (stderr, " -r scale  Expand the volume with block size scale\n");
	fprintf (stderr, " -v size   Recreate /var as size megabytes and don't copy /var\n");
	fprintf (stderr, " -S size   Recreate swap as size megabytes\n");
	fprintf (stderr, " -l        Leave at least 2 partitions free\n");
	fprintf (stderr, " -b        Force no byte swapping on target\n");
	fprintf (stderr, " -B        Force byte swapping on target\n");
	fprintf (stderr, " -z        Zero out partitions not copied\n");
	fprintf (stderr, " -R        Just copy raw blocks instead of rebuilding data structures\n");
	fprintf (stderr, " -M 32/64  Write MFS structures as 32 or 64 bit\n");
}

static unsigned int
get_percent (uint64_t current, uint64_t max)
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

static uint64_t
sectors_no_reserved (uint64_t sectors)
{
	if (sectors < 14 * 1024 * 1024 * 2)
		return sectors;
	if (sectors > 72 * 1024 * 1024 * 2)
		return sectors - 12 * 1024 * 1024 * 2;
	return sectors - (sectors - 14 * 1024 * 1024 * 2) / 4;
}

static void
display_backup_info (struct backup_info *info)
{
	zone_header *hdr = 0;
	uint64_t sizes[32];
	int count = 0;
	int loop;
	uint64_t backuptot = 0;
	uint64_t backupmfs = 0;

	for (loop = 0; loop < info->nmfs; loop++)
	{
		backupmfs += info->mfsparts[loop].sectors;
	}

	while ((hdr = mfs_next_zone (info->mfs, hdr)) != 0)
	{
		unsigned int zonetype;
		uint64_t zonesize;
		uint64_t zonefirst;

		if (mfs_is_64bit (info->mfs))
		{
			zonetype = intswap32 (hdr->z64.type);
			zonesize = intswap64 (hdr->z64.size);
			zonefirst = intswap64 (hdr->z64.first);
		}
		else
		{
			zonetype = intswap32 (hdr->z32.type);
			zonesize = intswap32 (hdr->z32.size);
			zonefirst = intswap32 (hdr->z32.first);
		}

		if (zonetype == ztMedia)
		{
			unsigned int size = zonesize;
			if (zonefirst < backupmfs)
				backuptot += size;
			sizes[count++] = size;
		}
		else
			while (count > 1)
			{
				sizes[0] += sizes[--count];
			}
	}

	if (sizes > 0)
	{
		unsigned int running = sizes[0];
		fprintf (stderr, "Source drive size is %d hours\n", sectors_no_reserved (running) / SABLOCKSEC);
		if (count > 1)
			for (loop = 1; loop < count; loop++)
			{
				running += sizes[loop];
				fprintf (stderr, "         Upgraded to %d hours\n", sectors_no_reserved (running) / SABLOCKSEC);
			}
		if (info->back_flags & BF_SHRINK)
			fprintf (stderr, "Target drive will be %d hours\n", sectors_no_reserved (backuptot) / SABLOCKSEC);
	}
}

static int
expand_drive (struct mfs_handle *mfshnd, char *tivodev, char *realdev, unsigned int blocksize)
{
	unsigned int maxfree = tivo_partition_largest_free (realdev);
	unsigned int totalfree = tivo_partition_total_free (realdev);
	unsigned int used = maxfree & ~(blocksize - 1);
	unsigned int required = mfs_volume_pair_app_size (mfshnd, used, blocksize);
	unsigned int part1, part2;
	char app[MAXPATHLEN];
	char media[MAXPATHLEN];

	unsigned int newsize, oldsize;

	oldsize = mfs_sa_hours_estimate (mfshnd);

	if (totalfree - maxfree < required && maxfree - used < required)
	{
		used = (maxfree - required) & ~(blocksize - 1);
		required = mfs_volume_pair_app_size (mfshnd, used, blocksize);
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

void
get_drives (char *drives, char *adrive, char *bdrive)
{
	int devlen = strcspn (drives, ":");

	strncpy (adrive, drives, devlen);
	adrive[devlen] = 0;

	if (drives[devlen] != 0)
		strcpy (bdrive, drives + devlen + 1);

	fprintf (stderr, "Drives: %s and %s\n", adrive, bdrive);
}

int
copy_main (int argc, char **argv)
{
	char source_a[PATH_MAX], source_b[PATH_MAX];
	char dest_a[PATH_MAX], dest_b[PATH_MAX];
	char *tmp;
	struct backup_info *info_b, *info_r;
	int opt, loop, thresh = 0, threshopt = 0;
	unsigned int varsize = 0, swapsize = 0;
	unsigned int bflags = BF_BACKUPVAR, rflags = 0;
	int quiet = 0;
	int bswap = 0;
	int expand = 0;
	int expandscale = 2;
	int restorebits = 0;
	int rawcopy = 0;

	tivo_partition_direct ();

	while ((opt = getopt (argc, argv, "hqf:L:tTaspxr:v:S:lbBzEM:R")) > 0)
	{
		switch (opt)
		{
		case 'q':
			quiet++;
			break;
		case 's':
			bflags |= BF_SHRINK;
			break;
		case 'E':
			bflags |= BF_TRUNCATED;
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
		case 'L':
			if (threshopt)
			{
				fprintf (stderr, "%s: -l and -%c cannot be used together\n", argv[0], threshopt);
				return 1;
			}
			threshopt = loop;
			thresh = strtoul (optarg, &tmp, 10);
			thresh *= 1024 * 2;
			bflags |= BF_THRESHSIZE;
			if (*tmp)
			{
				fprintf (stderr, "%s: Non integer argument to -L\n", argv[0]);
				return 1;
			}
			break;
		case 't':
			bflags |= BF_THRESHTOT;
			break;
		case 'T':
			bflags |= BF_STREAMTOT;
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

		case 'v':
			bflags &= ~BF_BACKUPVAR;
			varsize = strtoul (optarg, &tmp, 10);
			varsize *= 1024 * 2;
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -v.\n", argv[0]);
				return 1;
			}
			break;
		case 'S':
			swapsize = strtoul (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -s.\n", argv[0]);
				return 1;
			}
			break;
		case 'z':
			rflags |= RF_ZEROPART;
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
			rflags |= RF_BALANCE;
			break;
		case 'l':
			rflags |= RF_NOFILL;
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
			if (rawcopy)
			{
				fprintf (stderr, "%s: MFS structure type can not be specified with raw block copy\n", argv[0]);
				return 1;
			}
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
		case 'R':
			if (restorebits)
			{
				fprintf (stderr, "%s: MFS structure type can not be specified with raw block copy\n", argv[0]);
				return 1;
			}
			rawcopy = 1;
			break;
		default:
			copy_usage (argv[0]);
			return 1;
		}
	}

	// Split out the drive names
	source_a[0] = 0;
	source_b[0] = 0;
	dest_a[0] = 0;
	dest_b[0] = 0;

	if (argc - optind < 4)
	{
		if (optind < argc)
		{
			get_drives (argv[optind++], source_a, source_b);
		}
		if (optind < argc)
		{
			get_drives (argv[optind++], dest_a, dest_b);
		}
	}
	else
	{
// Special case for convenience - 2 source and 2 target named
		strcpy (source_a, argv[optind++]);
		strcpy (source_b, argv[optind++]);
		strcpy (dest_a, argv[optind++]);
		strcpy (dest_b, argv[optind++]);
	}

	if (optind < argc || !*source_a || !*dest_a)
	{
		copy_usage (argv[0]);
		return 1;
	}

	if (swapsize > 128)
	{
		rflags |= RF_SWAPV1;
	}

	if (expand > 0)
		rflags |= RF_NOFILL;

	if (rawcopy)
	{
		info_b = init_backup_v1 (source_a, source_b, bflags);
	}
	else
	{
		info_b = init_backup_v3 (source_a, source_b, bflags);
	}

	// Try to continue anyway despite error.
	if (bflags & BF_TRUNCATED && backup_has_error (info_b))
	{
		backup_perror (info_b, "WARNING");
		fprintf (stderr, "Attempting copy anyway\n");
		backup_check_truncated_volume (info_b);
		if (info_b && backup_has_error (info_b))
		{
			backup_perror (info_b, "Copy source");
			return 1;
		}
	}

	if (info_b && backup_has_error (info_b))
	{
		backup_perror (info_b, "Copy source");

		fprintf (stderr, "To attempt copy anyway, try again with -E.  -s is implied by -E.\n");
		return 1;
	}

	info_r = init_restore (rflags);
	if (info_r && restore_has_error (info_r))
	{
		restore_perror (info_r, "Copy target");
		return 1;
	}

	if (!info_b || !info_r)
	{
		fprintf (stderr, "%s: Copy failed to start.  Make sure you specified the right\ndevices, and that the drives are not locked.\n", argv[0]);
		return 1;
	}
	else
	{
		unsigned starttime;
		char buf[BUFSIZE];
		unsigned int curcount = 0;
		int nread, nwrit;

		if (threshopt)
			backup_set_thresh (info_b, thresh);

		if (varsize)
			restore_set_varsize (info_r, varsize);
		if (swapsize)
			restore_set_swapsize (info_r, swapsize * 1024 * 2);
		if (bswap)
			restore_set_bswap (info_r, bswap);
		if (restorebits)
			restore_set_mfs_type (info_r, restorebits);

		if (quiet < 2)
			fprintf (stderr, "Scanning source drive.  Please wait a moment.\n");

		if (backup_start (info_b) < 0)
		{
			if (backup_has_error (info_b))
				backup_perror (info_b, "Copy source");
			else
				fprintf (stderr, "Copy source failed.\n");
			return 1;
		}

// Fill the buffer up to start.  Restore needs some information to bootstrap
// the process.
		while (curcount < BUFSIZE && (nread = backup_read (info_b, buf, BUFSIZE - curcount)) > 0)
		{
			curcount += nread;
		}

		if (curcount < BUFSIZE)
		{
			if (backup_has_error (info_b))
				backup_perror (info_b, "Copy source");
			else
				fprintf (stderr, "Copy source failed.\n");
			return 1;
		}

		nread = curcount;

		nwrit = restore_write (info_r, buf, nread);
		if (nwrit < 0)
		{
			if (restore_has_error (info_r))
				restore_perror (info_r, "Copy target");
			else
				fprintf (stderr, "Copy target failed.\n");
			return 1;
		}

		if (swapsize > 128 && !(info_r->back_flags & BF_NOBSWAP))
			fprintf (stderr, "    ***WARNING***\nUsing version 1 swap signature to get >128MiB swap size, but the backup looks\nlike a series 1.  Stock SERIES 1 TiVo kernels do not support the version 1\nswap signature.  If you are using a stock SERIES 1 TiVo kernel, 128MiB is the\nlargest usable swap size.\n");
		if (restorebits == 64 && !(info_r->back_flags & BF_64))
			fprintf (stderr, "    ***WARNING***\nConverting MFS structure to 64 bit if very experimental, and will only work on\nSeries 3 based TiVo platforms or later, such as the TiVo HD.\n");

		if (restore_trydev (info_r, dest_a, dest_b) < 0)
		{
			if (restore_has_error (info_r))
				restore_perror (info_r, "Copy target");
			else
				fprintf (stderr, "Copy target failed.\n");
			return 1;
		}

		if (restore_start (info_r) < 0)
		{
			if (restore_has_error (info_r))
				restore_perror (info_r, "Copy target");
			else
				fprintf (stderr, "Copy target failed.\n");
			return 1;
		}

		if (restore_write (info_r, buf + nwrit, nread - nwrit) != nread - nwrit)
		{
			if (restore_has_error (info_r))
				restore_perror (info_r, "Copy target");
			else
				fprintf (stderr, "Copy target failed.\n");
			return 1;
		}

		starttime = time (NULL);

		fprintf (stderr, "Starting copy\nSize: %d megabytes\n", info_r->nsectors / 2048);
		while ((curcount = backup_read (info_b, buf, BUFSIZE)) > 0)
		{
			unsigned int prcnt, compr;
			if (restore_write (info_r, buf, curcount) != curcount)
			{
				if (quiet < 1)
					fprintf (stderr, "\n");
				if (restore_has_error (info_r))
					restore_perror (info_r, "Copy source");
				else
					fprintf (stderr, "Copy source failed.\n");
				return 1;
			}
			prcnt = get_percent (info_r->cursector, info_r->nsectors);
			if (quiet < 1)
			{
				unsigned timedelta = time(NULL) - starttime;

				fprintf (stderr, "\rCopying %d of %d mb (%d.%02d%%)", info_r->cursector / 2048, info_r->nsectors / 2048, prcnt / 100, prcnt % 100);

				if (prcnt > 100 && timedelta > 15)
				{
					unsigned ETA = timedelta * (10000 - prcnt) / prcnt;
					fprintf (stderr, " %d mb/sec (ETA %d:%02d:%02d)", info_r->cursector / timedelta / 2048, ETA / 3600, ETA / 60 % 60, ETA % 60);
				}
			}
		}

		if (quiet < 1)
			fprintf (stderr, "\n");

		if (backup_has_error (info_b))
		{
			backup_perror (info_b, "Copy source");
			return 1;
		}

		if (restore_has_error (info_r))
		{
			restore_perror (info_r, "Copy target");
			return 1;
		}
	}

	if (backup_finish (info_b) < 0)
	{
		if (backup_has_error (info_b))
			backup_perror (info_b, "Copy source");
		else
			fprintf (stderr, "Copy source failed.\n");
		return 1;
	}

	if (info_b->back_flags & BF_TRUNCATED)
	{
		fprintf (stderr, "***WARNING***\nCopy was made of an incomplete volume.  While the copy succeeded,\nit is possible there was some required data missing.  Verify your copy.\n");
	}

	if (quiet < 2)
		fprintf (stderr, "Cleaning up target.  Please wait a moment.\n");

	if (restore_finish (info_r) < 0)
	{
		if (restore_has_error (info_r))
			restore_perror (info_r, "Copy target");
		else
			fprintf (stderr, "Copy target failed.\n");
		return 1;
	}

	if (quiet < 2)
		fprintf (stderr, "Copy done!\n");

	if (expand > 0)
	{
		int blocksize = 0x800;
		struct mfs_handle *mfshnd;

		expand = 0;

		mfshnd = mfs_init (dest_a, dest_b, O_RDWR);
		if (!mfshnd)
		{
			fprintf (stderr, "Drive expansion failed.\n");
			return 1;
		}

		if (mfs_has_error (mfshnd))
		{
			mfs_perror (mfshnd, "Target expand");
			return 1;
		}

		while (expandscale-- > 0)
			blocksize *= 2;

		if (tivo_partition_largest_free (dest_a) > 1024 * 1024 * 2)
		{
			if (expand_drive (mfshnd, "/dev/hda", dest_a, blocksize) < 0)
			{
				if (mfs_has_error (mfshnd))
					mfs_perror (mfshnd, "Expand drive A");
				else
					fprintf (stderr, "Drive A expansion failed.\n");
				return 1;
			}
			expand++;
		}

		if (dest_b[0] && tivo_partition_largest_free (dest_b) > 1024 * 1024 * 2)
		{
			if (expand_drive (mfshnd, "/dev/hdb", dest_b, blocksize) < 0)
			{
				if (mfs_has_error (mfshnd))
					mfs_perror (mfshnd, "Expand drive B");
				else
					fprintf (stderr, "Drive B expansion failed.\n");
				return 1;
			}
			expand++;
		}

		if (!expand)
		{
			fprintf (stderr, "Not enough extra space to expand on A drive%s.\n", dest_b[0]? " or B drive": "");
		}
	}

	return 0;
}
