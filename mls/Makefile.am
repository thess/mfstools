INCLUDES += -I${top_srcdir}/include
LDADD += -L${top_builddir}/lib -lmfs

bin_PROGRAMS = mls
noinst_LIBRARIES = libmls.a

mls_SOURCES = mls.c
mls_LDFLAGS = -Wl,--defsym,main=mls_main

libmls_a_SOURCES = mls.c
