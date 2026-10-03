#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
main="`basename "$1"`"
objects="object/${main}.o"
libraries=''
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries}
