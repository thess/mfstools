#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include "mfs.h"

#define mfsadd_usage()

int
mfsadd_main (int argc, char **argv)
{
	unsigned int minalloc = 0x800;
	int opt;
	int extendall = 0;
	char *xdevs[2];
	int npairs = 0;
	char *pairs[32];
	char pairnums[32];
	int loop, loop2;
	char *drives[2] = {0, 0};

	while ((opt = getopt (argc, argv, "x:p:")) > 0)
	{
		switch (opt)
		{
		case 'x':
			if (extendall < 2)
			{
				xdevs[extendall] = optarg;
				extendall++;
				break;
			}

			fprintf (stderr, "%s: Can only extend MFS to 2 devices.\n", argv[0]);
			return 1;
		default:
			mfsadd_usage ();
			return 1;
		}
	}

	while (optind < argc)
	{
		int len = strlen (argv[optind]);

		if (isdigit (argv[optind][len - 1]))
		{
			if (npairs >= sizeof (pairs) / sizeof (*pairs))
			{
				fprintf (stderr, "%s: Too many new partitions!\n", argv[0]);
				return 1;
			}
			pairs[npairs] = argv[optind];
			npairs++;
		}
		else
		{
			if (!drives[0])
				drives[0] = argv[optind];
			else if (!drives[1])
				drives[1] = argv[optind];
			else
			{
				mfsadd_usage ();
				return 1;
			}
		}
		optind++;
	}

	if (!drives[0])
	{
		mfsadd_usage ();
		return 1;
	}

	if (npairs & 1)
	{
		fprintf (stderr, "%s: Number of new partitions must be even.\n", argv[0]);
		return 1;
	}

	for (loop = 0; loop < extendall; loop++)
	{
		if (!strcmp (xdevs[loop], drives[0]))
			xdevs[loop] = drives[0];
		else if (!drives[1] || !strcmp (xdevs[loop], drives[1]))
			xdevs[loop] = drives[1];
		else
		{
			fprintf (stderr, "%s: Arguments to -x must be one of %s or %s.\n", argv[0], drives[0], drives[1]);
			return 1;
		}
	}

	if (extendall == 2 && xdevs[0] == xdevs[1])
	{
		fprintf (stderr, "%s: -x argument only makes sense once for each device.\n", argv[0]);
		return 1;
	}

	for (loop = 0; loop < npairs; loop++)
	{
		int tmp;
		char *str;

		if (!strncmp (pairs[loop], drives[0], strlen (drives[0])))
			pairnums[loop] = 0x00;
		else if (!drives[1] || !strncmp (pairs[loop], drives[1], strlen (drives[1])))
			pairnums[loop] = 0x40;
		else
		{
			fprintf (stderr, "%s: added partitions must be partitions of either %s or %s\n", argv[0], drives[0], drives[1]);
			return 1;
		}

		if (!drives[pairnums[loop] >> 6])
		{
			char *num;
			char *new;

/* Known memory leak.  Deal. */
			new = strdup (drives[pairnums[loop] >> 6]);

			if (!new)
			{
				fprintf (stderr, "%s: Memory allocation error.\n", argv[0]);
				return 1;
			}

			for (num = pairs[loop] + strlen (pairs[loop]) - 1; num > pairs[loop] && isdigit (*num); num--)
				;
			num++;
			*num = 0;

			drives[pairnums[loop] >> 6] = new;
			drives[pairnums[loop] >> 6][num - pairs[loop] + 1] = 0;
		}

		tmp = strtoul (pairs[loop] + strlen (drives[pairnums[loop] >> 6]), &str, 10);
		if (str && *str)
		{
			fprintf (stderr, "%s: added partition %s is not a partition of %s\n", argv[0], pairs[loop], drives[pairnums[loop] >> 6]);
			return 1;
		}

		if (tmp < 2 || tmp > 16)
		{
			fprintf (stderr, "%s: TiVo only supports partiton numbers 2 through 16\n", argv[0]);
			return 1;
		}

		pairnums[loop] |= tmp;

		for (loop2 = 0; loop2 < loop; loop2++)
		{
			if (pairnums[loop] == pairnums[loop2])
			{
				fprintf (stderr, "%s: Can only add %s once.\n", argv[0], pairs[loop]);
				return 1;
			}
		}
	}

	return 0;
}
