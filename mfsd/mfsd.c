#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "mfs.h"
#include "log.h"

static char *progname;

static struct mfs_handle *mfs;

static void
usage ()
{
	fprintf (stderr, "Usage:\n%s [-i inode] [-f fsid] [-t transaction] [-s sector] [-c count] [-h] [-b] /dev/hda [/dev/hdb]\n", progname);
	fprintf (stderr, "    -f	Read from fsid\n");
	fprintf (stderr, "    -i	Read from inode\n");
	fprintf (stderr, "    -t	Read from transaction\n");
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
		else if (sector == 0xffffffff)
		{
			printf ("\t");
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

void
dump_inode (mfs_inode *inode_buf, int indent)
{
	char *itype;

	if (indent)
		printf ("\t");
	printf ("Inode: %-13dFSid: %-14dRefcount: %d\n", htonl (inode_buf->inode), htonl (inode_buf->fsid), htonl (inode_buf->refcount));

	switch (inode_buf->type)
	{
	case tyDir:
		itype = "tyDir";
		break;
	case tyDb:
		itype = "tyDb";
		break;
	case tyStream:
		itype = "tyStream";
		break;
	case tyFile:
		itype = "tyFile";
		break;
	default:
		itype = "ty???";
		break;
	}

	if (indent)
		printf ("\t");
	printf ("Type: %-14sZone: %-14dModified: %d:%d\n", itype, inode_buf->zone, htonl (inode_buf->bootcycles), htonl (inode_buf->bootsecs));
	if (inode_buf->type == tyStream)
	{
		if (indent)
			printf ("\t");
		printf ("Size: %d blocks of %d bytes (%llu)\n", htonl (inode_buf->size), htonl (inode_buf->unk3), (unsigned long long) htonl (inode_buf->unk3) * (unsigned long long) htonl (inode_buf->size));
		if (indent)
			printf ("\t");
		printf ("Used: %d blocks of %d bytes (%llu)\n", htonl (inode_buf->blockused), htonl (inode_buf->blocksize), (unsigned long long) htonl (inode_buf->blockused) * (unsigned long long) htonl (inode_buf->blocksize));
	}
	else
	{
		if (indent)
			printf ("\t");
		printf ("Size: %-14d(%d %d %d)\n", htonl (inode_buf->size), htonl (inode_buf->blockused), htonl (inode_buf->blocksize), htonl (inode_buf->unk3));
	}
	if (indent)
		return;
	printf ("Flags: %-08x     Checksum: %08x  Pad: %04x Sig: %08x\n", htonl (inode_buf->inode_flags), htonl (inode_buf->checksum), htons (inode_buf->pad), htonl (inode_buf->sig));
	if (!(inode_buf->inode_flags & htonl (INODE_DATA)))
	{
		int count = htonl (inode_buf->numblocks);
		int loop;
		printf ("Data is in %d blocks:\n", count);
		for (loop = 0; loop < count; loop++)
		{
			printf ("At %-8x %d sectors\n", htonl (inode_buf->datablocks[loop].sector), htonl (inode_buf->datablocks[loop].count));
		}
	}
	else
	{
		printf ("Data is in inode block.\n");
	}
}

void
dump_zone_map (zone_header *zone)
{
	char *ztype;
	unsigned fsmem_base;
	int loop;
	unsigned nbits;
	unsigned blocksize;

	unsigned *fsmem_ptrs = (unsigned *)((char *)zone + sizeof (zone_header));

	switch (htonl (zone->type))
	{
	case ztInode:
		ztype = "ztInode";
		break;
	case ztApplication:
		ztype = "ztApplication";
		break;
	case ztMedia:
		ztype = "ztMedia";
		break;
	default:
		ztype = "zt???";
		break;
	}

	nbits = htonl (zone->size) / htonl (zone->min);
	blocksize = htonl (zone->min);

	fsmem_base = htonl (fsmem_ptrs[0]) - (sizeof (zone_header) + htonl (zone->num) * 4);

	printf ("This zone:                Sector: %08x    Backup: %08x\n", htonl (zone->sector), htonl (zone->sbackup));
	printf ("      Length: %08x    Size: %08x      Block size: %08x\n\n", htonl (zone->length), htonl (zone->size), htonl (zone->min));
	printf ("Next zone:                Sector: %08x    Backup: %08x\n", htonl (zone->next.sector), htonl (zone->next.sbackup));
	printf ("      Length: %08x    Size: %08x      Block size: %08x\n\n", htonl (zone->next.length), htonl (zone->next.size), htonl (zone->next.min));
	printf ("Type: %-14sFree: %08x\n", ztype, htonl (zone->free));
	printf ("First: %08x     Last: %08x\n", htonl (zone->first), htonl (zone->last));
	printf ("Logstamp: %08x  Checksum: %08x  Zero: %d\n", htonl (zone->logstamp), htonl (zone->checksum), htonl (zone->zero));
	printf ("Bitmaps: %-11dBase fsmem address: %08x\n", htonl (zone->num), fsmem_base);

	for (loop = 0; loop < htonl (zone->num); loop++, nbits /= 2, blocksize *= 2)
	{
		int loop2;
		unsigned *bits;
		int found = 0;

		bitmap_header *bitmap = (void *)(htonl (fsmem_ptrs[loop]) - fsmem_base + (unsigned)zone);

		if ((unsigned)bitmap < (unsigned)zone || (unsigned)bitmap > (unsigned)zone + htonl (zone->length) * 512)
		{
			fprintf (stderr, "\nBitmap %d out of range\n", loop);
			continue;
		}

		printf ("\nBitmap at %08x  Blocksize: %-9x\n", htonl (fsmem_ptrs[loop]), blocksize);
		printf (" Words: %-12dBits: %-14dActual: %d\n", htonl (bitmap->nints), htonl (bitmap->nbits), nbits);
		printf (" Free: %-13dLast free: %08x (%d)\n", htonl (bitmap->freeblocks), htonl (bitmap->last) * blocksize + htonl (zone->first), htonl (bitmap->last));

		bits = (unsigned *)(bitmap + 1);
		for (loop2 = 0; loop2 < htonl (bitmap->nints); loop2++)
		{
			if (bits[loop2])
			{
				int bitloop;

				for (bitloop = 0; bitloop < 32; bitloop++)
				{
					if (htonl (bits[loop2]) & (1 << (31 - bitloop)))
					{
						unsigned bitaddr = (loop2 * 32 + bitloop) * blocksize + htonl (zone->first);
						if (!found)
						{
							printf ("    Free blocks:         ");
							++found;
						}

						if (!(found & 3))
							printf ("\t");
						else
							printf (" ");

						printf ("%08x-%08x", bitaddr, bitaddr + blocksize - 1);
						if (!(++found & 3))
						{
							printf ("\n");
						}
					}
				}
			}
		}

		if (found & 3)
			printf ("\n");
	}
}

void
dump_log_entry (log_entry_all *log, int offset)
{
	unsigned displayed = 0;
	mfs_inode *inode;

	char *transtype = "!!!";

	switch (htonl (log->log.transtype))
	{
	case ltMapUpdate:
		transtype = "Map update";
		break;
	case ltInodeUpdate:
		transtype = "Inode update";
		break;
	case ltCommit:
		transtype = "Commit";
		break;
	case ltFsSync:
		transtype = "Sync";
		break;
	default:
		transtype = "???";
	}

	printf ("\nLog at %-9xMark: %4d %-10d", offset, htonl (log->log.bootcycles), htonl (log->log.bootsecs));
	if (log->log.unk1 || log->log.unk2)
		printf ("???: %-2d %-2d", htonl (log->log.unk1), htonl (log->log.unk2));

	printf ("\nFsid: %-10dLength: %-8dType: %s\n", htonl (log->log.inode), htons (log->log.length), transtype);

	displayed = sizeof (log->log) - 2;

	switch (htonl (log->log.transtype))
	{
	case ltMapUpdate:
		printf ("%15s at %-8x %d sectors\t\t??? %d\n", log->zonemap.remove? "Free": "Allocate", htonl (log->zonemap.sector), htonl (log->zonemap.size), htonl (log->zonemap.unk));
		displayed = sizeof (log_map_update) - 2;
		break;
	case ltInodeUpdate:
		inode = (mfs_inode *)((char *)log + displayed + 2);
		dump_inode (inode, 1);
		displayed += offsetof (mfs_inode, sig) + 8;
		if (!log->inode.inodedata)
		{
			int loop;
			int count = htonl (log->inode.datasize) / 8;

			printf ("Data is in %d blocks:\n", count);
			for (loop = 0; loop < count; loop++)
			{
				displayed += 8;
				printf ("At %-8x %d sectors\n", htonl (log->inode.datablocks[loop].sector), htonl (log->inode.datablocks[loop].count));
			}
		}
		else
		{
			printf ("Data is in inode block (%d)\n", htonl (log->inode.datasize));
		}
		break;
	case ltCommit:
		break;
	case ltFsSync:
		break;
	default:
		break;
	}

	if (displayed < htons (log->log.length))
	{
		hexdump ((char *)log + displayed + 2, ~0, htons (log->log.length) - displayed);
	}
}

char *
read_log (unsigned logstamp, unsigned nativedump)
{
	char tmp_entry[512 + sizeof (log_entry)];
	char buf[1024];
	unsigned curstart = 0;
	log_hdr *hdrs[2] = {(log_hdr *)buf, (log_hdr *)(buf + 512)};
	log_entry *entry;

	if (mfs_log_read (mfs, buf, logstamp) != 512)
	{
		perror ("read_log");
		return 0;
	}

	if (!nativedump)
	{
		char *ret = malloc (512);
		if (!ret)
			return 0;
		memcpy (ret, buf, 512);
		return ret;
	}

	printf ("Logstamp: %08x  CRC: %08x  Continued: %02x  Size: %03x\n", htonl (hdrs[0]->logstamp), htonl (hdrs[0]->crc), htonl (hdrs[0]->first), htonl (hdrs[0]->size));

	curstart = htonl (hdrs[0]->first) + 0x10;

	while (curstart < htonl (hdrs[0]->size) + 0x10 && curstart < 512)
	{
		entry = (void *)(buf + curstart);

		if (curstart + htonl (entry->length) > 512)
		{
			if (mfs_log_read (mfs, buf + 512, logstamp + 1) != 512)
			{
				printf ("... Continues on next log, which is missing\n");
				return 0;
			}
			memmove (buf + 512, buf + 512 + 0x10, 512 - 0x10);
		}

		if (entry->length)
			dump_log_entry ((void *)entry, curstart - 0x10);

		curstart += htons (entry->length) + 2;
	}

	return 0;
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
	unsigned int logno = 0;
	unsigned int zonemap = 0xdeadbeef;

	mfs_inode *inode_buf = NULL;

	enum
	{ None, Hex, Bin }
	format = None;

	progname = argv[0];

	while ((curarg = getopt (argc, argv, "bhc:s:i:f:t:z:")) >= 0)
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
		case 'c':
			count = strtoul (optarg, 0, 0);
			break;
		case 'f':
			if (zonemap != 0xdeadbeef || inode != 0xdeadbeef || logno)
			{
				fprintf (stderr, "Only 1 of -z -f -i and -t may be used.\n");
				usage ();
				return 1;
			}
			fsid = strtoul (optarg, 0, 0);
			break;
		case 'i':
			if (zonemap != 0xdeadbeef || fsid || logno)
			{
				fprintf (stderr, "Only 1 of -z -f -i and -t may be used.\n");
				usage ();
				return 2;
			}
			inode = strtoul (optarg, 0, 0);
			break;
		case 't':
			if (zonemap != 0xdeadbeef || fsid || inode != 0xdeadbeef)
			{
				fprintf (stderr, "Only 1 of -z -f -i and -t may be used.\n");
				usage ();
				return 2;
			}
			logno = strtoul (optarg, 0, 0);
			break;
		case 'z':
			if (fsid || logno || inode != 0xdeadbeef)
			{
				fprintf (stderr, "Only 1 of -z -f -i and -t may be used.\n");
				usage ();
				return 2;
			}
			zonemap = strtoul (optarg, 0, 0);
			break;
		default:
			usage ();
			return 3;
		}
	}

	if (sector == 0xdeadbeef && !fsid && inode == 0xdeadbeef && !logno && zonemap == 0xdeadbeef || optind == argc || optind >= argc + 2)
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
	else if (logno)
	{
		buf = read_log (logno, format != Bin && format != Hex);
		if (buf)
			bufsize = 512;
	}
	else if (zonemap != 0xdeadbeef)
	{
		zone_header *zone = NULL;
		sector = 0xdeadbeef;

		for (zone = mfs_next_zone (mfs, zone); zone && zonemap-- > 0; zone = mfs_next_zone (mfs, zone));

		if (!zone)
		{
			fprintf (stderr, "Zone map out of range\n");
			return 0;
		}

		buf = (void *)zone;
		bufsize = htonl (zone->length) * 512;
	}

	if (!logno && sector != 0xdeadbeef)
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

	if (format == Bin)
	{
		write (1, buf, bufsize);
	}
	else if (format == Hex || sector != 0xdeadbeef)
	{
		int offset = 0;
		while (offset * 512 < bufsize)
		{
			hexdump (buf + offset * 512, sector == 0xdeadbeef ? sector : offset + sector, bufsize - offset * 512);
			offset++;
		}
	}
	else if (inode_buf)
	{
		dump_inode (inode_buf, 0);
	}
	else if (zonemap != 0xdeadbeef)
	{
		dump_zone_map ((zone_header *)buf);
	}
	return 0;
}
