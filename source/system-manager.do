#!/bin/sh -e
main="`basename "$1"`"
objects="main-exec.o builtins-${main}.o"
libraries="builtins.a manager.a utils.a"
#test _"`uname`" = _"FreeBSD" || kqueue=-lkqueue
#test _"`uname`" = _"FreeBSD" || uuid=-luuid
test _"`uname`" = _"Linux" && rt=-lrt
test _"`uname`" = _"FreeBSD" && static="-static"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} ${uuid} ${rt} ${static}
