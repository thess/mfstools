#!/bin/bash

[ -z "$1" ] && echo Version required && exit 1

# Create source package
./sourcepkg.sh "$1"
[ -e "mfstools-$1.orig.tar.gz" ] || ln -s "mfstools-$1.tar.gz" "../mfstools_$1.orig.tar.gz"

# Setup build location and unpack source
mkdir ../mfstools-$1
cd ..
tar -xf mfstools-$1.tar.gz

# build .deb
cd mfstools-$1
debuild --no-lintian --no-sign
