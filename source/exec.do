#!/bin/sh -e
main="`basename "$1"`"
objects="main-exec.o builtins-${main}.o"
libraries="builtins.a utils.a"
#[ "`uname`" = "FreeBSD" ] || kqueue=-lkqueue
[ "`uname`" = "Linux" ] && uuid=-luuid
[ "`uname`" = "Linux" ] && rt=-lrt
[ "`uname`" = "FreeBSD" ] && static="-static"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} ${uuid} ${rt} ${static}
