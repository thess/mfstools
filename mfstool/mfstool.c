#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "mfs.h"
#include "backup.h"

typedef int (*mainfunc) (int, char **);

extern int backup_main (int, char **);
extern int restore_main (int, char **);
extern int mfsadd_main (int, char **);
extern int mls_main (int, char **);
extern int mfsd_main (int, char **);

struct {
	char *name;
	mainfunc main;
	char *desc;
} funcs[] = {
	{"backup", backup_main, "Backup TiVo drive fast and small."},
	{"restore", restore_main, "Restore mfstool backups to TiVo drive."},
	{"mfsadd", mfsadd_main, "Add partitions to your TiVo MFS volume."},
	{"mls", mls_main, "List files in the MFS volume."},
	{"mfsd", mfsd_main, "Dump raw data from MFS volume."},
	{0, 0, 0}
};

mainfunc
find_function (char *name)
{
	int loop;

	for (loop = 0; funcs[loop].name; loop++)
		if (!strcasecmp (funcs[loop].name, name))
			return funcs[loop].main;

	return 0;
}

int
main (int argc, char **argv)
{
	mainfunc main;
	char *tmp;
	int loop;

	tmp = strchr(argv[0], '/');
	tmp = tmp? tmp + 1: argv[0];

	if (main = find_function (tmp))
	{
		return main (argc, argv);
	}

	if (argc > 1 && (main = find_function (argv[1])))
	{
		return main (argc - 1, argv + 1);
	}

	fprintf (stderr, "%s %s\n", PACKAGE, VERSION);
	fprintf (stderr, "Usage: %s <function> <args> or <function> <args>\n", argv[0]);
	fprintf (stderr, "Available functions:\n");
	for (loop = 0; funcs[loop].name; loop++)
		fprintf (stderr, "  %-10s%s\n", funcs[loop].name, funcs[loop].desc);
	fprintf (stderr, "For help on a particular function: %s <function> -h\n", argv[0]);
	return 1;
}
