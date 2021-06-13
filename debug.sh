#!/bin/sh

sh autogen.sh
./configure CPPFLAGS=-D_FILE_OFFSET_BITS=64 --enable-debug --enable-deprecated
make all
make install
