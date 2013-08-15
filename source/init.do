#!/bin/sh -e
main="`basename "$1"`"
objects="${main}.o main-exec.o builtins-init.o telinit.o"
libraries="builtins.a utils.a"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries}
