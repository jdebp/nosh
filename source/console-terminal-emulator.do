#!/bin/sh -e
main="`basename "$1"`"
objects="main-exec.o builtins-${main}.o"
libraries="builtins.a utils.a"
#test _"`uname`" = _"FreeBSD" || kqueue=-lkqueue
( test _"`uname`" = _"Linux" || test _"`uname`" = _"FreeBSD" ) && crypt=-lcrypt
# Needed because emergency-login can be run before filesystems are mounted.
test _"`uname`" = _"FreeBSD" && static="-static"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries} ${kqueue} -lncursesw ${crypt} ${static}
