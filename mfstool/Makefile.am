INCLUDES += -I${top_srcdir}/include

if BUILD_MFSTOOL
MFSAPPS = mfstool
if BUILD_BACKUP
MFSTOOLS_BACKUP = -L${top_builddir}/backup -lbackup -Wl,-u,backup_main
ZLIB = -lz
else
MFSTOOLS_BACKUP =
endif
if BUILD_RESTORE
MFSTOOLS_RESTORE = -L${top_builddir}/restore -lrestore -Wl,-u,restore_main
ZLIB = -lz
else
MFSTOOLS_RESTORE =
endif
if BUILD_COPY
MFSTOOLS_COPY = -L${top_builddir}/mfscopy -lmfscopy -Wl,-u,copy_main
ZLIB = -lz
else
MFSTOOLS_COPY =
endif
if BUILD_MLS
MFSTOOLS_MLS = -L${top_builddir}/mls -lmls -Wl,-u,mls_main
else
MFSTOOLS_MLS =
endif
if BUILD_MFSD
MFSTOOLS_MFSD = -L${top_builddir}/mfsd -lmfsd -Wl,-u,mfsd_main
else
MFSTOOLS_MFSD =
endif
if BUILD_MFSADD
MFSTOOLS_MFSADD = -L${top_builddir}/mfsadd -lmfsadd -Wl,-u,mfsadd_main
else
MFSTOOLS_MFSADD =
endif
if BUILD_MFSINFO
MFSTOOLS_MFSINFO = -L${top_builddir}/mfsinfo -lmfsinfo -Wl,-u,mfsinfo_main
else
MFSTOOLS_MFSINFO =
endif
else
MFSAPPS =
MFSTOOLS_BACKUP =
MFSTOOLS_RESTORE =
MFSTOOLS_COPY =
MFSTOOLS_MLS =
MFSTOOLS_MFSD =
MFSTOOLS_MFSADD =
MFSTOOLS_MFSINFO =
endif

bin_PROGRAMS = $(MFSAPPS)

mfstool_SOURCES = mfstool.c
mfstool_LDFLAGS += -L${top_builddir}/lib $(MFSTOOLS_BACKUP) $(MFSTOOLS_RESTORE) $(MFSTOOLS_COPY) $(MFSTOOLS_MLS) $(MFSTOOLS_MFSD) $(MFSTOOLS_MFSADD) $(MFSTOOLS_MFSINFO) $(ZLIB) -lmfs -lmfsvol -lmacpart

