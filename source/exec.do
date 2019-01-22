#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
main="`basename "$1"`"
objects="main-exec.o builtins-${main}.o"
libraries="builtins.a utils.a"
test _"`uname`" = _"Linux" && uuid=-luuid
test _"`uname`" = _"Linux" && rt=-lrt
test _"`uname`" = _"FreeBSD" && util=-lutil
test _"`uname`" = _"FreeBSD" && static="-static"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${uuid} ${rt} ${util} ${static}
