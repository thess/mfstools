#include <stdio.h>
#include <fcntl.h>
#include "mfs.h"

int main(int argc, char **argv)
{
    if (argc != 3) {
	fprintf (stderr, "Usage: %s /dev/hdXX /dev/hdYY\n", argv[0]);
	return (1);
    }

    if (mfs_init (O_RDWR) < 0) {
	fprintf (stderr, "mfsinit: Failed!  Bailing.\n");
	return (1);
    }

    if (mfs_add_volume_pair (argv[1], argv[2]) < 0) {
	fprintf (stderr, "%s failed!\n", argv[0]);
	return (1);
    }

    return (0);
}
