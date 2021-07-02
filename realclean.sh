#!/bin/sh

make -k distclean

# Cleanup autogen artifacts
find . -name "*.in" -delete
rm aclocal.m4 install-sh missing compile depcomp configure
rm -rf autom4te.cache
rm -rf dist
