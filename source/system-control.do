#!/bin/sh -e
main="`basename "$1"`"
objects="main-exec.o builtins-${main}.o"
libraries="builtins.a manager.a utils.a"
#[ "`uname`" = "FreeBSD" ] || kqueue=-lkqueue
[ "`uname`" = "Linux" ] && uuid=-luuid
[ "`uname`" = "Linux" ] && rt=-lrt
( test "`uname`" = "Linux" || test "`uname`" = "FreeBSD" ) && tinfo=-ltinfo
# Needed because system-control can be run after filesystems are unmounted.
[ "`uname`" = "FreeBSD" ] && static="-static"
[ "`uname`" = "Linux" ] || ncursesw="-lncursesw"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} ${ncursesw} ${tinfo} ${uuid} ${rt} ${static}
