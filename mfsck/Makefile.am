AM_CPPFLAGS = -I${top_srcdir}/include
LDADD = -L${top_builddir}/lib -lmfs -lmfsvol -lmacpart

if BUILD_MFSCK
if BUILD_MFSTOOL
MFSTOOLS = libmfsck.a
else
MFSTOOLS =
endif
if BUILD_MFSAPPS
MFSAPPS = mfsck
else
MFSAPPS =
endif
else
MFSAPPS =
MFSTOOLS =
endif
 
bin_PROGRAMS = $(MFSAPPS)
noinst_LIBRARIES = $(MFSTOOLS)

mfsck_SOURCES = mfsck.c
mfsck_LDFLAGS = -Wl,--defsym,main=mfsck_main

libmfsck_a_SOURCES = mfsck.c
