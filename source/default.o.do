#!/bin/sh -e
src="`basename "$1"`.cpp"
redo-ifchange "${src}" compile 
# The compile script will do this, so it isn't necessary to do it here twice.
#redo-ifchange "${src}"
exec ./compile "$3" "${src}" "$1.d"
