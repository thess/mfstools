INCLUDES = -I${top_srcdir}/include
LDADD = -L${top_builddir}/lib -lmfs -lmfsvol -lmacpart

if BUILD_MFSADD
if BUILD_MFSTOOL
MFSTOOLS = libmfsadd.a
else
MFSTOOLS =
endif
if BUILD_MFSAPPS
MFSAPPS = mfsadd
else
MFSAPPS =
endif
else
MFSAPPS =
MFSTOOLS =
endif
 
bin_PROGRAMS = $(MFSAPPS)
noinst_LIBRARIES = $(MFSTOOLS)

mfsadd_SOURCES = mfsadd.c
mfsadd_LDFLAGS = -Wl,--defsym,main=mfsadd_main

libmfsadd_a_SOURCES = mfsadd.c
