#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
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
	printf ("Inode: %-13dFSid: %d\n", htonl (entry->inode), htonl (entry->fsid));
	printf ("Refcount: %-10dType: ", htonl (entry->refcount));

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

	printf ("Last update boot: %-15dSecs: %d\n", htonl (entry->bootcycles), htonl (entry->bootsecs));
	if (entry->type == tyStream)
	{
		printf ("Size: %d blocks of %d bytes (%llu)\n", htonl (entry->size), htonl (entry->unk3), (unsigned long long) htonl (entry->unk3) * (unsigned long long) htonl (entry->size));
		printf ("Used: %d blocks of %d bytes (%llu)\n", htonl (entry->blockused), htonl (entry->blocksize), (unsigned long long) htonl (entry->blockused) * (unsigned long long) htonl (entry->blocksize));
	}
	else
	{
		printf ("Size: %d bytes\n", htonl (entry->size));
	}
	printf ("Data in inode: %d\n", htonl (entry->inodedata));
	if (!entry->inodedata)
	{
		int loop;
		printf ("Data is in %d blocks:\n", htonl (entry->datasize) / sizeof (entry->datablocks[0]));
		for (loop = 0; loop < htonl (entry->datasize) / sizeof (entry->datablocks[0]); loop++)
		{
			printf ("At %-8x %d sectors\n", htonl (entry->datablocks[loop].sector), htonl (entry->datablocks[loop].count));
		}
	}
	else
	{
		printf ("Data is in inode block.\n");
		hexdump ((void *)&entry->datablocks[0], 0, htonl (entry->datasize));
	}

	return 1;
}

int
dump_inode (mfs_inode *inode_buf, unsigned char *buf, unsigned int bufsize)
{
	if (!inode_buf && bufsize >= 512)
	{
		// If it wasn't read as an inode, check if it looks like one
		inode_buf = (mfs_inode *)buf;
		if (inode_buf->pad != htons(0xbeef) ||
			inode_buf->sig != htonl(0x91231ebc) ||
			!MFS_check_crc (inode_buf, 512, inode_buf->checksum))
			return 0;
	}

	printf("\n    Inode block\n");
	printf ("Inode: %-13dFSid: %d\n", htonl (inode_buf->inode), htonl (inode_buf->fsid));
	printf ("Refcount: %-10dType: ", htonl (inode_buf->refcount));

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

	printf ("Last update boot: %-15dSecs: %d\n", htonl (inode_buf->bootcycles), htonl (inode_buf->bootsecs));
	if (inode_buf->type == tyStream)
	{
		printf ("Size: %d blocks of %d bytes (%llu)\n", htonl (inode_buf->size), htonl (inode_buf->unk3), (unsigned long long) htonl (inode_buf->unk3) * (unsigned long long) htonl (inode_buf->size));
		printf ("Used: %d blocks of %d bytes (%llu)\n", htonl (inode_buf->blockused), htonl (inode_buf->blocksize), (unsigned long long) htonl (inode_buf->blockused) * (unsigned long long) htonl (inode_buf->blocksize));
	}
	else
	{
		printf ("Size: %d bytes\n", htonl (inode_buf->size));
	}
	printf ("Checksum: %08x  Flags:", inode_buf->checksum);
	if (inode_buf->inode_flags & htonl(INODE_CHAINED))
	{
		printf (" CHAINED");
	}
	if (inode_buf->inode_flags & htonl(INODE_DATA))
	{
		printf (" DATA");
	}
	if (inode_buf->inode_flags & htonl(~(INODE_DATA | INODE_CHAINED)))
	{
		printf (" ? (%08x)\n", htonl(inode_buf->inode_flags));
	}
	else
	{
		printf ("\n");
	}
	printf ("Sigs: %04x %08x (Always beef 91231ebc?)\n", htons (inode_buf->pad), htonl (inode_buf->sig));
	if (htonl (inode_buf->numblocks))
	{
		int loop;
		printf ("Data is in %d blocks:\n", htonl (inode_buf->numblocks));
		for (loop = 0; loop < htonl (inode_buf->numblocks); loop++)
		{
			printf ("At %-8x %d sectors\n", htonl (inode_buf->datablocks[loop].sector), htonl (inode_buf->datablocks[loop].count));
		}
	}
	else
	{
		printf ("Data is in inode block.\n");
	}

	return 1;
}

int
dump_mfs_header (unsigned char *buf, unsigned int bufsize)
{
	volume_header *hdr;

	if (bufsize < sizeof (volume_header))
		return 0;

	hdr = (volume_header *)buf;

	if (hdr->abbafeed != htonl (0xabbafeed) ||
		!MFS_check_crc (hdr, sizeof (volume_header), hdr->checksum))
		return 0;

	printf ("\n    MFS Volume Header\n");
	printf ("Sig: %08x   CRC: %08x   Size: %d\n", htonl (hdr->abbafeed), htonl (hdr->checksum), htonl (hdr->total_sectors));
	printf ("MFS Partitions: %s\n", hdr->partitionlist);
	printf ("Root FSID: %-13dLog stamp: %d\n", htonl (hdr->root_fsid), htonl (hdr->logstamp));
	printf ("Redo log start: %-13dSize: %d\n", htonl (hdr->logstart), htonl (hdr->lognsectors));
	printf ("?        start: %-13dSize: %d\n", htonl (hdr->unkstart), htonl (hdr->unksectors));
	printf ("Zone map start: %-13dSize: %d\n", htonl (hdr->zonemap.sector), htonl (hdr->zonemap.length));
	printf ("        backup: %-13dZone size: %-13dAllocation size: %d\n", htonl (hdr->zonemap.sbackup), htonl (hdr->zonemap.size), htonl (hdr->zonemap.min));
	printf ("Last sync boot: %-13dTimestamp: %d\n", htonl (hdr->bootcycles), htonl (hdr->bootsecs));

	if (hdr->off00 || hdr->off0c || hdr->off14 || hdr->off18 || hdr->off1c || hdr->off20 || hdr->offa8 || hdr->offc0 || hdr->offd8)
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
		if (hdr->offd8)
		{
			printf ("00000000:0d8 %02x %02x %02x %02x\n", buf[216], buf[217], buf[218], buf[219]);
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

	if (sector != htonl (zone->sector) && sector != htonl (zone->sbackup) || htonl (zone->length) * 512 < bufsize || !MFS_check_crc (zone, htonl (zone->length) * 512, zone->checksum))
		return 0;

	printf ("\n    Zone map ");
	switch (htonl (zone->type))
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
			printf ("(Unknown type %d)\n", htonl (zone->type));
	}
	printf ("Sector: %-13dBackup: %-13dLength: %d\n", htonl (zone->sector), htonl (zone->sbackup), htonl (zone->length));
	printf ("Next:   %-13dBackup: %-13dLength: %d\n", htonl (zone->next.sector), htonl (zone->next.sbackup), htonl (zone->next.length));
	printf ("Next zone size: %-13dAllocation size: %d\n", htonl (zone->next.size), htonl (zone->next.min));
	printf ("CRC: %08x     Logstamp: %d\n", htonl (zone->checksum), htonl (zone->logstamp));
	printf ("Zone sectors: %d-%-13dAllocation size: %d\n", htonl (zone->first), htonl (zone->last), htonl (zone->min));
	printf ("Zone size: %-13dFree: %d\n", htonl (zone->size), htonl (zone->free));
	if (zone->zero)
		printf ("???: %08x\n", htonl (zone->zero));

	printf ("Bitmaps: %d\n", htonl (zone->num));

	ptrs = (unsigned long *)(buf + sizeof (*zone));
	bitmap0 = (bitmap_header *)&ptrs[htonl (zone->num)];

	for (loop = 0; loop < htonl (zone->num); loop++)
	{
		bitmap_header *bitmap = (bitmap_header *)((unsigned long)bitmap0 + (htonl (ptrs[loop]) - htonl (ptrs[0])));
		printf ("    Bitmap addr: %08x  Start: %08x:%03x\n", htonl (ptrs[loop]), ((unsigned long)(bitmap + 1) - (unsigned long)buf) / 512 + sector, ((unsigned long)(bitmap + 1) - (unsigned long)buf) % 512);
		printf ("          Nbits: %-10dNints: %-10dFree: %-10dLast: %d\n", htonl (bitmap->nbits), htonl (bitmap->nints), htonl (bitmap->freeblocks), htonl (bitmap->last));
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

	if ((sector != 0xdeadbeef && sector != (htonl (hdr->logstamp) % htonl (mfs->vol_hdr.lognsectors)) + htonl (mfs->vol_hdr.logstart)) || !MFS_check_crc(buf, 512, hdr->crc))
		return 0;

	printf ("\n    Log entry stamp %d\n", htonl (hdr->logstamp));
	printf ("Size: %-13dFirst: %-13dCRC: %08x\n", htonl (hdr->size), htonl (hdr->first), htonl (hdr->crc));

	off = htonl (hdr->first);
	hdroff = 0;
	while (off < bufsize && off < htonl (hdr->size) || hdroff + 512 <= bufsize)
	{
		unsigned char *allocated = NULL;
		unsigned int allocwritten = 0;
		log_entry_all *entry;

		if (off >= htonl (hdr->size))
		{
			unsigned int oldlogstamp = htonl (hdr->logstamp);

			hdroff += 512;
			off = 0;
			hdr = (log_hdr *)(buf + hdroff);
			if (hdroff >= bufsize || oldlogstamp + 1 != htonl (hdr->logstamp) || !MFS_check_crc(buf + hdroff, 512, hdr->crc))
				return 1;

			printf ("\n    Log entry stamp %d\n", htonl (hdr->logstamp));
			printf ("Size: %-13dFirst: %-13dCRC: %08x\n", htonl (hdr->size), htonl (hdr->first), htonl (hdr->crc));

			continue;
		}

		entry = (log_entry_all *)(buf + off + hdroff + sizeof (log_hdr));

		if (entry->log.length == 0)
		{
			off += 2;
			continue;
		}

		// Entry extends into the next log sector
		while (off + htons (entry->log.length) + 2 - allocwritten > htonl (hdr->size))
		{
			unsigned int oldlogstamp = htonl (hdr->logstamp);

			if (!allocated)
			{
				allocated = malloc (htons (entry->log.length) + 2);
				allocwritten = 0;
				entry = (log_entry_all *)allocated;
			}
			memcpy (allocated + allocwritten, buf + hdroff + off + sizeof (log_hdr), htonl (hdr->size) - off);
			allocwritten += htonl (hdr->size) - off;

			hdroff += 512;
			off = 0;

			hdr = (log_hdr *)(buf + hdroff);
			if (hdroff >= bufsize || oldlogstamp + 1 != htonl (hdr->logstamp) || !MFS_check_crc(buf + hdroff, 512, hdr->crc))
			{
				printf("... Continued in next log entry\n");
				free (allocated);
				return 1;
			}

			printf ("\n    Continued in log entry stamp %d\n", htonl (hdr->logstamp));
			printf ("Size: %-13dFirst: %-13dCRC: %08x\n", htonl (hdr->size), htonl (hdr->first), htonl (hdr->crc));

			continue;
		}

		if (allocated)
		{
			memcpy (allocated + allocwritten, buf + hdroff + off + sizeof (log_hdr), htons (entry->log.length) + 2 - allocwritten);
			off += htons (entry->log.length) + 2 - allocwritten;
		}
		else
			off += htons (entry->log.length) + 2;

		printf ("\nLog entry length: %-13dType: ", htons (entry->log.length));
		switch (htonl (entry->log.transtype))
		{
			case ltMapUpdate:
				printf ("Zone Map Update\n");
				break;
			case ltInodeUpdate:
				printf ("iNode Update\n");
				break;
			case ltCommit:
				printf ("Log Commit\n");
				break;
			case ltFsSync:
				printf ("FS Sync Complete\n");
				break;
			default:
				printf ("Unknown (%d)\n", htonl (entry->log.transtype));
		}

		printf ("Boot: %-13dTimestamp: %d\n", htonl (entry->log.bootcycles), htonl (entry->log.bootsecs));
		printf ("FSId: %-13d???: %-13d???: %d\n", htonl (entry->log.fsid), htonl (entry->log.unk1), htonl (entry->log.unk2));

		switch (htonl (entry->log.transtype))
		{
			case ltMapUpdate:
				printf ("Zone map update:\n");
				printf ("  Remove: %-13d???: %d\n", htonl (entry->zonemap.remove), htonl (entry->zonemap.unk));
				printf ("  Sector: %-13dSize: %d\n", htonl (entry->zonemap.sector), htonl (entry->zonemap.size));
				break;
			case ltInodeUpdate:
				printf ("iNode update:\n");
				dump_inode_log (&entry->log);
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

	mfs_inode *inode_buf = NULL;

	enum
	{ None, Hex, Bin }
	format = None;

	progname = argv[0];

	while ((curarg = getopt (argc, argv, "bhc:s:i:f:l:")) >= 0)
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

	if (fsid)
	{
		inode_buf = mfs_read_inode_by_fsid (mfs, fsid);
		if (!inode_buf)
		{
			mfs_perror (mfs, "Read fsid");
			return 1;
		}
		bufsize = sizeof (*inode_buf) + htonl (inode_buf->numblocks) * 8;
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
		bufsize = sizeof (*inode_buf) + htonl (inode_buf->numblocks) * 8;
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
