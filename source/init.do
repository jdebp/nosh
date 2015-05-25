#!/bin/sh -e
main="`basename "$1"`"
objects="main-exec.o builtins-init.o telinit.o init.o"
libraries="builtins.a utils.a"
redo-ifchange link ${objects} ${libraries}
exec ./link "$3" ${objects} ${libraries}
