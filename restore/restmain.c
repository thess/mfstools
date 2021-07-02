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
#ifdef HAVE_ASM_TYPES_H
#include <asm/types.h>
#endif
#include <sys/param.h>
#include <inttypes.h>

#include "mfs.h"
#include "backup.h"
#include "macpart.h"

#define BUFSIZE 512 * 256

/**
 * Tivo device names.
 * Defaults to /dev/hd{a,b}.  For a Premier backup, we'll replace these
 * with /dev/sd{a,b}.
 */
extern char* tivo_devnames[];

void
restore_usage (char *progname)
{
	fprintf (stderr, "%s %s\n", PACKAGE, VERSION);
	fprintf (stderr, "Usage: %s [options] Adrive [Bdrive]\n", progname);
	fprintf (stderr, "Options:\n");
	fprintf (stderr, " -h        Display this help message\n");
	fprintf (stderr, " -i file   Input from file, - for stdin\n");
#if DEPRECATED
	// Optimized layout is now the default.  Probably no reason to allow a non-optimized layout...
	//fprintf (stderr, " -p        Optimize partition layout\n");
	fprintf (stderr, " -P        Do NOT optimize the partition layout\n");
#endif
	fprintf (stderr, " -k        Optimize partition layout with kernels first\n");
	fprintf (stderr, " -r scale  Override v3 media blocksize of 20480 with 2048<<scale (scale=0 to 4)\n");
	fprintf (stderr, " -q        Do not display progress\n");
	fprintf (stderr, " -qq       Do not display anything but error messages\n");
	fprintf (stderr, " -v size   Recreate /var as size MiB (Only if not in backup)\n");
	fprintf (stderr, " -d size   Recreate /db (SQLite in source) as size MiB (if not in backup)\n");
	fprintf (stderr, " -S size   Recreate swap as size MiB\n");
	fprintf (stderr, " -l        Leave at least 2 partitions free\n");
	fprintf (stderr, " -b        Force no byte swapping on restore\n");
	fprintf (stderr, " -B        Force byte swapping on restore\n");
#if DEPRECATED
	// Arguments could be made to keep this, but I suspect few will actually miss it.
	fprintf (stderr, " -z        Zero out partitions not backed up\n");
#endif
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

int
restore_main (int argc, char **argv)
{
	char *drive, *drive2, *tmp;
	struct backup_info *info;
	int opt;
	unsigned int varsize = 0, dbsize = 0, swapsize = 0, rflags = RF_BALANCE;;
	char *filename = 0;
	int quiet = 0;
	int bswap = 0;
	int restorebits = 0;
	int64_t carveA = 0;
	int64_t carveB = 0;
	int64_t maxdisk = 0;
	int64_t maxmedia = 0;
	unsigned int minalloc = 0;
	unsigned starttime = time (NULL);

	tivo_partition_direct ();
#if DEPRECATED
	while ((opt = getopt (argc, argv, "hi:v:S:zqbBPkxlr:w:c:C:d:m:M:")) > 0)
#else
	while ((opt = getopt (argc, argv, "hi:v:S:qbBkxlr:w:c:C:d:m:M:")) > 0)
#endif
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
		case 'd':
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
		case 'P':
			rflags &= ~RF_BALANCE;
			break;
		case 'l':
			rflags |= RF_NOFILL;
			break;
		case 'x':
			restore_usage (argv[0]);
			fprintf (stderr, "\n Deprecated argument -x.  Use mfsadd after restore to expand drive(s).\n");
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
				fprintf (stderr, "%s: Value for -r must be between 0 and 4.\n", argv[0]);
				return 1;
			}
			break;
		case 'w':
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
		case 'k':
			rflags |= RF_BALANCE;
			rflags |= RF_KOPT;
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
		rflags |= RF_SWAPV1;
	}

	info = init_restore (rflags);
	if (restore_has_error (info))
	{
		restore_perror (info, "Restore");
		return 1;
	}

	if (info)
	{
		int fd, nread, nwrit;
		unsigned char buf[BUFSIZE];
		unsigned int cursec = 0, curcount;

		if (varsize)
			restore_set_varsize (info, varsize);
		if (dbsize)
			restore_set_dbsize (info, dbsize);
		if (swapsize)
			restore_set_swapsize (info, swapsize * 1024 * 2);
		if (bswap)
			restore_set_bswap (info, bswap);
		if (restorebits)
			restore_set_mfs_type (info, restorebits);
		if (minalloc)
			restore_set_minalloc (info, minalloc);
		if (maxdisk)
			restore_set_maxdisk (info, maxdisk);
		if (maxmedia)
			restore_set_maxmedia (info, maxmedia);

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

		// MFS is little endian
		if (info->back_flags & BF_MFSLSB)
		{
			mfsLSB=1;
#if DEBUG
			fprintf (stderr, "MFS detected as LSB (Roamio or newer?).  Switching MFS to little endian.\n");
#endif
		}

		// TiVo Partitions are little endian
		if (info->back_flags & BF_PARTLSB)
		{
			partLSB=1;
#if DEBUG
			fprintf (stderr, "TiVo partitions detected as LSB (Roamio or newer?).  Switching partitions to little endian.\n");
#endif
		}
		
		if (swapsize > 128 && !(info->back_flags & BF_NOBSWAP))
			fprintf (stderr, "    ***WARNING***\nUsing version 1 swap signature to get >128MiB swap size, but the backup looks\nlike a series 1.  Stock SERIES 1 TiVo kernels do not support the version 1\nswap signature.  If you are using a stock SERIES 1 TiVo kernel, 128MiB is the\nlargest usable swap size.\n");
		if (restorebits == 64 && !(info->back_flags & BF_64))
			fprintf (stderr, "    ***WARNING***\nConverting MFS structure to 64 bit if very experimental, and will only work on\nSeries 3 based TiVo platforms or later, such as the TiVo HD.\n");

		if (info->back_flags & BF_TRUNCATED)
		{
			fprintf (stderr, "    ***WARNING***\nRestoring from a backup of an incomplete volume.  While the backup is whole,\nit is possible there was some required data missing.  Verify the restore.\n");
		}

		if (restore_trydev (info, drive, drive2, carveA, carveB) < 0)
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

		fprintf (stderr, "Starting restore\nUncompressed backup size: %" PRId64 " MiB\n", info->nsectors / 2048);
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
					fprintf (stderr, "     \rRestoring %" PRId64 " of %" PRId64 " MiB (%d.%02d%%) (%d.%02d%% comp)", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100, compr / 100, compr % 100);
				else
					fprintf (stderr, "     \rRestoring %" PRId64 " of %" PRId64 " MiB (%d.%02d%%)", info->cursector / 2048, info->nsectors / 2048, prcnt / 100, prcnt % 100);

				if (prcnt > 100 && timedelta > 15)
				{
					unsigned ETA = timedelta * (10000 - prcnt) / prcnt;
					fprintf (stderr, " %" PRId64 " MiB/sec (ETA %d:%02d:%02d)", info->cursector / timedelta / 2048, ETA / 3600, ETA / 60 % 60, ETA % 60);
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
		{
		unsigned tot = time(NULL) - starttime;
		fprintf (stderr, "Restore done! (%d:%02d:%02d)\n", tot / 3600, tot / 60 % 60, tot % 60);
		}

	fprintf (stderr, "Revalidating partion table on %s...  ", drive);
	if (revalidate_drive(drive) < 0)
		fprintf (stderr, "Failed!\n");
				else
		fprintf (stderr, "Success!\n");
	if (drive2)
			{
		fprintf (stderr, "Revalidating partion table on %s...  ", drive2);
		if (revalidate_drive(drive2) < 0)
			fprintf (stderr, "Failed!\n");
				else
			fprintf (stderr, "Success!\n");
		}

	fprintf (stderr, "Syncing drives...\n");
	sync();
	sync();

	return 0;
}
