#!/bin/sh -e
## **************************************************************************
## For copyright and licensing terms, see the file named COPYING.
## **************************************************************************
# vim: set filetype=sh:
main="`basename "$1"`"
objects="object/main-exec.o object/builtins-${main}.o"
libraries='library/builtins.a library/manager.a library/utils.a'
test _"`uname`" = _"Linux" && uuid=-luuid
test _"`uname`" = _"Linux" && rt=-lrt
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${uuid} ${rt}
