#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "mfs.h"

char *progname;

static struct mfs_handle *mfs;

static void
usage ()
{
	fprintf (stderr, "Usage:\n%s [-i inode] [-f fsid] [-s sector] [-c count] [-h] [-b]\n", progname);
	fprintf (stderr, "    -f	Read from fsid\n");
	fprintf (stderr, "    -i	Read from inode\n");
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
mfsd_main (int argc, char **argv)
{
	int args[2];
	int curarg;
	int curargv;
	unsigned char *buf;

	unsigned int sector = 0xdeadbeef, count = 1;
	unsigned int fsid = 0;
	unsigned int inode = 0xdeadbeef;
	unsigned int bufsize;

	mfs_inode *inode_buf = NULL;

	enum
	{ None, Hex, Bin }
	format = None;

	progname = argv[0];

	while ((curarg = getopt (argc, argv, "bhc:s:i:f:")) >= 0)
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

	if (sector == 0xdeadbeef && !fsid && inode == 0xdeadbeef)
	{
		usage ();
		return 4;
	}

	mfs = mfs_init (O_RDONLY);

	if (fsid)
	{
		inode_buf = mfs_read_inode_by_fsid (mfs, fsid);
		if (!inode_buf)
		{
			fprintf (stderr, "Unable to read fsid %d\n", fsid);
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
			fprintf (stderr, "Unable to read inode %d\n", inode);
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
				fprintf (stderr, "Error from mfs_read_inode_data_part\n");
				return 1;
			}
			bufsize = nread;
		}
		else
		{
			int nread = mfs_read_data (mfs, buf, sector, count);

			if (nread <= 0)
			{
				fprintf (stderr, "Error from mfs_read_data\n");
				return 1;
			}

			bufsize = nread;
		}
	}

	if (format == Bin)
	{
		write (1, buf, bufsize);
	}
	else if (format == Hex || !inode_buf || sector != 0xdeadbeef)
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
			printf ("???");
		}

		printf ("???: %-15d???: %d\n", htonl (inode_buf->unk1), htonl (inode_buf->unk2));
		if (inode_buf->type == tyStream)
		{
			printf ("Size: %d blocks of %d bytes (%llu)\n", htonl (inode_buf->size), htonl (inode_buf->unk3), (unsigned long long) htonl (inode_buf->unk3) * (unsigned long long) htonl (inode_buf->size));
			printf ("Used: %d blocks of %d bytes (%llu)\n", htonl (inode_buf->blockused), htonl (inode_buf->blocksize), (unsigned long long) htonl (inode_buf->blockused) * (unsigned long long) htonl (inode_buf->blocksize));
		}
		else
		{
			printf ("Size: %d bytes\n", htonl (inode_buf->size));
		}
		printf ("Checksum: %08x  Flags(?): %d\n", inode_buf->checksum, htonl (inode_buf->inode_flags));
		printf ("Sigs: %02x %04x %08x (Always 02 beef 91231ebc?)\n", inode_buf->unk6, htons (inode_buf->beef), htonl (inode_buf->sig));
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
	}
	return 0;
}
