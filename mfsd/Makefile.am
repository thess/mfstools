INCLUDES += -I${top_srcdir}/include
LDADD += -L${top_builddir}/lib -lmfs

bin_PROGRAMS = mfsd
noinst_LIBRARIES = libmfsd.a

mfsd_SOURCES = mfsd.c
mfsd_LDFLAGS = -Wl,--defsym,main=mfsd_main

libmfsd_a_SOURCES = mfsd.c
