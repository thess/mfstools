INCLUDES += -I${top_srcdir}/include
LDADD += -L${top_builddir}/lib -lmfs

bin_PROGRAMS = mfsadd
noinst_LIBRARIES = libmfsadd.a

mfsadd_SOURCES = mfsadd.c
mfsadd_LDFLAGS = -Wl,--defsym,main=mfsadd_main

libmfsadd_a_SOURCES = mfsadd.c
