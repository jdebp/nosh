#!/bin/sh -e
main="`basename "$1"`"
objects="${main}.o main-exec.o builtins-terminal-emulator.o"
libraries="builtins.a utils.a"
[ "`uname`" = "FreeBSD" ] || kqueue=-lkqueue
[ "`uname`" = "FreeBSD" ] && static="-static"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} -lncursesw ${static}