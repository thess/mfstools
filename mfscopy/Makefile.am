INCLUDES = -I${top_srcdir}/include
LDADD = -L${top_builddir}/lib -lmfs -lmfsvol -lmacpart -lz

if BUILD_COPY
if BUILD_MFSTOOL
MFSTOOLS = libmfscopy.a
else
MFSTOOLS =
endif
if BUILD_MFSAPPS
MFSAPPS = mfscopy
else
MFSAPPS =
endif
else
MFSAPPS =
MFSTOOLS =
endif

bin_PROGRAMS = $(MFSAPPS)
noinst_LIBRARIES = $(MFSTOOLS)

mfscopy_SOURCES = backup.c backupv1.c backupv3.c restore.c copy.c
mfscopy_LDFLAGS = -Wl,--defsym,main=copy_main

libmfscopy_a_SOURCES = copy.c

vpath %.c ${top_builddir}/backup ${top_builddir}/restore
