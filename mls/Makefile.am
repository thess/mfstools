INCLUDES = -I${top_srcdir}/include
LDADD = -L${top_builddir}/lib -lmfs -lmfsvol -lmacpart

if BUILD_MLS
if BUILD_MFSTOOL
MFSTOOLS = libmls.a
else
MFSTOOLS =
endif
if BUILD_MFSAPPS
MFSAPPS = mls
else
MFSAPPS =
endif
else
MFSTOOLS =
MFSAPPS =
endif
 
bin_PROGRAMS = $(MFSAPPS)
noinst_LIBRARIES = $(MFSTOOLS)

mls_SOURCES = mls.c
mls_LDFLAGS = -Wl,--defsym,main=mls_main

libmls_a_SOURCES = mls.c
