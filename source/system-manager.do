#!/bin/sh -e
main="`basename "$1"`"
objects="main-exec.o builtins-${main}.o"
libraries="builtins.a manager.a utils.a"
#[ "`uname`" = "FreeBSD" ] || kqueue=-lkqueue
#[ "`uname`" = "FreeBSD" ] || uuid=-luuid
[ "`uname`" = "FreeBSD" ] || rt=-lrt
[ "`uname`" = "FreeBSD" ] && static="-static"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} ${uuid} ${rt} ${static}
