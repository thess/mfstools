#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include "mfs.h"
#include "macpart.h"

/**
 * Tivo device names.
 * Defaults to /dev/hd{a,b}.  For a Premier backup, we'll replace these
 * with /dev/sd{a,b}.
 */
extern char* tivo_devnames[];

void
mfsadd_usage (char *progname)
{
	fprintf (stderr, "%s %s\n", PACKAGE, VERSION);
	fprintf (stderr, "Usage: %s [options] Adrive [Bdrive] [NewApp NewMedia]\n", progname);
	fprintf (stderr, "Options:\n");
	fprintf (stderr, " -h      	 Display this help message\n");
	fprintf (stderr, " -r scale	 Override media blocksize of 20480 with 2048<<scale\n");
	fprintf (stderr, "		    (scale=0 to 4)\n");
	fprintf (stderr, " -x      	 Create partition(s) on all drives\n");
	fprintf (stderr, " -X drive	 Create partition(s) on a specific drive\n");
	fprintf (stderr, " -m size 	 Maximum media partition size in GiB\n");
	fprintf (stderr, " -M size 	 Maximum drive size in GiB (ie lba28 would be 128)\n");
	fprintf (stderr, " -f		 Use with -m to fill the drive multiple media partitions\n");
	fprintf (stderr, " -c		 Used to force the ordering of the added partitions to\n");
	fprintf (stderr, "		    allow for coalescing at a later time\n");
	fprintf (stderr, "NewApp NewMedia  ");
#if TARGET_OS_MAC
	fprintf (stderr, "Add existing partitions (Such as /dev/disk1s14 /dev/disk1s15)\n");
#else
	fprintf (stderr, "Add existing partitions (Such as /dev/hda13 /dev/hda14)\n");
#endif
	fprintf (stderr, "		    to the MFS volume set\n");
	fprintf (stderr, " -l		 Use with NewApp NewMedia to allow partitions with numbers\n");
	fprintf (stderr, "		    less than 10 into the MFS\n");

}

int
mfsadd_scan_partitions (struct mfs_handle *mfs, int *used, char *seconddrive)
{
	char partitions[256];
	char *loop = partitions;
	int havebdrive = 0, havecdrive = 0;

	strncpy (partitions, mfs_partition_list (mfs), 255);
	partitions[255] = 0;

	if (!strncmp(partitions,"/dev/sd",7))
	{
		tivo_devnames[0] = "/dev/sda";
		tivo_devnames[1] = "/dev/sdb";
	}

	*seconddrive = 0;

	while (*loop)
	{
		int drive;
		int partition;

		while (*loop && isspace (*loop))
		{
			loop++;
		}

		// Premiere and later use /dev/sd*
		if (!(strncmp (loop, "/dev/hd", 7)==0 || strncmp (loop, "/dev/sd", 7)==0))
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

		if (*loop && (!isspace (*loop) || partition < 2 || partition > 16))
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
mfsadd_add_extends (struct mfs_handle *mfs, char **drives, char **xdevs, char **pairs, char *pairnums, int *npairs, int minalloc,	int64_t maxdisk , int64_t maxmedia, int fill, int coalesce)
{
	int loop = 0;
	int loop2 = 0;
	char tmp[MAXPATHLEN];
	char appname[32];
	char medianame[32];
	int nparts = 0;
	char *mfs_partitions;

	// Get the current mfs partition count, so we can name the adds and make sure we don't exceed the max allowed
	mfs_partitions = mfs_partition_list (mfs);
	loop = 0;
	while (mfs_partitions[loop])
	{
		nparts++;
		while (mfs_partitions[loop] && !isspace (mfs_partitions[loop]))
			loop++;
		while (mfs_partitions[loop] && isspace (mfs_partitions[loop]))
			loop++;
	}

	for (loop = 0; loop < 2 && xdevs[loop]; loop++)
	{
		do 
		{
			uint64_t maxfree = tivo_partition_largest_free (xdevs[loop]);
			uint64_t totalfree = tivo_partition_total_free (xdevs[loop]);
			uint64_t totalused = tivo_partition_total_used (xdevs[loop]);
			uint64_t mediasize = 0;
			uint64_t appsize = 0;
		unsigned int part1, part2;
		int devn = xdevs[loop] == drives[0]? 0: 1;

		if (maxfree < 1024 * 1024 * 2)
				break;
			
			// Limit the total disk size if set
			if (maxdisk && maxdisk < totalfree + totalused)
			{
				if (maxdisk > totalused)
					totalfree = maxdisk - totalused;
				else
					totalfree = 0;
				if (maxfree > totalfree)
					maxfree = totalfree;
			}
			
			// Limit the parttion size if needed
			if (maxmedia && maxmedia < maxfree)
				maxfree = maxmedia;

			// TODO: Change mediasize to this in order to round to the nearest chunk size (TiVo doesn't bother, so neither do we for now)
			//mediasize = maxfree & ~(minalloc - 1); /* only works when minalloc is base-2, which it might not be now, better to use the next one */
			//mediasize = maxfree / minalloc * minalloc; /* this math works better for rounding down to the nearest minalloc, but doesn't take into account rounding to 4K boundary for modern disks, which tivo_partition_add will do, move along */
			//mediasize = (maxfree / minalloc * minalloc) / 8 * 8; /* That should do it for rounding to the nearest chunk size (minalloc), if we must */
			mediasize = maxfree / 8 * 8; /* Round down to the nearest 4K boundary because tivo_partition_add does */
			appsize = mfs_volume_pair_app_size (mfs, mediasize, minalloc);

			if (totalfree - maxfree < appsize && maxfree - mediasize < appsize)
		{
				//TODO: Change mediasize to this in order to round to the nearest chunk size (TiVo doesn't bother, so neither do we for now)
				//mediasize = (maxfree - required) & ~(minalloc - 1);; /* only works when minalloc is base-2, which it might not be now, better to use the next one */
				//mediasize = (maxfree - required) / minalloc * minalloc; /* this math works better for rounding down to the nearest minalloc, but doesn't take into account rounding to 4K boundary for modern disks, which tivo_partition_add will do, move along */
				//mediasize = ((maxfree - required) / minalloc * minalloc) / 8 * 8; /* That should do it for rounding to the nearest chunk size (minalloc), if we must */
				mediasize = (maxfree - appsize) / 8 * 8; /* Round down to the nearest 4K boundary because tivo_partition_add does */
				appsize = mfs_volume_pair_app_size (mfs, mediasize, minalloc);
		}

			// Friendly partition names
			sprintf (appname, "MFS application region %d", (*npairs / 2) + (nparts / 2) + 1);
			sprintf (medianame, "MFS media region %d", (*npairs / 2) + (nparts / 2) + 1);

			if (totalfree - maxfree >= appsize && maxfree - mediasize < appsize && coalesce != 1)
		{
				part2 = tivo_partition_add (xdevs[loop], mediasize, 0, medianame, "MFS");
				part1 = tivo_partition_add (xdevs[loop], appsize, part2, appname, "MFS");

			part2++;
		}
		else
		{
				part1 = tivo_partition_add (xdevs[loop], appsize, 0, appname, "MFS");
				part2 = tivo_partition_add (xdevs[loop], mediasize, 0, medianame, "MFS");
		}

		if (part1 < 2 || part2 < 2 || part1 > 16 || part2 > 16)
		{
				if (*npairs)
					break; // We were able to expand, just not in this iteration
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
				if (*npairs)
					break; // We were able to expand, just not in this iteration
			fprintf (stderr, "Memory exhausted!\n");
			return -1;
		}

			if (fill == 0)
				break; // Not asked to create multiple mfs partitions on a drive
			if (totalfree - mediasize - appsize < minalloc + 4)
				break; // Out of space
			if (part2 + 2 > 16)
				break; // Reached the max partitions for this drive
			if (nparts + *npairs + 2 > 12)
				break; // Reached the max total partitions
		} while (1);
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
	unsigned int minalloc = 20480;
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
	int64_t maxdisk = 0;
	int64_t maxmedia = 0;
	int fill = 0;
	int low = 0;
	int coalesce = 0;

	tivo_partition_direct ();

	while ((opt = getopt (argc, argv, "xX:r:hem:M:fcl")) > 0)
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
				fprintf (stderr, "%s: Integer argument expected for -r.\n", argv[0]);
				return 1;
			}
			if (minalloc < 0x800 || minalloc > (0x800 << 4))
			{
				fprintf (stderr, "%s: Value for -r must be between 0 and 4.\n", argv[0]);
				return 1;
			}
			break;
		case 'e':
			usecdrive = 1;
			break;
		case 'm':
			maxmedia = strtoull (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -m.\n", argv[0]);
				return 1;
			}
			maxmedia = maxmedia * 1024 * 1024 * 1024 / 512; //Convert GiB to sectors
			break;
		case 'M':
			maxdisk = strtoull (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -M.\n", argv[0]);
				return 1;
			}
			maxdisk = (maxdisk * 1024 * 1024 * 1024 / 512) - 1; //Convert GiB to sectors, subtract one to make sure we end below the requested size
			break;
		case 'f':
			fill = 1;
			break;
		case 'l':
			low = 1;
			break;
		case 'c':
			coalesce = 1;
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

	if (extendmfs || (xdevs[0] && xdevs[0] == drives[1]) ||
	    (xdevs[1] && xdevs[1] == drives[1]))
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

	mfs = mfs_init (drives[0], drives[1], (O_RDWR | MFS_ERROROK));

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

/* If allow MFS partitions in partitions less than 10 is true, then do not allow it to overwrite the inital APM descriptor entry */

		if ((pairnums[loop] >> 6) == 0 && ((pairnums[loop] & 31) < (10 - 8 * low)))
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

	if (mfsadd_add_extends (mfs, drives, xdevs, pairs, pairnums, &npairs, minalloc, maxdisk, maxmedia, fill, coalesce) < 0)
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
		sprintf (app, "%s%d", tivo_devnames[pairnums[loop]>>6] + usecdrive, pairnums[loop] & 31);
		sprintf (media, "%s%d", tivo_devnames[pairnums[loop + 1]>>6] + usecdrive, pairnums[loop + 1] & 31);

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
	{
		fprintf (stderr, "Done!  Estimated standalone gain: %d hours\n", loop2 - hours);
		fprintf (stderr, "Revalidating partion table on %s...  ", drives[0]);
		if (revalidate_drive(drives[0]) < 0)
			fprintf (stderr, "Failed!\n");
		else
			fprintf (stderr, "Success!\n");
		if (drives[1])
		{
			fprintf (stderr, "Revalidating partion table on %s...  ", drives[1]);
			if (revalidate_drive(drives[1]) < 0)
				fprintf (stderr, "Failed!\n");
			else
				fprintf (stderr, "Success!\n");
		}
	}

	return 0;
}
