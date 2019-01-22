#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
main="`basename "$1"`"
objects="main-exec.o builtins-${main}.o"
libraries="builtins.a utils.a"
( test _"`uname`" = _"Linux" || test _"`uname`" = _"FreeBSD" ) && crypt=-lcrypt
# Needed because emergency-login can be run before filesystems are mounted.
test _"`uname`" = _"FreeBSD" && static="-static"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${crypt} ${static}
