AM_CPPFLAGS = -I${top_srcdir}/include
LDADD = -L${top_builddir}/lib -lmfs -lmfsvol -lmacpart

if BUILD_MFSINFO
if BUILD_MFSTOOL
MFSTOOLS = libmfsinfo.a
else
MFSTOOLS =
endif
if BUILD_MFSAPPS
MFSAPPS = mfsinfo
else
MFSAPPS =
endif
else
MFSTOOLS =
MFSAPPS =
endif
 
bin_PROGRAMS = $(MFSAPPS)
noinst_LIBRARIES = $(MFSTOOLS)

mfsinfo_SOURCES = mfsinfo.c
mfsinfo_LDFLAGS = -Wl,--defsym,main=mfsinfo_main

libmfsinfo_a_SOURCES = mfsinfo.c
