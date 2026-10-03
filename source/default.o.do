#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
src="$(basename "$1").cpp"
redo-ifchange "${src}" compile
exec ./compile "$3" "${src}" "$(dirname "$3")"/"$(basename "$1").d"
