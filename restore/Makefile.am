INCLUDES = -I${top_srcdir}/include
LDADD = -L${top_builddir}/lib -lmfs -lmfsvol -lmacpart -lz

if BUILD_RESTORE
if BUILD_MFSTOOL
MFSTOOLS = librestore.a
else
MFSTOOLS =
endif
if BUILD_MFSAPPS
MFSAPPS = restore
else
MFSAPPS =
endif
else
MFSTOOLS =
MFSAPPS =
endif
 
bin_PROGRAMS = $(MFSAPPS)
noinst_LIBRARIES = $(MFSTOOLS)

restore_SOURCES = restmain.c restore.c
restore_LDFLAGS = -Wl,--defsym,main=restore_main

librestore_a_SOURCES = restmain.c restore.c
