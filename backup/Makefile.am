INCLUDES += -I${top_srcdir}/include
LDADD += -L${top_builddir}/lib -lmfs -lz

bin_PROGRAMS = backup
noinst_LIBRARIES = libbackup.a

backup_SOURCES = backmain.c backup.c
backup_LDFLAGS = -Wl,--defsym,main=backup_main

libbackup_a_SOURCES = backmain.c backup.c
