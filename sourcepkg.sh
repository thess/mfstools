#!/bin/bash

[ -z "$1" ] && echo "Version required" && exit 1

# Source tarball creation
./realclean.sh

tar --transform "s/^\./mfstools-$1/" -cvzf ../mfstools-$1.tar.gz --exclude-vcs .
