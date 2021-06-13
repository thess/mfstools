#!/bin/sh

# Cleanup autogen artifacts
find . -name "*.in" -delete
rm aclocal.m4 install-sh missing compile depcomp configure
rm -rf autom4te.cache

# Force reconf gen with missing installed
autoreconf -vfi
