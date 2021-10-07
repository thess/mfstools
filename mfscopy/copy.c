#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <inttypes.h>

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
	fprintf (stderr, "%s %s\n", PACKAGE, VERSION);
	fprintf (stderr, "Usage: %s [options] SourceA[:SourceB] DestA[:DestB]\n", progname);
	fprintf (stderr, "General options:\n");
	fprintf (stderr, " -h        Display this help message\n");
	fprintf (stderr, " -q        Do not display progress\n");
	fprintf (stderr, " -qq       Do not display anything but error messages\n");
	fprintf (stderr, "Source options:\n");
#if DEPRECATED
	// Mostly used to copy loopsets when excluding recordings.  Loopsets are now handled automatically, so these are less useful now...
	fprintf (stderr, " -f max    Copy only fsids below max\n");
	fprintf (stderr, " -L max    Copy only streams less than max MiB\n");
#endif
	fprintf (stderr, " -t        Use total length of stream in calculations\n");
	fprintf (stderr, " -T        Copy total length of stream instead of used length\n");
	fprintf (stderr, " -a        Copy all streams\n");
	fprintf (stderr, " -i        Include all non-mfs partitions from Adrive (alternate, custom, etc.)\n");
#if DEPRECATED
	// C'mon, who would do this ???
	fprintf (stderr, " -D        Do not force loopset and demo files to be added\n");
#endif
	fprintf (stderr, "Target options:\n");
	fprintf (stderr, " -s        Shrink MFS whily copying (implied for v3 copies)\n");
#if DEPRECATED
	// Optimized layout is now the default.  Probably no reason to allow a non-optimized layout...
	//fprintf (stderr, " -p        Optimize partition layout\n");
	fprintf (stderr, " -P        Do NOT optimize the partition layout\n");
#endif
	fprintf (stderr, " -k        Optimize partition layout with kernels first\n");
	fprintf (stderr, " -r scale  Override v3 media blocksize of 20480 with 2048<<scale (scale=0 to 4)\n");
	fprintf (stderr, " -v size   Recreate /var as size MiB and don't copy /var\n");
	fprintf (stderr, " -d size   Recreate /db (SQLite in source) as size MiB and don't copy /db\n");
	fprintf (stderr, " -S size   Recreate swap as size MiB\n");
	fprintf (stderr, " -l        Leave at least 2 partitions free\n");
	fprintf (stderr, " -b        Force no byte swapping on target\n");
	fprintf (stderr, " -B        Force byte swapping on target\n");
#if DEPRECATED
	// Arguments could be made to keep this, but I suspect few will actually miss it.
	fprintf (stderr, " -z        Zero out partitions not copied\n");
#endif
	fprintf (stderr, " -R        Just copy raw blocks (v1) instead of rebuilding data structures (v3)\n");
	fprintf (stderr, " -w 32/64  Write MFS structures as 32 or 64 bit\n");
	fprintf (stderr, " -c size   Carve (leave free) in blocks on drive A\n");
	fprintf (stderr, " -C size   Carve (leave free) in blocks on Drive B\n");
	fprintf (stderr, " -m size   Maximum media partition size in GiB for v3 restore\n");
	fprintf (stderr, " -M size   Maximum drive size in GiB (ie lba28 would be 128)\n");
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

void
get_drives (char *drives, char *adrive, char *bdrive)
{
	int devlen = strcspn (drives, ":");

	strncpy (adrive, drives, devlen);
	adrive[devlen] = 0;

	if (drives[devlen] != 0)
		strcpy (bdrive, drives + devlen + 1);

	//fprintf (stderr, "Drives: %s and %s\n", adrive, bdrive);
}

int
copy_main (int argc, char **argv)
{
	char source_a[PATH_MAX], source_b[PATH_MAX];
	char dest_a[PATH_MAX], dest_b[PATH_MAX];
	char *tmp;
	struct backup_info *info_b, *info_r;
	int opt, threshopt = 0;
	unsigned int thresh = 0;
	unsigned int varsize = 0, dbsize = 0, swapsize = 0;
	unsigned int bflags = BF_BACKUPVAR, rflags = RF_BALANCE;;
	int quiet = 0;
	int bswap = 0;
	int restorebits = 0;
	int rawcopy = 0;
	int64_t carveA = 0;
	int64_t carveB = 0;
	int norescheck = 0;
	unsigned int skipdb = 0;
	int64_t maxdisk = 0;
	int64_t maxmedia = 0;
	unsigned int minalloc = 0;
	unsigned starttime = 0;

	tivo_partition_direct ();

#if DEPRECATED
	while ((opt = getopt (argc, argv, "hqf:L:tTasPxr:v:S:lbBzEw:RiDkc:C:d:m:M:")) > 0)
#else
	while ((opt = getopt (argc, argv, "hqtTasxr:v:S:lbBEw:Rikc:C:d:m:M:")) > 0)
#endif
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
			threshopt = 'f';
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
				fprintf (stderr, "%s: -L and -%c cannot be used together\n", argv[0], threshopt);
				return 1;
			}
			threshopt = 'L';
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
			threshopt = 'a';
			thresh = ~0;
			// No need to check for resource files if we are including all streams
			norescheck = 1;
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
		case 'd':
			skipdb = 1;
			dbsize = strtoul (optarg, &tmp, 10);
			dbsize *= 1024 * 2;
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -d.\n", argv[0]);
				return 1;
			}
			break;
		case 'S':
			swapsize = strtoul (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -S.\n", argv[0]);
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
		case 'P':
			rflags &= ~RF_BALANCE;
			break;
		case 'l':
			rflags |= RF_NOFILL;
			break;
		case 'x':
			copy_usage (argv[0]);
			fprintf (stderr, "\n Deprecated argument -x.  Use mfsadd after copy to expand drive(s).\n");
			return 1;
		case 'r':
			minalloc = 0x800 << strtoul (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -r.\n", argv[0]);
				return 1;
			}
			if (minalloc < 0x800 || minalloc > (0x800 << 4))
			{
				fprintf (stderr, "%s: Value for -r must be between 1 and 0.\n", argv[0]);
				return 1;
			}
			break;
		case 'w':
			if (rawcopy)
			{
				fprintf (stderr, "%s: MFS structure type can not be specified with raw block copy\n", argv[0]);
				return 1;
			}
			restorebits = strtoul (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -w.\n", argv[0]);
				return 1;
			}
			if (restorebits != 32 && restorebits != 64)
			{
				fprintf (stderr, "%s: Value for -w must be 32 or 64\n", argv[0]);
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
		case 'i':
			bflags |= BF_BACKUPALL;
			break;
		case 'k':
			rflags |= RF_BALANCE;
			rflags |= RF_KOPT;
			break;
		case 'c':
			carveA = strtoull (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -c.\n", argv[0]);
				return 1;
			}
			break;
		case 'C':
			carveB = strtoull (optarg, &tmp, 10);
			if (tmp && *tmp)
			{
				fprintf (stderr, "%s: Integer argument expected for -C.\n", argv[0]);
				return 1;
			}
			break;
		case 'D':
			norescheck = 1;
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
			maxdisk = (maxdisk * 1024 * 1024 * 1024 / 512) - 1; //Convert GiB to sectors
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

	if (rawcopy)
	{
		info_b = init_backup_v1 (source_a, source_b, bflags);
	}
	else
	{
		// Shrinking implied with v3, unless -a flag was set.
		if (threshopt != 'a')
			bflags |= BF_SHRINK;
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
		unsigned char buf[BUFSIZE];
		int curcount = 0;
		int nread, nwrit;

		if (threshopt)
			backup_set_thresh (info_b, thresh);
		if (!norescheck)
			backup_set_resource_check(info_b);
		if (skipdb)
			backup_set_skipdb (info_b, skipdb);

		if (varsize)
			restore_set_varsize (info_r, varsize);
		if (dbsize)
			restore_set_dbsize (info_r, dbsize);
		if (swapsize)
			restore_set_swapsize (info_r, swapsize * 1024 * 2);
		if (bswap)
			restore_set_bswap (info_r, bswap);
		if (restorebits)
			restore_set_mfs_type (info_r, restorebits);
		if (minalloc)
			restore_set_minalloc (info_r, minalloc);
		if (maxdisk)
			restore_set_maxdisk (info_r, maxdisk);
		if (maxmedia)
			restore_set_maxmedia (info_r, maxmedia);

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

		if (restore_trydev (info_r, dest_a, dest_b, carveA, carveB) < 0)
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

		fprintf (stderr, "Starting copy\nSize: %" PRId64 " MiB\n", info_r->nsectors / 2048);
		while ((curcount = backup_read (info_b, buf, BUFSIZE)) > 0)
		{
			unsigned int prcnt;
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

				fprintf (stderr, "\rCopying %" PRId64 " of %" PRId64 " MiB (%d.%02d%%)", info_r->cursector / 2048, info_r->nsectors / 2048, prcnt / 100, prcnt % 100);

				if (prcnt > 100 && timedelta > 15)
				{
					unsigned ETA = timedelta * (10000 - prcnt) / prcnt;
					fprintf (stderr, " %" PRId64 " MiB/sec (ETA %d:%02d:%02d)", info_r->cursector / timedelta / 2048, ETA / 3600, ETA / 60 % 60, ETA % 60);
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
			{
		unsigned tot = time(NULL) - starttime;
		fprintf (stderr, "Copy done! (%d:%02d:%02d)\n", tot / 3600, tot / 60 % 60, tot % 60);
	}

	return 0;
}
