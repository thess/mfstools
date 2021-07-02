AM_CPPFLAGS = -I${top_srcdir}/include

if BUILD_APMUTILS
bin_PROGRAMS = apmfix
else
bin_PROGRAMS =
endif

apmfix_SOURCES = apmfix.c
apmfix_LDADD = -lz
