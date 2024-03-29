AC_INIT(MFSTools, v3.34-3)
AC_CONFIG_SRCDIR(include/mfs.h)
AC_CONFIG_HEADERS(include/config.h:include/config.h.in)

AM_INIT_AUTOMAKE

AC_ARG_ENABLE(debug,
[  --enable-debug	Turn on debugging output],
[case "${enableval}" in
  yes) AC_DEFINE(DEBUG) ;;
  no)  : ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
esac])
AH_TEMPLATE([DEBUG],[Build with debug symbols and debug messages enabled.])

AC_ARG_ENABLE(deprecated,
[  --enable-deprecated	Turn on deprecated features],
[case "${enableval}" in
  yes) AC_DEFINE(DEPRECATED) ;;
  no)  : ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-deprecated) ;;
esac])
AH_TEMPLATE([DEPRECATED],[Build with deprecated features enabled.])

AC_ARG_ENABLE(apmutils,
[  --disable-apmutils	Don't build apmutils],
[case "${enableval}" in
  yes) build_apmutils=true; AC_DEFINE(BUILD_APMUTILS) ;;
  no)  build_apmutils=false ;;
esac],[build_apmutils=true; AC_DEFINE(BUILD_APMUTILS)])
AM_CONDITIONAL(BUILD_APMUTILS, test x$build_apmutils = xtrue)
AH_TEMPLATE([BUILD_APMUTILS],
	[Build the APM Utils (apmfix, bootsectorfix, mfsaddfix, 8TBprep).])

AC_ARG_ENABLE(mfsadd,
[  --disable-mfsadd	Don't build mfsadd],
[case "${enableval}" in
  yes) build_mfsadd=true; AC_DEFINE(BUILD_MFSADD) ;;
  no)  build_mfsadd=false ;;
esac],[build_mfsadd=true; AC_DEFINE(BUILD_MFSADD)])
AM_CONDITIONAL(BUILD_MFSADD, test x$build_mfsadd = xtrue)
AH_TEMPLATE([BUILD_MFSADD],
	[Build the mfsadd standalone utility or mfstool utility.])
  
AC_ARG_ENABLE(mfsck,
[  --disable-mfsck	Don't build mfsck],
[case "${enableval}" in
  yes) build_mfsck=true; AC_DEFINE(BUILD_MFSCK) ;;
  no)  build_mfsck=false ;;
esac],[build_mfsck=true; AC_DEFINE(BUILD_MFSCK)])
AM_CONDITIONAL(BUILD_MFSCK, test x$build_mfsck = xtrue)
AH_TEMPLATE([BUILD_MFSCK],
	[Build the mfsck standalone utility or mfstool utility.])
  
AC_ARG_ENABLE(mls,
[  --disable-mls		Don't build mls],
[case "${enableval}" in
  yes) build_mls=true; AC_DEFINE(BUILD_MLS) ;;
  no)  build_mls=false ;;
esac],[build_mls=true; AC_DEFINE(BUILD_MLS)])
AM_CONDITIONAL(BUILD_MLS, test x$build_mls = xtrue)
AH_TEMPLATE([BUILD_MLS],
	[Build the mls standalone utility or mfstool utility.])
  
AC_ARG_ENABLE(supersize,
[  --disable-supersize	Don't build supersize],
[case "${enableval}" in
  yes) build_supersize=true; AC_DEFINE(BUILD_SUPERSIZE) ;;
  no)  build_supersize=false ;;
esac],[build_supersize=true; AC_DEFINE(BUILD_SUPERSIZE)])
AM_CONDITIONAL(BUILD_SUPERSIZE, test x$build_supersize = xtrue)
AH_TEMPLATE([BUILD_SUPERSIZE],
	[Build the supersize standalone utility or mfstool utility.])
  
AC_ARG_ENABLE(mfsd,
[  --disable-mfsd	Don't build mfsd],
[case "${enableval}" in
  yes) build_mfsd=true; AC_DEFINE(BUILD_MFSD) ;;
  no)  build_mfsd=false ;;
esac],[build_mfsd=true; AC_DEFINE(BUILD_MFSD)])
AM_CONDITIONAL(BUILD_MFSD, test x$build_mfsd = xtrue)
AH_TEMPLATE([BUILD_MFSD],
	[Build the mfs dump standalone utility or mfstool utility.])
  
AC_ARG_ENABLE(backup,
[  --disable-backup	Don't build backup],
[case "${enableval}" in
  yes) build_backup=true; AC_DEFINE(BUILD_BACKUP) ;;
  no)  build_backup=false ;;
esac],[build_backup=true; AC_DEFINE(BUILD_BACKUP)])
AM_CONDITIONAL(BUILD_BACKUP, test x$build_backup = xtrue)
AH_TEMPLATE([BUILD_BACKUP],
	[Build the mfs backup standalone utility or mfstool utility.])
  
AC_ARG_ENABLE(restore,
[  --disable-restore	Don't build restore],
[case "${enableval}" in
  yes) build_restore=true; AC_DEFINE(BUILD_RESTORE) ;;
  no)  build_restore=false ;;
esac],[build_restore=true; AC_DEFINE(BUILD_RESTORE)])
AM_CONDITIONAL(BUILD_RESTORE, test x$build_restore = xtrue)
AH_TEMPLATE([BUILD_RESTORE],
	[Build the mfs restore standalone utility or mfstool utility.])

AC_ARG_ENABLE(copy,
[  --disable-copy	Don't build copy],
[case "${enableval}" in
  yes) build_copy=true; AC_DEFINE(BUILD_COPY) ;;
  no)  build_copy=false ;;
esac],[build_copy=true; AC_DEFINE(BUILD_COPY)])
AM_CONDITIONAL(BUILD_COPY, test x$build_copy = xtrue)
AH_TEMPLATE([BUILD_COPY],
	[Build the mfs copy standalone utility or mfstool utility.])
  
AC_ARG_ENABLE(mfsinfo,
[  --disable-mfsinfo	Don't build mfsinfo],
[case "${enableval}" in
  yes) build_mfsinfo=true; AC_DEFINE(BUILD_MFSINFO) ;;
  no)  build_mfsinfo=false ;;
esac],[build_mfsinfo=true; AC_DEFINE(BUILD_MFSINFO)])
AM_CONDITIONAL(BUILD_MFSINFO, test x$build_mfsinfo = xtrue)
AH_TEMPLATE([BUILD_MFSINFO],
	[Build the mfs info standalone utility or mfstool utility.])
  
AC_ARG_ENABLE(mfstool,
[  --disable-mfstool	Don't build mfstool mega-app],
[case "${enableval}" in
  yes) build_mfstool=true ;;
  no)  build_mfstool=false ;;
esac],[build_mfstool=true])
AM_CONDITIONAL(BUILD_MFSTOOL, test x$build_mfstool = xtrue)
AH_TEMPLATE([BUILD_MFSINFO],
	[Build the utilities into a single mfstool command.])
  
AC_ARG_ENABLE(apps,
[  --disable-apps	Don't build standalone apps],
[case "${enableval}" in
  yes) build_apps=true ;;
  no)  build_apps=false ;;
esac],[build_apps=true])
AM_CONDITIONAL(BUILD_MFSAPPS, test x$build_apps = xtrue)
AH_TEMPLATE([BUILD_MFSAPPS],
	[Build the utilities as standalone applications.])

AC_PROG_CC
AC_PROG_MAKE_SET
AC_PROG_INSTALL
AC_PROG_RANLIB

AC_CHECK_FUNCS(Endian16_Swap)
AC_CHECK_FUNCS(Endian32_Swap)
AC_CHECK_FUNCS(Endian64_Swap)
AC_CHECK_FUNCS(sync)
  
AC_CHECK_HEADERS(unistd.h)
AC_CHECK_HEADERS(asm/page.h)
AC_CHECK_HEADERS(asm/types.h)
AC_CHECK_HEADERS(ctype.h)
AC_CHECK_HEADERS(errno.h)
AC_CHECK_HEADERS(fcntl.h)
AC_CHECK_HEADERS(linux/fs.h)
AC_CHECK_HEADERS(linux/ide-tivo.h)
AC_CHECK_HEADERS(linux/unistd.h)
AC_CHECK_HEADERS(malloc.h)
AC_CHECK_HEADERS(stdio.h)
AC_CHECK_HEADERS(stdlib.h)
AC_CHECK_HEADERS(string.h)
AC_CHECK_HEADERS(sys/ioctl.h)
AC_CHECK_HEADERS(sys/param.h)
AC_CHECK_HEADERS(sys/stat.h)
AC_CHECK_HEADERS(sys/types.h)
AC_CHECK_HEADERS(stdint.h)
AC_CHECK_HEADERS(stddef.h)
AC_CHECK_HEADERS(time.h)
AC_CHECK_HEADERS(unistd.h)
AC_CHECK_HEADERS(zlib.h)
AC_CHECK_HEADERS(byteorder.h)

AC_CHECK_FUNCS(lseek64)
AC_CHECK_FUNCS(llseek)

AC_OUTPUT(
Makefile
lib/Makefile
mfsadd/Makefile
mfsck/Makefile
mls/Makefile
supersize/Makefile
mfsd/Makefile
backup/Makefile
restore/Makefile
mfscopy/Makefile
mfsinfo/Makefile
mfstool/Makefile
apmutils/mfsaddfix/Makefile
apmutils/bootsectorfix/Makefile
apmutils/apmfix/Makefile
apmutils/8TBprep/Makefile
)
