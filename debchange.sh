#!/bin/bash

[ -z "$1" ] && echo Version required && exit 1

# Bump build/package number in debian/changelog
dch -v $1 -D UNRELEASED --release-heuristic log
