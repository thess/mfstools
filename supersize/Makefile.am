INCLUDES = -I${top_srcdir}/include
LDADD = -L${top_builddir}/lib -lmfs -lmfsvol -lmacpart

if BUILD_SUPERSIZE
if BUILD_MFSTOOL
MFSTOOLS = libsupersize.a
else
MFSTOOLS =
endif
if BUILD_MFSAPPS
MFSAPPS = supersize
else
MFSAPPS =
endif
else
MFSTOOLS =
MFSAPPS =
endif
 
bin_PROGRAMS = $(MFSAPPS)
noinst_LIBRARIES = $(MFSTOOLS)

supersize_SOURCES = supersize.c
supersize_LDFLAGS = -Wl,--defsym,main=supersize_main

libsupersize_a_SOURCES = supersize.c
