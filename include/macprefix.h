/* this is the prefix file for Mac OS X development */

#include <stdint.h>
#include <Carbon/Carbon.h>

#if !HAVE_CONFIG_H
/* not exactly sure what needs to be done to premake.sh for Mac OS X */
/* meanwhile here is the contents of include/config.h that would be generated */

#define BUILD_MFSADD 1
#define BUILD_MLS 1
#define BUILD_SUPERSIZE 1
#define BUILD_MFSD 1
#define BUILD_BACKUP 1
#define BUILD_RESTORE 1
#define BUILD_MFSINFO 1
#define DEBUG 1

/* Define if you have the <asm/page.h> header file.  */
/* #undef HAVE_ASM_PAGE_H */

/* Define if you have the <asm/types.h> header file.  */
/* #undef HAVE_ASM_TYPES_H */

/* Define if you have the <ctype.h> header file.  */
#define HAVE_CTYPE_H 1

/* Define if you have the <errno.h> header file.  */
#define HAVE_ERRNO_H 1

/* Define if you have the <fcntl.h> header file.  */
#define HAVE_FCNTL_H 1

/* Define if you have the <linux/fs.h> header file.  */
/* #undef HAVE_LINUX_FS_H */

/* Define if you have the <linux/ide-tivo.h> header file.  */
/* #undef HAVE_LINUX_IDE_TIVO_H */

/* Define if you have the <linux/unistd.h> header file.  */
/* #undef HAVE_LINUX_UNISTD_H */

/* Define if you have the <malloc.h> header file.  */
/* #undef HAVE_MALLOC_H */

/* Define if you have the <netinet/in.h> header file.  */
#define HAVE_NETINET_IN_H 1

/* Define if you have the <stdio.h> header file.  */
#define HAVE_STDIO_H 1

/* Define if you have the <stdlib.h> header file.  */
#define HAVE_STDLIB_H 1

/* Define if you have the <string.h> header file.  */
#define HAVE_STRING_H 1

/* Define if you have the <sys/disk.h> header file.  */
#define HAVE_SYS_DISK_H 1

/* Define if you have the <sys/errno.h> header file.  */
/* #undef HAVE_SYS_ERRNO_H */

/* Define if you have the <sys/ioctl.h> header file.  */
#define HAVE_SYS_IOCTL_H 1

/* Define if you have the <sys/malloc.h> header file.  */
#define HAVE_SYS_MALLOC_H 1

/* Define if you have the <sys/param.h> header file.  */
#define HAVE_SYS_PARAM_H 1

/* Define if you have the <sys/stat.h> header file.  */
#define HAVE_SYS_STAT_H 1

/* Define if you have the <sys/types.h> header file.  */
#define HAVE_SYS_TYPES_H 1

/* Define if you have the <time.h> header file.  */
#define HAVE_TIME_H 1

/* Define if you have the <unistd.h> header file.  */
#define HAVE_UNISTD_H 1

/* Define if you have the <stdint.h> header file.  */
#define HAVE_STDINT_H 1

/* Define if you have the <zlib.h> header file.  */
#define HAVE_ZLIB_H 1

/* Name of package */
#define PACKAGE "MFSTools"

/* Version number of package */
#define VERSION "3.2"

#endif // !HAVE_CONFIG_H