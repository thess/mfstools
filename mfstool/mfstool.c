#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "mfs.h"
#include "backup.h"

typedef int (*mainfunc) (int, char **);

#if BUILD_BACKUP
extern int backup_main (int, char **);
#endif
#if BUILD_RESTORE
extern int restore_main (int, char **);
#endif
#if BUILD_MFSADD
extern int mfsadd_main (int, char **);
#endif
#if BUILD_MLS
extern int mls_main (int, char **);
#endif
#if BUILD_MFSD
extern int mfsd_main (int, char **);
#endif
#if BUILD_MFSINFO
extern int mfsinfo_main (int, char **);
#endif

struct {
	char *name;
	mainfunc main;
	char *desc;
} funcs[] = {
#if BUILD_BACKUP
	{"backup", backup_main, "Backup TiVo drive fast and small."},
#endif
#if BUILD_RESTORE
	{"restore", restore_main, "Restore mfstool backups to TiVo drive."},
#endif
#if BUILD_MFSADD
	{"add", mfsadd_main, "Add partitions to your TiVo MFS volume."},
#endif
#if BUILD_MLS
	{"mls", mls_main, "List files in the MFS volume."},
#endif
#if BUILD_MFSD
	{"d", mfsd_main, "Dump raw data from MFS volume."},
#endif
#if BUILD_MFSINFO
	{"info", mfsinfo_main, "Display information about MFS volume."},
#endif
	{0, 0, 0}
};

mainfunc
find_function (char *name)
{
	int loop;

	for (loop = 0; funcs[loop].name; loop++)
		if (!strcasecmp (funcs[loop].name, name))
			return funcs[loop].main;

	if (!strncmp (name, "mfs", 3))
		return find_function (name + 3);

	return 0;
}

int
main (int argc, char **argv)
{
	mainfunc toolmain;
	char *tmp;
	int loop;

	tmp = strrchr(argv[0], '/');
	tmp = tmp? tmp + 1: argv[0];

	if ((toolmain = find_function (tmp)))
	{
		return toolmain (argc, argv);
	}

	if (argc > 1 && (toolmain = find_function (argv[1])))
	{
		return toolmain (argc - 1, argv + 1);
	}

	fprintf (stderr, "%s %s\n", PACKAGE, VERSION);
	fprintf (stderr, "Usage: %s <function> <args> or <function> <args>\n", argv[0]);
	fprintf (stderr, "Available functions:\n");
	for (loop = 0; funcs[loop].name; loop++)
		fprintf (stderr, "  %-10s%s\n", funcs[loop].name, funcs[loop].desc);
	fprintf (stderr, "For help on a particular function: %s <function> -h\n", argv[0]);
	return 1;
}
