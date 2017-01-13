#!/bin/sh -e
main="`basename "$1"`"
objects="main-exec.o builtins-${main}.o"
libraries="builtins.a utils.a"
#test _"`uname`" = _"FreeBSD" || kqueue=-lkqueue
test _"`uname`" = _"Linux" && uuid=-luuid
test _"`uname`" = _"Linux" && rt=-lrt
test _"`uname`" = _"FreeBSD" && util=-lutil
test _"`uname`" = _"FreeBSD" && static="-static"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} ${uuid} ${rt} ${util} ${static}
