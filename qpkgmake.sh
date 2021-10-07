#!/bin/bash

# Quick build small Debian package using checkinstall

[ -z "$1" ] && echo "Version required" && exit 1
[ -z "$2" ] && echo "Package release required" && exit 1

# Building from source
./autogen.sh
./configure
make

# Create Debian package using checkinstall
sudo checkinstall -D --nodoc --install=no --backup=no --maintainer=various \
	--pkgname=mfstools --pkgversion=$1  --pkgrelease=$2
