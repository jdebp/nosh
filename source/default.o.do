#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
src="`basename "$1"`.cpp"
redo-ifchange "${src}" compile 
# The compile script will do this, so it isn't necessary to do it here twice.
#redo-ifchange "${src}"
exec ./compile "$3" "${src}" "$1.d"
