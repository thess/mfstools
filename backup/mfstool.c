#include <stdio.h>
#include <fcntl.h>
#include "mfs.h"
#include "backup.h"

void main()
{
	struct backup_info *info;
	int loop;

	if (mfs_init (O_RDONLY) < 0) {
		fprintf (stderr, "mfsinit: Failed!  Bailing.\n");
		exit (1);
	}

	info = init_backup (128 * 1024 * 2);
	//info = init_backup (-1);

	if (info) {
		char buf[1024 * 1024];
		unsigned int cursec, curcount;
		fprintf (stderr, "Backup of %d blocks\n", info->nblocks);
		for (loop = 0; loop < info->nblocks; loop++) {
			fprintf (stderr, "Backup block %d for %d blocks\n", info->blocks[loop].firstsector, info->blocks[loop].sectors);

			cursec = info->blocks[loop].firstsector;
			curcount = info->blocks[loop].sectors;

			while (curcount > 2048) {
				if (mfs_read_data (buf, cursec, 2048) != 512 * 2048) {
					fprintf (stderr, "MFS read error!\n");
				}
				write (1, buf, 512 * 2048);
				curcount -= 2048;
				cursec += 2048;
			}

			if (curcount) {
				if (mfs_read_data (buf, cursec, curcount) != 512 * curcount) {
					fprintf (stderr, "MFS read error!\n");
				}
				write (1, buf, 512 * curcount);
			}
		}
	}

	exit (0);
}
