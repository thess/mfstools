INCLUDES += -I${top_srcdir}/include
LDADD += -L${top_builddir}/lib -L${top_builddir}/backup -L${top_builddir}/restore -L${top_builddir}/mls -L${top_builddir}/mfsd -L${top_builddir}/mfsadd -lbackup -lrestore -lmls -lmfsadd -lmfsd -lz -lmfs

bin_PROGRAMS = mfstool

mfstool_SOURCES = mfstool.c
