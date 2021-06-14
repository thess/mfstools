AM_CPPFLAGS = -I${top_srcdir}/include
LDADD = -L${top_builddir}/lib -lmfs -lmfsvol -lmacpart -lz

if BUILD_BACKUP
if BUILD_MFSTOOL
MFSTOOLS = libbackup.a
else
MFSTOOLS =
endif
if BUILD_MFSAPPS
MFSAPPS = backup
else
MFSAPPS =
endif
else
MFSAPPS =
MFSTOOLS =
endif

bin_PROGRAMS = $(MFSAPPS)
noinst_LIBRARIES = $(MFSTOOLS)

backup_SOURCES = backmain.c backup.c backupv1.c backupv3.c
backup_LDFLAGS = -Wl,--defsym,main=backup_main

libbackup_a_SOURCES = backmain.c backup.c backupv1.c backupv3.c
