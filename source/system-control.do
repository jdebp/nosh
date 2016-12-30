#!/bin/sh -e
main="`basename "$1"`"
objects="main-exec.o builtins-${main}.o"
libraries="builtins.a manager.a utils.a"
#test _"`uname`" = _"FreeBSD" || kqueue=-lkqueue
test _"`uname`" = _"Linux" && uuid=-luuid
test _"`uname`" = _"Linux" && rt=-lrt
( test _"`uname`" = _"Linux" || test _"`uname`" = _"FreeBSD" ) && tinfo=-ltinfo
# Needed because system-control can be run after filesystems are unmounted.
test _"`uname`" = _"FreeBSD" && static="-static"
test _"`uname`" = _"Linux" || ncursesw="-lncursesw"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} ${ncursesw} ${tinfo} ${uuid} ${rt} ${static}
