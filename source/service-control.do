#!/bin/sh -e
main="`basename "$1"`"
objects="main-exec.o builtins-service-control.o"
libraries="builtins.a utils.a"
[ "`uname`" = "FreeBSD" ] || kqueue=-lkqueue
[ "`uname`" = "FreeBSD" ] || rt=-lrt
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} -ltinfo ${rt}
