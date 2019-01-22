#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
main="`basename "$1"`"
objects="main-exec.o builtins-${main}.o"
libraries="builtins.a manager.a utils.a"
#test _"`uname`" = _"FreeBSD" || uuid=-luuid
test _"`uname`" = _"Linux" && rt=-lrt
# Needed because system-manager runs before filesystems are mounted.
test _"`uname`" = _"FreeBSD" && static="-static"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${uuid} ${rt} ${static}
