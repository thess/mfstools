#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include "mfs.h"
#include "macpart.h"

void
mfsadd_usage (char *progname)
{
	fprintf (stderr, "Usage: %s [options] Adrive [Bdrive] [NewApp NewMedia]\n", progname);
	fprintf (stderr, "Options:\n");
	fprintf (stderr, " -h        Display this help message\n");
	fprintf (stderr, " -r scale  Set scale factor of media block size\n");
	fprintf (stderr, " -x        Create partitions to fill all drives\n");
	fprintf (stderr, " -X drive  Create partitions to fill specific drive\n");
	fprintf (stderr, "NewApp / NewMedia\n");
#if TARGET_OS_MAC
	fprintf (stderr, "  Existing partitions (Such as /dev/disk1s14 /dev/disk1s15) to add to\n");
#else
	fprintf (stderr, "  Existing partitions (Such as /dev/hda13 /dev/hda14) to add to\n");
#endif
	fprintf (stderr, "  the MFS volume set\n");
}

int
mfsadd_scan_partitions (struct mfs_handle *mfs, int *used, char *seconddrive)
{
	char partitions[256];
	char *loop = partitions;
	int havebdrive = 0, havecdrive = 0;

	strncpy (partitions, mfs_partition_list (mfs), 255);
	partitions[255] = 0;

	*seconddrive = 0;

	while (*loop)
	{
		int drive;
		int partition;

		while (*loop && isspace (*loop))
		{
			loop++;
		}

		if (strncmp (loop, "/dev/hd", 7))
		{
			fprintf (stderr, "Non-standard drive in MFS - giving up.\n");
			return -1;
		}
		loop += 7;

		switch (*loop)
		{
		case 'a':
			drive = 0;
			break;
		case 'b':
			drive = 1;
			havebdrive = 1;
			break;
		case 'c':
			drive = 1;
			havecdrive = 1;
			break;
		default:
			fprintf (stderr, "Non-standard drive in MFS - giving up.\n");
			return -1;
		}

		loop++;

		partition = strtoul (loop, &loop, 10);

		if (*loop && !isspace (*loop) || partition < 2 || partition > 16)
		{
			fprintf (stderr, "Non-standard partition in MFS - giving up.\n");
			return -1;
		}

		if (used[drive] & (1 << partition))
		{
			fprintf (stderr, "Non-standard partition in MFS - giving up.\n");
			return -1;
		}

		used[drive] |= 1 << partition;
	}

	if (havebdrive && havecdrive)
	{
		fprintf (stderr, "Don't know how to handle both internal and external expansion drives - giving up.\n");
		return -1;
	}

	if (havebdrive)
		*seconddrive = 'b';
	else if (havecdrive)
		*seconddrive = 'c';

	return 0;
}

int
mfsadd_add_extends (struct mfs_handle *mfs, char **drives, char **xdevs, char **pairs, char *pairnums, int *npairs, int minalloc)
{
	int loop;
	char tmp[MAXPATHLEN];

	for (loop = 0; loop < 2 && xdevs[loop]; loop++)
	{
		unsigned int maxfree = tivo_partition_largest_free (xdevs[loop]);
		unsigned int totalfree = tivo_partition_total_free (xdevs[loop]);
		unsigned int used = maxfree & ~(minalloc - 1);
		unsigned int required = mfs_volume_pair_app_size (used, minalloc);
		unsigned int part1, part2;
		int devn = xdevs[loop] == drives[0]? 0: 1;

		if (maxfree < 1024 * 1024 * 2)
			continue;

		if (totalfree - maxfree < required && maxfree - used < required)
		{
			used = (maxfree - required) & ~(minalloc - 1);
			required = mfs_volume_pair_app_size (used, minalloc);
		}

		if (totalfree - maxfree >= required && maxfree - used < required)
		{
			part2 = tivo_partition_add (xdevs[loop], used, 0, "New MFS Media", "MFS");
			part1 = tivo_partition_add (xdevs[loop], required, part2, "New MFS Application", "MFS");

			part2++;
		}
		else
		{
			part1 = tivo_partition_add (xdevs[loop], required, 0, "New MFS Application", "MFS");
			part2 = tivo_partition_add (xdevs[loop], used, 0, "New MFS Media", "MFS");
		}

		if (part1 < 2 || part2 < 2 || part1 > 16 || part2 > 16)
		{
			fprintf (stderr, "Expand of %s would result in too many partitions.\n", xdevs[loop]);
			return -1;
		}

		pairnums[(*npairs)++] = (devn << 6) | part1;
		pairnums[(*npairs)++] = (devn << 6) | part2;
#if TARGET_OS_MAC
		sprintf (tmp, "%ss%d", xdevs[loop], part1);
		pairs[*npairs - 2] = strdup (tmp);
		sprintf (tmp, "%ss%d", xdevs[loop], part2);
		pairs[*npairs - 1] = strdup (tmp);
#else
		sprintf (tmp, "%s%d", xdevs[loop], part1);
		pairs[*npairs - 2] = strdup (tmp);
		sprintf (tmp, "%s%d", xdevs[loop], part2);
		pairs[*npairs - 1] = strdup (tmp);
#endif
		if (!pairs[*npairs - 2] || !pairs[*npairs - 1])
		{
			fprintf (stderr, "Memory exhausted!\n");
			return -1;
		}
	}

	return 0;
}

int
check_partition_count (struct mfs_handle *mfs, char *pairnums, int npairs)
{
	int loop, total;

	total = strlen (mfs_partition_list (mfs));

	for (loop = 0; loop < npairs; loop++)
	{
		if ((pairnums[loop] & 31) >= 10)
			total += 11;
		else
			total += 10;
	}

	if (total >= 128)
	{
		fprintf (stderr, "Added partitions would exceed MFS limit!\n");
		return -1;
	}

	return 0;
}

int
mfsadd_main (int argc, char **argv)
{
	unsigned int minalloc = 0x800 << 2;
	int opt;
	int init_b_part = 0;
	int extendall = 0;
	int extendmfs = 0;
	char *xdevs[2] = {0, 0};
	int npairs = 0;
	char *pairs[32];
	char pairnums[32] = {0};
	int loop, loop2;
	int hours;
	char *drives[2] = {0, 0};
	char partitioncount[2] = {0, 0};
	char *tmp;
	int used[2] = {0, 0};
	struct mfs_handle *mfs;
	int changed[2] = {0, 0};
	int usecdrive = 0;
	char seconddrive = 0;

	tivo_partition_direct ();

	while ((opt = getopt (argc, argv, "xX:r:he")) > 0)
	{
		switch (opt)
		{
		case 'X':
			if (extendall < 2)
			{
				xdevs[extendall] = optarg;
				extendall++;
				break;
			}

			fprintf (stderr, "%s: Can only extend MFS to 2 devices.\n", argv[0]);
			return 1;
		case 'x':
			extendmfs = 1;
			break;
		case 'r':
			minalloc = 0x800 << strtoul (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -s.\n", argv[0]);
				return 1;
			}
			if (minalloc < 0x800 || minalloc > (0x800 << 4))
			{
				fprintf (stderr, "%s: Value for -s must be between 1 and 4.\n", argv[0]);
				return 1;
			}
			break;
		case 'e':
			usecdrive = 1;
			break;
		default:
			mfsadd_usage (argv[0]);
			return 1;
		}
	}

	while (optind < argc)
	{
#if TARGET_OS_MAC
		int disk;
		int partnum;
		char extra;
		
		if (sscanf (argv[optind], "/dev/disk%ds%d%c", &disk, &partnum, &extra) == 2 && partnum > 0)
#else
		int len = strlen (argv[optind]);

		if (isdigit (argv[optind][len - 1]))
#endif
		{
/* If the device is a partition, add that partition to the list. */
			if (npairs + 4 >= sizeof (pairs) / sizeof (*pairs))
			{
				fprintf (stderr, "%s: Too many new partitions!\n", argv[0]);
				return 1;
			}
			pairs[npairs] = argv[optind];
			npairs++;
		}
		else
		{
/* If the device is a drive, set it as the A or B drive. */
			if (!drives[0])
				drives[0] = argv[optind];
			else if (!drives[1])
				drives[1] = argv[optind];
			else
			{
				mfsadd_usage (argv[0]);
				return 1;
			}
		}
		optind++;
	}

/* Can't do anything without an A or B drive. */
	if (!drives[0])
	{
		mfsadd_usage (argv[0]);
		return 1;
	}

/* The error message says it all. */
	if (npairs & 1)
	{
		fprintf (stderr, "%s: Number of new partitions must be even.\n", argv[0]);
		return 1;
	}

/* Map the drives being extended to the A and B drives. */
	for (loop = 0; loop < extendall; loop++)
	{
		if (!strcmp (xdevs[loop], drives[0]))
			xdevs[loop] = drives[0];
		else if (!drives[1])
			drives[1] = xdevs[loop];
 		else if (!strcmp (xdevs[loop], drives[1]))
			xdevs[loop] = drives[1];
		else
		{
			fprintf (stderr, "%s: Arguments to -x must be one of %s or %s.\n", argv[0], drives[0], drives[1]);
			return 1;
		}
	}

	if (extendmfs || xdevs[0] && xdevs[0] == drives[1] ||
		xdevs[1] && xdevs[1] == drives[1])
	{
		init_b_part = 1;
	}

/* Make sure both extend drives are not the same device. */
	if (extendall == 2 && xdevs[0] == xdevs[1])
	{
		fprintf (stderr, "%s: -x argument only makes sense once for each device.\n", argv[0]);
		return 1;
	}

/* Go through all the added pairs. */
	for (loop = 0; loop < npairs; loop++)
	{
		int tmp;
		char *str;

/* Figure out what drive it's on. */
		if (!strncmp (pairs[loop], drives[0], strlen (drives[0])))
			pairnums[loop] = 0x00;
		else if (!drives[1] || !strncmp (pairs[loop], drives[1], strlen (drives[1])))
			pairnums[loop] = 0x40;
		else
		{
			fprintf (stderr, "%s: added partitions must be partitions of either %s or %s\n", argv[0], drives[0], drives[1]);
			return 1;
		}

/* If it is a new drive, add it as the B drive. */
		if (!drives[pairnums[loop] >> 6])
		{
			char *num;
			char *new;

/* Known memory leak.  Deal. */
			new = strdup (pairs[loop]);

			if (!new)
			{
				fprintf (stderr, "%s: Memory allocation error.\n", argv[0]);
				return 1;
			}

/* This is just strrspn - if it existed. */
			for (num = new + strlen (new) - 1; num > new && isdigit (*num); num--)
				;
			num++;
			*num = 0;

/* Set the new drive name. */
			drives[pairnums[loop] >> 6] = new;
		}

#if TARGET_OS_MAC
		tmp = strtoul (pairs[loop] + strlen (drives[pairnums[loop] >> 6]) + 1, &str, 10);
#else
		tmp = strtoul (pairs[loop] + strlen (drives[pairnums[loop] >> 6]), &str, 10);
#endif
		if (str && *str)
		{
			fprintf (stderr, "%s: added partition %s is not a partition of %s\n", argv[0], pairs[loop], drives[pairnums[loop] >> 6]);
			return 1;
		}

		if (tmp < 2 || tmp > 16)
		{
			fprintf (stderr, "%s: TiVo only supports partiton numbers 2 through 16\n", argv[0]);
			return 1;
		}

		pairnums[loop] |= tmp;

		for (loop2 = 0; loop2 < loop; loop2++)
		{
			if (pairnums[loop] == pairnums[loop2])
			{
				fprintf (stderr, "%s: Can only add %s once.\n", argv[0], pairs[loop]);
				return 1;
			}
		}
	}

	mfs = mfs_init (drives[0], drives[1], O_RDWR);

	if (!mfs)
	{
		fprintf (stderr, "Unable to open MFS volume.\n");
		return 1;
	}

	if (mfs_has_error (mfs))
	{
		mfs_perror (mfs, argv[0]);
		return 1;
	}

	if (mfsadd_scan_partitions (mfs, used, &seconddrive) < 0)
		return 1;

	if (seconddrive == 'b')
	{
		if (usecdrive)
		{
			fprintf (stderr, "Can not add eSATA drive with -e when internal B drive already in use.\n");
			return 1;
		}
	}
	else if (seconddrive == 'c')
	{
		usecdrive = 1;
	}

	if (extendmfs)
	{
		for (loop = 0; loop < sizeof (drives) / sizeof (*drives); loop++)
		{
			if (!drives[loop])
				break;

			for (loop2 = 0; loop2 < extendall; loop2++)
			{
				if (xdevs[loop2] == drives[loop])
					break;
			}

			if (loop2 >= extendall)
			{
				xdevs[extendall++] = drives[loop];
			}
		}
	}

	for (loop = 0; loop < npairs; loop++)
	{
		if (used[pairnums[loop] >> 6] & (1 << (pairnums[loop] & 31)))
		{
			fprintf (stderr, "%s already in MFS set.\n", pairs[loop]);
			return 1;
		}
	}

	partitioncount[0] = tivo_partition_count (drives[0]);
	if (drives[1])
		partitioncount[1] = tivo_partition_count (drives[1]);
	for (loop = 0; loop < npairs; loop++)
	{
		char *ptype;

		if (partitioncount[pairnums[loop] >> 6] < (pairnums[loop] & 31))
		{
			fprintf (stderr, "Partition %s doesn't exist!\n", pairs[loop]);
			return 1;
		}

		if ((pairnums[loop] >> 6) == 0 && (pairnums[loop] & 31) < 10)
		{
			fprintf (stderr, "Partition %s would trash system partition!\n", pairs[loop]);
			return 1;
		}

		ptype = tivo_partition_type (drives[pairnums[loop] >> 6], pairnums[loop] & 31);
		if (!ptype || strcmp (ptype, "MFS"))
		{
			fprintf (stderr, "Partition %s is of type %s, should be MFS\n", pairs[loop], ptype? ptype: "(NULL)");
			return 1;
		}
	}

	if (drives[1] && !used[1])
	{
		int swab = 0;
		if (tivo_partition_swabbed (drives[0]))
			swab ^= 1;
		if (tivo_partition_devswabbed (drives[0]))
			swab ^= 1;
		if (tivo_partition_devswabbed (drives[1]))
			swab ^= 1;
		if (init_b_part && tivo_partition_table_init (drives[1], swab) < 0)
		{
			fprintf (stderr, "Error initializing new B drive!\n");
			return 1;
		}
	}

	if (mfsadd_add_extends (mfs, drives, xdevs, pairs, pairnums, &npairs, minalloc) < 0)
		return 1;

	if (check_partition_count (mfs, pairnums, npairs) < 0)
		return 1;

	for (loop = 0; loop < npairs; loop++)
	{
		changed[pairnums[loop] >> 6] = 1;
	}

	if (changed[0] && drives[0])
		tivo_partition_table_write (drives[0]);
	if (changed[1] && drives[1])
		tivo_partition_table_write (drives[1]);

	hours = mfs_sa_hours_estimate (mfs);
	loop2 = hours;

	fprintf (stderr, "Current estimated standalone size: %d hours\n", loop2);

	for (loop = 0; loop < npairs; loop += 2)
	{
		char app[MAXPATHLEN];
		char media[MAXPATHLEN];
		int newsize;

		fprintf (stderr, "Adding pair %s-%s...\n", pairs[loop], pairs[loop + 1]);
		sprintf (app, "/dev/hd%c%d", 'a' + (pairnums[loop] >> 6) + usecdrive, pairnums[loop] & 31);
		sprintf (media, "/dev/hd%c%d", 'a' + (pairnums[loop + 1] >> 6) + usecdrive, pairnums[loop + 1] & 31);
		if (mfs_add_volume_pair (mfs, app, media, minalloc) < 0)
		{
			fprintf (stderr, "Adding %s-%s", pairs[loop], pairs[loop + 1]);
			mfs_perror (mfs, "");
			return 1;
		}

		newsize = mfs_sa_hours_estimate (mfs);
		fprintf (stderr, "New estimated standalone size: %d hours (%d more)\n", newsize, newsize - loop2);
		loop2 = newsize;
	}

	if (npairs < 1)
		fprintf (stderr, "Nothing to add!\n");
	else
		fprintf (stderr, "Done!  Estimated standalone gain: %d hours\n", loop2 - hours);

	return 0;
}
