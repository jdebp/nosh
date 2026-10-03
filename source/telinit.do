#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
main="`basename "$1"`"
objects="object/main-exec.o object/builtins-${main}.o object/${main}.o"
libraries='library/builtins.a library/utils.a'
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries}
