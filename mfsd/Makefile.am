INCLUDES += -I${top_srcdir}/include
LDADD += -L${top_builddir}/lib -lmfs -lmfsvol -lmacpart

if BUILD_MFSD
if BUILD_MFSTOOL
MFSTOOLS = libmfsd.a
else
MFSTOOLS =
endif
if BUILD_MFSAPPS
MFSAPPS = mfsd
else
MFSAPPS =
endif
else
MFSTOOLS =
MFSAPPS =
endif
 
bin_PROGRAMS = $(MFSAPPS)
noinst_LIBRARIES = $(MFSTOOLS)

mfsd_SOURCES = mfsd.c
mfsd_LDFLAGS = -Wl,--defsym,main=mfsd_main

libmfsd_a_SOURCES = mfsd.c
