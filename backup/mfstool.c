#include <stdio.h>
#include <fcntl.h>
#include <zlib.h>
#include "mfs.h"
#include "backup.h"

int main(int argc, char **argv)
{
	struct backup_info *info;
	int loop;

	if (mfs_init (O_RDONLY) < 0) {
		fprintf (stderr, "mfsinit: Failed!  Bailing.\n");
		exit (1);
	}

	info = init_backup (48 * 1024 * 2);
	//info = init_backup (-1);

	if (info) {
		char inbuf[1024 * 1024];
		char outbuf[1024 * 1024];
		z_stream strm;
		unsigned int cursec, curcount;

		strm.zalloc = Z_NULL;
		strm.zfree = Z_NULL;
		strm.opaque = Z_NULL;
		if (deflateInit (&strm, 6) != Z_OK) {
			fprintf (stderr, "Could not initialize compression\n");
			return 1;
		}

		fprintf (stderr, "Backup of %d blocks\n", info->nblocks);
		for (loop = 0; loop < info->nblocks; loop++) {
			fprintf (stderr, "Backup block %d for %d blocks\n", info->blocks[loop].firstsector, info->blocks[loop].sectors);

			cursec = info->blocks[loop].firstsector;
			curcount = info->blocks[loop].sectors;

			while (curcount > 0) {
				int toread = curcount > 2048? 2048: curcount;

				if (mfs_read_data (inbuf, cursec, toread) != 512 * toread) {
					fprintf (stderr, "MFS read error!\n");
				}
				write (3, inbuf, 512 * toread);
				strm.next_in = inbuf;
				strm.avail_in = 512 * toread;

				while (strm.avail_in > 0) {
					strm.next_out = outbuf;
					strm.avail_out = 512 * 2048;
					deflate (&strm, Z_NO_FLUSH);
					write (1, outbuf, 512 * 2048 - strm.avail_out);
				}

				curcount -= toread;
				cursec += toread;
			}
		}

		strm.next_out = outbuf;
		strm.avail_out = 512 * 2048;
		while (deflate (&strm, Z_FINISH) == Z_OK) {
			write (1, outbuf, 512 * 2048 - strm.avail_out);
			strm.next_out = outbuf;
			strm.avail_out = 512 * 2048;
		}
		if (strm.avail_out != 512 * 2048) {
			write (1, outbuf, 512 * 2048 - strm.avail_out);
		}

		deflateEnd (&strm);
	}

	exit (0);
}
