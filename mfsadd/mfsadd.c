#include <stdio.h>
#include <fcntl.h>
#include "mfs.h"

int
main (int argc, char **argv)
{
	unsigned int minalloc = 0x800;

	if (argc != 3 && argc != 4)
	{
		fprintf (stderr, "Usage: %s /dev/hdXX /dev/hdYY [minalloc]\n", argv[0]);
		return (1);
	}

	if (argc != 3)
	{
		char *endptr;
		unsigned int loop;

		minalloc = strtoul (argv[3], &endptr, 0);

		if (endptr && *endptr)
		{
			fprintf (stderr, "Invalid minimum allocation size %s.\n", argv[3]);
			return (1);
		}

		for (loop = 0; loop < 32; loop++)
		{
			if (minalloc & (1 << loop))
			{
				break;
			}
		}

		if (loop == 32)
		{
			fprintf (stderr, "Minimum allocation size must be at least 1.\n");
			return (1);
		}

		for (loop++; loop < 32; loop++)
		{
			if (minalloc & (1 << loop))
			{
				break;
			}
		}

		if (loop < 32)
		{
			fprintf (stderr, "Minimum allocation size must be a power of 2.\n");
			return (1);
		}
	}

	if (mfs_init (O_RDWR) < 0)
	{
		fprintf (stderr, "mfsinit: Failed!  Bailing.\n");
		return (1);
	}

	if (mfs_add_volume_pair (argv[1], argv[2], minalloc) < 0)
	{
		fprintf (stderr, "%s failed!\n", argv[0]);
		return (1);
	}

	return (0);
}
