#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
main="`basename "$1"`"
objects="main-exec.o builtins-${main}.o ${main}.o"
libraries="builtins.a utils.a"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries}
