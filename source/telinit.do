#!/bin/sh -e
main="`basename "$1"`"
objects="main-exec.o builtins-${main}.o ${main}.o"
libraries="builtins.a utils.a"
[ "`uname`" = "FreeBSD" ] || kqueue=-lkqueue
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} 
