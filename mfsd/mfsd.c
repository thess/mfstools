#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include "mfs.h"
#include "log.h"

static char *progname;

static struct mfs_handle *mfs;

static void
usage ()
{
	fprintf (stderr, "Usage:\n%s [-i inode] [-f fsid] [-s sector] [-c count] [-h] [-b] /dev/hda [/dev/hdb]\n", progname);
	fprintf (stderr, "    -f	Read from fsid\n");
	fprintf (stderr, "    -i	Read from inode\n");
	fprintf (stderr, "    -l	Read from transaction log\n");
	fprintf (stderr, "    -s	Read from sector, or from offset into file\n");
	fprintf (stderr, "    -c	Read count sectors, where applicable\n");
	fprintf (stderr, "    -C    Perform consistency checkpoint before displaying data\n");
	fprintf (stderr, "    -h	Display in hex, no matter the format\n");
	fprintf (stderr, "    -b	Display in binary, no matter the format\n");
}

static void
hexdump (unsigned char *buf, unsigned int sector, unsigned int size)
{
	int ofs;

	for (ofs = 0; ofs < 512 && size > 0; ofs += 16)
	{
		unsigned char line[20];
		int myo;

		if (sector == 0xdeadbeef)
		{
			printf ("%03x ", ofs);
		}
		else
		{
			printf ("%08x:%03x ", sector, ofs);
		}

		for (myo = 0; myo < 16; myo++)
		{
			if (size--)
			{
				printf ("%02x%c", buf[myo + ofs], myo < 15 && (myo & 3) == 3 ? '-' : ' ');
				line[myo] = (isprint (buf[myo + ofs]) ? buf[myo + ofs] : '.');
			}
			else
			{
				line[myo] = '|';
				line[myo + 1] = '\n';
				line[myo + 2] = 0;
				do
				{
					printf ("  %c", myo < 15 && (myo & 3) == 3 ? '-' : ' ');
				}
				while (++myo < 16);
				printf ("|%s", line);
				return;
			}
		}

		printf ("|");
		line[16] = '|';
		line[17] = '\n';
		line[18] = 0;
		printf ("%s", line);
	}
}

int
dump_inode_log (log_inode_update *entry)
{
	unsigned char date[17] = "xx/xx/xx xx:xx";
	time_t modtime;

	printf ("Inode: %-13dFSid: %d\n", intswap32 (entry->inode), intswap32 (entry->fsid));
	printf ("Refcount: %-10dType: ", intswap32 (entry->refcount));

	switch (entry->type)
	{
	case tyDir:
		printf ("tyDir\n");
		break;
	case tyDb:
		printf ("tyDb\n");
		break;
	case tyStream:
		printf ("tyStream\n");
		break;
	case tyFile:
		printf ("tyFile\n");
		break;
	default:
		printf ("??? (%d)\n", entry->type);
	}

	modtime = intswap32 (entry->lastmodified);
	strftime (date, 16, "%D %R", localtime (&modtime));
	printf ("Last modified: %s\n", date);

	printf ("Last update boot: %-15dSecs: %d\n", intswap32 (entry->bootcycles), intswap32 (entry->bootsecs));
	if (entry->type == tyStream)
	{
		printf ("Size: %d blocks of %d bytes (%llu)\n", intswap32 (entry->size), intswap32 (entry->unk3), (unsigned long long) intswap32 (entry->unk3) * (unsigned long long) intswap32 (entry->size));
		printf ("Used: %d blocks of %d bytes (%llu)\n", intswap32 (entry->blockused), intswap32 (entry->blocksize), (unsigned long long) intswap32 (entry->blockused) * (unsigned long long) intswap32 (entry->blocksize));
	}
	else
	{
		printf ("Size: %d bytes\n", intswap32 (entry->size));
	}
	if (entry->inodedata && entry->inodedata != intswap32 (1))
		printf ("Data in inode: %d\n", intswap32 (entry->inodedata));
	if (!entry->inodedata)
	{
		int loop;
		printf ("Data is in %d blocks:\n", intswap32 (entry->datasize) / sizeof (entry->datablocks[0]));
		for (loop = 0; loop < intswap32 (entry->datasize) / sizeof (entry->datablocks[0]); loop++)
		{
			printf ("At %-8x %d sectors\n", intswap32 (entry->datablocks[loop].sector), intswap32 (entry->datablocks[loop].count));
		}
	}
	else
	{
		printf ("Data is in inode block.\n");
		hexdump ((void *)&entry->datablocks[0], 0, intswap32 (entry->datasize));
	}

	return 1;
}

int
dump_inode (mfs_inode *inode_buf, unsigned char *buf, unsigned int bufsize)
{
	unsigned char date[17] = "xx/xx/xx xx:xx";
	time_t modtime;

	if (!inode_buf && bufsize >= 512)
	{
		// If it wasn't read as an inode, check if it looks like one
		inode_buf = (mfs_inode *)buf;
		if (inode_buf->sig != intswap32(0x91231ebc) ||
			!MFS_check_crc (inode_buf, 512, inode_buf->checksum))
			return 0;
	}

	do
	{
		printf("\n    Inode block\n");
		printf ("Inode: %-13dFSid: %d\n", intswap32 (inode_buf->inode), intswap32 (inode_buf->fsid));
		printf ("Refcount: %-10dType: ", intswap32 (inode_buf->refcount));

		switch (inode_buf->type)
		{
		case tyDir:
			printf ("tyDir\n");
			break;
		case tyDb:
			printf ("tyDb\n");
			break;
		case tyStream:
			printf ("tyStream\n");
			break;
		case tyFile:
			printf ("tyFile\n");
			break;
		default:
			printf ("??? (%d)\n", inode_buf->type);
		}

		modtime = intswap32 (inode_buf->lastmodified);
		strftime (date, 16, "%D %R", localtime (&modtime));
		printf ("Last modified: %s\n", date);

		printf ("Last update boot: %-15dSecs: %d\n", intswap32 (inode_buf->bootcycles), intswap32 (inode_buf->bootsecs));
		if (inode_buf->type == tyStream)
		{
			printf ("Size: %d blocks of %d bytes (%llu)\n", intswap32 (inode_buf->size), intswap32 (inode_buf->unk3), (unsigned long long) intswap32 (inode_buf->unk3) * (unsigned long long) intswap32 (inode_buf->size));
			printf ("Used: %d blocks of %d bytes (%llu)\n", intswap32 (inode_buf->blockused), intswap32 (inode_buf->blocksize), (unsigned long long) intswap32 (inode_buf->blockused) * (unsigned long long) intswap32 (inode_buf->blocksize));
		}
		else
		{
			printf ("Size: %d bytes\n", intswap32 (inode_buf->size));
		}
		printf ("Checksum: %08x  Flags:", inode_buf->checksum);
		if (inode_buf->inode_flags & intswap32(INODE_CHAINED))
		{
			printf (" CHAINED");
		}
		if (inode_buf->inode_flags & intswap32(INODE_DATA))
		{
			printf (" DATA");
		}
		if (inode_buf->inode_flags & intswap32(~(INODE_DATA | INODE_CHAINED)))
		{
			printf (" ? (%08x)\n", intswap32(inode_buf->inode_flags));
		}
		else
		{
			printf ("\n");
		}
		printf ("Sigs: %04x %08x (Always beef 91231ebc?)\n", intswap16 (inode_buf->pad), intswap32 (inode_buf->sig));
		if (intswap32 (inode_buf->numblocks))
		{
			int loop;
			printf ("Data is in %d blocks:\n", intswap32 (inode_buf->numblocks));
			for (loop = 0; loop < intswap32 (inode_buf->numblocks); loop++)
			{
				printf ("At %-8x %d sectors\n", intswap32 (inode_buf->datablocks[loop].sector), intswap32 (inode_buf->datablocks[loop].count));
			}
		}
		else
		{
			printf ("Data is in inode block.\n");
		}
		
		buf += 1024;
		bufsize -= 1024;
		inode_buf = (mfs_inode *)buf;
	}
	while (bufsize > 512 && inode_buf->sig == intswap32(0x91231ebc) &&
		MFS_check_crc (inode_buf, 512, inode_buf->checksum));

	return 1;
}

int
dump_mfs_header (unsigned char *buf, unsigned int bufsize)
{
	volume_header *hdr;

	if (bufsize < sizeof (volume_header))
		return 0;

	hdr = (volume_header *)buf;

	if (hdr->abbafeed != intswap32 (0xabbafeed) ||
		!MFS_check_crc (hdr, sizeof (volume_header), hdr->checksum))
		return 0;

	printf ("\n    MFS Volume Header\n");
	printf ("Sig: %08x   CRC: %08x   Size: %d\n", intswap32 (hdr->abbafeed), intswap32 (hdr->checksum), intswap32 (hdr->total_sectors));
	printf ("MFS Partitions: %s\n", hdr->partitionlist);
	printf ("Root FSID: %-13dNext FSID: %d\n", intswap32 (hdr->root_fsid), intswap32 (hdr->next_fsid));
	printf ("Redo log start: %-13dSize: %d\n", intswap32 (hdr->logstart), intswap32 (hdr->lognsectors));
	printf ("?        start: %-13dSize: %d\n", intswap32 (hdr->unkstart), intswap32 (hdr->unksectors));
	printf ("Zone map start: %-13dSize: %d\n", intswap32 (hdr->zonemap.sector), intswap32 (hdr->zonemap.length));
	printf ("        backup: %-13dZone size: %-13dAllocation size: %d\n", intswap32 (hdr->zonemap.sbackup), intswap32 (hdr->zonemap.size), intswap32 (hdr->zonemap.min));
	printf ("Last sync boot: %-13dTimestamp: %-13dLast Commit: %d\n", intswap32 (hdr->bootcycles), intswap32 (hdr->bootsecs), intswap32 (hdr->logstamp));

	if (hdr->off00 || hdr->off0c || hdr->off14 || hdr->off18 || hdr->off1c || hdr->off20 || hdr->offa8 || hdr->offc0 || hdr->offe4)
	{
		printf ("Unknown data\n");
		if (hdr->off00)
		{
			printf ("00000000:000 %02x %02x %02x %02x\n", buf[0], buf[1], buf[2], buf[3]);
		}
		if (hdr->off0c)
		{
			printf ("00000000:00c %02x %02x %02x %02x\n", buf[12], buf[13], buf[14], buf[15]);
		}
		if (hdr->off14 || hdr->off18)
		{
			printf ("00000000:014 %02x %02x %02x %02x %02x %02x %02x %02x\n", buf[20], buf[21], buf[22], buf[23], buf[24], buf[25], buf[26], buf[27]);
		}
		if (hdr->off1c || hdr->off20)
		{
			printf ("00000000:01c %02x %02x %02x %02x %02x %02x %02x %02x\n", buf[28], buf[29], buf[30], buf[31], buf[32], buf[33], buf[34], buf[35]);
		}
		if (hdr->offa8)
		{
			printf ("00000000:0a8 %02x %02x %02x %02x\n", buf[168], buf[169], buf[170], buf[171]);
		}
		if (hdr->offc0)
		{
			printf ("00000000:0c0 %02x %02x %02x %02x\n", buf[192], buf[193], buf[194], buf[195]);
		}
		if (hdr->offe4)
		{
			printf ("00000000:0e4 %02x %02x %02x %02x\n", buf[228], buf[229], buf[230], buf[231]);
		}
	}

	return 1;
}

int
dump_zone_map (unsigned int sector, unsigned char *buf, unsigned int bufsize)
{
	zone_header *zone;
	unsigned long *ptrs;
	bitmap_header *bitmap0;
	int loop;

	if (bufsize < sizeof (zone_header))
		return 0;

	zone = (zone_header *)buf;

	if (sector != intswap32 (zone->sector) && sector != intswap32 (zone->sbackup) || intswap32 (zone->length) * 512 < bufsize || !MFS_check_crc (zone, intswap32 (zone->length) * 512, zone->checksum))
		return 0;

	printf ("\n    Zone map ");
	switch (intswap32 (zone->type))
	{
		case ztInode:
			printf ("(Inode)\n");
			break;
		case ztApplication:
			printf ("(Application)\n");
			break;
		case ztMedia:
			printf ("(Media)\n");
			break;
		default:
			printf ("(Unknown type %d)\n", intswap32 (zone->type));
	}
	printf ("Sector: %-13dBackup: %-13dLength: %d\n", intswap32 (zone->sector), intswap32 (zone->sbackup), intswap32 (zone->length));
	printf ("Next:   %-13dBackup: %-13dLength: %d\n", intswap32 (zone->next.sector), intswap32 (zone->next.sbackup), intswap32 (zone->next.length));
	printf ("Next zone size: %-13dAllocation size: %d\n", intswap32 (zone->next.size), intswap32 (zone->next.min));
	printf ("CRC: %08x     Logstamp: %d\n", intswap32 (zone->checksum), intswap32 (zone->logstamp));
	printf ("Zone sectors: %d-%-13dAllocation size: %d\n", intswap32 (zone->first), intswap32 (zone->last), intswap32 (zone->min));
	printf ("Zone size: %-13dFree: %d\n", intswap32 (zone->size), intswap32 (zone->free));
	if (zone->zero)
		printf ("???: %08x\n", intswap32 (zone->zero));

	printf ("Bitmaps: %d\n", intswap32 (zone->num));

	ptrs = (unsigned long *)(buf + sizeof (*zone));
	bitmap0 = (bitmap_header *)&ptrs[intswap32 (zone->num)];

	for (loop = 0; loop < intswap32 (zone->num); loop++)
	{
		bitmap_header *bitmap = (bitmap_header *)((unsigned long)bitmap0 + (intswap32 (ptrs[loop]) - intswap32 (ptrs[0])));
		printf ("    Bitmap addr: %08x  Start: %08x:%03x\n", intswap32 (ptrs[loop]), ((unsigned long)(bitmap + 1) - (unsigned long)buf) / 512 + sector, ((unsigned long)(bitmap + 1) - (unsigned long)buf) % 512);
		printf ("          Nbits: %-10dNints: %-10dFree: %-10dLast: %d\n", intswap32 (bitmap->nbits), intswap32 (bitmap->nints), intswap32 (bitmap->freeblocks), intswap32 (bitmap->last));
	}

	return 1;
}

int
dump_log_entry (unsigned int sector, unsigned char *buf, unsigned int bufsize)
{
	log_hdr *hdr;
	unsigned int off;
	unsigned int hdroff;

	if (bufsize < 512)
		return 0;

	hdr = (log_hdr *)buf;

	if ((sector != 0xdeadbeef && sector != (intswap32 (hdr->logstamp) % intswap32 (mfs->vol_hdr.lognsectors)) + intswap32 (mfs->vol_hdr.logstart)) || !MFS_check_crc(buf, 512, hdr->crc))
		return 0;

	printf ("\n    Log entry stamp %d\n", intswap32 (hdr->logstamp));
	printf ("Size: %-13dFirst: %-13dCRC: %08x\n", intswap32 (hdr->size), intswap32 (hdr->first), intswap32 (hdr->crc));

	off = intswap32 (hdr->first);
	hdroff = 0;
	while (off < bufsize && off < intswap32 (hdr->size) || hdroff + 512 <= bufsize)
	{
		unsigned char *allocated = NULL;
		unsigned int allocwritten = 0;
		log_entry_all *entry;

		if (off >= intswap32 (hdr->size))
		{
			unsigned int oldlogstamp = intswap32 (hdr->logstamp);

			hdroff += 512;
			off = 0;
			hdr = (log_hdr *)(buf + hdroff);
			if (hdroff >= bufsize || oldlogstamp + 1 != intswap32 (hdr->logstamp) || !MFS_check_crc(buf + hdroff, 512, hdr->crc))
				return 1;

			printf ("\n    Log entry stamp %d\n", intswap32 (hdr->logstamp));
			printf ("Size: %-13dFirst: %-13dCRC: %08x\n", intswap32 (hdr->size), intswap32 (hdr->first), intswap32 (hdr->crc));

			continue;
		}

		entry = (log_entry_all *)(buf + off + hdroff + sizeof (log_hdr));

		if (entry->log.length == 0)
		{
			off += 2;
			continue;
		}

		// Entry extends into the next log sector
		while (off + intswap16 (entry->log.length) + 2 - allocwritten > intswap32 (hdr->size))
		{
			unsigned int oldlogstamp = intswap32 (hdr->logstamp);

			if (!allocated)
			{
				allocated = malloc (intswap16 (entry->log.length) + 2);
				allocwritten = 0;
				entry = (log_entry_all *)allocated;
			}
			memcpy (allocated + allocwritten, buf + hdroff + off + sizeof (log_hdr), intswap32 (hdr->size) - off);
			allocwritten += intswap32 (hdr->size) - off;

			hdroff += 512;
			off = 0;

			hdr = (log_hdr *)(buf + hdroff);
			if (hdroff >= bufsize || oldlogstamp + 1 != intswap32 (hdr->logstamp) || !MFS_check_crc(buf + hdroff, 512, hdr->crc))
			{
				printf("... Continued in next log entry\n");
				free (allocated);
				return 1;
			}

			printf ("\n    Continued in log entry stamp %d\n", intswap32 (hdr->logstamp));
			printf ("Size: %-13dFirst: %-13dCRC: %08x\n", intswap32 (hdr->size), intswap32 (hdr->first), intswap32 (hdr->crc));

			continue;
		}

		if (allocated)
		{
			memcpy (allocated + allocwritten, buf + hdroff + off + sizeof (log_hdr), intswap16 (entry->log.length) + 2 - allocwritten);
			off += intswap16 (entry->log.length) + 2 - allocwritten;
		}
		else
			off += intswap16 (entry->log.length) + 2;

		printf ("\nLog entry length: %-13dType: ", intswap16 (entry->log.length));
		switch (intswap32 (entry->log.transtype))
		{
			case ltMapUpdate:
				printf ("Zone Map Update\n");
				break;
			case ltInodeUpdate:
				printf ("Inode Update\n");
				break;
			case ltInodeUpdate2:
				printf ("Inode Update 2\n");
				break;
			case ltCommit:
				printf ("Log Commit\n");
				break;
			case ltFsSync:
				printf ("FS Sync Complete\n");
				break;
			default:
				printf ("Unknown (%d)\n", intswap32 (entry->log.transtype));
		}

		printf ("Boot: %-13dTimestamp: %d\n", intswap32 (entry->log.bootcycles), intswap32 (entry->log.bootsecs));
		printf ("FSId: %-13d???: %-13d???: %d\n", intswap32 (entry->log.fsid), intswap32 (entry->log.unk1), intswap32 (entry->log.unk2));

		switch (intswap32 (entry->log.transtype))
		{
			case ltMapUpdate:
				printf ("Zone map update:\n");
				if (!entry->zonemap.remove)
					printf ("Change: Allocate     ");
				else if (entry->zonemap.remove == intswap32 (1))
					printf ("Change: Free         ");
				else
					printf ("Change: ?%-12d", intswap32 (entry->zonemap.remove));
				printf ("???: %d\n", intswap32 (entry->zonemap.unk));
				printf ("Sector: %-13dSize: %d\n", intswap32 (entry->zonemap.sector), intswap32 (entry->zonemap.size));
				break;
			case ltInodeUpdate:
			case ltInodeUpdate2:
				printf ("Inode update:\n");
				dump_inode_log (&entry->inode);
				break;
		}

		if (allocated)
			free (allocated);
	}
	return 1;
}

int
mfsd_main (int argc, char **argv)
{
	int curarg;
	unsigned char *buf = NULL;

	unsigned int sector = 0xdeadbeef, count = 1;
	unsigned int fsid = 0;
	unsigned int inode = 0xdeadbeef;
	unsigned int bufsize = 0;
	unsigned int logstamp = 0xdeadbeef;
	int dofssync = 0;

	mfs_inode *inode_buf = NULL;

	enum
	{ None, Hex, Bin }
	format = None;

	progname = argv[0];

	while ((curarg = getopt (argc, argv, "bhc:s:i:f:l:C")) >= 0)
	{
		switch (curarg)
		{
		case 'b':
			format = Bin;
			break;
		case 'h':
			format = Hex;
			break;
		case 's':
			sector = strtoul (optarg, 0, 0);
			break;
		case 'l':
			logstamp = strtoul (optarg, 0, 0);
			break;
		case 'c':
			count = strtoul (optarg, 0, 0);
			break;
		case 'f':
			if (inode != 0xdeadbeef)
			{
				fprintf (stderr, "Only 1 of -f and -i may be used.\n");
				usage ();
				return 1;
			}
			fsid = strtoul (optarg, 0, 0);
			break;
		case 'i':
			if (fsid)
			{
				fprintf (stderr, "Only 1 of -f and -i may be used.\n");
				usage ();
				return 2;
			}
			inode = strtoul (optarg, 0, 0);
			break;
		case 'C':
			dofssync = 1;
			break;
		default:
			usage ();
			return 3;
		}
	}

	if (sector == 0xdeadbeef && !fsid && inode == 0xdeadbeef && logstamp == 0xdeadbeef || optind == argc || optind >= argc + 2 || logstamp != 0xdeadbeef && (fsid || inode != 0xdeadbeef || sector != 0xdeadbeef))
	{
		usage ();
		return 4;
	}

	mfs = mfs_init (argv[optind], optind + 1 < argc? argv[optind + 1] : NULL, O_RDONLY);

	if (mfs_has_error (mfs))
	{
		mfs_perror (mfs, argv[0]);
		return 1;
	}

	if (dofssync)
	{
		mfs_enable_memwrite (mfs);
		if (mfs_log_fssync (mfs) < 1)
		{
			mfs_perror (mfs, "fssync");
		}
	}

	if (fsid)
	{
		inode_buf = mfs_read_inode_by_fsid (mfs, fsid);
		if (!inode_buf)
		{
			mfs_perror (mfs, "Read fsid");
			return 1;
		}
		bufsize = sizeof (*inode_buf) + intswap32 (inode_buf->numblocks) * 8;
		buf = (unsigned char *) inode_buf;
	}
	else if (inode != 0xdeadbeef)
	{
		inode_buf = mfs_read_inode (mfs, inode);
		if (!inode_buf)
		{
			mfs_perror (mfs, "Read inode");
			return 1;
		}
		bufsize = sizeof (*inode_buf) + intswap32 (inode_buf->numblocks) * 8;
		buf = (unsigned char *) inode_buf;
	}

	if (sector != 0xdeadbeef)
	{
		buf = calloc (count, 512);

		if (!buf)
		{
			fprintf (stderr, "%s: Couldn't allocate %d bytes!\n", progname, count * 512);
			return 1;
		}

		if (inode_buf)
		{
			int nread = mfs_read_inode_data_part (mfs, inode_buf, buf, sector, count);
			if (nread <= 0)
			{
				mfs_perror (mfs, "Read data");
				return 1;
			}
			bufsize = nread;
		}
		else
		{
			int nread = mfs_read_data (mfs, buf, sector, count);

			if (nread <= 0)
			{
				mfs_perror (mfs, "Read data");
				return 1;
			}

			bufsize = nread;
		}
	}
	else if (logstamp != 0xdeadbeef)
	{
		int loop;

		buf = calloc (count, 512);
		bufsize = 0;

		for (loop = 0; loop < count; loop++)
		{
			int nread = mfs_log_read (mfs, buf + bufsize, logstamp + loop);

			if (nread < 0)
			{
				mfs_perror (mfs, "Read log");
				return 1;
			}

			if (nread == 0)
			{
				if (bufsize == 0)
				{
					fprintf (stderr, "Log entry not found\n");
					return 1;
				}

				break;
			}

			bufsize += nread;
		}
	}

	if (format == Bin)
	{
		write (1, buf, bufsize);
	}
	else if (format == Hex || (inode_buf && sector != 0xdeadbeef))
	{
		int offset = 0;
		while (offset * 512 < bufsize)
		{
			hexdump (buf + offset * 512, sector == 0xdeadbeef ? sector : offset + sector, bufsize - offset * 512);
			offset++;
		}
	}
	else
	{
		// Provide a context to break from
		// No, it's not a goto, it's just nicer looking than a long chained if
		do
		{
			if (format != Hex && (!inode_buf || sector == 0xdeadbeef))
			{
				// Try and infer the format of the data
				if (dump_inode (inode_buf, buf, bufsize))
					break;
				if (dump_mfs_header (buf, bufsize))
					break;
				if (dump_zone_map (sector, buf, bufsize))
					break;
				if (dump_log_entry (sector, buf, bufsize))
					break;
			}

			// No known format, just hexdump it
			int offset = 0;
			while (offset * 512 < bufsize)
			{
				hexdump (buf + offset * 512, sector == 0xdeadbeef ? sector : offset + sector, bufsize - offset * 512);
				offset++;
			}
		} while (0);
	}
	return 0;
}
