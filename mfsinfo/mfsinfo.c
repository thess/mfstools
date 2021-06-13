#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>
#include "mfs.h"
#include "macpart.h"

void
mfsinfo_usage (char *progname)
{
	fprintf (stderr, "%s %s\n", PACKAGE, VERSION);
	fprintf (stderr, "Usage: %s [options] Adrive [Bdrive]\n", progname);
	fprintf (stderr, "Options:\n");
	fprintf (stderr, " -d        Display extra partition detail\n");
	fprintf (stderr, " -h        Display this help message\n");
}

int
partition_info (struct mfs_handle *mfs, char *drives[])
{
	int count = 0;
	int loop;
	uint64_t offset;
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

	fprintf (stdout, "The MFS volume set contains %d partitions\n", count);
	offset = 0;
	fprintf (stdout, "   Partition       Sectors         Size\n");
	for (loop = 0; loop < count; loop++)
	{
		int d;
		int p;
		uint64_t size;

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
				fprintf (stdout, "  %ss%d  ", drives[d], p);
#else
				fprintf (stdout, "  %s%d  ", drives[d], p);
#endif
			else
				fprintf (stdout, "  %.*s  ", namelens[loop], names[loop]);
		}
		else
			fprintf (stdout, "  %.*s  ", namelens[loop], names[loop]);

		size = mfs_volume_size (mfs, offset);
		fprintf (stdout, "%12" PRIu64 " %12" PRIu64 " MiB\n", size, size / (1024 * 2));
		offset += size;
	}
	fprintf (stdout, "Total MFS sectors: %" PRIu64 "\n", offset);
	fprintf (stdout, "Total MFS volume size: %" PRIu64 " MiB\n", offset / (1024 * 2));
	fprintf (stdout, "Total Inodes: %d\n",mfs_inode_count(mfs) ); 
	fprintf (stdout, "\n");

	return count;
}

int display_partition_map (char *drive)
{
	int i;

  	struct tivo_partition_table *table;
  	table = (struct tivo_partition_table*) tivo_read_partition_table (drive, O_RDONLY);
	fprintf (stdout, "\n---------------------------------------------------------------------\n");
	fprintf (stdout, "partition table for %s\n", drive);
	fprintf (stdout, "---------------------------------------------------------------------\n");
  	fprintf (stdout, "[##]:                     Name  1st Sector     Sectors        Next\n");
	fprintf (stdout, "==================================================================\n");
	for (i=0; i<table->count; i++)
	{
		fprintf (stdout, "[%02i]: %24s %11ju %11ju %11ju\n", i+1, 
			table->partitions[i].name,
			table->partitions[i].start, table->partitions[i].sectors, 
			table->partitions[i].start+table->partitions[i].sectors);
	}
	
	return 0;
}

int
mfsinfo_main (int argc, char **argv)
{
	int opt;
	int ndrives = 0;
	char *drives[3];
	struct mfs_handle *mfs;
	int nparts = 0;
	int partition_detail = 0;

	tivo_partition_direct ();

	while ((opt = getopt (argc, argv, "hd")) > 0)
	{
		switch (opt)
		{
		case 'h':
			mfsinfo_usage (argv[0]);
			return 1;
		case 'd':
			partition_detail = 1;
			break;
		}
	}
	
	while (partition_detail + ndrives + 1 < argc && ndrives < 3)
	{
		drives[ndrives] = argv[partition_detail + ndrives + 1];
		ndrives++;
	}

	if (ndrives == 0 || ndrives > 2)
	{
		mfsinfo_usage (argv[0]);
		return 1;
	}
	mfs = mfs_init (drives[0], ndrives == 2? drives[1]: NULL, (O_RDONLY | MFS_ERROROK));
	if (!mfs)
	{
		fprintf (stderr, "Could not open MFS volume set.\n");
		return 1;
	}

	if (mfs_has_error (mfs))
	{
		mfs_perror (mfs, argv[0]);
		return 1;
	}

	fprintf(stdout,"---------------------------------------------------------------------\n");
	if (mfs->is_64)
	{
		fprintf(stdout,"MFS Volume Header (64-bit)\n");
		fprintf (stdout,"---------------------------------------------------------------------\n");
		fprintf(stdout, "\tstate=%x magic=%x\n\tdevlist=%s\n\tzonemap_ptr=%" PRIu64 " total_secs=%" PRIu64 " next_fsid=%d\n",
						mfsLSB ? intswap32 (mfs->vol_hdr.v64.magicMSB) : intswap32 (mfs->vol_hdr.v64.magicLSB),
						mfsLSB ? intswap32 (mfs->vol_hdr.v64.magicLSB) : intswap32 (mfs->vol_hdr.v64.magicMSB),
						mfs->vol_hdr.v64.partitionlist, intswap64 (mfs->vol_hdr.v64.zonemap.sector), intswap64 (mfs->vol_hdr.v64.total_sectors), intswap32 (mfs->vol_hdr.v64.next_fsid));
	}
	else 
	{
		fprintf(stdout,"MFS Volume Header (32-bit)\n");
		fprintf (stdout,"---------------------------------------------------------------------\n");
		fprintf(stdout, "\tstate=%x magic=%x\n\tdevlist=%s\n\tzonemap_ptr=%u total_secs=%u next_fsid=%d\n",
						mfsLSB ? intswap32 (mfs->vol_hdr.v32.magicMSB) : intswap32 (mfs->vol_hdr.v32.magicLSB),
						mfsLSB ? intswap32 (mfs->vol_hdr.v32.magicLSB) : intswap32 (mfs->vol_hdr.v32.magicMSB),
						mfs->vol_hdr.v32.partitionlist, intswap32 (mfs->vol_hdr.v32.zonemap.sector),intswap32 (mfs->vol_hdr.v32.total_sectors), intswap32 (mfs->vol_hdr.v32.next_fsid));
	}
	
	fprintf (stdout, "\n");
	fprintf (stdout,"---------------------------------------------------------------------\n");
	fprintf (stdout, "MFS volume set for %s%s%s\n", drives[0], ndrives > 1? " and ": "", ndrives > 1? drives[1]: "");
	fprintf (stdout,"---------------------------------------------------------------------\n");

	nparts = partition_info (mfs, drives);

	fprintf (stdout,"---------------------------------------------------------------------\n");
	fprintf (stdout,"Zone Maps \n");
	fprintf (stdout,"---------------------------------------------------------------------\n");
	int loop = 0;
	struct zone_map *cur = NULL; 
	for (cur = mfs->loaded_zones; cur; cur = cur->next_loaded) {
		if (mfs->is_64) 
		{
			fprintf (stdout,"Zone %d: ",loop);
			fprintf (stdout,"type=%u logstamp=%u checksum=%u first=%" PRIu64 " last=%" PRIu64 "\n",
							intswap32 (cur->map->z64.type), intswap32 (cur->map->z64.logstamp), intswap32 (cur->map->z64.checksum), intswap64 (cur->map->z64.first), intswap64 (cur->map->z64.last));
			fprintf (stdout,"\tsector=%" PRIu64 " sbackup=%" PRIu64 " length=%u\n",
							intswap64 (cur->map->z64.sector), intswap64 (cur->map->z64.sbackup), intswap32 (cur->map->z64.length));
			fprintf (stdout,"\tsize=%" PRIu64 " min=%u free=%" PRIu64 " zero=%u num=%u\n",
							intswap64 (cur->map->z64.size), intswap32 (cur->map->z64.min), intswap64 (cur->map->z64.free), intswap32 (cur->map->z64.zero), intswap32 (cur->map->z64.num));
			fprintf (stdout,"\tnext_sector=%" PRIu64 " next_sbackup=%" PRIu64 " next_length=%u\n\tnext_size=%" PRIu64 " next_min=%u\n",
							intswap64 (cur->map->z64.next_sector), cur->map->z64.next_sbackup == 0xaaaaaaaaaaaaaaaa ? 0 : intswap64 (cur->map->z64.next_sbackup), intswap32 (cur->map->z64.next_length), intswap64 (cur->map->z64.next_size), intswap32 (cur->map->z64.next_min));
		}
		else 
		{
			fprintf (stdout,"Zone %d: ",loop);
			fprintf (stdout,"type=%u logstamp=%u checksum=%u first=%u last=%u\n",
							intswap32 (cur->map->z32.type), intswap32 (cur->map->z32.logstamp), intswap32 (cur->map->z32.checksum), intswap32 (cur->map->z32.first), intswap32 (cur->map->z32.last));
			fprintf (stdout,"\tsector=%u sbackup=%u length=%u\n",
							intswap32 (cur->map->z32.sector), intswap32 (cur->map->z32.sbackup), intswap32 (cur->map->z32.length));
			fprintf (stdout,"\tsize=%u min=%u free=%u zero=%u num=%u\n",
							intswap32 (cur->map->z32.size), intswap32 (cur->map->z32.min), intswap32 (cur->map->z32.free), intswap32 (cur->map->z32.zero), intswap32 (cur->map->z32.num));
			fprintf (stdout,"\tnext.sector=%u next.sbackup=%u next.length=%u\n\tnext.size=%u next.min=%u\n",
							intswap32 (cur->map->z32.next.sector), cur->map->z32.next.sbackup == 0xaaaaaaaa ? 0 : intswap32 (cur->map->z32.next.sbackup), intswap32 (cur->map->z32.next.length), intswap32 (cur->map->z32.next.size), intswap32 (cur->map->z32.next.min));
		}
	
		loop++;
	}
	
	if (partition_detail != 0)
	{
		display_partition_map(drives[0]);
		if (ndrives>1) display_partition_map(drives[1]);
	}
	
	fprintf (stdout, "\nEstimated hours in a standalone TiVo: %d\n", mfs_sa_hours_estimate (mfs));
	fprintf (stdout, "This MFS volume may be expanded %d more time%s\n", (12 - nparts) / 2, nparts == 10? "": "s");

	return 0;
}
