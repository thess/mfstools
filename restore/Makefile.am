INCLUDES += -I${top_srcdir}/include
LDADD += -L${top_builddir}/lib -lmfs -lz

bin_PROGRAMS = restore
noinst_LIBRARIES = librestore.a

restore_SOURCES = restmain.c restore.c
restore_LDFLAGS = -Wl,--defsym,main=restore_main

librestore_a_SOURCES = restmain.c restore.c
