AM_CPPFLAGS = -I${top_srcdir}/include
 
if BUILD_APMUTILS
bin_PROGRAMS = mfsaddfix
else
bin_PROGRAMS =
endif

mfsaddfix_SOURCES = mfsaddfix.c
mfsaddfix_LDADD = -lz
